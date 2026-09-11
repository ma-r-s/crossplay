#include "WikipediaScreens.h"

#include <cstdio>
#include <cstring>

// Three arrangements of the home and the article chrome, rendered side by side
// before one is kept (docs/building-apps.md, "Offer designs by rendering
// them"). The switch is deleted with the losers in the same commit.
//   1  Field and keyboard: the keyboard is always up; matches, or the doors
//      back in, sit between the field and the keys.
//   2  Doors: the keyboard rises only when the field is tapped; the home is
//      CONTINUE as a card, RANDOM as a bar, RECENT as a list.
//   3  Reading first: like 2, but CONTINUE is a full card with its page, RECENT
//      is a two-column grid, and the article footer is a progress rule.
#ifndef WIKIPEDIA_VARIANT
#define WIKIPEDIA_VARIANT 1
#endif

namespace wikiui {
namespace {

constexpr int16_t kMargin = 16;
constexpr int16_t kRowHeight = 44;
// Result rows are tighter: eight of them plus the field must clear the keyboard.
constexpr int16_t kResultRowHeight = 40;
constexpr int16_t kFieldHeight = 52;
constexpr int16_t kFooterHeight = 28;

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

// Mixed-case text placed by its box, one line, cut with the font's ellipsis.
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

// A trailing button on the band: paper ink and a paper outline on the band's
// own black. Left at the defaults it is painted black on black and simply is
// not there (Hacker News paid for this first; see its bandOutlineStyles).
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

void chrome(toybox::Screen& screen, const char* title, const char* trailingLabel = nullptr,
            const fui::ActionId trailingAction = 0) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
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

// A row of a title list: the title, one line, and its whole width tappable.
// A redirect shows its own name; opening it lands on the target.
void drawTitleRow(toybox::Screen& screen, const fui::Rect& row, const Row& item, const fui::ActionId action,
                  const int value, const bool rule) {
  const fui::Rect text = row.inset(fui::Insets{0, kMargin, 0, kMargin});
  drawLine(screen, text, item.title, toybox::kBodyFont);
  if (rule) hairline(screen, static_cast<int16_t>(row.bottom() - 1));
  screen.frame().hit(row, action, static_cast<int16_t>(value));
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

// The field under the band. Tapping it is how the keyboard rises in the
// variants that hide it; in the first it is simply where the query shows.
fui::Rect drawField(toybox::Screen& screen, const SearchModel& model) {
  const fui::Rect body = screen.body();
  // toybox::kGutter below the band's rule, which the chrome probe holds every
  // screen to; the field's stroke is the first ink under the band.
  const fui::Rect field{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.y + toybox::kGutter + 2),
                        static_cast<int16_t>(body.width - kMargin * 2), kFieldHeight};
  screen.target().stroke(field, fui::Paint::solid(fui::Color::Black), 2);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  const fui::Rect text = field.inset(fui::Insets{0, 12, 0, 12});
  drawLine(screen, text, empty ? "Search Wikipedia" : model.query, toybox::kBodyFont);
  screen.frame().hit(field, ActionField);
  if (!empty) {
    // A small clear box at the field's right edge, inside its stroke.
    const fui::Rect clear{static_cast<int16_t>(field.right() - 48), field.y, 48, field.height};
    drawLabel(screen, clear, "X", toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
    screen.frame().hit(clear, ActionClear);
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
    if (y + kResultRowHeight > bottom) break;
    const fui::Rect row{body.x, y, body.width, kResultRowHeight};
    drawTitleRow(screen, row, model.results[i], ActionResult, i, true);
    y = static_cast<int16_t>(y + kResultRowHeight);
  }
}

void drawFooterLine(toybox::Screen& screen, const int16_t bottom, const char* text) {
  const fui::Rect body = screen.body();
  const fui::Rect foot{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(bottom - 26),
                       static_cast<int16_t>(body.width - kMargin * 2), 22};
  drawLine(screen, foot, text, toybox::kSmallFont, fui::TextAlign::Center);
}

}  // namespace

#if WIKIPEDIA_VARIANT == 1

// The home. The keyboard is always up; between the field and the keys sit the
// matches, or the doors back in.
void buildSearch(toybox::Screen& screen, const SearchModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t bottom = static_cast<int16_t>(body.bottom() - model.keyboardHeight);
  const fui::Rect field = drawField(screen, model);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  int16_t y = static_cast<int16_t>(field.bottom() + 8);
  auto rowRect = [&](const int16_t top) { return fui::Rect{body.x, top, body.width, kRowHeight}; };

  if (!empty) {
    drawResults(screen, model, y, bottom);
    return;
  }
  if (model.partsLine) {
    const fui::Rect row = rowRect(y);
    drawCaption(screen, row, model.partsLine);
    screen.frame().hit(row, ActionInstall);
    y = static_cast<int16_t>(y + kRowHeight);
  }
  if (model.continueTitle) {
    drawCaption(screen, rowRect(y), "CONTINUE");
    y = static_cast<int16_t>(y + 28);
    Row row;
    row.title = model.continueTitle;
    drawTitleRow(screen, rowRect(y), row, ActionContinue, 0, true);
    y = static_cast<int16_t>(y + kRowHeight);
  }
  {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(y + 8),
                        static_cast<int16_t>(body.width - kMargin * 2), 48};
    if (box.bottom() <= bottom) drawAction(screen, box, "RANDOM ARTICLE", ActionRandom, false);
    y = static_cast<int16_t>(y + 64);
  }
  if (model.recentCount > 0 && y + 28 + kRowHeight <= bottom) {
    drawCaption(screen, rowRect(y), "RECENT");
    y = static_cast<int16_t>(y + 28);
    for (int i = 0; i < model.recentCount && i < kMaxRecent; ++i) {
      if (y + kRowHeight > bottom) break;
      drawTitleRow(screen, rowRect(y), model.recent[i], ActionRecent, i, true);
      y = static_cast<int16_t>(y + kRowHeight);
    }
  }
  if (y + 24 <= bottom) drawFooterLine(screen, bottom, model.footer);
}

#elif WIKIPEDIA_VARIANT == 2

// Doors. The keyboard rises when the field is tapped; until then the home is
// three doors: the article you were in, a random one, and the recent trail.
void buildSearch(toybox::Screen& screen, const SearchModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t bottom = static_cast<int16_t>(body.bottom() - model.keyboardHeight);
  const fui::Rect field = drawField(screen, model);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  int16_t y = static_cast<int16_t>(field.bottom() + 12);
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);

  if (!empty || model.keyboardHeight > 0) {
    drawResults(screen, model, y, bottom);
    return;
  }
  if (model.partsLine) {
    const fui::Rect row{body.x, y, body.width, kRowHeight};
    drawCaption(screen, row, model.partsLine);
    screen.frame().hit(row, ActionInstall);
    y = static_cast<int16_t>(y + kRowHeight);
  }
  if (model.continueTitle) {
    // A card: caption, then the title in the display cut, the whole card tappable.
    const fui::Rect card{static_cast<int16_t>(body.x + kMargin), y, inner, 104};
    screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), 2);
    drawLabel(screen, fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 10), 220, 24},
              "CONTINUE READING", toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut);
    drawLine(screen,
             fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 44),
                       static_cast<int16_t>(inner - 28), 44},
             model.continueTitle, toybox::kDisplayFont);
    screen.frame().hit(card, ActionContinue);
    y = static_cast<int16_t>(card.bottom() + 12);
  }
  {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), y, inner, 56};
    drawAction(screen, box, "RANDOM ARTICLE", ActionRandom, true);
    y = static_cast<int16_t>(box.bottom() + 20);
  }
  if (model.recentCount > 0) {
    drawCaption(screen, fui::Rect{body.x, y, body.width, 28}, "RECENT");
    y = static_cast<int16_t>(y + 30);
    for (int i = 0; i < model.recentCount && i < kMaxRecent; ++i) {
      if (y + kRowHeight > bottom - 30) break;
      drawTitleRow(screen, fui::Rect{body.x, y, body.width, kRowHeight}, model.recent[i], ActionRecent, i, true);
      y = static_cast<int16_t>(y + kRowHeight);
    }
  }
  drawFooterLine(screen, bottom, model.footer);
}

#else

// Reading first. The keyboard rises when the field is tapped. CONTINUE is a
// full card that says where you are; RECENT is a two-column grid so ten titles
// fit above the fold.
void buildSearch(toybox::Screen& screen, const SearchModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t bottom = static_cast<int16_t>(body.bottom() - model.keyboardHeight);
  const fui::Rect field = drawField(screen, model);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  int16_t y = static_cast<int16_t>(field.bottom() + 12);
  const int16_t inner = static_cast<int16_t>(body.width - kMargin * 2);

  if (!empty || model.keyboardHeight > 0) {
    drawResults(screen, model, y, bottom);
    return;
  }
  if (model.partsLine) {
    const fui::Rect row{body.x, y, body.width, kRowHeight};
    drawCaption(screen, row, model.partsLine);
    screen.frame().hit(row, ActionInstall);
    y = static_cast<int16_t>(y + kRowHeight);
  }
  if (model.continueTitle) {
    const fui::Rect card{static_cast<int16_t>(body.x + kMargin), y, inner, 132};
    screen.target().fill(card, fui::Paint::solid(fui::Color::Black));
    drawLabel(screen, fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 12), 220, 24},
              "CONTINUE", toybox::kSmallFont, fui::TextAlign::Left, toybox::kButtonCut, fui::Color::White);
    fui::TextStyle title = textStyle(toybox::kDisplayFont, fui::TextAlign::Left, fui::Color::White);
    title.maxLines = 2;
    screen.target().text(fui::Rect{static_cast<int16_t>(card.x + 14), static_cast<int16_t>(card.y + 44),
                                   static_cast<int16_t>(inner - 28), 80},
                         model.continueTitle, title);
    screen.frame().hit(card, ActionContinue);
    y = static_cast<int16_t>(card.bottom() + 12);
  }
  {
    const fui::Rect box{static_cast<int16_t>(body.x + kMargin), y, inner, 52};
    drawAction(screen, box, "RANDOM ARTICLE", ActionRandom, false);
    y = static_cast<int16_t>(box.bottom() + 20);
  }
  if (model.recentCount > 0) {
    drawCaption(screen, fui::Rect{body.x, y, body.width, 28}, "RECENT");
    y = static_cast<int16_t>(y + 30);
    const int16_t colWidth = static_cast<int16_t>(inner / 2);
    for (int i = 0; i < model.recentCount && i < kMaxRecent; ++i) {
      const int16_t rowY = static_cast<int16_t>(y + (i / 2) * 36);
      if (rowY + 36 > bottom - 30) break;
      const fui::Rect cell{static_cast<int16_t>(body.x + kMargin + (i % 2) * colWidth), rowY, colWidth, 36};
      drawLine(screen, cell.inset(fui::Insets{0, 4, 0, 8}), model.recent[i].title, toybox::kBodyFont);
      screen.frame().hit(cell, ActionRecent, static_cast<int16_t>(i));
    }
  }
  drawFooterLine(screen, bottom, model.footer);
}

#endif

fui::Rect buildArticleChrome(toybox::Screen& screen, const ArticleChromeModel& model) {
  chrome(screen, model.title, model.contents ? "CONTENTS" : nullptr, ActionContents);
  const fui::Rect body = screen.body();
  return fui::Rect{body.x, body.y, body.width, static_cast<int16_t>(body.height - kFooterHeight - 4)};
}

#if WIKIPEDIA_VARIANT == 3

// A progress rule across the foot of the page, the section name over it, and
// the page number at the right end. Nothing moves while the layout is still
// counting: the rule only fills once the total is known.
void buildArticleFooter(toybox::Screen& screen, const ArticleFooterModel& model) {
  const fui::Rect body = screen.body();
  const fui::Rect foot{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - kFooterHeight),
                       static_cast<int16_t>(body.width - kMargin * 2), kFooterHeight};
  const int16_t half = static_cast<int16_t>(foot.width * 2 / 3);
  drawLine(screen, fui::Rect{foot.x, foot.y, half, static_cast<int16_t>(foot.height - 8)}, model.right,
           toybox::kSmallFont);
  drawLine(screen,
           fui::Rect{static_cast<int16_t>(foot.x + half), foot.y, static_cast<int16_t>(foot.width - half),
                     static_cast<int16_t>(foot.height - 8)},
           model.left, toybox::kSmallFont, fui::TextAlign::Right);
  const fui::Rect rule{foot.x, static_cast<int16_t>(foot.bottom() - 4), foot.width, 2};
  screen.target().fill(rule, fui::Paint::solid(fui::Color::Black));
  if (model.total > 0 && model.page > 0) {
    const int16_t filled = static_cast<int16_t>(static_cast<int32_t>(foot.width) * model.page / model.total);
    screen.target().fill(fui::Rect{foot.x, static_cast<int16_t>(foot.bottom() - 6), filled, 6},
                         fui::Paint::solid(fui::Color::Black));
  }
}

#else

void buildArticleFooter(toybox::Screen& screen, const ArticleFooterModel& model) {
  const fui::Rect body = screen.body();
  const fui::Rect foot{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - kFooterHeight),
                       static_cast<int16_t>(body.width - kMargin * 2), kFooterHeight};
  const int16_t half = static_cast<int16_t>(foot.width / 2);
  drawLine(screen, fui::Rect{foot.x, foot.y, half, foot.height}, model.left, toybox::kSmallFont);
  drawLine(screen, fui::Rect{static_cast<int16_t>(foot.x + half), foot.y, half, foot.height}, model.right,
           toybox::kSmallFont, fui::TextAlign::Right);
}

#endif

void buildContents(toybox::Screen& screen, const ContentsModel& model) {
  chrome(screen, model.title, "CLOSE", ActionClose);
  const fui::Rect body = screen.body();
  int16_t y = static_cast<int16_t>(body.y + 8);
  for (int i = model.first; i < model.count && i < model.first + kContentsRows; ++i) {
    if (y + kRowHeight > body.bottom() - 36) break;
    const fui::Rect row{body.x, y, body.width, kRowHeight};
    const fui::Rect text = row.inset(fui::Insets{0, static_cast<int16_t>(kMargin + 16), 0, kMargin});
    if (i == model.current) {
      screen.target().fill(fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(y + 16), 6, 12},
                           fui::Paint::solid(fui::Color::Black));
    }
    drawLine(screen, text, model.headings[i], toybox::kBodyFont);
    hairline(screen, static_cast<int16_t>(row.bottom() - 1));
    screen.frame().hit(row, ActionHeading, static_cast<int16_t>(i));
    y = static_cast<int16_t>(y + kRowHeight);
  }
  // The window's place in the list, and where the rest is. Paged by the side
  // buttons or by tapping the line.
  if (model.count > kContentsRows) {
    char more[48];
    const int pages = (model.count + kContentsRows - 1) / kContentsRows;
    const int page = model.first / kContentsRows + 1;
    snprintf(more, sizeof(more), "%d of %d %s", page, pages, page < pages ? "  MORE >" : "");
    const fui::Rect line{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - 34),
                         static_cast<int16_t>(body.width - kMargin * 2), 30};
    drawLabel(screen, line, more, toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
    screen.frame().hit(line, ActionMore);
  }
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
    drawLine(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), y, inner, 24}, model.partsLine,
             toybox::kSmallFont, fui::TextAlign::Center);
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
