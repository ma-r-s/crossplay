#include "MinesweeperScreens.h"

#include <cstdio>

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

// The mark on a flagged cell and in the counter: a filled triangle on a staff,
// drawn rather than lettered because it appears eleven times on a full board.
void drawFlag(toybox::Screen& screen, const fui::Rect& where, const bool paper) {
  const fui::Paint ink = fui::Paint::solid(paper ? fui::Color::White : fui::Color::Black);
  const int16_t cx = static_cast<int16_t>(where.x + where.width / 2);
  const int16_t cy = static_cast<int16_t>(where.y + where.height / 2);
  // Staff, then a pennant of stacked bars: a triangle from fills, since the
  // draw target's triangle takes three points and this reads cleaner at 56px.
  screen.target().fill(fui::makeRect(static_cast<int16_t>(cx + 5), static_cast<int16_t>(cy - 13), 3, 26), ink);
  for (int step = 0; step < 7; ++step) {
    const int16_t width = static_cast<int16_t>(14 - step * 2);
    if (width <= 0) break;
    screen.target().fill(
        fui::makeRect(static_cast<int16_t>(cx + 5 - width), static_cast<int16_t>(cy - 13 + step * 2), width, 2), ink);
  }
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

int howToPages() { return 3; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
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
      "SWITCH TO FLAG TO MARK A MINE. SWITCH BACK TO DIG TO OPEN A CELL.",
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

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "MINESWEEPER";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);

  const fui::DeviceContext device = screen.device();

  // The tool switch IS the bottom capsule, split in two. A mode you cannot see
  // is what makes modes bad, and this game is forced into one: it needs two
  // verbs on a cell and the device gives games exactly one gesture. So the mode
  // is never off screen and never ambiguous -- the live half is filled.
  const fui::Rect capsule = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const int16_t half = static_cast<int16_t>(capsule.width / 2);
  const bool digging = model.tool == ms::Tool::Dig;

  fui::ButtonProps dig;
  dig.label = "DIG";
  dig.action = ActionPickDig;
  dig.borderEdges = fui::EdgesAll;
  if (!digging) {
    fui::StyleSet outline;
    outline.explicitlySet = true;
    outline.normal.border = fui::Paint::solid(fui::Color::Black);
    outline.normal.borderWidth = 3;
    dig.styles = outline;
    dig.text.color = fui::Color::Black;
  }
  screen.button(dig, fui::makeRect(capsule.x, capsule.y, half, capsule.height));

  fui::ButtonProps flag;
  flag.label = "FLAG";
  flag.action = ActionPickFlag;
  flag.borderEdges = fui::EdgesAll;
  if (digging) {
    fui::StyleSet outline;
    outline.explicitlySet = true;
    outline.normal.border = fui::Paint::solid(fui::Color::Black);
    outline.normal.borderWidth = 3;
    flag.styles = outline;
    flag.text.color = fui::Color::Black;
  }
  screen.button(flag, fui::makeRect(static_cast<int16_t>(capsule.x + half), capsule.y, half, capsule.height));

  // How many mines are still unaccounted for. Goes negative when the player
  // over-flags, which is the board telling them it disagrees.
  char left[16];
  std::snprintf(left, sizeof(left), "%d", ms::minesRemaining(model.game));
  fui::TextStyle count;
  count.font = toybox::kDisplayFont;
  count.align = fui::TextAlign::Right;
  const int16_t countTop = static_cast<int16_t>(boardTop() + kBoardHeight + toybox::kGutter);
  screen.target().text(
      fui::makeRect(toybox::kMargin, countTop, static_cast<int16_t>(device.width - toybox::kMargin * 2 - 40), 40), left,
      count);
  drawFlag(screen, fui::makeRect(static_cast<int16_t>(device.width - toybox::kMargin - 34), countTop, 34, 40), false);

  for (int column = 0; column < ms::kColumns; ++column) {
    for (int row = 0; row < ms::kRows; ++row) {
      const fui::Rect box = cellRect(device, column, row);
      const uint8_t cell = model.game.cell[column][row];
      const bool revealed = (cell & ms::kRevealed) != 0;

      // Unopened ground is dithered, opened ground is paper. That is the whole
      // read of the board at a glance: what is left to do.
      if (!revealed) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 1);

      if (cell & ms::kFlagged) {
        drawFlag(screen, box, false);
      } else if (revealed && (cell & ms::kMine)) {
        // The one you stepped on: filled, so it is obvious which ended it.
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
