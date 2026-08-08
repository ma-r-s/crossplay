#include "HackerNewsScreens.h"

#include <FreeInkUIIcon.h>

#include "../ui/ToyboxIcons.h"

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
// It can also carry a save mark on its right edge, which fills once the article
// is on the device -- the same thing the footer segments do, so the app has one
// way of saying "you are here" rather than a second vocabulary for a second
// control.
//
// `rightLabel` is drawn in paper, not ink. The band is solid black and
// Screen::header() resolves the trailing style from the theme's body text,
// whose colour is Black -- so a label left at the default is painted black on
// black and simply is not there. That is how the page indicator went missing on
// the first render of the reader, and it is the same defect the header title
// had when this fork first adopted FreeInkUI.
void chrome(toybox::Screen& screen, const char* title, const char* rightLabel, const int titleLines = 1,
            const bool showSave = false, const bool saved = false, const fui::FontId titleFont = 0) {
  fui::HeaderProps header;
  header.title = title;
  // The line count is applied whether or not a font was named. It used to hang
  // off `titleFont`, and when the caller switched to rebinding the title slot
  // instead of naming a face, `titleLines` silently stopped being honoured: a
  // headline fitted to two lines was drawn on one and clipped mid-word, which
  // looks exactly like the step-down never happening.
  header.titleText = screen.theme().titleText;
  if (titleFont != 0) header.titleText.font = titleFont;
  header.titleText.maxLines = static_cast<uint8_t>(titleLines < 1 ? 1 : titleLines);
  if (showSave) {
    header.trailingIcon = fui::bitmapFromIcon(icon_saved_32);
    header.trailingAction = saved ? ActionUnsave : ActionSave;
    header.trailingStyles = saved ? toybox::invertedStyles() : toybox::rowStyles();
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    // subtitleText, not trailingText: the component draws rightLabel with the
    // subtitle style and reserves the trailing one for an action button. Styling
    // the wrong one leaves the label black on the black band, which looks
    // exactly like the label never being set at all -- the page indicator was
    // missing through two renders before this was read rather than assumed.
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
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
  std::string line;          // the line being filled
  size_t lineStart = 0;      // where it begins in `whole`
  size_t consumed = 0;       // end of the last word that fitted anywhere
  size_t lastLineStart = 0;  // where the final drawn line begins
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

// --- A pair of segments ---------------------------------------------------
//
// The one control this app repeats: two halves naming what there is to look at,
// with the one you are in filled. It is on the list (FRONT PAGE / SAVED) and in
// the reader (ARTICLE / COMMENTS), and both are drawn by this so they cannot
// drift into looking like different ideas.

// Three states, each saying something different. Here: filled, this is what you
// are looking at. There: outlined, you could be. Nothing: dithered, because a
// control that vanishes moves its neighbours -- and the dither goes in the fill,
// since this panel has no grey text.
enum class Seat { Here, There, Nothing };

void seatButton(toybox::Screen& screen, const char* label, const fui::ActionId action, const fui::Rect& where,
                const Seat seat) {
  fui::ButtonProps button;
  button.label = label;
  button.action = seat == Seat::There ? action : fui::NO_ACTION;
  button.text = toybox::buttonText(screen.theme());
  if (seat == Seat::Nothing) {
    button.styles = toybox::disabledButtonStyles();
  } else if (seat == Seat::There) {
    button.styles = toybox::rowStyles();
  }
  screen.button(button, where);
}

fui::Rect footerRow(const fui::DeviceContext& device) {
  return fui::makeRect(toybox::kMargin, static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight),
                       static_cast<int16_t>(device.width - 2 * toybox::kMargin), kFooterHeight);
}

// --- The front page ------------------------------------------------------

fui::Rect listBand(const fui::DeviceContext& device) {
  // Stops above the segment pair. Shared with the Activity's scroll maths, so a
  // row can never be styled as selected on a line the footer is covering.
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

  // The same two-segment control the reader wears, so the app has one shape for
  // "here are two things to look at, and this is the one you are in". Taken
  // before the list so the rows can never grow into it.
  // Taken out of the content rect rather than merely drawn over it. screen.list()
  // lays its rows into whatever content is left, so a footer that is only
  // painted on top gets rows drawn straight through it -- which is exactly what
  // the first render did.
  const fui::DeviceContext& device = screen.device();
  const fui::Rect footer = screen.takeBottom(kFooterHeight, toybox::kGutter);
  const int16_t half = static_cast<int16_t>(footer.width / 2);
  seatButton(screen, "FRONT PAGE", ActionShowFrontPage, fui::makeRect(footer.x, footer.y, half, footer.height),
             model.showingSaved ? Seat::There : Seat::Here);
  seatButton(screen, "SAVED", ActionShowSaved,
             fui::makeRect(static_cast<int16_t>(footer.x + half), footer.y, static_cast<int16_t>(footer.width - half),
                           footer.height),
             model.showingSaved ? Seat::Here : Seat::There);

  if (model.count <= 0 && model.emptyHeadline != nullptr) {
    // An empty SAVED shelf is the normal state of a new device, not a fault, so
    // it says what it is and what to do rather than leaving a blank panel that
    // reads as a failed load.
    fui::TextStyle headline = screen.theme().titleText;
    headline.color = fui::Color::Black;
    headline.align = fui::TextAlign::Center;
    const fui::Rect body = listBand(device);
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(body.y + body.height / 3), body.width,
                                       screen.target().lineHeight(headline.font)),
                         model.emptyHeadline, headline);
    fui::TextStyle message = screen.theme().bodyText;
    message.align = fui::TextAlign::Center;
    message.maxLines = 3;
    screen.target().text(
        fui::makeRect(body.x,
                      static_cast<int16_t>(body.y + body.height / 3 + screen.target().lineHeight(headline.font) +
                                           toybox::kGutter),
                      body.width, static_cast<int16_t>(3 * screen.target().lineHeight(message.font))),
        model.emptyMessage, message);
    // The whole empty body is the control, registered from the rect it was
    // drawn into rather than computed a second time. The design language's
    // front-door rule: the headline is the hit target, so the commonest tap is
    // the largest thing on screen and there is no button to miss.
    if (model.emptyAction != fui::NO_ACTION) screen.frame().hit(body, model.emptyAction);
    return;
  }

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

uint16_t readerVisibleLines(const fui::DrawTarget& target, const fui::DeviceContext& device,
                            const fui::ThemeTokens& tokens) {
  return fui::textAreaVisibleLines(readerBody(device), target.lineHeight(tokens.bodyText.font));
}

void appendWrapped(const fui::DrawTarget& target, const fui::DeviceContext& device, const fui::ThemeTokens& tokens,
                   const char* paragraph, const int depth, const bool isAuthor, std::vector<std::string>& text,
                   std::vector<ReaderLine>& meta) {
  ReaderLine line;
  line.depth = static_cast<int16_t>(depth);
  line.isAuthor = isAuthor;

  // An empty paragraph is a deliberate gap between comments. It still carries
  // its depth, so the thread rules run through it unbroken.
  if (paragraph == nullptr || paragraph[0] == '\0') {
    text.emplace_back();
    meta.push_back(line);
    return;
  }

  const fui::Rect body = readerBody(device);
  const int16_t width = static_cast<int16_t>(body.width - depth * kThreadIndent);
  const std::string whole(paragraph);
  fui::textAreaWalk(target, width, paragraph, tokens.bodyText, [&](uint32_t, const fui::TextAreaLine& walked) {
    text.push_back(whole.substr(walked.start, walked.len));
    meta.push_back(line);
  });
}

int16_t readerTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                         const fui::ThemeTokens& tokens, const bool withSaveMark, const char* pageLabel) {
  // The band, less its side padding on both edges and the widest page label it
  // will ever carry. The component reserves the label's width out of the title
  // rect, so a title measured against the whole band would be cut by exactly
  // this much and no ellipsis would ever appear.
  // The label that is actually there, not the widest one imaginable. Reserving
  // "888/888" for a band showing "1/24" threw away sixty pixels of headline on
  // every screen, which is most of a word.
  const int16_t label =
      pageLabel != nullptr ? target.measureText(tokens.smallText.font, pageLabel, tokens.smallText).width : 0;
  // And the save mark, when there is one. The component takes an icon-only
  // trailing button as a square the height of the band less its padding, then
  // reserves that plus a gap out of the title rect -- so a title measured
  // without it is cut by that much again, which is how "Civilian plane crash in
  // New Mexico" came out as "Civilian plane...".
  const int16_t mark = withSaveMark ? static_cast<int16_t>(tokens.headerHeight - 8 + 8) : 0;
  return static_cast<int16_t>(device.width - 2 * tokens.headerSidePadding - label - toybox::kGutter - mark);
}

void buildReader(toybox::Screen& screen, const ReaderModel& model) {
  chrome(screen, model.title, model.pageLabel, model.titleLines, model.canSave, model.saved, model.titleFont);

  const fui::DeviceContext& device = screen.device();

  // --- the footer -------------------------------------------------------
  //
  // The middle is not a button naming where it goes; it is a pair of segments
  // naming what there is to read, with the one you are in filled. A control
  // that says COMMENTS while you are reading the article has to be read twice:
  // once to see the word, once to remember whether it is a label or a
  // destination. Filled-means-here needs no second reading, and it is the same
  // shape a tab has been for thirty years.
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const int16_t usable = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  // The arrows get what they need for a chevron and no more; the segments get
  // the rest, because "COMMENTS" has to fit inside one and an even split
  // clipped it to "COMME".
  const int16_t arrow = static_cast<int16_t>(usable / 8);
  const int16_t segment = static_cast<int16_t>((usable - 2 * arrow - 2 * toybox::kGutter) / 2);

  const auto footerButton = [&screen, footerY](const char* label, const fui::ActionId action, const int16_t x,
                                               const int16_t width, const Seat seat) {
    seatButton(screen, label, action, fui::makeRect(x, footerY, width, kFooterHeight), seat);
  };

  const int16_t left = toybox::kMargin;
  footerButton("<", ActionPagePrev, left, arrow, model.canPagePrev ? Seat::There : Seat::Nothing);

  const int16_t articleX = static_cast<int16_t>(left + arrow + toybox::kGutter);
  const int16_t commentsX = static_cast<int16_t>(articleX + segment);
  footerButton("ARTICLE", ActionShowArticle, articleX, segment,
               !model.showingComments   ? Seat::Here
               : model.articleAvailable ? Seat::There
                                        : Seat::Nothing);
  footerButton("COMMENTS", ActionShowComments, commentsX, segment,
               model.showingComments     ? Seat::Here
               : model.commentsAvailable ? Seat::There
                                         : Seat::Nothing);

  footerButton(">", ActionPageNext, static_cast<int16_t>(commentsX + segment + toybox::kGutter), arrow,
               model.canPageNext ? Seat::There : Seat::Nothing);

  // --- the page ---------------------------------------------------------

  const fui::Rect body = readerBody(device);
  const int16_t lineHeight = screen.target().lineHeight(screen.theme().bodyText.font);
  if (lineHeight <= 0 || model.lineCount <= 0) return;
  const uint16_t visible = fui::textAreaVisibleLines(body, lineHeight);

  for (uint16_t row = 0; row < visible; ++row) {
    const uint32_t index = model.topLine + row;
    if (index >= static_cast<uint32_t>(model.lineCount)) break;
    const ReaderLine& line = model.lineMeta[index];
    const int16_t y = static_cast<int16_t>(body.y + row * lineHeight);

    // One short rule per ancestor level, drawn beside every line rather than
    // once per comment. Stacked down the page they join into the continuous
    // verticals Reddit draws -- and because each is only as tall as its own
    // line, a thread that breaks across a page turn cannot leave a rule
    // hanging past the last line of the page.
    for (int16_t level = 0; level < line.depth; ++level) {
      screen.target().fill(
          fui::makeRect(static_cast<int16_t>(body.x + level * kThreadIndent), y, kThreadRule, lineHeight),
          fui::Paint::solid(fui::Color::Black));
    }

    const char* text = model.lineText[index];
    if (text == nullptr || text[0] == '\0') continue;
    const int16_t indent = static_cast<int16_t>(line.depth * kThreadIndent);
    fui::TextStyle style = screen.theme().bodyText;
    screen.target().text(
        fui::makeRect(static_cast<int16_t>(body.x + indent), y, static_cast<int16_t>(body.width - indent), lineHeight),
        text, style);
  }
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
    action.text = toybox::buttonText(screen.theme());
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

    if (model.state != nullptr && model.state[0] != '\0') {
      fui::TextStyle state = screen.theme().bodyText;
      const int16_t stateHeight = screen.target().lineHeight(state.font);
      screen.target().text(fui::makeRect(toybox::kMargin, y, width, stateHeight), model.state, state);
      y = static_cast<int16_t>(y + stateHeight + toybox::kGutter);
    }

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
