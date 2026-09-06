#include "MinesweeperScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>

#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxIcons.h"

namespace mineui {

namespace {

namespace ms = minesweeper;

// 60px, and eight of them are exactly the panel's 480: the minefield is the
// fork's one full-bleed play surface, the terrain rather than a widget on it.
// Mario picked this over two candidates that bought bigger cells by shrinking
// the game -- the field grows, the game does not. No gap between cells: a
// minefield is a continuous surface, and gaps would make eighty separate
// objects out of one.
constexpr int16_t kCell = 60;
constexpr int16_t kBoardWidth = kCell * ms::kColumns;
constexpr int16_t kBoardHeight = kCell * ms::kRows;

// The first CELL. The frame bar sits kRule above it and is the topmost thing
// drawn, so the clearance that matters is measured from the bar, not from here:
// at kHeaderHeight + 8 the bar landed at 81, inside the rows the header rule
// owns, and the minefield's top edge and the header's bottom edge were one
// line. Measured from the whole chrome now (band + gap + rule), plus a gutter,
// plus the bar's own weight.
int16_t boardTop() { return static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter + toybox::kRule); }

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

// The header band with the offset rule under it, as jaipur and the dungeon
// wear it. A local copy rather than a shared helper, for the reason
// LinkScreens gives: a copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, and the theme's default is black on
  // the black band. Jaipur paid for this discovery; see its toyboxChrome.
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// A fragment of minefield from a picture-string: '.' covered, 'o' open,
// '*' a covered mine, 'F' a flag on a mine, 'M' a mine laid bare for teaching.
// The numbers are computed from the fragment's own mines, so a diagram cannot
// disagree with the rule it illustrates -- the previous page one showed a 3
// with no mines in its picture at all.
void lessonField(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t cell,
                 const char* const* rows, const int columns, const int rowCount) {
  const auto mineAt = [&](const int c, const int r) {
    if (c < 0 || c >= columns || r < 0 || r >= rowCount) return false;
    const char mark = rows[r][c];
    return mark == '*' || mark == 'F' || mark == 'M';
  };
  for (int r = 0; r < rowCount; ++r) {
    for (int c = 0; c < columns; ++c) {
      const fui::Rect box =
          fui::makeRect(static_cast<int16_t>(left + c * cell), static_cast<int16_t>(top + r * cell), cell, cell);
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
          // A cell is 60px and the display cut's line box is 63, so the box
          // itself would sit the digit low against the cell's own frame.
          screen.target().text(toybox::inkCentred(box, toybox::kDisplayCut), digit, number);
        }
      }
    }
  }
}

// The front door in the documented band order: record line, rule, ornament,
// doors bottom-anchored with PLAY loudest. Returns the room left for the
// ornament between the rule and the doors.
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

  // Row 0 reads selected by default, jaipur's own trick: the most likely tap
  // is also the loudest thing below the rule.
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

// The five lessons. The fourth is the win condition, and that flags are notes
// rather than homework -- which nothing on the device said before.
//
// The fifth is the chord, and it is here because a move nobody is told about is
// worth half of one. Every other rule on this list can be discovered by tapping
// the board; this one cannot, because the cell it wants is one a player has
// learnt is already spent.
const char* const kLessonLines[] = {
    "A NUMBER COUNTS THE MINES TOUCHING IT, CORNERS INCLUDED.",
    "TAP TO DIG. HOLD TO PLANT A FLAG. THE BUTTON UNDER THE BOARD SWITCHES WHAT A TAP DOES.",
    "YOUR FIRST DIG IS ALWAYS SAFE, AND ALWAYS OPENS A SPACE.",
    "OPEN EVERY SAFE CELL AND THE FIELD IS CLEARED. FLAGS ARE NOTES: NONE ARE NEEDED TO WIN.",
    "TAP A NUMBER THAT HAS ALL ITS FLAGS AND THE REST OPENS. A WRONG FLAG ENDS IT.",
};

// One diagram per lesson, centred in the vertical band it is given.
void lessonDiagram(toybox::Screen& screen, const int page, const int16_t bandTop, const int16_t bandBottom,
                   const int16_t bigCell, const int16_t smallCell) {
  const fui::DeviceContext device = screen.device();
  static const char* const kCount[] = {"M..", ".oM", "..M"};
  static const char* const kTools[] = {"oF."};
  static const char* const kFlood[] = {"oo...", "oo**.", "ooo..", "ooo*."};
  static const char* const kWon[] = {"ooooo", "ooo*o", "ooooo", "ooFoo"};
  // The chord's board: a 1 with its one flag already planted, and the covered
  // cells it will open. The state to RECOGNISE, which is the half a picture
  // can carry -- the sentence carries what happens next.
  static const char* const kChord[] = {".F.", ".o.", "..."};

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
  } else if (page == 4) {
    rows = kChord;
    columns = 3;
    rowCount = 3;
  }
  const int16_t width = static_cast<int16_t>(cell * columns);
  const int16_t height = static_cast<int16_t>(cell * rowCount);
  const int16_t left = static_cast<int16_t>((device.width - width) / 2);
  const int16_t room = static_cast<int16_t>(bandBottom - bandTop);
  const int16_t top = static_cast<int16_t>(bandTop + (room > height ? (room - height) / 2 : 0));
  lessonField(screen, left, top, cell, rows, columns, rowCount);
}

// The bottom strip: what is left to find on the left, what a tap will do on
// the right.
//
// The counter is labelled. A bare numeral and a mark could not say what it
// counted, and with no total on screen the player could not recover the
// denominator -- so it names both, and the flag beside it says which unit.
//
// The mode is a control rather than a caption: rowStyles() so it is outlined
// while it is DIG and inverted once it is FLAG. The button default paints
// solid in every state, so the two modes came out identical black pills
// differing by one word, which is not a difference a glance carries on a
// one-bit panel.
void boardStrip(toybox::Screen& screen, const BoardModel& model) {
  const fui::Rect strip = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);

  fui::ButtonProps mode;
  mode.label = model.flagMode ? "FLAG" : "DIG";
  mode.action = ActionToggleMode;
  mode.styles = toybox::rowStyles();
  mode.state = model.flagMode ? fui::StateSelected : fui::StateNormal;
  const int16_t modeWidth = static_cast<int16_t>(strip.width / 3);
  screen.button(mode, fui::makeRect(static_cast<int16_t>(strip.right() - modeWidth), strip.y, modeWidth, strip.height));

  char line[toybox::kOfCounterChars];
  std::snprintf(line, sizeof(line), "%d OF %d", ms::minesRemaining(model.game), ms::kMines);
  fui::TextStyle count;
  count.font = toybox::kDisplayFont;
  count.align = fui::TextAlign::Center;
  const int16_t markSide = static_cast<int16_t>(strip.height);
  const int16_t countWidth = static_cast<int16_t>(strip.width - modeWidth - markSide - toybox::kGutter);
  screen.target().text(
      toybox::inkCentred(fui::makeRect(strip.x, strip.y, countWidth, strip.height), toybox::kDisplayCut), line, count);
  drawFlag(screen, fui::makeRect(static_cast<int16_t>(strip.x + countWidth), strip.y, markSide, markSide), false);
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

int howToPages() { return 5; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  const fui::Rect room = menuFrontDoor(screen, model);
  if (!model.hasHistory) return;

  // The last field is the centrepiece, at a size that carries its own numbers:
  // a replica of the settled board, not a thumbnail of it. Ornament made of
  // the app's own material and the app's own data, the only kind this fork
  // allows.
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

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int pages = howToPages();
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);

  // The page counter lives in the black band, jaipur's way, so it costs no
  // body space.
  char progress[toybox::kOfCounterChars];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, pages);
  toyboxChrome(screen, "HOW TO PLAY", progress);

  // Taken before the page's own drawing, so no branch can skip the way
  // forward.
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

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "MINESWEEPER";
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
  // The board itself is full bleed; only the strip below it keeps the margins.
  screen.insetContent(fui::Insets{0, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::DeviceContext device = screen.device();
  const fui::Rect first = cellRect(device, 0, 0);

  // A frame, so the minefield is one object rather than eighty rectangles
  // that happen to be adjacent. Two full-width bars TIGHT against the first
  // and last rows, not the floating kBoardFrame rule the inset board wore:
  // that outset is 9px, which on a full-bleed surface put the top bar flush
  // under the header band (where it vanished into it) and the bottom bar
  // adrift below the field, with the side bars under the cells. Full bleed
  // means the bezel is the side frame; the bars bound the terrain.
  screen.target().fill(fui::makeRect(0, static_cast<int16_t>(first.y - toybox::kRule), device.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.target().fill(fui::makeRect(0, static_cast<int16_t>(first.y + kBoardHeight), device.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

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
          // A cell is 60px and the display cut's line box is 63, so the box
          // itself would sit the digit low against the cell's own frame.
          screen.target().text(toybox::inkCentred(box, toybox::kDisplayCut), digit, number);
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

  // A settled board stays on screen, mines bared, and wears its verdict where
  // the tools were: the counter and the mode are questions, and the game has
  // just answered both. The capsule is the door to the stats -- the board is
  // not yanked away the tick it becomes worth looking at.
  if (ms::over(model.game)) {
    fui::ButtonProps verdict;
    verdict.label = model.game.status == ms::Status::Won ? "CLEARED" : "BOOM";
    verdict.action = ActionSeeResult;
    screen.button(verdict, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));
    return;
  }

  boardStrip(screen, model);
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  fui::HeaderProps header;
  header.title = model.won ? "CLEARED" : "BOOM";
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
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

  // The verdict as a sentence, before the stats: BOOM in the band names the
  // event, this names what it means for you.
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 2;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 30), area.width, 60),
                       model.won ? "YOU CLEARED THE FIELD" : "YOU HIT A MINE", body);

  char line[48];
  std::snprintf(line, sizeof(line), "%d OF %d CELLS OPENED", model.revealed, ms::kCells - ms::kMines);
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 110), area.width, 60), line, body);

  char flags[48];
  std::snprintf(flags, sizeof(flags), "%d OF %d MINES FLAGGED", model.flagsRight, ms::kMines);
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 180), area.width, 60), flags, body);
}

}  // namespace mineui
