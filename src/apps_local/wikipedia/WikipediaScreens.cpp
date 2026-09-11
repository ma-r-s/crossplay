#include "WikipediaScreens.h"

#include <cstdio>
#include <cstring>

namespace wikiui {
namespace {

constexpr int16_t kMargin = 16;
constexpr int16_t kRowHeight = 44;
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

void chrome(toybox::Screen& screen, const char* title, const char* trailingLabel = nullptr,
            const fui::ActionId trailingAction = 0) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  if (trailingLabel) {
    header.trailingLabel = trailingLabel;
    header.trailingAction = trailingAction;
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

}  // namespace

// The home. The field sits under the band; what follows depends on whether
// anything is typed: matches, or the places to go back to. The keyboard's
// height is reserved at the bottom and drawn by the activity, because the
// keyboard component registers more hit rects than a toybox screen holds.
void buildSearch(toybox::Screen& screen, const SearchModel& model) {
  chrome(screen, "WIKIPEDIA");
  const fui::Rect body = screen.body();
  const int16_t bottom = static_cast<int16_t>(body.bottom() - model.keyboardHeight);

  // The field: a box with the query, or the hint in it.
  const fui::Rect field{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.y + 10),
                        static_cast<int16_t>(body.width - kMargin * 2), kFieldHeight};
  screen.target().stroke(field, fui::Paint::solid(fui::Color::Black), 2);
  const bool empty = model.query == nullptr || model.query[0] == '\0';
  {
    const fui::Rect text = field.inset(fui::Insets{0, 12, 0, 12});
    if (empty) {
      drawLine(screen, text, "Search Wikipedia", toybox::kBodyFont);
    } else {
      drawLine(screen, text, model.query, toybox::kBodyFont);
    }
  }
  if (!empty) {
    // A small clear box at the field's right edge, inside its stroke.
    const fui::Rect clear{static_cast<int16_t>(field.right() - 48), field.y, 48, field.height};
    drawLabel(screen, clear, "X", toybox::kSmallFont, fui::TextAlign::Center, toybox::kButtonCut);
    screen.frame().hit(clear, ActionClear);
  }

  int16_t y = static_cast<int16_t>(field.bottom() + 8);
  const int16_t rowWidth = body.width;
  auto rowRect = [&](const int16_t top) { return fui::Rect{body.x, top, rowWidth, kRowHeight}; };

  if (!empty) {
    if (model.resultCount == 0 && model.noMatch) {
      drawProse(screen, fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(y + 12),
                                  static_cast<int16_t>(body.width - kMargin * 2), 60},
                "No article with that name");
    }
    for (int i = 0; i < model.resultCount && i < kMaxResults; ++i) {
      if (y + kRowHeight > bottom) break;
      drawTitleRow(screen, rowRect(y), model.results[i], ActionResult, i, true);
      y = static_cast<int16_t>(y + kRowHeight);
    }
    return;
  }

  // Nothing typed: the way back in, then a random door, then the trail.
  if (model.partsLine) {
    const fui::Rect row = rowRect(y);
    drawCaption(screen, row, model.partsLine);
    screen.frame().hit(row, ActionInstall);
    y = static_cast<int16_t>(y + kRowHeight);
  }
  if (model.continueTitle) {
    const fui::Rect cap = rowRect(y);
    drawCaption(screen, cap, "CONTINUE");
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
  // The footer sits just above the keyboard.
  if (y + 24 <= bottom) {
    const fui::Rect foot{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(bottom - 26),
                         static_cast<int16_t>(body.width - kMargin * 2), 22};
    drawLine(screen, foot, model.footer, toybox::kSmallFont, fui::TextAlign::Center);
  }
}

fui::Rect buildArticleChrome(toybox::Screen& screen, const ArticleChromeModel& model) {
  chrome(screen, model.title, model.contents ? "CONTENTS" : nullptr, ActionContents);
  const fui::Rect body = screen.body();
  const fui::Rect foot{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - kFooterHeight),
                       static_cast<int16_t>(body.width - kMargin * 2), kFooterHeight};
  const int16_t half = static_cast<int16_t>(foot.width / 2);
  drawLine(screen, fui::Rect{foot.x, foot.y, half, foot.height}, model.footerLeft, toybox::kSmallFont);
  drawLine(screen, fui::Rect{static_cast<int16_t>(foot.x + half), foot.y, half, foot.height}, model.footerRight,
           toybox::kSmallFont, fui::TextAlign::Right);
  return fui::Rect{body.x, body.y, body.width, static_cast<int16_t>(body.height - kFooterHeight - 4)};
}

void buildContents(toybox::Screen& screen, const ContentsModel& model) {
  chrome(screen, model.title, "CLOSE", ActionClose);
  const fui::Rect body = screen.body();
  int16_t y = static_cast<int16_t>(body.y + 8);
  for (int i = model.first; i < model.count && i < model.first + kContentsRows; ++i) {
    if (y + kRowHeight > body.bottom()) break;
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
  // Paging by the side buttons; a tap on the last row's rule is not a control.
  if (model.count > kContentsRows) {
    char more[32];
    snprintf(more, sizeof(more), "%d of %d", model.first / kContentsRows + 1,
             (model.count + kContentsRows - 1) / kContentsRows);
    drawLine(screen,
             fui::Rect{static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(body.bottom() - 30),
                       static_cast<int16_t>(body.width - kMargin * 2), 24},
             more, toybox::kSmallFont, fui::TextAlign::Center);
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
