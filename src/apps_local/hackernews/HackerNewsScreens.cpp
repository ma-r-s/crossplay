#include "HackerNewsScreens.h"

#include <FreeInkUIIcon.h>

#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxText.h"

namespace hnui {
namespace {

// The top of any body: below the header band and the rule Toybox draws under
// it. Shared by all three screens so they line up with each other and with the
// shelf the reader just came from.
constexpr int kBodyTop = toybox::kHeaderHeight + toybox::kGutter * 3;

// The reader's footer: one row of three controls.
constexpr int kFooterHeight = toybox::kPillHeight;

// A control that sits ON the header band, where the ordinary pair of styles is
// upside down.
//
// toybox::invertedStyles() is a solid black fill: on paper it is the loud one,
// on this band it IS the band, so "filled" disappears and only the knocked-out
// glyph is left. rowStyles() is a white fill with a black hairline: on the band
// the hairline vanishes and the white fill is the loudest thing on the screen.
// A mark styled "filled means saved" out of those two therefore reads exactly
// backwards, and two cold testers read it backwards -- one of them removed an
// article believing they had just kept it.
//
// So the band gets its own pair, the same idea re-derived against black ground:
// present is the white chip, absent is the outline drawn in paper.
fui::StyleSet bandFilledStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::White);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  styles.disabled = styles.normal;
  return styles;
}

fui::StyleSet bandOutlineStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  // The band's own black, so the chip is a shape drawn in its outline rather
  // than a second ground. The border has to be PAPER: a black hairline on a
  // black band is the invisible half of the bug above.
  styles.normal.background = fui::Paint::solid(fui::Color::Black);
  styles.normal.foreground = fui::Paint::solid(fui::Color::White);
  styles.normal.border = fui::Paint::solid(fui::Color::White);
  styles.normal.borderWidth = toybox::kHairline;
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  styles.disabled = styles.normal;
  return styles;
}

// Header band, rule, and the page margin. Every screen here opens with this.
//
// `rightLabel` is drawn in paper, not ink. The band is solid black and the
// header component draws rightLabel with `subtitleText`, whose default colour
// is Black -- so a label left at the default is painted black on black and
// simply is not there. The first fix styled `trailingText`, which the
// component only consults for its trailing BUTTON, never for rightLabel; the
// label stayed invisible through two renders and the suite carried a failing
// pin (paperOnTheBand) against exactly this until the fix landed. The games'
// toyboxChrome copies had the right slot all along; jaipur paid for it first.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel,
            const fui::TextStyle* titleText = nullptr, const bool showSave = false, const bool saved = false) {
  fui::HeaderProps header;
  header.title = title;
  if (showSave) {
    // The chip is filled once the piece is on the device, and it says so in a
    // word. The mark alone cannot: this icon is one 1-bpp mask, so the glyph
    // never fills and the only thing that ever changed was the chip behind it.
    // A verb for what a tap will do, a past tense for what it did -- which is
    // also the confirmation the screen owed anyone who just tapped it.
    header.trailingIcon = fui::bitmapFromIcon(icon_saved_32);
    header.trailingLabel = saved ? "SAVED" : "SAVE";
    header.trailingAction = saved ? ActionUnsave : ActionSave;
    header.trailingStyles = saved ? bandFilledStyles() : bandOutlineStyles();
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (titleText != nullptr) header.titleText = *titleText;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

// --- Fitting a headline --------------------------------------------------

std::string fitLines(const fui::DrawTarget& target, const char* text, const int16_t width, const int lines,
                     const fui::TextStyle& style) {
  // Moved to src/apps_local/ui/ToyboxText.h when a second app needed it. Kept
  // as a wrapper rather than deleted: this name is what the header promises
  // and what host-tests/ui asserts against, and a rename would have been a
  // second change riding an extraction.
  return toybox::fitLines(target, text, width, lines, style);
}

// --- The front page ------------------------------------------------------

fui::Rect listBand(const fui::DeviceContext& device) {
  // The segment row lives at the bottom, so the rows stop above it -- the same
  // shape the reader's footer already has. Reserved here rather than at the
  // draw site because the Activity counts the rows that fit in this exact
  // rect: leaving it full-height drew rows underneath the segments and paged
  // by a count the screen never showed.
  const int bottom = toybox::kMargin + kFooterHeight + toybox::kGutter;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

int16_t listRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens) {
  // Two lines of headline plus air. A Hacker News title *is* the story, so it
  // gets the room and the comment count rides along as a footnote beside it.
  return static_cast<int16_t>(2 * target.lineHeight(tokens.bodyText.font) + toybox::kGutter);
}

fui::TextStyle listCountStyle(const fui::ThemeTokens& tokens) {
  fui::TextStyle count = tokens.smallText;
  count.font = toybox::kTileFont;
  // FONT_SLOT_SMALL is 0, and textStyleUnset() reads a style whose font is 0
  // and whose every other field is default as "the caller did not set this" --
  // so Screen::list() would put the theme's value style back and the footnote
  // would return at full size. Naming the alignment the component applies
  // anyway is what marks this style as owned.
  count.align = fui::TextAlign::Right;
  return count;
}

int16_t listTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                       const fui::ThemeTokens& tokens) {
  // What the label actually gets: the row less its side padding on both edges,
  // less the widest count the footnote will ever show and the gap before it.
  // Derived from the numbers buildList hands the component, so a headline is
  // measured against the space it is really drawn into.
  const fui::TextStyle count = listCountStyle(tokens);
  const int16_t countWidth = target.measureText(count.font, "8888", count).width;
  return static_cast<int16_t>(listBand(device).width - 2 * tokens.listSidePadding - countWidth - toybox::kGutter);
}

void buildList(toybox::Screen& screen, const ListModel& model) {
  chrome(screen, model.title, nullptr);

  // The two halves of the library, as a pair of segments rather than a toggle:
  // each names where it goes, so the one you are already in is simply inert.
  //
  // takeBottom rather than absolute coordinates: it removes the strip from the
  // content flow, so the list below cannot draw rows into it. Positioning the
  // segments absolutely left them underneath the list, which both hid them and
  // gave the rows the taps meant for them.
  {
    const fui::Rect strip = screen.takeBottom(kFooterHeight, toybox::kGutter);
    const int16_t y = strip.y;
    const int16_t half = static_cast<int16_t>((strip.width - toybox::kGutter) / 2);
    const auto segment = [&screen, y, half](const char* label, const fui::ActionId action, const int16_t x,
                                            const bool here) {
      fui::ButtonProps button;
      button.label = label;
      // Inert where you already are, rather than absent: a segment that
      // disappears moves its neighbour, and the pair is the map.
      button.action = here ? fui::NO_ACTION : action;
      // Filled is where you are, outlined is where you can go. Both must be
      // set: leaving the other to the default drew two filled segments, which
      // says "both" and so says nothing.
      button.styles = here ? toybox::invertedStyles() : toybox::rowStyles();
      screen.button(button, fui::makeRect(x, y, half, kFooterHeight));
    };
    segment("FRONT PAGE", ActionShowFrontPage, strip.x, !model.showingSaved);
    segment("SAVED", ActionShowSaved, static_cast<int16_t>(strip.x + half + toybox::kGutter), model.showingSaved);
  }

  if (model.count <= 0) {
    // An empty SAVED shelf is the ordinary state of a new device, so it gets a
    // sentence rather than a blank panel that reads as a fault.
    if (model.emptyHeadline != nullptr) {
      // OFF THE BAND, SO IT HAS TO BE INK. The display cut is otherwise only
      // ever set on the header band, so its token colour is paper -- and taken
      // as given here it painted NOTHING SAVED YET white on white paper. Same
      // trap as the page label that went missing on the band, one screen along
      // and the other way up.
      //
      // And the invisible half was hiding the visible one: centeredText centres
      // in the content rect and CONSUMES NOTHING, so two calls draw at the same
      // y. The headline was painted over the sentence all along and no one
      // could see it happening. So the pair is laid out as a block, the way
      // buildNotice stacks its own, and centred as a block.
      fui::TextStyle headline = screen.theme().titleText;
      headline.color = fui::Color::Black;
      headline.align = fui::TextAlign::Center;
      const fui::Rect body = screen.body();
      // Both blocks reserve what the SDK's OWN WRAP will emit, never a
      // single-line width divided by the column. Greedy wrapping breaks between
      // words, so it does not fill a line to the edge: a sentence 2.6 columns
      // wide needs THREE lines while the division says two, and the third is
      // dropped with an ellipsis this cut is perfectly able to draw -- so the
      // glyph gate stays quiet and the screenshot looks finished. That is how
      // "Saved articles do ..." got as far as a render. measureWrappedText's
      // own header warns against the estimate it replaces.
      headline.maxLines = 2;
      const int16_t headlineH =
          fui::measureWrappedText(screen.target(), model.emptyHeadline, headline, body.width).height;

      fui::TextStyle message = screen.theme().smallText;
      message.align = fui::TextAlign::Center;
      // A ceiling, not a target: the wrap emits what the sentence needs and the
      // reservation follows it. Four lines is more than any wording here wants
      // and still bounds a mistake.
      message.maxLines = 4;
      const bool hasMessage = model.emptyMessage != nullptr;
      const int16_t messageH =
          hasMessage ? fui::measureWrappedText(screen.target(), model.emptyMessage, message, body.width).height : 0;
      const int16_t gap = hasMessage ? toybox::kGutter : 0;

      // The control, when there is one. Sized like every other pill in this app
      // so it reads as a button rather than as a second heading.
      const bool hasAction = model.emptyActionLabel != nullptr && model.emptyAction != fui::NO_ACTION;
      const int16_t actionH = hasAction ? static_cast<int16_t>(kFooterHeight + toybox::kGutter * 2) : 0;

      // Every element ADVANCES y as it is placed, and none of them advances for
      // an element that was not drawn. The button used to step over messageH
      // whether or not a message had been drawn, so a headline with no sentence
      // under it would have had the control composited on top of it -- which is
      // the standing trap on this screen (centeredText consumes nothing, and
      // that is how the headline came to be painted over the sentence).
      int16_t y = static_cast<int16_t>(body.y + (body.height - headlineH - gap - messageH - actionH) / 2);
      screen.target().text(fui::makeRect(body.x, y, body.width, headlineH), model.emptyHeadline, headline);
      y = static_cast<int16_t>(y + headlineH);
      if (hasMessage) {
        y = static_cast<int16_t>(y + gap);
        screen.target().text(fui::makeRect(body.x, y, body.width, messageH), model.emptyMessage, message);
        y = static_cast<int16_t>(y + messageH);
      }
      if (hasAction) {
        y = static_cast<int16_t>(y + toybox::kGutter * 2);
        fui::ButtonProps button;
        button.label = model.emptyActionLabel;
        button.action = model.emptyAction;
        button.styles = toybox::invertedStyles();
        button.radius = static_cast<uint8_t>(toybox::kPillRadius);
        // Narrower than the body and centred, so it cannot be mistaken for the
        // full-width segment strip below it.
        const int16_t width = static_cast<int16_t>(body.width * 3 / 4);
        screen.button(button, fui::makeRect(static_cast<int16_t>(body.x + (body.width - width) / 2), y, width,
                                            static_cast<int16_t>(kFooterHeight)));
      }
    } else {
      screen.centeredText("NOTHING TO READ", screen.theme().bodyText);
    }
    return;
  }

  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  list.topIndex = static_cast<uint16_t>(model.topIndex);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionOpenStory;
  list.rowHeight = listRowHeight(screen.target(), screen.theme());
  list.labelText = screen.theme().bodyText;
  list.labelText.maxLines = 2;
  list.valueText = listCountStyle(screen.theme());
  // Off, or a wrapping headline is capped at 60% of the row so it sits prettily
  // beside its value. That balance is right for a settings row with a short
  // value; here the headline is the content and the count is a footnote.
  list.balanceWrappedLabelWithValue = false;
  screen.list(list);
}

// --- The reader ----------------------------------------------------------

fui::Rect readerBody(const fui::DeviceContext& device) {
  const int bottom = toybox::kMargin + kFooterHeight + toybox::kGutter;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

uint32_t readerLineCount(const fui::DrawTarget& target, const fui::DeviceContext& device, ReaderBody& body) {
  if (body.wrap == nullptr) return 0;
  return body.wrap->lineCount(target, readerBody(device).width, body.text, body.style);
}

void buildReader(toybox::Screen& screen, const ReaderModel& model, ReaderBody& body) {
  // The band carries the story's own headline. Within this app chrome is
  // Jersey and content is the reading face, and a title is content --
  // somebody's sentence, in its own case -- so the band borrows the reading
  // cut in paper, the way a book's running header borrows the text's. Fitted
  // to the room the page label leaves; fitLines marks the cut when a headline
  // will not go.
  fui::TextStyle bandTitle = screen.theme().bodyText;
  bandTitle.color = fui::Color::White;
  bandTitle.maxLines = 1;
  const fui::TextStyle& labelStyle = screen.theme().smallText;
  const int16_t labelWidth =
      model.pageLabel != nullptr && model.pageLabel[0] != '\0'
          ? static_cast<int16_t>(screen.target().measureText(labelStyle.font, model.pageLabel, labelStyle).width +
                                 toybox::kGutter)
          : 0;
  const int16_t room = static_cast<int16_t>(screen.device().width - 2 * toybox::kMargin - labelWidth);
  const std::string headline = fitLines(screen.target(), model.title, room, 1, bandTitle);
  chrome(screen, headline.c_str(), model.pageLabel, &bandTitle, model.canSave, model.saved);

  const fui::DeviceContext& device = screen.device();

  // Three controls across the bottom: back a page, swap between the article and
  // the thread, forward a page. Laid out from one set of numbers, and each
  // button registers the rect it was drawn into, so a control cannot be live
  // anywhere its pixels are not. The wide middle is the deliberate one -- it is
  // the only control here that changes what you are reading.
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const int16_t usable = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t arrow = static_cast<int16_t>((usable - 2 * toybox::kGutter) / 4);
  const int16_t middle = static_cast<int16_t>(usable - 2 * arrow - 2 * toybox::kGutter);

  const auto footerButton = [&screen, footerY](const char* label, const fui::ActionId action, const int16_t x,
                                               const int16_t width, const bool enabled) {
    fui::ButtonProps button;
    button.label = label;
    button.action = enabled ? action : fui::NO_ACTION;
    // Dimmed rather than removed: a control that vanishes moves its neighbours,
    // and on e-ink that costs a repaint of the whole bar. The dither goes in
    // the fill because there is no grey text on this panel.
    if (!enabled) button.styles = toybox::disabledButtonStyles();
    screen.button(button, fui::makeRect(x, footerY, width, kFooterHeight));
  };

  const int16_t left = toybox::kMargin;
  footerButton("<", ActionPagePrev, left, arrow, model.canPagePrev);
  footerButton(model.showingComments ? "ARTICLE" : "COMMENTS", ActionSwapView,
               static_cast<int16_t>(left + arrow + toybox::kGutter), middle, model.swapAvailable);
  footerButton(">", ActionPageNext, static_cast<int16_t>(left + arrow + middle + 2 * toybox::kGutter), arrow,
               model.canPageNext);

  // Drawn into readerBody() rather than into the Screen's running content rect,
  // because the Activity counts the lines that fit in this exact rect to decide
  // what a page turn does. Two ways of arriving at the same rectangle is how a
  // page turn starts eating a line, so there is one function and both callers
  // use it.
  // Through the wrap rather than fui::textArea(). A flattened comment thread
  // is tens of kilobytes and textArea() walks it from byte zero to find the
  // twenty lines it draws, so paging into the middle of a thread cost the
  // whole thread -- twice per paint, counting the measure above.
  if (body.wrap != nullptr) {
    body.wrap->draw(screen.target(), readerBody(device), body.text, body.style, model.topLine);
  }
}

// --- Notices -------------------------------------------------------------

NoticeControl noticeControl(const bool unreadable) {
  // An unreadable link is not a failure: the app reached Hacker News, judged the
  // page, and is telling you so. The conversation is the thing still worth
  // having, so the button goes onward to it.
  if (unreadable) return {"READ THE COMMENTS", ActionNotice};
  // Everything else is a failure, and the useful destination is the LIST --
  // which carries both segments, so the SAVED shelf is one tap from it. Not
  // "try again": the network has just been shown to be down, and the half of
  // this app that does not need one is what the reader wants offered.
  return {"BACK TO THE LIST", ActionNoticeBack};
}

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  // The band says the app, always, and never repaints between notices. What a
  // particular notice is about goes in the headline below it, where a sentence
  // has room to be one: at the display cut the band holds about fourteen
  // characters, and "NOT READABLE HERE" came out as "NOT READABLE HE".
  chrome(screen, "HACKER NEWS", nullptr);

  const fui::DeviceContext& device = screen.device();
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);

  // Bottom-anchored, and taken first so the body can never grow into it. The
  // lesser doors sit at the bottom, where a thumb rests.
  //
  // The action comes from the model now. It used to be hard-wired to
  // ActionNotice -- "read the comments" -- so the only notice that could carry
  // a control was the one that wanted that particular one, and every failure
  // screen drew none.
  const bool hasAction = model.actionLabel != nullptr && model.action != fui::NO_ACTION;
  if (hasAction) {
    fui::ButtonProps action;
    action.label = model.actionLabel;
    action.action = model.action;
    screen.button(action, fui::makeRect(toybox::kMargin,
                                        static_cast<int16_t>(device.height - toybox::kMargin - toybox::kPillHeight),
                                        width, toybox::kPillHeight));
  }

  // Everything else stacks from the top. A block floating with equal slack
  // above and below reads as unresolved, so this is anchored under the header
  // and the slack becomes one deliberate zone above the button.
  int16_t y = kBodyTop;

  if (model.mark != nullptr) {
    const int16_t markSize = toybox::kIconSize * 2;
    // Black on paper. The mark is a 1-bpp mask painted in one colour, so it is
    // invisible on a background of that colour; this one has to stay off the
    // header band, which is why it starts at kBodyTop.
    screen.target().bitmap(fui::makeRect(toybox::kMargin, y, markSize, markSize), fui::bitmapFromIcon(*model.mark),
                           fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
    y = static_cast<int16_t>(y + markSize + toybox::kGutter);
  }

  if (model.headline != nullptr && model.headline[0] != '\0') {
    fui::TextStyle headline = screen.theme().titleText;
    headline.color = fui::Color::Black;  // off the band now, so it has to be ink
    headline.align = fui::TextAlign::Left;
    headline.maxLines = 2;
    const int16_t headlineHeight = static_cast<int16_t>(2 * screen.target().lineHeight(headline.font));
    screen.target().text(fui::makeRect(toybox::kMargin, y, width, headlineHeight), model.headline, headline);
    y = static_cast<int16_t>(y + headlineHeight + toybox::kGutter);

    screen.target().fill(fui::makeRect(toybox::kMargin, y, width, toybox::kRule), fui::Paint::solid(fui::Color::Black));
    y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter * 2);
  }

  if (model.message != nullptr && model.message[0] != '\0') {
    // Through textArea because it wraps and centeredText does not: the first
    // build of this screen showed "This link is not a page of tex" and stopped.
    const int16_t reserved = hasAction ? static_cast<int16_t>(toybox::kPillHeight + toybox::kGutter) : 0;
    const int16_t bottom = static_cast<int16_t>(device.height - toybox::kMargin - reserved);
    fui::TextAreaProps message;
    message.text = model.message;
    message.showCaret = false;
    message.style = screen.theme().bodyText;
    fui::textArea(screen.frame(), fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(bottom - y)), message);
  }
}

}  // namespace hnui
