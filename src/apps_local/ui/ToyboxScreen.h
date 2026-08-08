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

#include "ToyboxTokens.h"

namespace toybox {

// One size for every screen in this fork, sized for the largest: the
// Connections board's sixteen tiles plus its three action buttons, with room to
// grow. This was 16 and it was not enough -- the board silently dropped all
// three buttons, and the only reason that took minutes rather than an afternoon
// is that the buffer records overflow and toybox::reportOverflow logs it. Raise
// this, do not trim a screen to fit it.
constexpr size_t kMaxInteractions = 24;

using Interactions = freeink::ui::InteractionBuffer<kMaxInteractions>;

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
}  // namespace detail

// The InputSnapshot is still held by reference, which is correct: every caller
// passes a named local, and copying it would break a frame whose input is
// filled in after construction.
class Frame : private detail::OwnedDevice, public freeink::ui::Frame<kMaxInteractions> {
 public:
  Frame(freeink::ui::DrawTarget& target, const freeink::ui::DeviceContext& device,
        const freeink::ui::InputSnapshot& input, Interactions& interactions,
        freeink::ui::AssetResolver* assets = nullptr)
      : detail::OwnedDevice{device},
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

}  // namespace toybox
