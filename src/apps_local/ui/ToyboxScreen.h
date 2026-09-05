#pragma once

// Toybox screens, the way the SDK intends them to be written.
//
// A screen is a free function taking a `toybox::Screen&` and a plain model
// struct. It touches no renderer, no Activity and no storage, which buys two
// things:
//
//   * `freeink::ui::Screen` substitutes theme tokens into every component it
//     builds (title style, side padding, row height, row gap, selection style).
//     Calling the components directly opts out of that, and the first two bugs
//     in this fork's adoption of FreeInkUI were exactly the tokens `Screen`
//     would have supplied: a header that defaulted to 6px of padding, and a
//     title drawn black on a black band.
//   * screens become host-testable. FreeInkUI is freestanding C++17, so a test
//     builds one against a fake draw target and asserts on what it drew and on
//     what it registered as tappable. See host-tests/ui/.
//
// That second point is why this header includes only the SDK and the tokens.
// Anything renderer-shaped belongs in ToyboxTheme.h, which screens must not
// include; host-tests/ui/run.sh compiles the builders with nothing else on the
// include path, so a stray dependency fails the build rather than quietly
// costing the tests.
//
// Activities keep what genuinely needs hardware: reading state, filling in the
// model, and drawing their own play surface into the body rect.

#include <FreeInkApp.h>
#include <FreeInkUI.h>
#include <FreeInkUIIcon.h>
#include <Icon.h>

#include "../../../lib/GfxRenderer/RevealedInteractions.h"
#include "ToyboxText.h"
#include "ToyboxTokens.h"

namespace toybox {

// One size for every screen in this fork, sized for the largest: the
// Connections board's sixteen tiles plus its three action buttons, with room to
// grow. This was 16 and it was not enough -- the board silently dropped all
// three buttons, and the only reason that took minutes rather than an afternoon
// is that the buffer records overflow and toybox::reportOverflow logs it. Raise
// this, do not trim a screen to fit it.
constexpr size_t kMaxInteractions = 24;

// The hit table, plus the one thing the SDK buffer cannot know: whether the
// panel has actually SHOWN the table being routed against.
//
// The mechanism, its rule and the reasoning behind that rule now live in
// lib/GfxRenderer/RevealedInteractions.h, because three different layers need
// it and none of them can reach the others: these toybox screens, the shared
// components built on a raw InteractionBuffer (src/components/OptionPopup.h,
// src/activities/util/KeyboardEntryActivity), and the eight games that
// hit-test a play surface against GEOMETRY rather than against a table. The
// short version: a tap routes unless the table it would route against has
// changed since the last one the panel showed and no paint has landed since,
// and an UNCHANGED table always routes, which is what keeps touch alive.
//
// Still not covered from anywhere, because nothing outside the SDK can reach
// it: the FreeInkApp stack's own table, which lives in FreeInkApp's private
// interactions_. src/components/UiAppHost.cpp gates that stack at screen
// ENTRY with a paintclock::RevealGate instead.
using Interactions = paintclock::RevealedInteractions<kMaxInteractions>;

namespace detail {
// Storage for the two things every screen hands in as a temporary.
//
// freeink::ui::Frame keeps a `const DeviceContext&` and freeink::ui::Screen a
// `const ThemeTokens&`, while `target.deviceContext()` and `themeTokens()` both
// return by value. Writing the obvious
//
//     toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
//     toybox::Screen screen(frame, toybox::themeTokens());
//
// binds each reference to a temporary that dies at the end of its own
// statement, and every layout read afterwards is undefined. It read correct for
// a year on both real targets because the dead stack slot still held the right
// bytes; compiled for wasm32 the next call reused it and Screen::contentRect()
// came back (60,21 0x0), so games drew their whole board off the top of the
// panel. The SDK knows the hazard -- FreeInkUIGfxRenderer.h's GfxRendererFrame
// keeps a DeviceContext member for exactly this reason -- but nothing stops a
// caller from getting it wrong.
//
// A base is initialised before the bases listed after it, so holding each value
// in a base of the wrapper gives the SDK object something that outlives it. The
// call sites do not change; they simply stop being wrong.
struct OwnedDevice {
  freeink::ui::DeviceContext ownedDevice;
};

// Runs before every other base, which is the whole point: freeink::ui::Frame's
// constructor calls interactions.clear(), so anything wanting to look at the
// table the panel is currently showing has to look before that. Listed first
// in Frame's base list below.
struct BuildMark {
  explicit BuildMark(Interactions& interactions) { interactions.beginBuild(); }
};
}  // namespace detail

// The InputSnapshot is still held by reference, which is correct: every caller
// passes a named local, and copying it would break a frame whose input is
// filled in after construction.
class Frame : private detail::BuildMark, private detail::OwnedDevice, public freeink::ui::Frame<kMaxInteractions> {
 public:
  Frame(freeink::ui::DrawTarget& target, const freeink::ui::DeviceContext& device,
        const freeink::ui::InputSnapshot& input, Interactions& interactions,
        freeink::ui::AssetResolver* assets = nullptr)
      : detail::BuildMark(interactions),
        detail::OwnedDevice{device},
        freeink::ui::Frame<kMaxInteractions>(target, OwnedDevice::ownedDevice, input, interactions, assets) {}
};

// The theme is not a parameter because it never varied: all 22 call sites asked
// for themeTokens(), and it is now a function-local static, so referring to it
// straight cannot dangle. That is the same guarantee the removed OwnedTheme base
// bought by copying, minus the 2712 bytes of stack the copy cost every frame.
//
// A screen wanting its own palette would take one again, but it would have to
// hand over storage that outlives the screen rather than a temporary. Nothing
// needs that today, and pretending otherwise is what nearly bricked v1.0.0.
class Screen : public freeink::ui::Screen<kMaxInteractions> {
 public:
  // The shared palette. What 20 of the 22 screens want.
  explicit Screen(freeink::ui::Frame<kMaxInteractions>& frame)
      : freeink::ui::Screen<kMaxInteractions>(frame, themeTokens()) {}

  // A palette of the caller's own, for the two screens that raise the header
  // band. `theme` is referred to, not copied, so it has to outlive the screen: a
  // named local in the same render() does, which is every real use.
  Screen(freeink::ui::Frame<kMaxInteractions>& frame, const freeink::ui::ThemeTokens& theme)
      : freeink::ui::Screen<kMaxInteractions>(frame, theme) {}

  // Passing a temporary is the bug the old owning copy existed to prevent, and
  // it now fails to compile instead of costing 2712 bytes of stack on every
  // screen to guard against. Deleting the rvalue overload is the whole fix.
  Screen(freeink::ui::Frame<kMaxInteractions>&, freeink::ui::ThemeTokens&&) = delete;
};

// Toybox chrome opts OUT of the device safe area: call FIRST in a screen
// builder, before headerBand(). The header band is paint and may bleed
// under the bezel -- its title ink sits well below the covered rows -- and
// every layout in these apps is tuned against the band at panel row 0.
// Shifting the chrome down by the insets ate the gaps those layouts were
// tuned for (boards touched the rule, band decorations drifted) while
// buying nothing: no game ever drew content in the covered rows. Content
// that must clear the glass (the readers, xkcd, system lists) takes the
// safe rect through its own path and does not use this.
inline void absoluteChrome(Screen& screen) { screen.setContentMarginAbsolute(freeink::ui::Insets{}); }

// The width the header component will really give the title, computed with the
// component's own arithmetic (components/controls/header.h) rather than from
// the page margins.
//
// Guessing it is not a smaller version of this; it is a different number. The
// two readers fit their headline to `width - 2 * kMargin - pageLabel`, and the
// band also carries a SAVE button whose width is in neither term -- so a
// headline is trimmed to a room it does not get and then trimmed AGAIN by the
// component, twice-elided, on the one screen in this fork whose title is always
// somebody else's sentence. Every term the component subtracts is subtracted
// here, in the same order, so the fit and the draw cannot drift.
//
// `props` must already carry the theme substitutions Screen::header() makes,
// because the padding and the right label's style are two of the terms.
inline int16_t headerTitleWidth(Screen& screen, const freeink::ui::Rect& band, const freeink::ui::HeaderProps& props) {
  namespace fui = freeink::ui;
  const int16_t sidePad = props.sidePadding < 0 ? 6 : props.sidePadding;
  int16_t width = static_cast<int16_t>(band.width - sidePad * 2);
  if (props.leftReserve > 0) width = static_cast<int16_t>(width - props.leftReserve);
  if (props.rightReserve > 0) width = static_cast<int16_t>(width - props.rightReserve);

  const fui::BitmapRef leading =
      props.leadingIcon ? props.leadingIcon : fui::resolveBitmap(screen.frame().assets(), props.leadingIconAsset);
  if (leading && props.leadingAction != fui::NO_ACTION && !props.centered) {
    width = static_cast<int16_t>(width - (band.height - 8) - 8);
  }

  const fui::BitmapRef trailing =
      props.trailingIcon ? props.trailingIcon : fui::resolveBitmap(screen.frame().assets(), props.trailingIconAsset);
  if ((props.trailingLabel != nullptr || trailing) && props.trailingAction != fui::NO_ACTION) {
    int16_t buttonW = static_cast<int16_t>(band.height - 8);
    if (props.trailingLabel != nullptr) {
      const fui::Size label =
          screen.target().measureText(props.trailingText.font, props.trailingLabel, props.trailingText);
      buttonW = static_cast<int16_t>(label.width + 20 + (trailing ? trailing.width + 4 : 0));
    }
    if (!props.centered) width = static_cast<int16_t>(width - buttonW - 8);
  }

  if (props.rightLabel != nullptr) {
    const fui::Size label = screen.target().measureText(props.subtitleText.font, props.rightLabel, props.subtitleText);
    const int16_t used = static_cast<int16_t>(label.width + 6);
    // A centred title gives up the same width on BOTH sides so it stays centred
    // on the band, which is the component's rule and not a doubling by mistake.
    width = static_cast<int16_t>(width - (props.centered ? used * 2 : used));
  }
  return width > 0 ? width : 0;
}

// The header band. Paint and ink are placed by different rules, and the split
// is the whole point.
//
// The PAINT starts at the panel's physical top-left corner and spans the full
// width, covering the rows and columns the bezel hides. Those pixels are not
// invisible, they are only invisible HEAD-ON: the glass sits above the panel,
// so an eye below the device sees past the bezel's edge, and paper left there
// reads as a white strip above a black band. Full-bleed is the same rule
// headerRule() already follows -- paint may run under the bezel, content may
// not.
//
// The INK stays in the visible part: the title (and any right label) centres
// between the bezel's safe top and the band's bottom, never in rows nobody can
// read.
//
// The band's bottom edge is wherever the chrome put it, taken from the content
// rect exactly as screen.header(props) would take it, so nothing below moves --
// under absolute chrome (bottom at the tuned kHeaderHeight) or under the safe
// area alike. Call in place of screen.header(props).
//
// The TITLE is fitted before it is drawn, which is the one thing this wrapper
// adds beyond placement. See headerTitleWidth() below for why the width is
// computed rather than guessed, and ToyboxText.h's fittedTitle() for the rule.
// Doing it here rather than at the call sites is deliberate: there are
// twenty-four copies of this chrome across the apps, every one of them ends in
// this function, and a rule added to one copy at a time is a rule the
// twenty-fifth copy will not have.
inline void headerBand(Screen& screen, const freeink::ui::HeaderProps& props) {
  namespace fui = freeink::ui;
  const fui::Rect band = screen.takeTop(screen.theme().headerHeight);
  const int16_t safeTop = screen.frame().safeRect().y;
  const int16_t inkTop = band.y > safeTop ? band.y : safeTop;
  const fui::StyleSet& styles = props.styles.unset() ? screen.theme().popup : props.styles;
  // Paint first, ink second: header() fills its own rect in the same colour, so
  // the two agree wherever they overlap. Square and borderless, which every
  // band in this fork is (popup radius 0, headerUnderline 0): a rounded or
  // top-bordered band would need its corners and its rule carried up here too.
  screen.target().fill(fui::makeRect(0, 0, screen.device().screen().width, band.bottom()),
                       styles.resolve(fui::StateNormal).background);

  // The rect the component is really handed, which is the band CLIPPED to the
  // visible rows: the bezel hides the top ten, and the ink stays below them.
  // The fitting is done against this one and not against the band, because the
  // component sizes its leading and trailing buttons from `rect.height - 8` --
  // measuring those against the taller band reserved ten pixels the title was
  // then never given back.
  const fui::Rect ink = fui::makeRect(band.x, inkTop, band.width, static_cast<int16_t>(band.bottom() - inkTop));

  fui::HeaderProps fitted = props;
  // The theme substitutions Screen::header() is about to make, made here first
  // because the fitting needs the real style and the real padding. Taking the
  // theme's title style whole -- rather than building one -- is what keeps the
  // band's ink WHITE: titleText.color is White in the tokens precisely because
  // this band is always solid black, and a style assembled field by field here
  // would paint every title in the fork black on black.
  if (fui::textStyleUnset(fitted.titleText)) {
    fitted.titleText = screen.theme().titleText;
    fitted.titleText.align = screen.theme().headerTitleAlign;
  }
  if (fui::textStyleUnset(fitted.subtitleText)) fitted.subtitleText = screen.theme().smallText;
  if (fitted.sidePadding < 0) fitted.sidePadding = screen.theme().headerSidePadding;
  const std::string title =
      fittedTitle(screen.target(), fitted.title, headerTitleWidth(screen, ink, fitted), fitted.titleText);
  if (fitted.title != nullptr) fitted.title = title.c_str();

  screen.header(fitted, ink);
}

// Vertical top for an element of height h centred in the VISIBLE part of the
// header band, for the decorations that ride it (folder marks, medal
// tallies, face doors). Matches headerBand()'s centring.
inline int16_t bandCenterY(Screen& screen, const int16_t elementH) {
  const int16_t visibleTop = screen.frame().safeRect().y;
  const int16_t bandH = screen.theme().headerHeight;
  return static_cast<int16_t>(visibleTop + (bandH - visibleTop - elementH) / 2);
}

// The rule under the header band. Full-bleed on purpose -- paint may run
// under the bezel, content may not -- and derived from the screen's content
// top rather than a constant, so it tracks the band wherever the screen
// puts it. Call immediately after screen.header(...): the band was just
// taken from the content top, so body().y is the band's bottom edge.
inline void headerRule(Screen& screen) {
  namespace fui = freeink::ui;
  screen.target().fill(
      fui::makeRect(0, static_cast<int16_t>(screen.body().y + 4), screen.device().screen().width, toybox::kRule),
      fui::Paint::solid(fui::Color::Black));
}

// Every icon this fork draws, at the one size ToyboxIcons.h generates.
constexpr int16_t kIconSize = 32;

// An icon at the right edge of row `index` in a list band.
//
// The FreeInkUI list component only draws icons on the left, which indents
// every label and leaves the text starting at a different x from the header
// above it. Right-aligned, the icons share one axis and the labels stay flush.
//
// `index` is the item's position in the whole list and `topIndex` is the item
// drawn on the band's first row, exactly as the list component understands
// them. Both are required rather than defaulted: this used to take the absolute
// index alone, which is correct only while nothing scrolls, and the tenth shelf
// item painted its icon a row below the band -- in black, on top of the black
// player footer. Every caller now has to say where its list is scrolled to, and
// a caller that does not scroll says 0 and means it.
//
// Rows above the band are skipped, and so is a row that would overflow it,
// matching the list component's own rule so the icons stop exactly where the
// labels do.
//
// `selected` picks the ink: the selected row is filled black, so its icon has
// to be paper.
inline void iconAtRowRight(Screen& screen, const freeink::ui::Rect& band, const int index, const int topIndex,
                           const freeink::Icon& icon, const bool selected) {
  namespace fui = freeink::ui;
  const int16_t rowHeight = screen.theme().rowHeight;
  const int16_t rowGap = screen.theme().listRowGap;
  const int row = index - topIndex;
  if (row < 0) return;
  const int16_t rowY = static_cast<int16_t>(band.y + row * (rowHeight + rowGap));
  if (rowY + rowHeight > band.y + band.height) return;
  const fui::Rect where = fui::makeRect(static_cast<int16_t>(band.x + band.width - kIconSize - kGutter * 2),
                                        static_cast<int16_t>(rowY + (rowHeight - kIconSize) / 2), kIconSize, kIconSize);
  screen.target().bitmap(where, fui::bitmapFromIcon(icon), fui::BitmapMode::Contain,
                         fui::Paint::solid(selected ? fui::Color::White : fui::Color::Black));
}

// A filled disc, by rows.
//
// Lives here rather than in one game because two of them are made of discs, and
// the first hand-rolled version was wrong in a way that is easy to repeat: it
// stacked fixed-height bars from a table of half-widths and drew the outline as
// vertical bars at each bar's ends. Nothing closed the top or the bottom, and
// consecutive bars did not overlap where the table jumped, so an outlined piece
// rendered as two parenthesis arcs and four floating dots.
//
// Use it in pairs to make a ring: a disc in ink with a smaller disc in paper on
// top closes by construction. The circle test is per row, which also avoids the
// flat-topped lozenge silhouette the table produced.
inline void disc(Screen& screen, const int16_t cx, const int16_t cy, const int16_t r, const freeink::ui::Color colour) {
  namespace fui = freeink::ui;
  const fui::Paint paint = fui::Paint::solid(colour);
  for (int16_t dy = static_cast<int16_t>(-r); dy <= r; ++dy) {
    int16_t half = 0;
    while ((half + 1) * (half + 1) + dy * dy <= r * r) ++half;
    if (half <= 0) continue;
    screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - half), static_cast<int16_t>(cy + dy),
                                       static_cast<int16_t>(half * 2), 1),
                         paint);
  }
}

// A ring: `r` outside, `weight` thick, over whatever `fill` the middle should be.
inline void ring(Screen& screen, const int16_t cx, const int16_t cy, const int16_t r, const int16_t weight,
                 const freeink::ui::Color edge, const freeink::ui::Color fill) {
  disc(screen, cx, cy, r, edge);
  disc(screen, cx, cy, static_cast<int16_t>(r - weight), fill);
}

// Four corner marks around a rect: flag a square without covering what stands on
// it. GfxRenderer has cornerMarks for the same job, but a freestanding screen
// builder has no renderer, so this is that shape drawn from fills.
inline void bracket(Screen& screen, const freeink::ui::Rect& box, const int16_t arm, const int16_t weight) {
  namespace fui = freeink::ui;
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int16_t right = static_cast<int16_t>(box.x + box.width - arm);
  const int16_t bottom = static_cast<int16_t>(box.y + box.height - weight);
  const int16_t far = static_cast<int16_t>(box.x + box.width - weight);
  const int16_t low = static_cast<int16_t>(box.y + box.height - arm);
  screen.target().fill(fui::makeRect(box.x, box.y, arm, weight), ink);
  screen.target().fill(fui::makeRect(right, box.y, arm, weight), ink);
  screen.target().fill(fui::makeRect(box.x, bottom, arm, weight), ink);
  screen.target().fill(fui::makeRect(right, bottom, arm, weight), ink);
  screen.target().fill(fui::makeRect(box.x, box.y, weight, arm), ink);
  screen.target().fill(fui::makeRect(far, box.y, weight, arm), ink);
  screen.target().fill(fui::makeRect(box.x, low, weight, arm), ink);
  screen.target().fill(fui::makeRect(far, low, weight, arm), ink);
}

}  // namespace toybox
