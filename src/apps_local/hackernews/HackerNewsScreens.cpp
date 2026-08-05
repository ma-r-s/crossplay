#include "HackerNewsScreens.h"

#include <FreeInkUIIcon.h>

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
// `rightLabel` is drawn in paper, not ink. The band is solid black and
// Screen::header() resolves the trailing style from the theme's body text,
// whose colour is Black -- so a label left at the default is painted black on
// black and simply is not there. That is how the page indicator went missing on
// the first render of the reader, and it is the same defect the header title
// had when this fork first adopted FreeInkUI.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    header.trailingText = screen.theme().smallText;
    header.trailingText.color = fui::Color::White;
  }
  screen.header(header);

  const fui::Rect panel = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, panel.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

// --- Fitting a headline --------------------------------------------------

std::string fitLines(const fui::DrawTarget& target, const char* text, const int16_t width, const int lines,
                     const fui::TextStyle& style) {
  if (text == nullptr || width <= 0 || lines <= 0) return std::string();

  const std::string whole(text);
  const auto fits = [&target, &style, width](const std::string& run) {
    return target.measureText(style.font, run.c_str(), style).width <= width;
  };

  // Greedy word wrap, the same shape the component uses when it draws this.
  // Walk the words; when a line will not take another, start the next one. If
  // the lines run out with words still to come, the title has to be cut.
  //
  // The last line is tracked separately from the text as a whole, and that is
  // the whole trick. The first version appended the ellipsis to the entire
  // string and then measured *that* against one line's width, so every headline
  // long enough to wrap was trimmed back until all of it fitted on line one:
  // the front page came out as "In Memory of My...", "Show HN: Simple...",
  // "I am retiring from...". Only the last line has to make room.
  std::string line;               // the line being filled
  size_t lineStart = 0;           // where it begins in `whole`
  size_t consumed = 0;            // end of the last word that fitted anywhere
  size_t lastLineStart = 0;       // where the final drawn line begins
  int lineNumber = 1;
  bool overflowed = false;

  size_t i = 0;
  while (i <= whole.size()) {
    const size_t space = whole.find(' ', i);
    const size_t end = space == std::string::npos ? whole.size() : space;
    const std::string word = whole.substr(i, end - i);
    const std::string candidate = line.empty() ? word : line + " " + word;

    if (fits(candidate)) {
      line = candidate;
      consumed = end;
      lastLineStart = lineStart;
    } else if (lineNumber < lines) {
      ++lineNumber;
      lineStart = i;
      line = word;
      // A single word wider than the line has nowhere to go, so let it be cut
      // rather than looping forever looking for a break that cannot exist.
      if (fits(word)) {
        consumed = end;
        lastLineStart = lineStart;
      }
    } else {
      overflowed = true;
      break;
    }

    if (end == whole.size()) break;
    i = end + 1;
  }

  if (!overflowed && consumed >= whole.size()) return whole;

  // Cut on a word boundary and say so. A title that stops mid-word reads as a
  // rendering fault; one that ends in an ellipsis reads as a long title, which
  // is what it is. Design language: shrink to fit, never break a word.
  std::string kept = whole.substr(0, consumed);
  while (!kept.empty() && kept.back() == ' ') kept.pop_back();
  if (lastLineStart > kept.size()) lastLineStart = 0;

  // Give back whole words from the final line until the ellipsis fits beside
  // it. Measured against that line alone, never against the paragraph.
  static constexpr const char* kEllipsis = "...";
  while (kept.size() > lastLineStart && !fits(kept.substr(lastLineStart) + kEllipsis)) {
    const size_t space = kept.rfind(' ');
    if (space == std::string::npos || space < lastLineStart) break;
    kept.resize(space);
  }
  return kept + kEllipsis;
}

// --- The front page ------------------------------------------------------

fui::Rect listBand(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - toybox::kMargin - kBodyTop));
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

  if (model.count <= 0) {
    screen.centeredText("NOTHING TO READ", screen.theme().bodyText);
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
  chrome(screen, model.title, model.pageLabel);

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
