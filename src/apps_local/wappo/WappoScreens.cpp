#include "WappoScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>
#include <cstdlib>

#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxText.h"
#include "WappoArt.h"

namespace wappoui {

namespace {

constexpr int16_t kCell = 60;
constexpr int16_t kBoardWidth = kCell * wappo::kBoardWidth;
constexpr int16_t kBoardHeight = kCell * wappo::kBoardHeight;

int16_t boardTop() { return static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter + 4); }

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
}

// The band with a title and a right label on ONE baseline. The header component
// centres each label's line box on its own, so when the title has to shrink to
// a smaller cut than the label, their letters stop sitting on the same line.
// Here the label with the taller capitals is cap-centred in the band and the
// other is set on its baseline.
void baselineChrome(toybox::Screen& screen, const char* title, const char* rightLabel) {
  chrome(screen, "", nullptr);

  // The readable part of the band (the glass hides its top rows), as headerBand() uses it.
  const fui::Rect inkBox = toybox::headerInkRect(screen).inset(fui::Insets{0, toybox::kMargin, 0, toybox::kMargin});

  fui::TextStyle rightStyle;
  rightStyle.font = toybox::kUiFont;
  rightStyle.color = fui::Color::White;
  rightStyle.inverted = true;
  rightStyle.align = fui::TextAlign::Right;
  const int16_t rightW = screen.target().measureText(rightStyle.font, rightLabel, rightStyle).width;

  fui::TextStyle titleStyle;
  titleStyle.font = toybox::kDisplayFont;
  titleStyle.color = fui::Color::White;
  titleStyle.inverted = true;
  titleStyle.align = fui::TextAlign::Left;
  const int16_t titleRoom = static_cast<int16_t>(inkBox.width - rightW - toybox::kGutter);
  const std::string fitted = toybox::fittedTitle(screen.target(), title, titleRoom, titleStyle);

  const toybox::CutMetrics* rightCut = toybox::cutForLineHeight(screen.target().lineHeight(rightStyle.font));
  const toybox::CutMetrics* titleCut = toybox::cutForLineHeight(screen.target().lineHeight(titleStyle.font));
  if (rightCut == nullptr || titleCut == nullptr) {  // unknown cut: fall back to line-box centring
    screen.target().text(inkBox, fitted.c_str(), titleStyle);
    screen.target().text(inkBox, rightLabel, rightStyle);
    return;
  }

  const toybox::CutMetrics& anchor = titleCut->inkHeight >= rightCut->inkHeight ? *titleCut : *rightCut;
  const int baseline = toybox::inkCentred(inkBox, anchor).y + anchor.ascender;
  const auto onBaseline = [&](const toybox::CutMetrics& cut, int16_t x, int16_t w) {
    return fui::makeRect(x, static_cast<int16_t>(baseline - cut.ascender), w, cut.lineHeight);
  };
  screen.target().text(onBaseline(*titleCut, inkBox.x, titleRoom), fitted.c_str(), titleStyle);
  screen.target().text(onBaseline(*rightCut, static_cast<int16_t>(inkBox.right() - rightW), rightW), rightLabel,
                       rightStyle);
}

// `halo`, when given, is painted white first so the piece stands clear of the floor.
void drawArt(toybox::Screen& screen, const fui::Rect& cell, const freeink::Icon& icon, fui::Color color,
             const freeink::Icon* halo = nullptr) {
  const int16_t size = cell.width < 32 ? cell.width : 32;
  const fui::Rect where = fui::makeRect(static_cast<int16_t>(cell.x + (cell.width - size) / 2),
                                        static_cast<int16_t>(cell.y + (cell.height - size) / 2), size, size);
  if (halo != nullptr) {
    screen.target().bitmap(where, fui::bitmapFromIcon(*halo), fui::BitmapMode::Contain,
                           fui::Paint::solid(fui::Color::White));
  }
  screen.target().bitmap(where, fui::bitmapFromIcon(icon), fui::BitmapMode::Contain, fui::Paint::solid(color));
}

// One word-wrapped paragraph pinned to the top of `area`; returns the height
// used. The target centres a wrapped block in its rect, clamped at the top, so
// a one-line rect keeps it top-aligned. Paragraphs are drawn one call each
// because the target's wrap does not honour '\n'.
int16_t drawParagraph(toybox::Screen& screen, const fui::Rect& area, const char* text, fui::TextStyle style) {
  style.maxLines = 16;
  int lines = 0;
  fui::layoutText(screen.target(), area, text, style, [&lines](const char*, fui::Rect) { ++lines; });
  const int16_t lh = screen.target().lineHeight(style.font);
  screen.target().text(fui::makeRect(area.x, area.y, area.width, lh), text, style);
  return static_cast<int16_t>(lines * lh);
}

// A check mark `size` px square with its top-left at (x, y), drawn from 3px
// squares along two strokes so it does not depend on the font having a glyph.
void drawTick(toybox::Screen& screen, int16_t x, int16_t y, int16_t size, fui::Color color) {
  const fui::Paint ink = fui::Paint::solid(color);
  const auto stroke = [&](int x0, int y0, int x1, int y1) {
    const int steps = std::abs(x1 - x0) > std::abs(y1 - y0) ? std::abs(x1 - x0) : std::abs(y1 - y0);
    for (int i = 0; i <= steps; ++i) {
      const int px = x0 + (x1 - x0) * i / (steps ? steps : 1);
      const int py = y0 + (y1 - y0) * i / (steps ? steps : 1);
      screen.target().fill(fui::makeRect(static_cast<int16_t>(x + px - 1), static_cast<int16_t>(y + py - 1), 3, 3),
                           ink);
    }
  };
  stroke(1, size / 2, size * 2 / 5, size - 2);
  stroke(size * 2 / 5, size - 2, size - 1, 2);
}

// Cleared: a plain tick. Cleared in par moves: the tick inside a filled badge.
void drawClearMark(toybox::Screen& screen, int16_t x, int16_t y, bool atPar, bool onBlack) {
  constexpr int16_t kBadge = 18;
  const fui::Color ink = onBlack ? fui::Color::White : fui::Color::Black;
  if (atPar) {
    screen.target().fill(fui::makeRect(x, y, kBadge, kBadge), fui::Paint::solid(ink));
    drawTick(screen, static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 3), 12,
             onBlack ? fui::Color::Black : fui::Color::White);
  } else {
    drawTick(screen, static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 3), 12, ink);
  }
}

// Floor, pieces, inner walls and perimeter, all read from the cell data.
void drawBoard(toybox::Screen& screen, const wappo::CellState* cells, int playerPos,
               const wappo::MonsterState* monsters, bool redMonster, int turn, int16_t left, int16_t top,
               int16_t cell) {
  const auto cellRect = [&](int x, int y) {
    return fui::makeRect(static_cast<int16_t>(left + x * cell), static_cast<int16_t>(top + y * cell), cell, cell);
  };

  for (int y = 0; y < wappo::kBoardHeight; ++y) {
    for (int x = 0; x < wappo::kBoardWidth; ++x) {
      const int idx = y * wappo::kBoardWidth + x;
      const fui::Rect rect = cellRect(x, y);

      if ((x + y) % 2 == 1) {
        screen.target().fill(rect, fui::Paint::dither(fui::Color::LightGray));
      }
      screen.target().stroke(rect, fui::Paint::solid(fui::Color::DarkGray), 1);

      if (cells[idx].special == 1) {
        drawArt(screen, rect, wappo::icon_pit_32, fui::Color::Black, &wappo::halo_pit_32);
      }

      for (int m = 0; m < 2; ++m) {
        const auto& mon = monsters[m];
        if (!mon.active || mon.pos != idx) continue;
        drawArt(screen, rect, redMonster ? wappo::icon_red_32 : wappo::icon_yel_32, fui::Color::Black,
                redMonster ? &wappo::halo_red_32 : &wappo::halo_yel_32);
        if (mon.trapped) {
          const int elapsed = turn - mon.trappedTurn;
          const int remaining = (elapsed <= 3) ? (3 - elapsed) : 0;
          char badgeStr[16];
          std::snprintf(badgeStr, sizeof(badgeStr), "T%d", remaining);  // T0: free next turn
          fui::TextStyle trapStyle;
          trapStyle.font = toybox::kSmallFont;
          trapStyle.color = fui::Color::White;
          trapStyle.inverted = true;  // paper-coloured ink on the black tag
          trapStyle.align = fui::TextAlign::Left;
          // Black tag in the cell's top-left corner, sized to hold its text with padding.
          constexpr int16_t kPadX = 5;
          constexpr int16_t kPadY = 3;
          const int16_t textW = screen.target().measureText(trapStyle.font, badgeStr, trapStyle).width;
          const int16_t textH = screen.target().lineHeight(trapStyle.font);
          const fui::Rect badge = fui::makeRect(rect.x, rect.y, static_cast<int16_t>(textW + 2 * kPadX),
                                                static_cast<int16_t>(textH + 2 * kPadY));
          screen.target().fill(badge, fui::Paint::solid(fui::Color::Black));
          screen.target().text(
              fui::makeRect(static_cast<int16_t>(rect.x + kPadX), static_cast<int16_t>(rect.y + kPadY), textW, textH),
              badgeStr, trapStyle);
        }
      }

      if (playerPos == idx) {
        drawArt(screen, rect, wappo::icon_player_32, fui::Color::Black, &wappo::halo_player_32);
      }
    }
  }

  // Walls: inner ones 7px centred on the cell edge, perimeter 5px on the outer edge.
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  for (int y = 0; y < wappo::kBoardHeight; ++y) {
    for (int x = 0; x < wappo::kBoardWidth; ++x) {
      const auto& c = cells[y * wappo::kBoardWidth + x];
      const fui::Rect rect = cellRect(x, y);
      if (c.wallRight && x < wappo::kBoardWidth - 1) {
        screen.target().fill(fui::makeRect(static_cast<int16_t>(rect.right() - 3), rect.y, 7, cell), ink);
      }
      if (c.wallDown && y < wappo::kBoardHeight - 1) {
        screen.target().fill(fui::makeRect(rect.x, static_cast<int16_t>(rect.bottom() - 3), cell, 7), ink);
      }
      if (x == 0 && c.wallLeft) {
        screen.target().fill(fui::makeRect(static_cast<int16_t>(rect.x - 3), rect.y, 5, cell), ink);
      }
      if (x == wappo::kBoardWidth - 1 && c.wallRight) {
        screen.target().fill(fui::makeRect(static_cast<int16_t>(rect.right() - 2), rect.y, 5, cell), ink);
      }
      if (y == 0 && c.wallUp) {
        screen.target().fill(fui::makeRect(rect.x, static_cast<int16_t>(rect.y - 3), cell, 5), ink);
      }
      if (y == wappo::kBoardHeight - 1 && c.wallDown) {
        screen.target().fill(fui::makeRect(rect.x, static_cast<int16_t>(rect.bottom() - 2), cell, 5), ink);
      }
    }
  }
}

}  // namespace

fui::Rect Layout::cellRect(const int col, const int row) const {
  return fui::makeRect(static_cast<int16_t>(board.x + col * cellSize), static_cast<int16_t>(board.y + row * cellSize),
                       cellSize, cellSize);
}

bool Layout::cellAt(const int x, const int y, int& col, int& row) const {
  const int dx = x - board.x;
  const int dy = y - board.y;
  if (dx < 0 || dy < 0 || dx >= board.width || dy >= board.height) return false;
  col = dx / cellSize;
  row = dy / cellSize;
  return col >= 0 && col < wappo::kBoardWidth && row >= 0 && row < wappo::kBoardHeight;
}

int LevelsLayout::levelAt(const int x, const int y) const {
  const int dx = x - grid.x;
  const int dy = y - grid.y;
  if (dx < 0 || dy < 0 || dx >= grid.width || dy >= grid.height) return -1;
  const int c = dx / cellW;
  const int r = dy / cellH;
  if (c < 0 || c >= cols || r < 0 || r >= rows) return -1;
  const int index = page * (cols * rows) + r * cols + c;
  if (index >= 0 && index < wappo::kLevelCount) return index;
  return -1;
}

int howToPages() { return 3; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  chrome(screen, "WAPPO", "PUZZLE");

  const fui::DeviceContext device = screen.device();

  const int16_t textTop = static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter * 2);

  char levelStr[32];
  std::snprintf(levelStr, sizeof(levelStr), "LEVEL %d / %d", model.currentLevel + 1, wappo::kLevelCount);
  fui::TextStyle levelStyle;
  levelStyle.font = toybox::kUiFont;
  levelStyle.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(0, textTop, device.width, 36), levelStr, levelStyle);

  char unlockedStr[48];
  if (model.progress && wappo::allCleared(*model.progress)) {
    std::snprintf(unlockedStr, sizeof(unlockedStr), "ALL LEVELS CLEARED - POINTS: %d",
                  wappo::totalPoints(*model.progress));
  } else {
    std::snprintf(unlockedStr, sizeof(unlockedStr), "UNLOCKED: %d",
                  (model.progress ? model.progress->maxUnlocked : 0) + 1);
  }
  fui::TextStyle unlockedStyle;
  unlockedStyle.font = toybox::kSmallFont;
  unlockedStyle.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(0, static_cast<int16_t>(textTop + 42), device.width, 20), unlockedStr,
                       unlockedStyle);

  // The level PLAY starts, as it starts: PLAY always begins a level fresh.
  constexpr int16_t kShowcaseCell = 46;
  wappo::CellState cells[wappo::kCellCount];
  wappo::buildCells(model.currentLevel, cells);
  const wappo::LevelDef& def = wappo::kLevels[model.currentLevel];
  const wappo::MonsterState monsters[2] = {{def.monsterStart[0], def.monsterCount >= 1, false, -1},
                                           {def.monsterStart[1], def.monsterCount >= 2, false, -1}};
  const int16_t showcaseW = kShowcaseCell * wappo::kBoardWidth;
  drawBoard(screen, cells, def.playerStart, monsters, false, 0, static_cast<int16_t>((device.width - showcaseW) / 2),
            static_cast<int16_t>(textTop + 80), kShowcaseCell);

  // Menu items list at bottom
  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};

  char playLabel[32];
  std::snprintf(playLabel, sizeof(playLabel), "PLAY LEVEL %d", model.currentLevel + 1);
  rows[static_cast<int>(MenuRow::Play)].label = playLabel;
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);

  rows[static_cast<int>(MenuRow::Levels)].label = "SELECT LEVEL";
  rows[static_cast<int>(MenuRow::Levels)].actionValue = static_cast<int16_t>(MenuRow::Levels);

  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  rows[static_cast<int>(MenuRow::HowTo)].actionValue = static_cast<int16_t>(MenuRow::HowTo);

  const int selected = model.selected < 0 ? 0 : model.selected;
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = ActionMenuRow;

  const int count = static_cast<int>(MenuRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);
}

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  char pageStr[32];
  std::snprintf(pageStr, sizeof(pageStr), "%d / %d", model.page + 1, howToPages());
  chrome(screen, "HOW TO PLAY", pageStr);

  const fui::DeviceContext device = screen.device();
  const int16_t top = static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter * 2);

  struct Page {
    const char* title;
    const freeink::Icon* icons[2];
    const char* paragraphs[3];
  };
  static constexpr Page kPages[] = {
      {"OBJECTIVE & CONTROLS",
       {&wappo::icon_player_32, nullptr},
       {"Guide Wappo to the exit gap in the edge of the 6x6 board.",
        "Tap an adjacent square, or press an arrow key, to move one step. Walls block both you and the monsters.",
        "Every turn needs a move: you cannot pass or wait."}},
      {"MONSTER RULES",
       {&wappo::icon_yel_32, nullptr},
       {"Monsters take 2 steps for every step you take.",
        "They always try to move sideways towards you first. If a wall is in the way, they move up or down "
        "towards you instead. If that is blocked too, they stay put.",
        "Use the walls to strand them!"}},
      {"PITS & MERGING",
       {&wappo::icon_pit_32, &wappo::icon_red_32},
       {"PITS: step into one and you lose. A monster that steps into one is stuck for 3 turns.",
        "MERGING: two monsters on the same square become one big monster.",
        "It takes 3 steps per turn and ignores pits."}},
  };
  const Page& page = kPages[model.page < howToPages() ? model.page : 0];

  fui::TextStyle titleStyle;
  titleStyle.font = toybox::kDisplayFont;
  titleStyle.align = fui::TextAlign::Center;

  fui::TextStyle bodyStyle;
  bodyStyle.font = toybox::kUiFont;
  bodyStyle.align = fui::TextAlign::Left;

  const int16_t left = toybox::kMargin + toybox::kGutter;
  const int16_t contentW = static_cast<int16_t>(device.width - 2 * left);
  int16_t y = top;

  y = static_cast<int16_t>(y + drawParagraph(screen, fui::makeRect(left, y, contentW, 120), page.title, titleStyle) +
                           toybox::kGutter);

  constexpr int16_t kIcon = 32;
  const int iconCount = page.icons[1] ? 2 : 1;
  const int16_t iconsW = static_cast<int16_t>(iconCount * kIcon + (iconCount - 1) * toybox::kGutter);
  for (int i = 0; i < iconCount; ++i) {
    const int16_t x = static_cast<int16_t>((device.width - iconsW) / 2 + i * (kIcon + toybox::kGutter));
    screen.target().bitmap(fui::makeRect(x, y, kIcon, kIcon), fui::bitmapFromIcon(*page.icons[i]),
                           fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
  }
  y = static_cast<int16_t>(y + kIcon + toybox::kGutter * 2);

  for (const char* paragraph : page.paragraphs) {
    y = static_cast<int16_t>(y + drawParagraph(screen, fui::makeRect(left, y, contentW, 400), paragraph, bodyStyle) +
                             toybox::kGutter);
  }

  // Navigation strip at bottom
  const fui::Rect strip = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const int16_t btnW = static_cast<int16_t>((strip.width - toybox::kGutter * 2) / 3);

  fui::ButtonProps prevBtn;
  prevBtn.label = "PREV";
  prevBtn.action = ActionHowToPrev;
  prevBtn.styles = toybox::rowStyles();
  prevBtn.state = model.page > 0 ? fui::StateNormal : fui::StateDisabled;
  screen.button(prevBtn, fui::makeRect(strip.x, strip.y, btnW, strip.height));

  fui::ButtonProps nextBtn;
  nextBtn.label = (model.page + 1 < howToPages()) ? "NEXT" : "DONE";
  nextBtn.action = ActionHowToNext;
  nextBtn.styles = toybox::rowStyles();
  screen.button(nextBtn,
                fui::makeRect(static_cast<int16_t>(strip.x + btnW + toybox::kGutter), strip.y, btnW, strip.height));

  fui::ButtonProps backBtn;
  backBtn.label = "BACK";
  backBtn.action = ActionBackMenu;
  backBtn.styles = toybox::rowStyles();
  screen.button(backBtn, fui::makeRect(static_cast<int16_t>(strip.x + (btnW + toybox::kGutter) * 2), strip.y, btnW,
                                       strip.height));
}

void buildLevels(toybox::Screen& screen, const LevelsModel& model, LevelsLayout& layout) {
  constexpr int kPerGrid = 20;
  const int totalPages = (wappo::kLevelCount + kPerGrid - 1) / kPerGrid;

  char sub[32];
  std::snprintf(sub, sizeof(sub), "PAGE %d / %d", model.page + 1, totalPages);
  chrome(screen, "SELECT LEVEL", sub);

  const fui::DeviceContext device = screen.device();
  const int16_t startLevel = static_cast<int16_t>(model.page * kPerGrid);

  layout.cols = 4;
  layout.rows = 5;
  layout.cellW = static_cast<int16_t>((device.width - 32) / layout.cols);
  layout.cellH = 48;
  layout.page = model.page;

  const int16_t gridW = static_cast<int16_t>(layout.cols * layout.cellW);
  const int16_t gridH = static_cast<int16_t>(layout.rows * layout.cellH);
  const int16_t gridX = static_cast<int16_t>((device.width - gridW) / 2);
  const int16_t gridY = static_cast<int16_t>(toybox::kChromeHeight + 16);
  layout.grid = fui::makeRect(gridX, gridY, gridW, gridH);

  for (int r = 0; r < layout.rows; ++r) {
    for (int c = 0; c < layout.cols; ++c) {
      const int lvl = startLevel + r * layout.cols + c;
      if (lvl >= wappo::kLevelCount) break;

      const fui::Rect box = fui::makeRect(static_cast<int16_t>(gridX + c * layout.cellW),
                                          static_cast<int16_t>(gridY + r * layout.cellH), layout.cellW, layout.cellH);
      const fui::Rect inner = fui::makeRect(static_cast<int16_t>(box.x + 3), static_cast<int16_t>(box.y + 3),
                                            static_cast<int16_t>(box.width - 6), static_cast<int16_t>(box.height - 6));

      const bool isCurrent = (lvl == model.currentLevel);
      const bool isUnlocked = model.progress && lvl <= model.progress->maxUnlocked;

      if (isCurrent) {
        screen.target().fill(inner, fui::Paint::solid(fui::Color::Black));
        screen.target().stroke(inner, fui::Paint::solid(fui::Color::Black), 2);
      } else if (isUnlocked) {
        screen.target().stroke(inner, fui::Paint::solid(fui::Color::Black), 1);
      } else {
        screen.target().fill(inner, fui::Paint::dither(fui::Color::LightGray));
        screen.target().stroke(inner, fui::Paint::solid(fui::Color::DarkGray), 1);
      }

      char numStr[16];
      std::snprintf(numStr, sizeof(numStr), "%d", lvl + 1);

      fui::TextStyle numStyle;
      numStyle.font = toybox::kUiFont;
      numStyle.color = isCurrent ? fui::Color::White : fui::Color::Black;
      numStyle.align = fui::TextAlign::Center;
      // Centred in the part of the tile left of the clear mark, on every tile, so the column lines up.
      const fui::Rect numBox = fui::makeRect(inner.x, inner.y, static_cast<int16_t>(inner.width - 24), inner.height);
      screen.target().text(numBox, numStr, numStyle);

      if (model.progress && wappo::isCleared(*model.progress, lvl)) {
        drawClearMark(screen, static_cast<int16_t>(inner.right() - 21), static_cast<int16_t>(inner.y + 3),
                      wappo::clearedAtPar(*model.progress, lvl), isCurrent);
      }
    }
  }

  // Legend and totals under the grid.
  if (model.progress) {
    fui::TextStyle legendStyle;
    legendStyle.font = toybox::kSmallFont;
    const int16_t lh = screen.target().lineHeight(legendStyle.font);
    int16_t y = static_cast<int16_t>(gridY + gridH + toybox::kGutter * 2);
    int16_t x = gridX;
    drawClearMark(screen, x, y, false, false);
    x = static_cast<int16_t>(x + 24);
    screen.target().text(fui::makeRect(x, y, 120, 18), "CLEARED", legendStyle);
    x = static_cast<int16_t>(x + screen.target().measureText(legendStyle.font, "CLEARED", legendStyle).width + 24);
    drawClearMark(screen, x, y, true, false);
    x = static_cast<int16_t>(x + 24);
    screen.target().text(fui::makeRect(x, y, static_cast<int16_t>(gridX + gridW - x), 18), "CLEARED IN PAR MOVES",
                         legendStyle);

    int cleared = 0;
    for (int i = 0; i < wappo::kLevelCount; ++i) cleared += wappo::isCleared(*model.progress, i) ? 1 : 0;
    char totals[96];
    std::snprintf(totals, sizeof(totals), "%d / %d CLEARED  -  %d AT PAR  -  %d POINTS", cleared, wappo::kLevelCount,
                  wappo::atParCount(*model.progress), wappo::totalPoints(*model.progress));
    legendStyle.align = fui::TextAlign::Center;
    screen.target().text(fui::makeRect(gridX, static_cast<int16_t>(y + 18 + toybox::kGutter), gridW, lh), totals,
                         legendStyle);
  }

  // Bottom buttons
  const fui::Rect strip = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const int16_t btnW = static_cast<int16_t>((strip.width - toybox::kGutter * 2) / 3);

  fui::ButtonProps prevBtn;
  prevBtn.label = "< PREV";
  prevBtn.action = ActionLevelsPrev;
  prevBtn.styles = toybox::rowStyles();
  prevBtn.state = model.page > 0 ? fui::StateNormal : fui::StateDisabled;
  screen.button(prevBtn, fui::makeRect(strip.x, strip.y, btnW, strip.height));

  fui::ButtonProps menuBtn;
  menuBtn.label = "BACK";
  menuBtn.action = ActionBackMenu;
  menuBtn.styles = toybox::rowStyles();
  screen.button(menuBtn,
                fui::makeRect(static_cast<int16_t>(strip.x + btnW + toybox::kGutter), strip.y, btnW, strip.height));

  fui::ButtonProps nextBtn;
  nextBtn.label = "NEXT >";
  nextBtn.action = ActionLevelsNext;
  nextBtn.styles = toybox::rowStyles();
  nextBtn.state = (model.page + 1 < totalPages) ? fui::StateNormal : fui::StateDisabled;
  screen.button(nextBtn, fui::makeRect(static_cast<int16_t>(strip.x + (btnW + toybox::kGutter) * 2), strip.y, btnW,
                                       strip.height));
}

void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout) {
  const wappo::Game& game = *model.game;
  char title[64];
  std::snprintf(title, sizeof(title), "LEVEL %d / %d", game.levelIndex + 1, wappo::kLevelCount);

  char rightLabel[64];
  std::snprintf(rightLabel, sizeof(rightLabel), "MOVE %d (PAR %d)", game.moves, model.par);
  baselineChrome(screen, title, rightLabel);

  const fui::DeviceContext device = screen.device();

  layout.cellSize = kCell;
  const int16_t bLeft = static_cast<int16_t>((device.width - kBoardWidth) / 2);
  const int16_t bTop = boardTop();
  layout.board = fui::makeRect(bLeft, bTop, kBoardWidth, kBoardHeight);

  drawBoard(screen, game.cells, game.playerPos, game.monsters, game.redMonster, game.turn, bLeft, bTop, kCell);

  // 4. Draw Planned Monster Path Arrows (when in MonstersTurn phase)
  if (game.phase == wappo::TurnPhase::MonstersTurn && game.outcome == wappo::Outcome::Playing) {
    for (int m = 0; m < 2; ++m) {
      const auto& plan = game.plannedPaths[m];
      if (plan.pathLen < 2) continue;

      for (int p = 0; p < plan.pathLen - 1; ++p) {
        const int fromIdx = plan.path[p];
        const int toIdx = plan.path[p + 1];
        if (fromIdx == toIdx) continue;

        const int fromX = fromIdx % wappo::kBoardWidth;
        const int fromY = fromIdx / wappo::kBoardWidth;
        const int toX = toIdx % wappo::kBoardWidth;
        const int toY = toIdx / wappo::kBoardWidth;

        const int16_t cx1 = static_cast<int16_t>(bLeft + fromX * kCell + kCell / 2);
        const int16_t cy1 = static_cast<int16_t>(bTop + fromY * kCell + kCell / 2);
        const int16_t cx2 = static_cast<int16_t>(bLeft + toX * kCell + kCell / 2);
        const int16_t cy2 = static_cast<int16_t>(bTop + toY * kCell + kCell / 2);

        // Draw connecting line bar
        if (cy1 == cy2) {
          const int16_t minX = (cx1 < cx2) ? cx1 : cx2;
          const int16_t lineW = static_cast<int16_t>(std::abs(cx2 - cx1));
          screen.target().fill(fui::makeRect(minX, static_cast<int16_t>(cy1 - 2), lineW, 5),
                               fui::Paint::solid(fui::Color::Black));
        } else if (cx1 == cx2) {
          const int16_t minY = (cy1 < cy2) ? cy1 : cy2;
          const int16_t lineH = static_cast<int16_t>(std::abs(cy2 - cy1));
          screen.target().fill(fui::makeRect(static_cast<int16_t>(cx1 - 2), minY, 5, lineH),
                               fui::Paint::solid(fui::Color::Black));
        }

        // On the final segment, a solid triangle whose tip sits on the cell centre
        // and points the way the monster travels.
        if (p == plan.pathLen - 2) {
          constexpr int kHead = 9;
          const int ux = (toX > fromX) - (toX < fromX);
          const int uy = (toY > fromY) - (toY < fromY);
          for (int d = 0; d <= kHead; ++d) {
            const int half = kHead - d;  // widest at the base, a point at the tip
            const int along = d - kHead;
            const fui::Rect strip =
                ux != 0 ? fui::makeRect(static_cast<int16_t>(cx2 + ux * along), static_cast<int16_t>(cy2 - half), 1,
                                        static_cast<int16_t>(2 * half + 1))
                        : fui::makeRect(static_cast<int16_t>(cx2 - half), static_cast<int16_t>(cy2 + uy * along),
                                        static_cast<int16_t>(2 * half + 1), 1);
            screen.target().fill(strip, fui::Paint::solid(fui::Color::Black));
          }
        }
      }
    }
  }

  // 5. Status Strip
  // Sized from the font, with room above and below the glyphs, so the bar never clips its text.
  const int16_t statusY = static_cast<int16_t>(bTop + kBoardHeight + toybox::kGutter + 4);
  const int16_t statusH = static_cast<int16_t>(screen.target().lineHeight(toybox::kUiFont) + 16);
  const fui::Rect statusBox = fui::makeRect(static_cast<int16_t>(toybox::kGutter), statusY,
                                            static_cast<int16_t>(device.width - 2 * toybox::kGutter), statusH);

  if (game.outcome == wappo::Outcome::Won) {
    const int score = wappo::scoreForLevel(model.par, game.moves);
    char winMsg[64];
    std::snprintf(winMsg, sizeof(winMsg), "CLEARED! SCORE: %d", score);
    screen.target().fill(statusBox, fui::Paint::solid(fui::Color::Black));
    fui::TextStyle style;
    style.font = toybox::kUiFont;
    style.color = fui::Color::White;
    style.align = fui::TextAlign::Center;
    screen.target().text(statusBox, winMsg, style);
  } else if (game.outcome == wappo::Outcome::LostCaught) {
    screen.target().fill(statusBox, fui::Paint::solid(fui::Color::Black));
    fui::TextStyle style;
    style.font = toybox::kUiFont;
    style.color = fui::Color::White;
    style.align = fui::TextAlign::Center;
    screen.target().text(statusBox, "CAUGHT! TRY UNDO", style);
  } else if (game.outcome == wappo::Outcome::LostPit) {
    screen.target().fill(statusBox, fui::Paint::solid(fui::Color::Black));
    fui::TextStyle style;
    style.font = toybox::kUiFont;
    style.color = fui::Color::White;
    style.align = fui::TextAlign::Center;
    screen.target().text(statusBox, "FELL IN A PIT! TRY UNDO", style);
  } else if (game.phase == wappo::TurnPhase::MonstersTurn) {
    int movingCount = 0;
    for (int m = 0; m < 2; ++m) {
      if (game.monsters[m].active && game.plannedPaths[m].pathLen > 1) {
        ++movingCount;
      }
    }
    screen.target().fill(statusBox, fui::Paint::solid(fui::Color::Black));
    fui::TextStyle style;
    style.font = toybox::kUiFont;
    style.color = fui::Color::White;
    style.align = fui::TextAlign::Center;
    const char* msg = (movingCount > 1) ? "TAP: MONSTERS MOVE" : "TAP: MONSTER MOVES";
    screen.target().text(statusBox, msg, style);
  } else {
    fui::TextStyle style;
    style.font = toybox::kUiFont;
    style.color = fui::Color::Black;
    style.align = fui::TextAlign::Center;
    screen.target().text(statusBox, "YOUR TURN", style);
  }

  // 6. Control buttons: Undo / Restart, then Levels / How to play
  const int16_t row1Y = static_cast<int16_t>(statusY + statusH + toybox::kGutter);
  const int16_t row2Y = static_cast<int16_t>(row1Y + toybox::kPillHeight + toybox::kGutter);
  // Full screen width, not board width: "HOW TO PLAY" does not fit a half-board pill.
  const int16_t rowLeft = static_cast<int16_t>(toybox::kGutter);
  const int16_t rowW = static_cast<int16_t>(device.width - 2 * toybox::kGutter);
  const int16_t halfW = static_cast<int16_t>((rowW - toybox::kGutter) / 2);
  const int16_t levelsW = static_cast<int16_t>((rowW - toybox::kGutter) * 2 / 5);
  const int16_t howToW = static_cast<int16_t>(rowW - toybox::kGutter - levelsW);

  // A cleared level has nothing to undo; its first button moves on instead.
  fui::ButtonProps firstBtn;
  firstBtn.styles = toybox::rowStyles();
  if (game.outcome == wappo::Outcome::Won) {
    const bool last = game.levelIndex + 1 >= wappo::kLevelCount;
    firstBtn.label = last ? "FINISH >" : "NEXT >";
    firstBtn.action = last ? ActionFinish : ActionNextLevel;
  } else {
    firstBtn.label = "UNDO";
    firstBtn.action = ActionUndo;
    firstBtn.state = game.historyCount > 0 ? fui::StateNormal : fui::StateDisabled;
  }
  screen.button(firstBtn, fui::makeRect(rowLeft, row1Y, halfW, toybox::kPillHeight));

  fui::ButtonProps restartBtn;
  restartBtn.label = "RESTART";
  restartBtn.action = ActionRestart;
  restartBtn.styles = toybox::rowStyles();
  screen.button(restartBtn, fui::makeRect(static_cast<int16_t>(rowLeft + halfW + toybox::kGutter), row1Y, halfW,
                                          toybox::kPillHeight));

  fui::ButtonProps levelsBtn;
  levelsBtn.label = "LEVELS";
  levelsBtn.action = ActionLevels;
  levelsBtn.styles = toybox::rowStyles();
  screen.button(levelsBtn, fui::makeRect(rowLeft, row2Y, levelsW, toybox::kPillHeight));

  fui::ButtonProps howToBtn;
  howToBtn.label = "HOW TO PLAY";
  howToBtn.action = ActionHowTo;
  howToBtn.styles = toybox::rowStyles();
  screen.button(howToBtn, fui::makeRect(static_cast<int16_t>(rowLeft + levelsW + toybox::kGutter), row2Y, howToW,
                                        toybox::kPillHeight));
}

void buildEnding(toybox::Screen& screen, const EndingModel& model) {
  chrome(screen, "WAPPO", "THE END");

  const fui::DeviceContext device = screen.device();
  const int16_t left = toybox::kMargin + toybox::kGutter;
  const int16_t contentW = static_cast<int16_t>(device.width - 2 * left);
  int16_t y = static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter * 3);

  constexpr int16_t kHero = 64;
  screen.target().bitmap(fui::makeRect(static_cast<int16_t>((device.width - kHero) / 2), y, kHero, kHero),
                         fui::bitmapFromIcon(wappo::icon_player_32), fui::BitmapMode::Contain,
                         fui::Paint::solid(fui::Color::Black));
  y = static_cast<int16_t>(y + kHero + toybox::kGutter * 2);

  // One line, at the largest cut that holds it: the display cut breaks it mid-word.
  fui::TextStyle titleStyle;
  titleStyle.font = toybox::kDisplayFont;
  titleStyle.align = fui::TextAlign::Center;
  const int16_t titleW = static_cast<int16_t>(device.width - 2 * toybox::kGutter);
  const std::string title = toybox::fittedTitle(screen.target(), "CONGRATULATIONS!", titleW, titleStyle);
  const int16_t titleH = screen.target().lineHeight(titleStyle.font);
  screen.target().text(fui::makeRect(static_cast<int16_t>(toybox::kGutter), y, titleW, titleH), title.c_str(),
                       titleStyle);
  y = static_cast<int16_t>(y + titleH + toybox::kGutter);

  fui::TextStyle bodyStyle;
  bodyStyle.font = toybox::kUiFont;
  bodyStyle.align = fui::TextAlign::Center;
  char line[48];
  std::snprintf(line, sizeof(line), "ALL %d LEVELS COMPLETED", wappo::kLevelCount);
  y = static_cast<int16_t>(y + drawParagraph(screen, fui::makeRect(left, y, contentW, 120), line, bodyStyle) +
                           toybox::kGutter * 3);

  // The score in a black band, like the board's status line.
  const int16_t bandH = static_cast<int16_t>(screen.target().lineHeight(toybox::kUiFont) + 16);
  const fui::Rect band = fui::makeRect(left, y, contentW, bandH);
  screen.target().fill(band, fui::Paint::solid(fui::Color::Black));
  fui::TextStyle bandStyle = bodyStyle;
  bandStyle.color = fui::Color::White;
  bandStyle.inverted = true;
  std::snprintf(line, sizeof(line), "POINTS: %d / %d", model.points, wappo::kMaxPoints);
  screen.target().text(band, line, bandStyle);
  y = static_cast<int16_t>(y + bandH + toybox::kGutter);

  fui::TextStyle smallStyle;
  smallStyle.font = toybox::kSmallFont;
  smallStyle.align = fui::TextAlign::Center;
  std::snprintf(line, sizeof(line), "%d OF %d LEVELS IN PAR MOVES", model.atPar, wappo::kLevelCount);
  screen.target().text(fui::makeRect(left, y, contentW, 20), line, smallStyle);

  const fui::Rect strip = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const int16_t btnW = static_cast<int16_t>((strip.width - toybox::kGutter) / 2);

  fui::ButtonProps levelsBtn;
  levelsBtn.label = "LEVELS";
  levelsBtn.action = ActionLevels;
  levelsBtn.styles = toybox::rowStyles();
  screen.button(levelsBtn, fui::makeRect(strip.x, strip.y, btnW, strip.height));

  fui::ButtonProps menuBtn;
  menuBtn.label = "MENU";
  menuBtn.action = ActionMenu;
  menuBtn.styles = toybox::rowStyles();
  screen.button(menuBtn,
                fui::makeRect(static_cast<int16_t>(strip.x + btnW + toybox::kGutter), strip.y, btnW, strip.height));
}

}  // namespace wappoui
