#include "KnucklebonesScreens.h"

#include <cstdio>

#include "../link/LinkScreens.h"

namespace knuckleui {

namespace {

namespace kb = knucklebones;

// A cell is square and the grid is three of them. Sized so two grids and the
// die strip between them fit the portrait panel with the header on top: the
// panel is 800 tall, the header band and its rule take 112, and what is left
// divides into two 300px grids and a strip wide enough for a die and a line of
// text.
// 73, down from 86, and the 80px that buys is spent on the bottom capsule.
//
// A cold design review measured this board at 7% ink against chess's 17.8% and
// murdle's 17.7% -- half the density of anything else in the fork -- and named
// the missing capsule as the single strongest family resemblance in the three
// screens it compared against. The first layout had given its entire vertical
// budget to cells: 792 of 800px, with nowhere to put one.
//
// The budget now: header and air (100), grid (231), column scores (22), the
// strip with the die (83), column scores (22), grid (231), then the capsule and
// its margins. 777 of 800.
constexpr int16_t kCell = 73;
constexpr int16_t kCellGap = 6;
constexpr int16_t kGridSide = kCell * kb::kColumns + kCellGap * (kb::kColumns - 1);
constexpr int16_t kScoreHeight = 22;

// The die drawn inside every cell.
constexpr int16_t kPipSize = 9;

// Anchored left rather than centred. Centred, the grid left 44% of the panel
// permanently empty down its full height while every number on the screen was
// squeezed to the smallest type the fork uses. The gutter now carries one large
// total per grid, beside the grid it belongs to -- which also kills the
// ordering ambiguity that a single "9 - 12" line could not resolve, since
// left-to-right reads as top-to-bottom to everyone.
constexpr int16_t kGridLeft = toybox::kMargin;

int16_t gridLeft(const fui::DeviceContext&) { return kGridLeft; }

constexpr int16_t kStripHeight = 83;

int16_t theirsTop() { return static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter * 2); }

int16_t theirsScoreTop() { return static_cast<int16_t>(theirsTop() + kGridSide + 2); }

int16_t stripTop() { return static_cast<int16_t>(theirsScoreTop() + kScoreHeight); }

int16_t yoursTop() { return static_cast<int16_t>(stripTop() + kStripHeight + toybox::kGutter); }

// Below your grid, not above it. Above, it sat in the same band as the corner
// brackets that mark a playable column and read as a number inside the bracket.
// Both score rows are still on the side of their grid that faces the middle for
// the opponent and the edge for you, which is the only asymmetry that matters:
// each number is adjacent to the grid it describes.
int16_t yoursScoreTop() { return static_cast<int16_t>(yoursTop() + kGridSide + 2); }

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

  // The opponent's ground is dithered, yours is paper. Before this the two
  // halves were the same picture twice and nothing said which you owned; the
  // only cue was remembering that yours is nearer. One side marked, not both,
  // so the board still reads as one object.
  if (!yours) {
    const fui::Rect ground = fui::makeRect(static_cast<int16_t>(kGridLeft - 4), static_cast<int16_t>(theirsTop() - 4),
                                           static_cast<int16_t>(kGridSide + 8), static_cast<int16_t>(kGridSide + 8));
    screen.target().fill(ground, fui::Paint::dither(fui::Color::LightGray));
  }

  for (int column = 0; column < kb::kColumns; ++column) {
    for (int row = 0; row < kb::kRows; ++row) {
      const fui::Rect cell = cellRect(device, column, row, yours);
      const uint8_t value = grid.cell[column][row];
      // An empty slot is drawn too: a grid that only shows what has been placed
      // gives no sense of how much room is left, which is the whole tension of
      // the endgame. Paper-filled first, so an empty slot reads as a hole in
      // the opponent's dithered ground rather than as more ground.
      screen.target().fill(cell, fui::Paint::solid(fui::Color::White));
      screen.target().stroke(cell, fui::Paint::solid(fui::Color::Black), 2);
      drawFace(screen, cell, value, false);
    }

    // Zero means "empty", which three empty slots already say. Nine zeros on a
    // fresh board were 100% of its numeric content and identical on every
    // device in every match -- live data that fails the fork's own test for
    // ornament.
    const int columnTotal = kb::columnScore(grid, column);
    if (columnTotal == 0) continue;
    char label[8];
    std::snprintf(label, sizeof(label), "%d", columnTotal);
    fui::TextStyle style;
    style.font = toybox::kSmallFont;
    style.align = fui::TextAlign::Center;
    const int16_t left = static_cast<int16_t>(kGridLeft + column * (kCell + kCellGap));
    screen.target().text(fui::makeRect(left, yours ? yoursScoreTop() : theirsScoreTop(), kCell, kScoreHeight), label,
                         style);
  }

  // The grand total, in the gutter, beside the grid it belongs to and at a size
  // that can be read across a table. Ten numbers at the smallest cut the fork
  // owns was the hierarchy exactly inverted: the constants loud, the variables
  // whispered.
  char total[8];
  std::snprintf(total, sizeof(total), "%d", kb::score(grid));
  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Center;
  const int16_t gutterX = static_cast<int16_t>(kGridLeft + kGridSide + toybox::kGutter);
  const int16_t gutterW = static_cast<int16_t>(device.width - toybox::kMargin - gutterX);
  const int16_t top = yours ? yoursTop() : theirsTop();
  screen.target().text(fui::makeRect(gutterX, static_cast<int16_t>(top + (kGridSide - 48) / 2), gutterW, 48), total,
                       big);
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
  const fui::Rect band = screen.body();
  screen.list(list);

  // The same mark chess, battleship and jaipur put on their own PLAY NEARBY
  // row. A symbol that means "connects to another device" only works if it is
  // on every one of them, and this was the game that broke "every".
  toybox::iconAtRowRight(screen, band, static_cast<int>(MenuRow::PlayNearby), linkui::nearbyMark(),
                         model.selected == static_cast<int>(MenuRow::PlayNearby));
}

// Defined below, beside the board that is its main caller. Declared here
// because the how-to's diagrams use the same mark to say "your columns", and
// teaching a signal with one shape and then using another would be worse than
// not teaching it.
void bracket(toybox::Screen& screen, const fui::Rect& box);

// A grid drawn at an arbitrary size, for the how-to's diagrams. The board's own
// drawGrid is tied to the board's layout; this one takes a rect, so a page can
// show the real shape of the game at whatever size the page has room for.
void miniGrid(toybox::Screen& screen, const int16_t x, const int16_t y, const int16_t cell,
              const uint8_t cells[kb::kColumns][kb::kRows], const bool bracketed) {
  constexpr int16_t kMiniGap = 4;
  for (int column = 0; column < kb::kColumns; ++column) {
    for (int row = 0; row < kb::kRows; ++row) {
      // Row 0 at the bottom, as on the real board, so the picture teaches the
      // direction dice actually stack.
      const fui::Rect box =
          fui::makeRect(static_cast<int16_t>(x + column * (cell + kMiniGap)),
                        static_cast<int16_t>(y + (kb::kRows - 1 - row) * (cell + kMiniGap)), cell, cell);
      screen.target().fill(box, fui::Paint::solid(fui::Color::White));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
      drawFace(screen, box, cells[column][row], false);
    }
    if (!bracketed) continue;
    const fui::Rect whole = fui::makeRect(static_cast<int16_t>(x + column * (cell + kMiniGap)), y, cell,
                                          static_cast<int16_t>(cell * kb::kRows + kMiniGap * (kb::kRows - 1)));
    bracket(screen, whole);
  }
}

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= howToPages() ? howToPages() - 1 : model.page);

  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // Each page is one sentence and one picture of the real game. The pictures
  // were the whole problem before: page one said "three columns" and drew one,
  // and page three described destroying their dice while showing a die in your
  // own grid. A diagram that contradicts its caption is worse than none.
  static const char* const kLines[] = {
      "TWO BOARDS FACING. ROLL A DIE, DROP IT IN ONE OF YOUR THREE COLUMNS.",
      "MATCHING DICE MULTIPLY. THREE 4s SCORE 36, NOT 12.",
      "YOUR 5 DESTROYS EVERY 5 IN THEIR FACING COLUMN.",
  };

  // Taken before anything else is drawn. The first version of this rewrite put
  // it at the end, after three page-specific branches that each returned early,
  // so two pages of three had no way forward at all. The test caught it; the
  // fix is to make the branch unable to skip it rather than to remember.
  fui::ButtonProps next;
  next.label = page + 1 < howToPages() ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();

  // Where you are in the sequence. Drawn in the body rather than as the
  // header's rightLabel, which IS drawn but comes out black on the black band
  // and is invisible -- the same class as this fork's known-failing
  // paperOnTheBand test. A host test asserting "the label was drawn" passes
  // either way, which is exactly why this one was caught by looking.
  char progress[8];
  std::snprintf(progress, sizeof(progress), "%d/%d", page + 1, howToPages());
  fui::TextStyle counter;
  counter.font = toybox::kSmallFont;
  counter.align = fui::TextAlign::Right;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 20), progress, counter);

  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 3;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 20), area.width, 130), kLines[page], body);

  const fui::DeviceContext device = screen.device();
  constexpr uint8_t kNone = kb::kEmpty;
  const int16_t diagramTop = static_cast<int16_t>(area.y + 170);

  if (page == 0) {
    // The shape of the whole game: two grids facing, yours bracketed, one die
    // in hand between them. Layout, ownership and action in one picture.
    constexpr int16_t kMini = 52;
    const int16_t width = kMini * kb::kColumns + 4 * (kb::kColumns - 1);
    const int16_t left = static_cast<int16_t>((device.width - width) / 2);
    const uint8_t theirs[kb::kColumns][kb::kRows] = {{3, kNone, kNone}, {kNone, kNone, kNone}, {6, 2, kNone}};
    const uint8_t yours[kb::kColumns][kb::kRows] = {{kNone, kNone, kNone}, {5, kNone, kNone}, {kNone, kNone, kNone}};
    miniGrid(screen, left, diagramTop, kMini, theirs, false);
    const fui::Rect die = fui::makeRect(static_cast<int16_t>((device.width - kMini) / 2),
                                        static_cast<int16_t>(diagramTop + 3 * kMini + 8 + 14), kMini, kMini);
    screen.target().stroke(die, fui::Paint::solid(fui::Color::Black), 4);
    drawFace(screen, die, 4, false);
    miniGrid(screen, left, static_cast<int16_t>(diagramTop + 3 * kMini + 8 + kMini + 28), kMini, yours, true);
    return;
  }

  if (page == 1) {
    // The multiplier, shown as the contrast the sentence names: three 4s in one
    // column against the same three spread out.
    constexpr int16_t kMini = 58;
    const uint8_t stacked[kb::kColumns][kb::kRows] = {{4, 4, 4}, {kNone, kNone, kNone}, {kNone, kNone, kNone}};
    const uint8_t spread[kb::kColumns][kb::kRows] = {{4, kNone, kNone}, {4, kNone, kNone}, {4, kNone, kNone}};
    const int16_t width = kMini * kb::kColumns + 4 * (kb::kColumns - 1);
    const int16_t left = static_cast<int16_t>((device.width - width) / 2);
    fui::TextStyle score;
    score.font = toybox::kDisplayFont;
    score.align = fui::TextAlign::Center;
    miniGrid(screen, left, diagramTop, kMini, stacked, false);
    screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(diagramTop + 3 * kMini + 14), area.width, 44), "36",
                         score);
    const int16_t secondTop = static_cast<int16_t>(diagramTop + 3 * kMini + 66);
    miniGrid(screen, left, secondTop, kMini, spread, false);
    screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(secondTop + 3 * kMini + 14), area.width, 44), "12",
                         score);
    return;
  }

  // Destruction, drawn as the two facing columns it actually involves: their
  // pair of fives above, your five below, in the same column.
  constexpr int16_t kMini = 58;
  const uint8_t theirs[kb::kColumns][kb::kRows] = {{5, 5, kNone}, {kNone, kNone, kNone}, {kNone, kNone, kNone}};
  const uint8_t yours[kb::kColumns][kb::kRows] = {{5, kNone, kNone}, {kNone, kNone, kNone}, {kNone, kNone, kNone}};
  const int16_t width = kMini * kb::kColumns + 4 * (kb::kColumns - 1);
  const int16_t left = static_cast<int16_t>((device.width - width) / 2);
  miniGrid(screen, left, diagramTop, kMini, theirs, false);
  fui::TextStyle arrow;
  arrow.font = toybox::kDisplayFont;
  arrow.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(diagramTop + 3 * kMini + 10), area.width, 44), "^",
                       arrow);
  miniGrid(screen, left, static_cast<int16_t>(diagramTop + 3 * kMini + 60), kMini, yours, true);
}

// Four corner brackets around a column, which is how this fork already says
// "here". Drawn rather than filled: the thing being pointed at is where the tap
// goes, and a filled column would both hide the dice in it and spend the ink
// budget on the largest repainting area on the screen.
void bracket(toybox::Screen& screen, const fui::Rect& box) {
  constexpr int16_t kArm = 16;
  constexpr int16_t kWeight = 4;
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const int16_t r = static_cast<int16_t>(box.right() - kArm);
  const int16_t b = static_cast<int16_t>(box.bottom() - kWeight);
  screen.target().fill(fui::makeRect(box.x, box.y, kArm, kWeight), ink);
  screen.target().fill(fui::makeRect(r, box.y, kArm, kWeight), ink);
  screen.target().fill(fui::makeRect(box.x, b, kArm, kWeight), ink);
  screen.target().fill(fui::makeRect(r, b, kArm, kWeight), ink);
  screen.target().fill(fui::makeRect(box.x, box.y, kWeight, kArm), ink);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(box.right() - kWeight), box.y, kWeight, kArm), ink);
  screen.target().fill(fui::makeRect(box.x, static_cast<int16_t>(box.bottom() - kArm), kWeight, kArm), ink);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(box.right() - kWeight),
                                     static_cast<int16_t>(box.bottom() - kArm), kWeight, kArm),
                       ink);
}

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  // Always the game, never the opponent. 57% of this device's possible names
  // are 16 characters or longer and truncate invisibly in this band -- Jersey
  // has no ellipsis glyph to truncate with. Chess does the same thing for the
  // same reason: the band is the device speaking, and who you are playing
  // belongs beside their face in the capsule, not in the chrome.
  header.title = "KNUCKLEBONES";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);

  const fui::DeviceContext device = screen.device();

  // Taken before anything else is drawn, so the board can never grow into it.
  // This is the shape every other game in the fork ends on -- chess's BLACK TO
  // MOVE, jaipur's YOUR MOVE, murdle's ACCUSE -- and its absence was what made
  // this screen read as a different app.
  fui::ButtonProps status;
  status.label = model.waiting ? "THEIR ROLL" : (model.yourTurn ? "YOUR ROLL" : "THEIR ROLL");
  status.action = fui::NO_ACTION;
  status.borderEdges = fui::EdgesNone;
  const fui::Rect capsule = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  // Their face when there is a person at the other end. A name says somebody is
  // there; a face says who, and it is the same mark every link game draws.
  screen.button(
      status, model.opponentName != nullptr ? linkui::withOpponentFace(screen, capsule, model.opponentName) : capsule);

  drawGrid(screen, model.theirs, false);
  drawGrid(screen, model.yours, true);

  // The die, outlined rather than filled. Filled, it was 7396 solid black
  // pixels repainting every single turn -- verbatim the case the ink-budget
  // rule forbids, and the largest repainting black area in the fork. It also
  // collided with an established meaning: in chess, filled versus outlined
  // already says WHOSE piece, not whose turn, so a filled die read as "theirs"
  // to anyone arriving from there. The capsule carries the turn now, which is
  // what a capsule is for.
  if (model.die != kb::kEmpty) {
    // Centred on the panel, not on the grid. It belongs to neither player -- it
    // is the one object both are looking at -- and hung off the left with the
    // grid it read as part of that grid's furniture.
    const fui::Rect dieRect =
        fui::makeRect(static_cast<int16_t>((device.width - kCell) / 2),
                      static_cast<int16_t>(stripTop() + (kStripHeight - kCell) / 2), kCell, kCell);
    screen.target().fill(dieRect, fui::Paint::solid(fui::Color::White));
    screen.target().stroke(dieRect, fui::Paint::solid(fui::Color::Black), 4);
    drawFace(screen, dieRect, model.die, false);
  }

  // The columns of your own grid, as targets, and only while the placement is
  // yours to make. A target that is live on the opponent's turn is a tap that
  // does nothing, which on a slow panel reads as the device having missed it.
  if (!model.yourTurn || model.die == kb::kEmpty) return;
  fui::StyleSet invisible;
  invisible.explicitlySet = true;
  for (int column = 0; column < kb::kColumns; ++column) {
    if (kb::columnCount(model.yours, column) >= kb::kRows) continue;
    const fui::Rect where = columnRect(device, column, true);
    // Marked as well as tappable. Nothing else on the board says where the die
    // can go, and "the three columns of the near grid" is a rule the player has
    // to be taught rather than one they can see.
    bracket(screen, where);
    fui::ButtonProps target;
    target.action = ActionColumn;
    target.value = static_cast<int16_t>(column);
    target.styles = invisible;
    target.minTouchSize = 0;
    screen.button(target, where);
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
