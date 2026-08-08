#include "KnucklebonesScreens.h"

#include <cstdio>

namespace knuckleui {

namespace {

namespace kb = knucklebones;

// A cell is square and the grid is three of them. Sized so two grids and the
// die strip between them fit the portrait panel with the header on top: the
// panel is 800 tall, the header band and its rule take 112, and what is left
// divides into two 300px grids and a strip wide enough for a die and a line of
// text.
// 86 rather than 92, which was the first guess and did not fit. The vertical
// budget is 800: header and its air (100), two grids (270 each), two rows of
// column scores (24 each), and a strip tall enough to hold a die at cell size
// (96), which totals 792. At 92 it came to 812 and nothing complained -- the
// opponent's scores drew behind the header band and mine ran off the bottom
// edge, which is only visible by looking.
constexpr int16_t kCell = 86;
constexpr int16_t kCellGap = 6;
constexpr int16_t kGridSide = kCell * kb::kColumns + kCellGap * (kb::kColumns - 1);
constexpr int16_t kScoreHeight = 24;

// The die drawn in the strip between the grids, and again inside every cell.
constexpr int16_t kPipSize = 10;

int16_t gridLeft(const fui::DeviceContext& device) { return static_cast<int16_t>((device.width - kGridSide) / 2); }

constexpr int16_t kStripHeight = 96;

int16_t theirsTop() { return static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter * 2); }

// Both score rows sit on the INNER edge of their grid, beside the strip, rather
// than on the outer edges. Outside, they had nowhere to go: one collided with
// the header band and the other with the bottom of the panel.
int16_t theirsScoreTop() { return static_cast<int16_t>(theirsTop() + kGridSide + 4); }

int16_t stripTop() { return static_cast<int16_t>(theirsScoreTop() + kScoreHeight); }

int16_t yoursScoreTop() { return static_cast<int16_t>(stripTop() + kStripHeight + 4); }

int16_t yoursTop() { return static_cast<int16_t>(yoursScoreTop() + kScoreHeight); }

// One cell's rect. Row 0 is the near end for whoever owns the grid, so your own
// grid stacks upward from the bottom and the opponent's stacks downward from
// the top -- the two grids face each other across the strip, the way two
// players face each other across a table.
fui::Rect cellRect(const fui::DeviceContext& device, const int column, const int row, const bool yours) {
  const int16_t left = static_cast<int16_t>(gridLeft(device) + column * (kCell + kCellGap));
  const int16_t top = yours ? yoursTop() : theirsTop();
  const int16_t offset = yours ? static_cast<int16_t>(kGridSide - kCell - row * (kCell + kCellGap))
                               : static_cast<int16_t>(row * (kCell + kCellGap));
  return fui::makeRect(left, static_cast<int16_t>(top + offset), kCell, kCell);
}

// The pip layout of a die face, as offsets in a 3x3 lattice. Drawn rather than
// written as a numeral because a die is a picture at this size and a numeral
// would need reading -- and because the same shape appears nine times a game.
void drawFace(toybox::Screen& screen, const fui::Rect& where, const uint8_t value, const bool paper) {
  static const int8_t kLayouts[7][9] = {
      {0, 0, 0, 0, 0, 0, 0, 0, 0},  // unused: value 0 is empty
      {0, 0, 0, 0, 1, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 0, 0, 0, 1}, {1, 0, 0, 0, 1, 0, 0, 0, 1},
      {1, 0, 1, 0, 0, 0, 1, 0, 1}, {1, 0, 1, 0, 1, 0, 1, 0, 1}, {1, 0, 1, 1, 0, 1, 1, 0, 1},
  };
  if (value < 1 || value > kb::kFaces) return;

  const fui::Paint ink = fui::Paint::solid(paper ? fui::Color::White : fui::Color::Black);
  const int16_t step = static_cast<int16_t>(where.width / 4);
  for (int index = 0; index < 9; ++index) {
    if (kLayouts[value][index] == 0) continue;
    const int16_t cx = static_cast<int16_t>(where.x + step * (index % 3 + 1));
    const int16_t cy = static_cast<int16_t>(where.y + step * (index / 3 + 1));
    screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - kPipSize / 2), static_cast<int16_t>(cy - kPipSize / 2),
                                       kPipSize, kPipSize),
                         ink);
  }
}

void drawGrid(toybox::Screen& screen, const kb::Grid& grid, const bool yours) {
  const fui::DeviceContext device = screen.device();
  for (int column = 0; column < kb::kColumns; ++column) {
    for (int row = 0; row < kb::kRows; ++row) {
      const fui::Rect cell = cellRect(device, column, row, yours);
      const uint8_t value = grid.cell[column][row];
      // An empty slot is an outline, a filled one is an outline with a face in
      // it. The slot is always drawn: a grid that only shows what has been
      // placed gives no sense of how much room is left, which is the whole
      // tension of the endgame.
      screen.target().stroke(cell, fui::Paint::solid(fui::Color::Black), value == kb::kEmpty ? 2 : 3);
      drawFace(screen, cell, value, false);
    }

    // The column's score, under your grid and over theirs, so each sits on the
    // outside edge and the two grids stay adjacent to the strip between them.
    char label[8];
    std::snprintf(label, sizeof(label), "%d", kb::columnScore(grid, column));
    fui::TextStyle style;
    style.font = toybox::kSmallFont;
    style.align = fui::TextAlign::Center;
    const int16_t left = static_cast<int16_t>(gridLeft(device) + column * (kCell + kCellGap));
    const int16_t y = yours ? yoursScoreTop() : theirsScoreTop();
    screen.target().text(fui::makeRect(left, y, kCell, kScoreHeight), label, style);
  }
}

}  // namespace

fui::Rect columnRect(const fui::DeviceContext& device, const int column, const bool yours) {
  // The whole column, not one cell: a player aims at a column, and the rules
  // take a column. Derived from the same cellRect the pixels came from, so the
  // target cannot drift from the drawing.
  const fui::Rect top = cellRect(device, column, kb::kRows - 1, yours);
  const fui::Rect bottom = cellRect(device, column, 0, yours);
  const int16_t y = top.y < bottom.y ? top.y : bottom.y;
  return fui::makeRect(top.x, y, kCell, kGridSide);
}

int howToPages() { return 3; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  fui::HeaderProps header;
  header.title = "KNUCKLEBONES";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  // Says who when somebody is there. A row that reads PLAY NEARBY when the
  // room is empty promises something the device cannot deliver yet.
  rows[static_cast<int>(MenuRow::PlayNearby)].label = "PLAY NEARBY";
  rows[static_cast<int>(MenuRow::PlayNearby)].subtitle = model.nearbyName;
  rows[static_cast<int>(MenuRow::PlayNearby)].actionValue = static_cast<int16_t>(MenuRow::PlayNearby);
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  rows[static_cast<int>(MenuRow::HowTo)].actionValue = static_cast<int16_t>(MenuRow::HowTo);

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionMenuRow;
  screen.list(list);
}

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // Three pages, each one sentence and one picture. The rules fit on a napkin,
  // so the how-to should not be a wall: a player who wanted to read would not
  // have opened a dice game.
  static const char* const kLines[] = {
      "ROLL A DIE. DROP IT IN ONE OF YOUR THREE COLUMNS.",
      // Digits, not words. Spelled out, this line ran past three lines and was
      // cut mid-word at "THIRTY SIX, N" -- losing the contrast the whole rule
      // is made of, and with no ellipsis, because Jersey is subset to ASCII.
      "MATCHING DICE MULTIPLY. THREE 4s SCORE 36, NOT 12.",
      "PLACING A VALUE DESTROYS EVERY COPY OF IT IN THEIR MATCHING COLUMN.",
  };
  const int page = model.page < 0 ? 0 : (model.page >= howToPages() ? howToPages() - 1 : model.page);

  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  // Three lines of room: the sentences are short but the panel is narrow, and
  // a single line would truncate the longest of them.
  body.maxLines = 3;
  const fui::Rect area = screen.body();
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 40), area.width, 160), kLines[page], body);

  // The example the sentence is talking about, drawn in the game's own
  // material rather than described. Page one is an empty column, page two the
  // three fours it names, page three the destruction it names.
  const fui::DeviceContext device = screen.device();
  const int16_t exampleTop = static_cast<int16_t>(area.y + 220);
  for (int row = 0; row < kb::kRows; ++row) {
    const int16_t left = static_cast<int16_t>((device.width - kCell) / 2);
    const fui::Rect cell = fui::makeRect(
        left, static_cast<int16_t>(exampleTop + (kb::kRows - 1 - row) * (kCell + kCellGap)), kCell, kCell);
    uint8_t value = kb::kEmpty;
    if (page == 1) value = 4;
    if (page == 2 && row == 0) value = 5;
    screen.target().stroke(cell, fui::Paint::solid(fui::Color::Black), value == kb::kEmpty ? 2 : 3);
    drawFace(screen, cell, value, false);
  }

  fui::ButtonProps next;
  next.label = page + 1 < howToPages() ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kRowHeight, toybox::kGutter));
}

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  // Always the game, never the opponent. 57% of this device's possible names
  // are 16 characters or longer and truncate invisibly in this band -- Jersey
  // has no ellipsis glyph to truncate with. Chess does the same thing for the
  // same reason: the band is the device speaking, and who you are playing
  // belongs beside their face, not in the chrome.
  header.title = "KNUCKLEBONES";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);

  const fui::DeviceContext device = screen.device();
  drawGrid(screen, model.theirs, false);
  drawGrid(screen, model.yours, true);

  // The strip between the grids: the die you are about to place, and a word
  // about whose turn it is. This is the only part of the board that changes
  // between turns, so it is the part that has to be unambiguous.
  const fui::Rect strip = fui::makeRect(toybox::kMargin, stripTop(),
                                        static_cast<int16_t>(device.width - 2 * toybox::kMargin), kStripHeight);
  if (model.die != kb::kEmpty) {
    const fui::Rect dieRect = fui::makeRect(static_cast<int16_t>(strip.x + (strip.width - kCell) / 2),
                                            static_cast<int16_t>(strip.y + (kStripHeight - kCell) / 2), kCell, kCell);
    // Filled when it is yours to place, outlined when it is theirs: the die is
    // the one thing on this screen that means "act now", so it carries the
    // turn rather than a separate label having to.
    if (model.yourTurn) {
      screen.target().fill(dieRect, fui::Paint::solid(fui::Color::Black));
      drawFace(screen, dieRect, model.die, true);
    } else {
      screen.target().stroke(dieRect, fui::Paint::solid(fui::Color::Black), 3);
      drawFace(screen, dieRect, model.die, false);
    }
  }

  fui::TextStyle turn;
  turn.font = toybox::kSmallFont;
  turn.align = fui::TextAlign::Left;
  const char* status = model.waiting ? "WAITING" : (model.yourTurn ? "YOUR ROLL" : "THEIR ROLL");
  screen.target().text(fui::makeRect(strip.x, static_cast<int16_t>(strip.y + kStripHeight / 2 - 10), 140, 20), status,
                       turn);

  // Totals at the far end of the strip, one per seat, so the score you are
  // chasing is next to the score you have.
  char totals[24];
  std::snprintf(totals, sizeof(totals), "%d - %d", kb::score(model.yours), kb::score(model.theirs));
  fui::TextStyle scoreStyle;
  scoreStyle.font = toybox::kSmallFont;
  scoreStyle.align = fui::TextAlign::Right;
  screen.target().text(fui::makeRect(static_cast<int16_t>(strip.right() - 140),
                                     static_cast<int16_t>(strip.y + kStripHeight / 2 - 10), 140, 20),
                       totals, scoreStyle);

  // The columns of your own grid, as targets, and only while the placement is
  // yours to make. A target that is live on the opponent's turn is a tap that
  // does nothing, which on a slow panel reads as the device having missed it.
  if (!model.yourTurn || model.die == kb::kEmpty) return;
  fui::StyleSet invisible;
  invisible.explicitlySet = true;
  for (int column = 0; column < kb::kColumns; ++column) {
    if (kb::columnCount(model.yours, column) >= kb::kRows) continue;
    fui::ButtonProps target;
    target.action = ActionColumn;
    target.value = static_cast<int16_t>(column);
    target.styles = invisible;
    target.minTouchSize = 0;
    screen.button(target, columnRect(device, column, true));
  }
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const bool won = model.yourScore > model.theirScore;
  const bool drew = model.yourScore == model.theirScore;

  fui::HeaderProps header;
  header.title = drew ? "A DRAW" : (won ? "YOU WIN" : "THEY WIN");
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  char line[48];
  std::snprintf(line, sizeof(line), "%d - %d", model.yourScore, model.theirScore);
  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Center;
  const fui::Rect area = screen.body();
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 60), area.width, 80), line, big);

  if (model.opponentName != nullptr) {
    fui::TextStyle who;
    who.font = toybox::kSmallFont;
    who.align = fui::TextAlign::Center;
    screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 150), area.width, 24), model.opponentName,
                         who);
  }

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  screen.button(done, screen.takeBottom(toybox::kRowHeight, toybox::kGutter));

  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  screen.button(again, screen.takeBottom(toybox::kRowHeight, toybox::kGutter));
}

}  // namespace knuckleui
