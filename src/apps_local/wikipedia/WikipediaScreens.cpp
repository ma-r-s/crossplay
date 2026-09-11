#include "WikipediaScreens.h"

#include <cstdio>
#include <cstring>

#include "../ui/ToyboxIcons.h"

// Three arrangements of the home and the article chrome, rendered side by side
// before one is kept (docs/building-apps.md, "Offer designs by rendering
// them"). The switch is deleted with the losers in the same commit.
//   1  Field and keyboard: the keyboard is always up; matches, or the doors
//      back in, sit between the field and the keys.
//   2  Doors: the keyboard rises only when the field is tapped; the home is
//      CONTINUE as a card, RANDOM as a bar, RECENT as a list.
//   3  Reading first: like 2, but CONTINUE is a filled card and the article
//      footer is a progress rule.
#ifndef WIKIPEDIA_VARIANT
#define WIKIPEDIA_VARIANT 1
#endif

namespace wikiui {
namespace {

constexpr int16_t kMargin = 16;
// A row is a thumb's height at least; a title that wraps makes its row taller.
constexpr int16_t kRowMin = 48;
constexpr int16_t kRowPad = 6;
// No title reaches this: three lines of the reading cut hold 120 characters.
constexpr int16_t kMaxTitleLines = 3;
constexpr int16_t kFieldHeight = 52;
constexpr int16_t kCaptionHeight = 28;
// The footer's words sit kFooterPad under the page and toybox::kGutter above
// the glass; the band is as tall as the small cut's line box needs, never a
// constant the cut can outgrow.
constexpr int16_t kFooterPad = 6;
// The count line at the foot of the home: a hairline, then the words.
constexpr int16_t kCountLine = 36;

fui::TextStyle textStyle(const fui::FontId font, const fui::TextAlign align,
                         const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.color = colour;
  return style;
}

// All-caps chrome, ink-centred on the cap band.
void drawLabel(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
               const fui::TextAlign align, const toybox::CutMetrics& cut, const fui::Color colour = fui::Color::Black) {
  screen.target().text(toybox::inkCentred(box, cut), text, textStyle(font, align, colour));
}

// One line placed by its box. Only for text that is the reader's own (the
// query they typed) or a hint (the footer's section): titles go through
// fitTitle, which never cuts.
void drawLine(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
              const fui::TextAlign align = fui::TextAlign::Left, const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style = textStyle(font, align, colour);
  style.maxLines = 1;
  screen.target().text(box, text, style);
}

// Mixed-case prose, as many lines as the box holds.
void drawProse(toybox::Screen& screen, const fui::Rect& box, const char* text,
               const fui::TextAlign align = fui::TextAlign::Left) {
  fui::TextStyle style = textStyle(toybox::kBodyFont, align);
  const int16_t lineHeight = screen.target().lineHeight(style.font);
  const int lines = lineHeight > 0 ? box.height / lineHeight : 1;
  style.maxLines = static_cast<uint8_t>(lines < 1 ? 1 : (lines > 16 ? 16 : lines));
  screen.target().text(box, text, style);
}

void hairline(toybox::Screen& screen, const int16_t y) {
  const fui::Rect body = screen.body();
  screen.target().fill(fui::makeRect(static_cast<int16_t>(body.x + kMargin), y,
                                     static_cast<int16_t>(body.width - kMargin * 2), toybox::kHairline),
                       fui::Paint::solid(fui::Color::Black));
}

// A button on the band: paper ink and a paper outline on the band's own
// black. Left at the defaults it is painted black on black and simply is not
// there (Hacker News paid for this first; see its bandOutlineStyles).
fui::StyleSet bandOutlineStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::Black);
  styles.normal.foreground = fui::Paint::solid(fui::Color::White);
  styles.normal.border = fui::Paint::solid(fui::Color::White);
  styles.normal.borderWidth = toybox::kHairline;
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  return styles;
}

// The band. On the article and contents screens the pill is set in the small
// reading cut rather than Jersey: those screens bind readerFaces so a title
// steps down through real reading cuts, and three slots is all there are.
void chrome(toybox::Screen& screen, const char* title, const char* trailingLabel = nullptr,
            const fui::ActionId trailingAction = 0, const bool back = false) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  if (back) {
    header.leadingIcon = fui::bitmapFromIcon(icon_wiki_back_32);
    header.leadingAction = ActionPrevious;
    header.leadingStyles = bandOutlineStyles();
    header.leadingRadius = toybox::kPillRadius / 2;
  }
  if (trailingLabel) {
    header.trailingLabel = trailingLabel;
    header.trailingAction = trailingAction;
    header.trailingStyles = bandOutlineStyles();
    header.trailingText = screen.theme().smallText;
    header.trailingText.color = fui::Color::White;
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  toybox::headerBand(screen, header);
}

// A title is never elided: it takes one line when it fits, wraps otherwise,
// and its row grows to hold it.
struct TitleFit {
  int16_t lines;
  int16_t height;  // the row's, kRowMin at least
};

TitleFit fitTitle(toybox::Screen& screen, const int16_t width, const char* text, const fui::FontId font) {
  fui::TextStyle style = textStyle(font, fui::TextAlign::Left);
  const int16_t lineHeight = screen.target().lineHeight(font);
  int16_t lines = 1;
  if (screen.target().measureText(font, text, style).width > width) {
    style.maxLines = kMaxTitleLines;
    const fui::Size size = fui::measureWrappedText(screen.target(), text, style, width);
    lines = lineHeight > 0 ? static_cast<int16_t>((size.height + lineHeight - 1) / lineHeight) : 1;
    if (lines < 1) lines = 1;
    if (lines > kMaxTitleLines) lines = kMaxTitleLines;
  }
  const int16_t height = static_cast<int16_t>(lines * lineHeight + kRowPad * 2);
  return TitleFit{lines, height < kRowMin ? kRowMin : height};
}

void drawTitle(toybox::Screen& screen, const fui::Rect& box, const char* text, const fui::FontId font,
               const TitleFit& fit, const fui::Color colour = fui::Color::Black) {
  fui::TextStyle style = textStyle(font, fui::TextAlign::Left, colour);
  style.maxLines = static_cast<uint8_t>(fit.lines);
  const int16_t textHeight = static_cast<int16_t>(fit.lines * screen.target().lineHeight(font));
  const int16_t top = static_cast<int16_t>(box.y + (box.height > textHeight ? (box.height - textHeight) / 2 : 0));
  screen.target().text(fui::Rect{box.x, top, box.width, textHeight}, text, style);
}

// A row of a title list, its whole width tappable. Returns the row's height,
// or 0 when it would not fit above `bottom` and so was not drawn. A redirect
// shows its own name; opening it lands on the target.
int16_t drawTitleRow(toybox::Screen& screen, const int16_t y, const int16_t bottom, const char* title,
                     const fui::ActionId action, const int value, const bool rule,
                     const fui::FontId font = toybox::kBodyFont) {
  const fui::Rect body = screen.body();
  const TitleFit fit = fitTitle(screen, static_cast<int16_t>(body.width - kMargin * 2), title, font);
  if (y + fit.height > bottom) return 0;
  const fui::Rect row{body.x, y, body.width, fit.height};
  drawTitle(screen, row.inset(fui::Insets{0, kMargin, 0, kMargin}), title, font, fit);
  if (rule) hairline(screen, static_cast<int16_t>(row.bottom() - 1));
  screen.frame().hit(row, action, static_cast<int16_t>(value));
  return fit.height;
}

void drawCaption(toybox::Screen& screen, const fui::Rect& row, const char* text) {
  const fui::Rect box = row.inset(fui::Insets{0, kMargin, 0, kMargin});
  drawLabel(screen, box, text, toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
}

// One wide action across a row, where a thumb rests.
void drawAction(toybox::Screen& screen, const fui::Rect& box, const char* label, const fui::ActionId action,
                const bool filled) {
  if (filled) {
    screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  } else {
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
  }
  drawLabel(screen, box, label, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut,
            filled ? fui::Color::White : fui::Color::Black);
  screen.frame().hit(box, action);
}

// The field under the band. Its prompt is in the app's own voice (Jersey
// caps) and what the reader types is in the reading cut, so an empty field
// and a typed one never look alike. Tapping it is how the keyboard rises in
// the variants that hide it. `clear` draws the X: it clears the query, and in
// those variants it also puts the keyboard down.
fui::Rect drawField(toybox::Screen& screen, const SearchModel& model, const bool clear) {
  const fui::Rect body = screen.body();
  // toybox::kGutter below the band's rule, which the chrome probe holds every
  // screen to; the field's stroke is the first ink under the band.
  const fui::Rect field{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.y + toybox::kGutter + 2),
                        static_cast<int16_t>(body.width - kMargin * 2), kFieldHeight};
  screen.target().stroke(field, fui::Paint::solid(fui::Color::Black), 2);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  const int16_t right = static_cast<int16_t>(clear ? 48 + 4 : 12);
  const fui::Rect text = field.inset(fui::Insets{0, right, 0, 12});
  if (empty) {
    drawLabel(screen, text, "SEARCH WIKIPEDIA", toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
  } else {
    drawLine(screen, text, model.query, toybox::kBodyFont);
  }
  screen.frame().hit(field, ActionField);
  if (clear) {
    // A small clear box at the field's right edge, inside its stroke.
    const fui::Rect box{static_cast<int16_t>(field.right() - 48), field.y, 48, field.height};
    drawLabel(screen, box, "X", toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
    screen.frame().hit(box, ActionClear);
  }
  return field;
}

// The matches under the field, each carrying its row.
void drawResults(toybox::Screen& screen, const SearchModel& model, int16_t y, const int16_t bottom) {
  const fui::Rect body = screen.body();
  if (model.resultCount == 0 && model.noMatch) {
    drawProse(screen,
              fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(y + 12),
                        static_cast<int16_t>(body.width - kMargin * 2), 60},
              "No article with that name");
  }
  for (int i = 0; i < model.resultCount && i < kMaxResults; ++i) {
    const int16_t h = drawTitleRow(screen, y, bottom, model.results[i].title, ActionResult, i, true);
    if (h == 0) break;
    y = static_cast<int16_t>(y + h);
  }
}

// The row that goes back to the install screen, when the pack is only partly here.
int16_t drawPartsRow(toybox::Screen& screen, const SearchModel& model, const int16_t y) {
  if (!model.partsLine) return y;
  const fui::Rect body = screen.body();
  const fui::Rect row{body.x, y, body.width, kRowMin};
  drawCaption(screen, row, model.partsLine);
  screen.frame().hit(row, ActionInstall);
  return static_cast<int16_t>(y + kRowMin);
}

// The recent trail: as many as fit above `bottom`. The activity leaves out the
// article you are in, which CONTINUE already names.
int16_t drawRecent(toybox::Screen& screen, const SearchModel& model, int16_t y, const int16_t bottom) {
  if (model.recentCount == 0 || y + kCaptionHeight + kRowMin > bottom) return y;
  const fui::Rect body = screen.body();
  drawCaption(screen, fui::Rect{body.x, y, body.width, kCaptionHeight}, "RECENT");
  y = static_cast<int16_t>(y + kCaptionHeight);
  for (int i = 0; i < model.recentCount && i < kMaxRecent; ++i) {
    const int16_t h = drawTitleRow(screen, y, bottom, model.recent[i].title, ActionRecent, i, true);
    if (h == 0) break;
    y = static_cast<int16_t>(y + h);
  }
  return y;
}

// What is on the card, in the app's own voice, on the last line above the
// keyboard or the glass, with a hairline so it reads as the page's foot and
// not as a key.
void drawCountLine(toybox::Screen& screen, const int16_t bottom, const char* text) {
  const fui::Rect body = screen.body();
  hairline(screen, static_cast<int16_t>(bottom - kCountLine));
  drawLabel(screen,
            fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(bottom - kCountLine + 4),
                      static_cast<int16_t>(body.width - kMargin * 2), static_cast<int16_t>(kCountLine - 8)},
            text, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
}

int16_t footerHeight(toybox::Screen& screen) {
  const int16_t lineHeight = screen.target().lineHeight(toybox::kSmallFont);
  return static_cast<int16_t>(kFooterPad + lineHeight + (WIKIPEDIA_VARIANT == 3 ? 10 : 0) + toybox::kGutter);
}

}  // namespace

#if WIKIPEDIA_VARIANT == 1

// The home. The keyboard is always up; between the field and the keys sit the
// matches, or the doors back in.
void buildSearch(toybox::Screen& screen, const SearchModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t bottom = static_cast<int16_t>(body.bottom() - model.keyboardHeight);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  const fui::Rect field = drawField(screen, model, !empty);
  int16_t y = static_cast<int16_t>(field.bottom() + toybox::kGutter);

  if (!empty) {
    drawResults(screen, model, y, bottom);
    return;
  }
  // The count line is reserved first; the trail gets what is left.
  const int16_t list = static_cast<int16_t>(bottom - kCountLine);
  y = drawPartsRow(screen, model, y);
  if (model.continueTitle) {
    drawCaption(screen, fui::Rect{body.x, y, body.width, kCaptionHeight}, "CONTINUE");
    y = static_cast<int16_t>(y + kCaptionHeight);
    y = static_cast<int16_t>(y + drawTitleRow(screen, y, list, model.continueTitle, ActionContinue, 0, false));
  }
  {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(y + 4),
                        static_cast<int16_t>(body.width - kMargin * 2), 48};
    if (box.bottom() <= list) drawAction(screen, box, "RANDOM ARTICLE", ActionRandom, false);
    y = static_cast<int16_t>(box.bottom() + toybox::kGutter);
  }
  drawRecent(screen, model, y, list);
  drawCountLine(screen, bottom, model.footer);
}

#elif WIKIPEDIA_VARIANT == 2

// Doors. The keyboard rises when the field is tapped; until then the home is
// three doors: the article you were in, a random one, and the recent trail.
// With the keyboard up and nothing typed the doors stay, above the keys.
void buildSearch(toybox::Screen& screen, const SearchModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t bottom = static_cast<int16_t>(body.bottom() - model.keyboardHeight);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  const fui::Rect field = drawField(screen, model, !empty || model.keyboardHeight > 0);
  int16_t y = static_cast<int16_t>(field.bottom() + toybox::kGutter);
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);

  if (!empty) {
    drawResults(screen, model, y, bottom);
    return;
  }
  const int16_t list = static_cast<int16_t>(bottom - kCountLine);
  y = drawPartsRow(screen, model, y);
  if (model.continueTitle) {
    // A card: caption, then the title, the whole card tappable.
    const TitleFit fit = fitTitle(screen, static_cast<int16_t>(inner - 28), model.continueTitle, toybox::kBodyFont);
    const int16_t textHeight = static_cast<int16_t>(fit.lines * screen.target().lineHeight(toybox::kBodyFont));
    const fui::Rect card{static_cast<int16_t>(body.x + kMargin), y, inner,
                         static_cast<int16_t>(10 + 24 + 6 + textHeight + 14)};
    if (card.bottom() <= list) {
      screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), 2);
      drawLabel(screen, fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 10), 220, 24},
                "CONTINUE READING", toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
      drawTitle(screen,
                fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 40),
                          static_cast<int16_t>(inner - 28), textHeight},
                model.continueTitle, toybox::kBodyFont, fit);
      screen.frame().hit(card, ActionContinue);
      y = static_cast<int16_t>(card.bottom() + toybox::kGutter);
    }
  }
  {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), y, inner, 56};
    if (box.bottom() <= list) drawAction(screen, box, "RANDOM ARTICLE", ActionRandom, false);
    y = static_cast<int16_t>(box.bottom() + 20);
  }
  drawRecent(screen, model, y, list);
  drawCountLine(screen, bottom, model.footer);
}

#else

// Reading first. Like the doors, but CONTINUE is a filled card that says where
// you are, the loudest thing on the page.
void buildSearch(toybox::Screen& screen, const SearchModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t bottom = static_cast<int16_t>(body.bottom() - model.keyboardHeight);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  const fui::Rect field = drawField(screen, model, !empty || model.keyboardHeight > 0);
  int16_t y = static_cast<int16_t>(field.bottom() + toybox::kGutter);
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);

  if (!empty) {
    drawResults(screen, model, y, bottom);
    return;
  }
  const int16_t list = static_cast<int16_t>(bottom - kCountLine);
  y = drawPartsRow(screen, model, y);
  if (model.continueTitle) {
    const TitleFit fit = fitTitle(screen, static_cast<int16_t>(inner - 28), model.continueTitle, toybox::kBodyFont);
    const int16_t textHeight = static_cast<int16_t>(fit.lines * screen.target().lineHeight(toybox::kBodyFont));
    const fui::Rect card{static_cast<int16_t>(body.x + kMargin), y, inner,
                         static_cast<int16_t>(12 + 24 + 6 + textHeight + 16)};
    if (card.bottom() <= list) {
      screen.target().fill(card, fui::Paint::solid(fui::Color::Black));
      drawLabel(screen, fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 12), 220, 24},
                "CONTINUE", toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut, fui::Color::White);
      drawTitle(screen,
                fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 42),
                          static_cast<int16_t>(inner - 28), textHeight},
                model.continueTitle, toybox::kBodyFont, fit, fui::Color::White);
      screen.frame().hit(card, ActionContinue);
      y = static_cast<int16_t>(card.bottom() + toybox::kGutter);
    }
  }
  {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), y, inner, 52};
    if (box.bottom() <= list) drawAction(screen, box, "RANDOM ARTICLE", ActionRandom, false);
    y = static_cast<int16_t>(box.bottom() + 20);
  }
  drawRecent(screen, model, y, list);
  drawCountLine(screen, bottom, model.footer);
}

#endif

fui::Rect buildArticleChrome(toybox::Screen& screen, const ArticleChromeModel& model) {
  chrome(screen, model.title, model.contents ? "CONTENTS" : nullptr, ActionContents, model.back);
  const fui::Rect body = screen.body();
  return fui::Rect{body.x, body.y, body.width, static_cast<int16_t>(body.height - footerHeight(screen))};
}

// The section the page is in at the left, the page at the right, as a book's
// running foot. The words are one line box tall, kFooterPad under the page,
// and end toybox::kGutter above the glass; the third arrangement adds a
// progress rule under them, filled once the total is known.
void buildArticleFooter(toybox::Screen& screen, const ArticleFooterModel& model) {
  const fui::Rect body = screen.body();
  const int16_t lineHeight = screen.target().lineHeight(toybox::kSmallFont);
  const fui::Rect foot{static_cast<int16_t>(body.x + kMargin),
                       static_cast<int16_t>(body.bottom() - footerHeight(screen)),
                       static_cast<int16_t>(body.width - kMargin * 2), footerHeight(screen)};
  const int16_t textY = static_cast<int16_t>(foot.y + kFooterPad);
  const int16_t pageWidth = static_cast<int16_t>(foot.width / 3);
  drawLine(screen, fui::Rect{foot.x, textY, static_cast<int16_t>(foot.width - pageWidth - 8), lineHeight}, model.right,
           toybox::kSmallFont);
  drawLine(screen, fui::Rect{static_cast<int16_t>(foot.right() - pageWidth), textY, pageWidth, lineHeight}, model.left,
           toybox::kSmallFont, fui::TextAlign::Right);
#if WIKIPEDIA_VARIANT == 3
  const int16_t ruleY = static_cast<int16_t>(textY + lineHeight + 4);
  screen.target().fill(fui::Rect{foot.x, ruleY, foot.width, 2}, fui::Paint::solid(fui::Color::Black));
  if (model.total > 0 && model.page > 0) {
    const int16_t filled = static_cast<int16_t>(static_cast<int32_t>(foot.width) * model.page / model.total);
    screen.target().fill(fui::Rect{foot.x, static_cast<int16_t>(ruleY - 2), filled, 6},
                         fui::Paint::solid(fui::Color::Black));
  }
#endif
}

int buildContents(toybox::Screen& screen, const ContentsModel& model) {
  chrome(screen, model.title, "CLOSE", ActionClose);
  const fui::Rect body = screen.body();
  int16_t y = static_cast<int16_t>(body.y + 8);
  // Room for the window line under the rows.
  const int16_t bottom = static_cast<int16_t>(body.bottom() - 40);
  const int16_t numberWidth = 56;
  const int16_t textX = static_cast<int16_t>(body.x + kMargin + 16);
  const int16_t textWidth = static_cast<int16_t>(body.width - kMargin * 2 - 16 - numberWidth - 8);
  int shown = 0;
  for (int i = model.first; i < model.count && shown < kContentsRows; ++i) {
    // The section you are in is set bold, with a bar in the margin.
    const bool here = i == model.current;
    const fui::FontId font = here ? toybox::kDisplayFont : toybox::kBodyFont;
    const TitleFit fit = fitTitle(screen, textWidth, model.headings[i], font);
    if (y + fit.height > bottom) break;
    const fui::Rect row{body.x, y, body.width, fit.height};
    if (here) {
      screen.target().fill(fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(y + 8), 4,
                                     static_cast<int16_t>(fit.height - 16)},
                           fui::Paint::solid(fui::Color::Black));
    }
    drawTitle(screen, fui::Rect{textX, y, textWidth, fit.height}, model.headings[i], font, fit);
    if (model.pages && model.pages[i] >= 0) {
      char number[8];
      snprintf(number, sizeof(number), "%d", model.pages[i] + 1);
      drawLine(screen, fui::Rect{static_cast<int16_t>(row.right() - kMargin - numberWidth), y, numberWidth, fit.height},
               number, toybox::kSmallFont, fui::TextAlign::Right);
    }
    hairline(screen, static_cast<int16_t>(row.bottom() - 1));
    screen.frame().hit(row, ActionHeading, static_cast<int16_t>(i));
    y = static_cast<int16_t>(y + fit.height);
    ++shown;
  }
  // The window's place in the list, and where the rest is. Paged by the side
  // buttons or by tapping the line.
  if (model.first > 0 || model.first + shown < model.count) {
    char where[32];
    snprintf(where, sizeof(where), "%d-%d of %d", model.first + 1, model.first + shown, model.count);
    const fui::Rect line{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - 34),
                         static_cast<int16_t>(body.width - kMargin * 2), 30};
    drawLabel(screen, line, where, toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
    drawLabel(screen, line, model.first + shown < model.count ? "MORE >" : "< FIRST", toybox::kSmallFont,
              fui::TextAlign::Right, toybox::kButtonCut);
    screen.frame().hit(line, ActionMore);
  }
  return shown;
}

// The first-open screen. The address first and large, the sentence, then the
// square the activity fills with the QR code, then what the cable is doing.
fui::Rect buildInstall(toybox::Screen& screen, const InstallModel& model) {
  chrome(screen, "GET WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);
  int16_t y = static_cast<int16_t>(body.y + 28);

  drawLine(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), y, inner, 40}, model.url, toybox::kDisplayFont,
           fui::TextAlign::Center);
  y = static_cast<int16_t>(y + 52);
  drawProse(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), y, inner, 72},
            "Open this in Chrome or Edge on a computer. About ten minutes.", fui::TextAlign::Center);
  y = static_cast<int16_t>(y + 84);

  const int16_t side = 232;
  const fui::Rect qr{static_cast<int16_t>(body.x + (body.width - side) / 2), y, side, side};
  y = static_cast<int16_t>(qr.bottom() + 28);

  const char* status = "Then plug the reader into the computer with its cable.";
  if (model.stage == InstallModel::Stage::Connected) status = "Connected. Follow the page on the computer.";
  if (model.stage == InstallModel::Stage::Failed) status = "The card could not be shared. Try again.";
  drawProse(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), y, inner, 72}, status, fui::TextAlign::Center);
  y = static_cast<int16_t>(y + 80);
  if (model.partsLine) {
    drawLabel(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), y, inner, 28}, model.partsLine,
              toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
  }
  if (model.stage == InstallModel::Stage::Failed) {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - 80), inner, 56};
    drawAction(screen, box, "TRY AGAIN", ActionRetry, true);
  }
  return qr;
}

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);
  const int16_t top = static_cast<int16_t>(body.y + 96);
  drawLabel(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), top, inner, 64}, model.headline,
            toybox::kDisplayFont, fui::TextAlign::Center, toybox::kLargeCut);
  drawProse(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(top + 88), inner, 200},
            model.body, fui::TextAlign::Center);
  if (model.actionLabel) {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - 80), inner, 56};
    drawAction(screen, box, model.actionLabel, model.action, true);
  }
}

}  // namespace wikiui
