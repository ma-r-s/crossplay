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
    // Filled means it is on the device, outlined means it can be. The mark is
    // the control, so there is no second button to find.
    header.trailingIcon = fui::bitmapFromIcon(icon_saved_32);
    header.trailingAction = saved ? ActionUnsave : ActionSave;
    header.trailingStyles = saved ? toybox::invertedStyles() : toybox::rowStyles();
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
      screen.centeredText(model.emptyHeadline, screen.theme().titleText);
      if (model.emptyMessage != nullptr) {
        screen.centeredText(model.emptyMessage, screen.theme().smallText);
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

void buildReader(toybox::Screen& screen, const ReaderModel& model) {
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
  fui::TextAreaProps body;
  body.text = model.text;
  body.topLine = model.topLine;
  body.showCaret = false;
  body.style = screen.theme().bodyText;
  fui::textArea(screen.frame(), readerBody(device), body);
}

// --- Notices -------------------------------------------------------------

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
  if (model.actionLabel != nullptr) {
    fui::ButtonProps action;
    action.label = model.actionLabel;
    action.action = ActionNotice;
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
    const int16_t reserved =
        model.actionLabel != nullptr ? static_cast<int16_t>(toybox::kPillHeight + toybox::kGutter) : 0;
    const int16_t bottom = static_cast<int16_t>(device.height - toybox::kMargin - reserved);
    fui::TextAreaProps message;
    message.text = model.message;
    message.showCaret = false;
    message.style = screen.theme().bodyText;
    fui::textArea(screen.frame(), fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(bottom - y)), message);
  }
}

}  // namespace hnui
