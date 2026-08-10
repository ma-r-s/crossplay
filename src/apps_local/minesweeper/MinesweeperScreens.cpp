#include "MinesweeperScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>
#include <cstdlib>

#include "../ui/ToyboxIcons.h"

namespace mineui {

namespace {

namespace ms = minesweeper;

// 56px, which is a thumb, and eight of them are exactly the 448px between the
// margins. No gap between cells: a minefield is a continuous surface, and gaps
// would make eighty separate objects out of one.
constexpr int16_t kCell = 56;
constexpr int16_t kBoardWidth = kCell * ms::kColumns;
constexpr int16_t kBoardHeight = kCell * ms::kRows;

int16_t boardTop() { return static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter * 2); }

// The mark on a flagged cell and beside the counter.
//
// Lucide's, not ours. Hand-drawn three times and wrong three times: every
// version put the pennant and the pole at the same height, so the pennant was
// coextensive with the staff and no flag could emerge from the geometry at any
// size. The first two attempts tuned the SIZE, which was never the problem.
//
// The fork already vendors Lucide and generates from it, and the design
// language says to draw our own only where no good licensed option exists. This
// is the same conclusion Solitaire reached about its pips after three rounds of
// hand-drawing.
void drawFlag(toybox::Screen& screen, const fui::Rect& where, const bool paper) {
  const int16_t side = where.width < where.height ? where.width : where.height;
  const int16_t inset = static_cast<int16_t>(side / 6);
  const fui::Rect box = fui::makeRect(static_cast<int16_t>(where.x + (where.width - side) / 2 + inset),
                                      static_cast<int16_t>(where.y + (where.height - side) / 2 + inset),
                                      static_cast<int16_t>(side - inset * 2), static_cast<int16_t>(side - inset * 2));
  screen.target().bitmap(box, fui::bitmapFromIcon(icon_mineflag_32), fui::BitmapMode::Contain,
                         fui::Paint::solid(paper ? fui::Color::White : fui::Color::Black));
}

// A mine: a filled disc approximated by stacked bars, with four spikes. Only
// ever seen once a game is settled.
void drawMine(toybox::Screen& screen, const fui::Rect& where, const bool paper) {
  const fui::Paint ink = fui::Paint::solid(paper ? fui::Color::White : fui::Color::Black);
  const int16_t cx = static_cast<int16_t>(where.x + where.width / 2);
  const int16_t cy = static_cast<int16_t>(where.y + where.height / 2);
  static const int8_t kHalfWidth[] = {4, 7, 9, 10, 10, 9, 7, 4};
  for (int i = 0; i < 8; ++i) {
    const int16_t half = kHalfWidth[i];
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(cx - half), static_cast<int16_t>(cy - 10 + i * 2 + 1), half * 2, 2), ink);
  }
  screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy - 16), 3, 6), ink);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy + 11), 3, 6), ink);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - 16), static_cast<int16_t>(cy - 1), 6, 3), ink);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(cx + 11), static_cast<int16_t>(cy - 1), 6, 3), ink);
}

// ---------------------------------------------------------------------------
// Art-pass candidates, behind runtime switches. ART_MENU / ART_HOWTO /
// ART_BOARD select a candidate on the simulator; unset (and on device) every
// screen renders the shipping layout, so the live watcher build is unchanged
// until the ballot lands. Losers and switches are deleted in the commit that
// keeps the winner, exactly as the four-game pass did.
// ---------------------------------------------------------------------------

#if defined(SIMULATOR)
int artVariant(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? 0 : std::atoi(value);
}
#else
int artVariant(const char*) { return 0; }
#endif

int artMenu() { return artVariant("ART_MENU"); }
int artHowTo() { return artVariant("ART_HOWTO"); }
int artBoard() { return artVariant("ART_BOARD"); }

// The header band with the offset rule under it, as jaipur and the dungeon
// wear it. A local copy rather than a shared helper, for the reason
// LinkScreens gives: a copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// A fragment of minefield from a picture-string: '.' covered, 'o' open,
// '*' a covered mine, 'F' a flag on a mine, 'M' a mine laid bare for teaching.
// The numbers are computed from the fragment's own mines, so a diagram cannot
// disagree with the rule it illustrates -- the checkers lesson learned that a
// picture that contradicts its caption is worse than none.
void lessonField(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t cell,
                 const char* const* rows, const int columns, const int rowCount) {
  const auto mineAt = [&](const int c, const int r) {
    if (c < 0 || c >= columns || r < 0 || r >= rowCount) return false;
    const char mark = rows[r][c];
    return mark == '*' || mark == 'F' || mark == 'M';
  };
  for (int r = 0; r < rowCount; ++r) {
    for (int c = 0; c < columns; ++c) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + c * cell),
                                          static_cast<int16_t>(top + r * cell), cell, cell);
      const char mark = rows[r][c];
      if (mark != 'o' && mark != 'M') screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
      if (mark == 'F') {
        drawFlag(screen, box, false);
      } else if (mark == 'M') {
        screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
        drawMine(screen, box, true);
      } else if (mark == 'o') {
        int touching = 0;
        for (int dc = -1; dc <= 1; ++dc) {
          for (int dr = -1; dr <= 1; ++dr) {
            if ((dc != 0 || dr != 0) && mineAt(c + dc, r + dr)) ++touching;
          }
        }
        if (touching > 0) {
          char digit[2] = {static_cast<char>('0' + touching), '\0'};
          fui::TextStyle number;
          number.font = toybox::kDisplayFont;
          number.align = fui::TextAlign::Center;
          screen.target().text(box, digit, number);
        }
      }
    }
  }
}

// The front door both menu candidates share: record line, rule, doors
// bottom-anchored with PLAY loudest. Returns the room left for the ornament.
fui::Rect menuFrontDoor(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "MINESWEEPER");

  char record[48];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d CLEARED", model.wins + model.losses, model.wins);
  const fui::Rect line = screen.takeTop(26);
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  screen.target().text(line, record, small);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
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
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

  const int16_t areaTop = static_cast<int16_t>(line.bottom() + 6 + toybox::kRule);
  return fui::makeRect(content.x, areaTop, content.width, static_cast<int16_t>(listBand.y - areaTop));
}

// Whether the remembered board ended in a dig on a mine.
bool lastGameLost(const ms::Game& board) {
  for (int column = 0; column < ms::kColumns; ++column) {
    for (int row = 0; row < ms::kRows; ++row) {
      const uint8_t cell = board.cell[column][row];
      if ((cell & ms::kMine) != 0 && (cell & ms::kRevealed) != 0) return true;
    }
  }
  return false;
}

// Menu A: the last field is the centrepiece, at a size that can carry its own
// numbers -- a full replica of the settled board, not a thumbnail of it.
void doMenuA(toybox::Screen& screen, const MenuModel& model) {
  const fui::Rect room = menuFrontDoor(screen, model);
  if (!model.hasHistory) return;

  const fui::DeviceContext device = screen.device();
  constexpr int16_t kMini = 34;
  const int16_t width = kMini * ms::kColumns;
  const int16_t height = kMini * ms::kRows;
  const int16_t stackH = static_cast<int16_t>(height + 10 + 24);
  const int16_t top = static_cast<int16_t>(room.y + (room.height > stackH ? (room.height - stackH) / 2 : 0));
  const int16_t left = static_cast<int16_t>((device.width - width) / 2);

  for (int column = 0; column < ms::kColumns; ++column) {
    for (int row = 0; row < ms::kRows; ++row) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + column * kMini),
                                          static_cast<int16_t>(top + row * kMini), kMini, kMini);
      const uint8_t cell = model.lastBoard.cell[column][row];
      if (cell & ms::kMine) {
        // Every mine laid bare, flagged or found or missed: the game is over
        // and where they were is the memory worth keeping.
        screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
      } else if ((cell & ms::kRevealed) == 0) {
        screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
        screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);
        if (cell & ms::kFlagged) drawFlag(screen, box, false);
      } else {
        screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);
        const int touching = ms::neighbouringMines(model.lastBoard, column, row);
        if (touching > 0) {
          char digit[2] = {static_cast<char>('0' + touching), '\0'};
          fui::TextStyle number;
          number.font = toybox::kSmallFont;
          number.align = fui::TextAlign::Center;
          screen.target().text(box, digit, number);
        }
      }
    }
  }
  screen.target().stroke(fui::makeRect(static_cast<int16_t>(left - 2), static_cast<int16_t>(top - 2),
                                       static_cast<int16_t>(width + 4), static_cast<int16_t>(height + 4)),
                         fui::Paint::solid(fui::Color::Black), 2);

  fui::TextStyle caption;
  caption.font = toybox::kTileFont;
  caption.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(room.x, static_cast<int16_t>(top + height + 10), room.width, 24),
                       lastGameLost(model.lastBoard) ? "LAST GAME: BOOM" : "LAST GAME: CLEARED", caption);
}

// Menu B: the record is the centrepiece -- the cleared count big, the last
// field kept small below it, the way yahtzee's door leads with its best.
void doMenuB(toybox::Screen& screen, const MenuModel& model) {
  const fui::Rect room = menuFrontDoor(screen, model);
  if (!model.hasHistory) return;

  const fui::DeviceContext device = screen.device();
  constexpr int16_t kMini = 22;
  const int16_t fieldW = kMini * ms::kColumns;
  const int16_t fieldH = kMini * ms::kRows;
  const int16_t stackH = static_cast<int16_t>(56 + 26 + 18 + fieldH + 10 + 24);
  const int16_t top = static_cast<int16_t>(room.y + (room.height > stackH ? (room.height - stackH) / 2 : 0));

  char big[16];
  std::snprintf(big, sizeof(big), "%d", model.wins);
  fui::TextStyle numeral;
  numeral.font = toybox::kDisplayFont;
  numeral.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(room.x, top, room.width, 56), big, numeral);
  fui::TextStyle label;
  label.font = toybox::kTileFont;
  label.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(room.x, static_cast<int16_t>(top + 56), room.width, 26),
                       model.wins == 1 ? "FIELD CLEARED" : "FIELDS CLEARED", label);

  const int16_t fieldTop = static_cast<int16_t>(top + 56 + 26 + 18);
  const int16_t left = static_cast<int16_t>((device.width - fieldW) / 2);
  for (int column = 0; column < ms::kColumns; ++column) {
    for (int row = 0; row < ms::kRows; ++row) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + column * kMini),
                                          static_cast<int16_t>(fieldTop + row * kMini), kMini, kMini);
      const uint8_t cell = model.lastBoard.cell[column][row];
      if (cell & ms::kMine) {
        screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
      } else if ((cell & ms::kRevealed) == 0) {
        screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      }
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);
    }
  }
  screen.target().text(fui::makeRect(room.x, static_cast<int16_t>(fieldTop + fieldH + 10), room.width, 24),
                       lastGameLost(model.lastBoard) ? "LAST GAME: BOOM" : "LAST GAME: CLEARED", label);
}

// The four lessons both how-to candidates teach. The fourth is new: the win
// condition, and that flags are notes rather than homework, which the current
// screens never say.
const char* const kLessonLines[] = {
    "A NUMBER COUNTS THE MINES TOUCHING IT, CORNERS INCLUDED.",
    "TAP TO DIG. HOLD TO PLANT A FLAG. THE BUTTON UNDER THE BOARD SWITCHES WHAT A TAP DOES.",
    "YOUR FIRST DIG IS ALWAYS SAFE, AND ALWAYS OPENS A SPACE.",
    "OPEN EVERY SAFE CELL AND THE FIELD IS CLEARED. FLAGS ARE NOTES: NONE ARE NEEDED TO WIN.",
};

// One diagram per lesson, centred in the vertical band it is given.
void lessonDiagram(toybox::Screen& screen, const int page, const int16_t bandTop, const int16_t bandBottom,
                   const int16_t bigCell, const int16_t smallCell) {
  const fui::DeviceContext device = screen.device();
  static const char* const kCount[] = {"M..", ".oM", "..M"};
  static const char* const kTools[] = {"oF."};
  static const char* const kFlood[] = {"oo...", "oo**.", "ooo..", "ooo*."};
  static const char* const kWon[] = {"ooooo", "ooo*o", "ooooo", "ooFoo"};

  const char* const* rows = kCount;
  int columns = 3;
  int rowCount = 3;
  int16_t cell = bigCell;
  if (page == 1) {
    rows = kTools;
    columns = 3;
    rowCount = 1;
  } else if (page == 2) {
    rows = kFlood;
    columns = 5;
    rowCount = 4;
    cell = smallCell;
  } else if (page == 3) {
    rows = kWon;
    columns = 5;
    rowCount = 4;
    cell = smallCell;
  }
  const int16_t width = static_cast<int16_t>(cell * columns);
  const int16_t height = static_cast<int16_t>(cell * rowCount);
  const int16_t left = static_cast<int16_t>((device.width - width) / 2);
  const int16_t room = static_cast<int16_t>(bandBottom - bandTop);
  const int16_t top = static_cast<int16_t>(bandTop + (room > height ? (room - height) / 2 : 0));
  lessonField(screen, left, top, cell, rows, columns, rowCount);
}

// How-to A: the banded shape knucklebones ships -- counter in the black band,
// one sentence and one picture, the pill the way forward.
void doHowToA(toybox::Screen& screen, const HowToModel& model) {
  const int pages = howToPages();
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);

  char progress[16];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, pages);
  toyboxChrome(screen, "HOW TO PLAY", progress);

  fui::ButtonProps next;
  next.label = page + 1 < pages ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 4;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 150), kLessonLines[page], body);

  lessonDiagram(screen, page, static_cast<int16_t>(area.y + 160), area.bottom(), 88, 64);
}

// How-to B: the tutorial shape checkers ships -- a titled page, the whole page
// as the button, dots at the foot.
void doHowToB(toybox::Screen& screen, const HowToModel& model) {
  const int pages = howToPages();
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);

  char progress[16];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, pages);
  toyboxChrome(screen, "HOW TO PLAY", progress);
  const fui::Rect body = screen.body();
  screen.frame().hit(body, ActionHowToNext, 0);

  constexpr int16_t kDot = 14;
  constexpr int16_t kDotGap = 10;
  const int16_t dotRow = static_cast<int16_t>(pages * kDot + (pages - 1) * kDotGap);
  const int16_t dotX = static_cast<int16_t>(body.x + (body.width - dotRow) / 2);
  const int16_t dotY = static_cast<int16_t>(body.bottom() - kDot);
  for (int i = 0; i < pages; ++i) {
    const fui::Rect dotAt = fui::makeRect(static_cast<int16_t>(dotX + i * (kDot + kDotGap)), dotY, kDot, kDot);
    if (i == page) {
      screen.target().fill(dotAt, fui::Paint::solid(fui::Color::Black), 7);
    } else {
      screen.target().stroke(dotAt, fui::Paint::dither(fui::Color::DarkGray), toybox::kHairline, 7);
    }
  }
  fui::TextStyle tapLine;
  tapLine.font = toybox::kTileFont;
  tapLine.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(dotY - 34), body.width, 22),
                       page + 1 == pages ? "TAP TO FINISH" : "TAP TO CONTINUE", tapLine);

  static const char* const kTitles[] = {"THE NUMBERS", "DIG AND FLAG", "THE FIRST DIG", "CLEARING"};
  fui::TextStyle title;
  title.font = toybox::kDisplayFont;
  title.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, body.y, body.width, 46), kTitles[page], title);

  fui::TextStyle cap;
  cap.font = toybox::kUiFont;
  cap.align = fui::TextAlign::Center;
  cap.maxLines = 4;
  const int16_t capTop = static_cast<int16_t>(body.bottom() - 56 - 204);
  screen.target().text(fui::makeRect(body.x, capTop, body.width, 200), kLessonLines[page], cap);

  lessonDiagram(screen, page, static_cast<int16_t>(body.y + 92), capTop, 96, 68);
}

}  // namespace

fui::Rect cellRect(const fui::DeviceContext& device, const int column, const int row) {
  const int16_t left = static_cast<int16_t>((device.width - kBoardWidth) / 2);
  return fui::makeRect(static_cast<int16_t>(left + column * kCell), static_cast<int16_t>(boardTop() + row * kCell),
                       kCell, kCell);
}

bool cellAt(const fui::DeviceContext& device, const int x, const int y, int& column, int& row) {
  const int16_t left = static_cast<int16_t>((device.width - kBoardWidth) / 2);
  const int dx = x - left;
  const int dy = y - boardTop();
  if (dx < 0 || dy < 0 || dx >= kBoardWidth || dy >= kBoardHeight) return false;
  column = dx / kCell;
  row = dy / kCell;
  return true;
}

// Four pages in the candidates: the win condition gets a page of its own.
int howToPages() { return artHowTo() == 0 ? 3 : 4; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  if (artMenu() == 1) {
    doMenuA(screen, model);
    return;
  }
  if (artMenu() == 2) {
    doMenuB(screen, model);
    return;
  }
  fui::HeaderProps header;
  header.title = "MINESWEEPER";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  rows[static_cast<int>(MenuRow::HowTo)].actionValue = static_cast<int16_t>(MenuRow::HowTo);

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionMenuRow;
  const fui::Rect band = screen.body();
  screen.list(list);

  if (!model.hasHistory) return;

  // The last minefield, opened, as the front door's ornament: made of the app's
  // own material and carrying the app's own data, which is the only kind of
  // decoration this fork allows. Two devices show different pictures here.
  const fui::DeviceContext device = screen.device();
  constexpr int16_t kMini = 22;
  const int16_t width = kMini * ms::kColumns;
  const int16_t left = static_cast<int16_t>((device.width - width) / 2);
  const int16_t top =
      static_cast<int16_t>(band.y + (toybox::kRowHeight + toybox::kGutter) * static_cast<int>(MenuRow::Count) + 36);

  for (int column = 0; column < ms::kColumns; ++column) {
    for (int row = 0; row < ms::kRows; ++row) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + column * kMini),
                                          static_cast<int16_t>(top + row * kMini), kMini, kMini);
      const uint8_t cell = model.lastBoard.cell[column][row];
      // Mines solid, cleared cells empty, unopened cells dithered. At 22px a
      // number would not read, and the shape of the field is the memory worth
      // keeping anyway.
      if (cell & ms::kMine) {
        screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
      } else if ((cell & ms::kRevealed) == 0) {
        screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      }
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);
    }
  }

  char record[32];
  std::snprintf(record, sizeof(record), "%d CLEARED  %d LOST", model.wins, model.losses);
  fui::TextStyle tally;
  tally.font = toybox::kSmallFont;
  tally.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(band.x, static_cast<int16_t>(top + kMini * ms::kRows + 12), band.width, 24),
                       record, tally);
}

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  if (artHowTo() == 1) {
    doHowToA(screen, model);
    return;
  }
  if (artHowTo() == 2) {
    doHowToB(screen, model);
    return;
  }
  const int page = model.page < 0 ? 0 : (model.page >= howToPages() ? howToPages() - 1 : model.page);

  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // Taken before the page's own drawing, so no branch can skip the way forward.
  fui::ButtonProps next;
  next.label = page + 1 < howToPages() ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();

  // Drawn in the body, not as the header's rightLabel: that renders in ink on
  // the black band and is invisible, and a host test asserting it was drawn
  // passes anyway.
  char progress[8];
  std::snprintf(progress, sizeof(progress), "%d/%d", page + 1, howToPages());
  fui::TextStyle counter;
  counter.font = toybox::kSmallFont;
  counter.align = fui::TextAlign::Right;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 20), progress, counter);

  static const char* const kLines[] = {
      "A NUMBER COUNTS THE MINES TOUCHING IT. THREE MEANS THREE OF ITS EIGHT NEIGHBOURS.",
      "TAP A CELL TO DIG IT. SWITCH THE BUTTON UNDER THE BOARD TO FLAG, AND A TAP PLANTS A FLAG INSTEAD. HOLDING A CELL ALWAYS FLAGS IT.",
      "YOUR FIRST DIG IS ALWAYS SAFE, AND ALWAYS OPENS A SPACE.",
  };
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 4;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 24), area.width, 150), kLines[page], body);

  // A picture of the real thing on every page, at the real cell size.
  const fui::DeviceContext device = screen.device();
  const int16_t top = static_cast<int16_t>(area.y + 200);
  const int16_t left = static_cast<int16_t>((device.width - kCell * 3) / 2);
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 2; ++row) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + column * kCell),
                                          static_cast<int16_t>(top + row * kCell), kCell, kCell);
      const bool opened = page == 0 ? (row == 0) : (page == 2 && row == 0 && column == 1);
      if (!opened) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);

      if (page == 0 && row == 0 && column == 1) {
        fui::TextStyle number;
        number.font = toybox::kDisplayFont;
        number.align = fui::TextAlign::Center;
        screen.target().text(box, "3", number);
      }
      if (page == 1 && row == 0 && column == 1) drawFlag(screen, box, false);
    }
  }
}

namespace {

// The bottom strip every board candidate shares: what is left to find on the
// left, what a tap will do on the right.
void boardStrip(toybox::Screen& screen, const BoardModel& model, const int remaining, const int total) {
  const fui::Rect strip = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);

  fui::ButtonProps mode;
  mode.label = model.flagMode ? "FLAG" : "DIG";
  mode.action = ActionToggleMode;
  mode.styles = toybox::rowStyles();
  mode.state = model.flagMode ? fui::StateSelected : fui::StateNormal;
  const int16_t modeWidth = static_cast<int16_t>(strip.width / 3);
  screen.button(mode,
                fui::makeRect(static_cast<int16_t>(strip.right() - modeWidth), strip.y, modeWidth, strip.height));

  char line[24];
  std::snprintf(line, sizeof(line), "%d OF %d", remaining, total);
  fui::TextStyle count;
  count.font = toybox::kDisplayFont;
  count.align = fui::TextAlign::Center;
  const int16_t markSide = static_cast<int16_t>(strip.height);
  const int16_t countWidth = static_cast<int16_t>(strip.width - modeWidth - markSide - toybox::kGutter);
  screen.target().text(fui::makeRect(strip.x, strip.y, countWidth, strip.height), line, count);
  drawFlag(screen, fui::makeRect(static_cast<int16_t>(strip.x + countWidth), strip.y, markSide, markSide), false);
}

// Board A: the game unchanged, the field grown to the panel's full width.
// Eight 60px columns are exactly 480; the board becomes the one full-bleed
// surface, the way a minefield is the whole terrain rather than a widget on it.
void doBoardA(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "MINESWEEPER";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{0, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::DeviceContext device = screen.device();
  constexpr int16_t kBig = 60;
  const int16_t top = static_cast<int16_t>(toybox::kHeaderHeight + 8);

  screen.target().stroke(fui::makeRect(0, static_cast<int16_t>(top - toybox::kBoardFrame), device.width,
                                       static_cast<int16_t>(kBig * ms::kRows + toybox::kBoardFrame * 2)),
                         fui::Paint::solid(fui::Color::Black), 3);

  for (int column = 0; column < ms::kColumns; ++column) {
    for (int row = 0; row < ms::kRows; ++row) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(column * kBig),
                                          static_cast<int16_t>(top + row * kBig), kBig, kBig);
      const uint8_t cell = model.game.cell[column][row];
      const bool revealed = (cell & ms::kRevealed) != 0;

      if (!revealed) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);

      if (cell & ms::kFlagged) {
        drawFlag(screen, box, false);
      } else if (revealed && (cell & ms::kMine)) {
        screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
        drawMine(screen, box, true);
      } else if (model.showMines && (cell & ms::kMine)) {
        drawMine(screen, box, false);
      } else if (revealed) {
        const int touching = ms::neighbouringMines(model.game, column, row);
        if (touching > 0) {
          char digit[2] = {static_cast<char>('0' + touching), '\0'};
          fui::TextStyle number;
          number.font = toybox::kDisplayFont;
          number.align = fui::TextAlign::Center;
          screen.target().text(box, digit, number);
        }
      }
    }
  }

  if (model.holdColumn >= 0 && model.holdRow >= 0) {
    const fui::Rect box = fui::makeRect(static_cast<int16_t>(model.holdColumn * kBig),
                                        static_cast<int16_t>(top + model.holdRow * kBig), kBig, kBig);
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 4);
  }

  boardStrip(screen, model, ms::minesRemaining(model.game), ms::kMines);
}

// Boards B and C: mock fields at grids the core does not have yet. Picking one
// means changing the game itself -- fewer columns, fewer mines -- so the mock
// exists to make that trade visible, not to be shipped. Same picture-string
// scheme as the lessons: numbers computed from the mock's own mines.
void doBoardMock(toybox::Screen& screen, const BoardModel& model, const char* const* rows, const int columns,
                 const int rowCount, const int16_t cell, const int16_t left, const int16_t top, const int total) {
  fui::HeaderProps header;
  header.title = "MINESWEEPER";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{0, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const int16_t frameX = static_cast<int16_t>(left >= toybox::kBoardFrame ? left - toybox::kBoardFrame : 0);
  const int16_t frameW = static_cast<int16_t>(cell * columns + (left >= toybox::kBoardFrame ? toybox::kBoardFrame * 2
                                                                                           : 0));
  screen.target().stroke(fui::makeRect(frameX, static_cast<int16_t>(top - toybox::kBoardFrame), frameW,
                                       static_cast<int16_t>(cell * rowCount + toybox::kBoardFrame * 2)),
                         fui::Paint::solid(fui::Color::Black), 3);

  const auto mineAt = [&](const int c, const int r) {
    if (c < 0 || c >= columns || r < 0 || r >= rowCount) return false;
    return rows[r][c] == '*' || rows[r][c] == 'F';
  };
  int flags = 0;
  for (int r = 0; r < rowCount; ++r) {
    for (int c = 0; c < columns; ++c) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + c * cell),
                                          static_cast<int16_t>(top + r * cell), cell, cell);
      const char mark = rows[r][c];
      if (mark != 'o') screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);
      if (mark == 'F') {
        ++flags;
        drawFlag(screen, box, false);
      } else if (mark == 'o') {
        int touching = 0;
        for (int dc = -1; dc <= 1; ++dc) {
          for (int dr = -1; dr <= 1; ++dr) {
            if ((dc != 0 || dr != 0) && mineAt(c + dc, r + dr)) ++touching;
          }
        }
        if (touching > 0) {
          char digit[2] = {static_cast<char>('0' + touching), '\0'};
          fui::TextStyle number;
          number.font = toybox::kDisplayFont;
          number.align = fui::TextAlign::Center;
          screen.target().text(box, digit, number);
        }
      }
    }
  }

  boardStrip(screen, model, total - flags, total);
}

}  // namespace

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  // B: 64px cells inside the margins, a 7x9 field of 8 mines.
  static const char* const kMockB[] = {
      "oo*....", "oooo**.", "oooo*..", "oooo...", "oooo..*", "ooooF..", "oooo...", "oooo...", "oooo*.*",
  };
  // C: 80px cells full-bleed, a 6x8 field of 6 mines.
  static const char* const kMockC[] = {
      "...*..", "*..F..", "ooooo*", "oooooo", "oooooo", "oooooo", "oooo*.", "ooo*..",
  };
  switch (artBoard()) {
    case 1:
      doBoardA(screen, model);
      return;
    case 2:
      doBoardMock(screen, model, kMockB, 7, 9, 64, toybox::kMargin, static_cast<int16_t>(toybox::kHeaderHeight + 16),
                  8);
      return;
    case 3:
      doBoardMock(screen, model, kMockC, 6, 8, 80, 0, static_cast<int16_t>(toybox::kHeaderHeight + 6), 6);
      return;
    default:
      break;
  }
  fui::HeaderProps header;
  header.title = "MINESWEEPER";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  // The board screen was the only one that skipped this, so its bottom control
  // bled into the left, right and bottom bezels. Only full-bleed chrome -- the
  // header band -- touches an edge.
  screen.insetContent(fui::Insets{toybox::kGutter * 2, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::DeviceContext device = screen.device();
  const fui::Rect first = cellRect(device, 0, 0);

  // A frame, so the minefield is one object rather than eighty rectangles that
  // happen to be adjacent. Without it the board's outer boundary was the same
  // hairline as its interior grid, and late in a game -- when most cells are
  // paper -- the board dissolved into the page around it.
  const fui::Rect frame = fui::makeRect(static_cast<int16_t>(first.x - toybox::kBoardFrame),
                                        static_cast<int16_t>(first.y - toybox::kBoardFrame),
                                        static_cast<int16_t>(kBoardWidth + toybox::kBoardFrame * 2),
                                        static_cast<int16_t>(kBoardHeight + toybox::kBoardFrame * 2));
  screen.target().stroke(frame, fui::Paint::solid(fui::Color::Black), 3);

  for (int column = 0; column < ms::kColumns; ++column) {
    for (int row = 0; row < ms::kRows; ++row) {
      const fui::Rect box = cellRect(device, column, row);
      const uint8_t cell = model.game.cell[column][row];
      const bool revealed = (cell & ms::kRevealed) != 0;

      if (!revealed) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);

      if (cell & ms::kFlagged) {
        drawFlag(screen, box, false);
      } else if (revealed && (cell & ms::kMine)) {
        screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
        drawMine(screen, box, true);
      } else if (model.showMines && (cell & ms::kMine)) {
        drawMine(screen, box, false);
      } else if (revealed) {
        const int touching = ms::neighbouringMines(model.game, column, row);
        if (touching > 0) {
          char digit[2] = {static_cast<char>('0' + touching), '\0'};
          fui::TextStyle number;
          number.font = toybox::kDisplayFont;
          number.align = fui::TextAlign::Center;
          screen.target().text(box, digit, number);
        }
      }
    }
  }

  // The cell a finger is currently resting on. A hold that shows nothing until
  // it fires is indistinguishable, on this panel, from a tap that missed.
  if (model.holdColumn >= 0 && model.holdRow >= 0) {
    const fui::Rect box = cellRect(device, model.holdColumn, model.holdRow);
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 4);
  }

  // The bottom strip carries two different things, so it is split rather than
  // shared: on the left what is left to find, on the right what a tap will do.
  //
  // The counter is labelled. A bare numeral and a mark could not say what it
  // counted, and with no total on screen the player could not recover the
  // denominator -- so it names both, and the flag beside it says which unit.
  const fui::Rect strip = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);

  // The mode, as a control rather than a caption: outlined while it is DIG,
  // inverted once it is FLAG.
  //
  // rowStyles() rather than the button default, and that is the whole point.
  // The default paints solid in every state, so the two modes came out
  // identical black pills differing by one word -- three letters against four,
  // which is not a difference a glance carries on a one-bit panel, and it made
  // DIG look like the emphasised action when it is only the resting one.
  // rowStyles is what settings rows already use for exactly this shape of
  // thing, so the fork's own two-state vocabulary is doing the work.
  fui::ButtonProps mode;
  mode.label = model.flagMode ? "FLAG" : "DIG";
  mode.action = ActionToggleMode;
  mode.styles = toybox::rowStyles();
  mode.state = model.flagMode ? fui::StateSelected : fui::StateNormal;
  const int16_t modeWidth = static_cast<int16_t>(strip.width / 3);
  screen.button(mode, fui::makeRect(static_cast<int16_t>(strip.right() - modeWidth), strip.y, modeWidth,
                                    strip.height));

  char line[24];
  std::snprintf(line, sizeof(line), "%d OF %d", ms::minesRemaining(model.game), ms::kMines);
  fui::TextStyle count;
  count.font = toybox::kDisplayFont;
  count.align = fui::TextAlign::Center;
  const int16_t markSide = static_cast<int16_t>(strip.height);
  const int16_t countWidth = static_cast<int16_t>(strip.width - modeWidth - markSide - toybox::kGutter);
  screen.target().text(fui::makeRect(strip.x, strip.y, countWidth, strip.height), line, count);
  drawFlag(screen, fui::makeRect(static_cast<int16_t>(strip.x + countWidth), strip.y, markSide, markSide), false);
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  fui::HeaderProps header;
  header.title = model.won ? "CLEARED" : "BOOM";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  screen.button(done, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  screen.button(again, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  char line[48];
  std::snprintf(line, sizeof(line), "%d OF %d CELLS OPENED", model.revealed, ms::kCells - ms::kMines);
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 2;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 40), area.width, 60), line, body);

  char flags[48];
  std::snprintf(flags, sizeof(flags), "%d OF %d MINES FLAGGED", model.flagsRight, ms::kMines);
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 110), area.width, 60), flags, body);
}

}  // namespace mineui
