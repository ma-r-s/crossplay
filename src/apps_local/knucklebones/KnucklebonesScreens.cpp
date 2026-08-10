#include "KnucklebonesScreens.h"

#include <cstdio>
#include <cstdlib>

#include "../link/LinkScreens.h"

namespace knuckleui {

namespace {

namespace kb = knucklebones;

// --- TEMP ART PASS ----------------------------------------------------------
// Runtime layout switches so one build renders every candidate for Mario to
// pick from (ART_MENU / ART_HOWTO / ART_BOARD = 1 or 2, 0 = shipping layout).
// The losing layouts and this switch are deleted together in the commit that
// keeps the winner.
int artVariant(const char* name) {
#if defined(SIMULATOR)
  const char* value = std::getenv(name);
  return value == nullptr ? 0 : std::atoi(value);
#else
  (void)name;
  return 0;
#endif
}
int menuVariant() {
  static const int variant = artVariant("ART_MENU");
  return variant;
}
int howToVariant() {
  static const int variant = artVariant("ART_HOWTO");
  return variant;
}
int boardVariant() {
  static const int variant = artVariant("ART_BOARD");
  return variant;
}

// The header band with the offset rule under it, as jaipur and the dungeon
// wear it. TEMP with the variants; promoted to Toybox if a banded layout wins.
void artChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
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
  screen.header(header);
  const fui::Rect band = screen.device().screen();
  screen.target().fill(fui::makeRect(0, toybox::kHeaderHeight + 4, band.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

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
constexpr int16_t kCellGap = 6;
constexpr int16_t kScoreHeight = 22;

// The die drawn inside every cell.
constexpr int16_t kPipSize = 9;

// TEMP ART PASS, round 2. Mario rejected every board candidate in round one
// for the same two sins: the whole composition hugged the left edge, and the
// column scores stayed in the smallest cut the fork owns. So these candidates
// change the board's whole shape rather than its trimmings. The matched-dice
// inversion he did like rides along in all three.
//
//  0  shipping: square cells, grids left, totals alone in the right gutter
//  1  MAT: cells widened until the grids fill the panel; mirrored score
//     bands on each player's own side; the strip between the grids becomes a
//     scoreboard, THEM - die - YOU
//  2  ACROSS: the grids side by side like two cards on a table, named totals
//     above each, the die big in the space that frees below
//  3  CENTRED: the shipping rhythm on the panel's own axis, totals named in
//     the strip
int boardLayout() { return boardVariant(); }

int16_t bCellW() {
  switch (boardLayout()) {
    case 1:
      return 140;
    case 2:
      return 70;
    default:
      return 73;
  }
}
int16_t bCellH() {
  switch (boardLayout()) {
    case 1:
      return 74;
    case 2:
      return 70;
    default:
      return 73;
  }
}
int16_t bGridW() { return static_cast<int16_t>(bCellW() * kb::kColumns + kCellGap * (kb::kColumns - 1)); }
int16_t bGridH() { return static_cast<int16_t>(bCellH() * kb::kRows + kCellGap * (kb::kRows - 1)); }

int16_t gridLeftOf(const fui::DeviceContext& device, const bool yours) {
  switch (boardLayout()) {
    case 1:
    case 3:
      return static_cast<int16_t>((device.width - bGridW()) / 2);
    case 2:
      // YOU left, THEM right, the order the Yahtzee card taught.
      return yours ? 12 : static_cast<int16_t>(12 + bGridW() + 12);
    default:
      // Shipping: anchored left so the right gutter can carry the totals.
      return toybox::kMargin;
  }
}

int16_t gridTopOf(const bool yours) {
  switch (boardLayout()) {
    case 1:
      return yours ? 448 : 126;
    case 2:
      return 190;
    case 3:
      return yours ? 452 : 100;
    default:
      return yours ? 450 : 100;
  }
}

int16_t scoreTopOf(const bool yours) {
  switch (boardLayout()) {
    case 1:
      // Mirrored: their tally on their far side, yours on your near side.
      return yours ? 688 : 92;
    case 2:
      return 420;
    case 3:
      return yours ? 687 : 335;
    default:
      return yours ? 683 : 333;
  }
}

int16_t scoreBandH() { return boardLayout() == 0 ? kScoreHeight : 28; }

// One cell's rect. Row 0 is the near end for whoever owns the grid, so your
// own grid stacks upward from the bottom and the opponent's stacks downward
// from the top -- facing each other across the strip the way two players face
// each other across a table. Side by side (layout 2) has no facing, so both
// grids stack the way gravity fills them.
fui::Rect cellRect(const fui::DeviceContext& device, const int column, const int row, const bool yours) {
  const int16_t left = static_cast<int16_t>(gridLeftOf(device, yours) + column * (bCellW() + kCellGap));
  const int16_t top = gridTopOf(yours);
  const bool upward = yours || boardLayout() == 2;
  const int16_t offset = upward ? static_cast<int16_t>(bGridH() - bCellH() - row * (bCellH() + kCellGap))
                                : static_cast<int16_t>(row * (bCellH() + kCellGap));
  return fui::makeRect(left, static_cast<int16_t>(top + offset), bCellW(), bCellH());
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
  // Pips lay out on the shorter side and centre on the longer one, so a die in
  // a widened cell is still a die rather than a stretched one.
  const int16_t side = where.width < where.height ? where.width : where.height;
  const int16_t originX = static_cast<int16_t>(where.x + (where.width - side) / 2);
  const int16_t originY = static_cast<int16_t>(where.y + (where.height - side) / 2);
  const int16_t step = static_cast<int16_t>(side / 4);
  for (int index = 0; index < 9; ++index) {
    if (kLayouts[value][index] == 0) continue;
    const int16_t cx = static_cast<int16_t>(originX + step * (index % 3 + 1));
    const int16_t cy = static_cast<int16_t>(originY + step * (index / 3 + 1));
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
    const fui::Rect ground =
        fui::makeRect(static_cast<int16_t>(gridLeftOf(device, false) - 4), static_cast<int16_t>(gridTopOf(false) - 4),
                      static_cast<int16_t>(bGridW() + 8), static_cast<int16_t>(bGridH() + 8));
    screen.target().fill(ground, fui::Paint::dither(fui::Color::LightGray));
  }

  for (int column = 0; column < kb::kColumns; ++column) {
    for (int row = 0; row < kb::kRows; ++row) {
      const fui::Rect cell = cellRect(device, column, row, yours);
      const uint8_t value = grid.cell[column][row];
      // TEMP ART PASS, board candidate 2: dice that are multiplying draw
      // inverted, so the game's one mechanic is visible on the board rather
      // than only in the column total. The cost is solid black in the play
      // surface, which the ink-budget rule frowns at; that trade is exactly
      // what the render is for.
      bool matched = false;
      if (boardLayout() != 0 && value != kb::kEmpty) {
        int same = 0;
        for (int r = 0; r < kb::kRows; ++r) {
          if (grid.cell[column][r] == value) ++same;
        }
        matched = same >= 2;
      }
      // An empty slot is drawn too: a grid that only shows what has been placed
      // gives no sense of how much room is left, which is the whole tension of
      // the endgame. Paper-filled first, so an empty slot reads as a hole in
      // the opponent's dithered ground rather than as more ground.
      if (matched) {
        screen.target().fill(cell, fui::Paint::solid(fui::Color::Black));
        drawFace(screen, cell, value, true);
      } else {
        screen.target().fill(cell, fui::Paint::solid(fui::Color::White));
        screen.target().stroke(cell, fui::Paint::solid(fui::Color::Black), 2);
        drawFace(screen, cell, value, false);
      }
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
    // The body cut off layout 0: "the numbers are so little" was half of what
    // sank round one, and a column score is a number the game is played for.
    style.font = boardLayout() == 0 ? toybox::kSmallFont : toybox::kUiFont;
    style.align = fui::TextAlign::Center;
    const int16_t left = static_cast<int16_t>(gridLeftOf(device, yours) + column * (bCellW() + kCellGap));
    screen.target().text(fui::makeRect(left, scoreTopOf(yours), bCellW(), scoreBandH()), label, style);
  }

  if (boardLayout() != 0) return;  // candidates draw named totals in buildBoard

  // The grand total, in the gutter, beside the grid it belongs to and at a size
  // that can be read across a table. Ten numbers at the smallest cut the fork
  // owns was the hierarchy exactly inverted: the constants loud, the variables
  // whispered.
  char total[8];
  std::snprintf(total, sizeof(total), "%d", kb::score(grid));
  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Center;
  const int16_t gutterX = static_cast<int16_t>(gridLeftOf(device, yours) + bGridW() + toybox::kGutter);
  const int16_t gutterW = static_cast<int16_t>(device.width - toybox::kMargin - gutterX);
  const int16_t top = gridTopOf(yours);
  screen.target().text(fui::makeRect(gutterX, static_cast<int16_t>(top + (bGridH() - 48) / 2), gutterW, 48), total,
                       big);
}

}  // namespace

// Both are defined further down, beside the board that is their main caller.
// Declared here because the menu's ornament and the how-to's diagrams draw the
// same grid and the same "your columns" mark that the board does: a signal
// taught with one shape and used with another would be worse than none.
void bracket(toybox::Screen& screen, const fui::Rect& box);
void miniGrid(toybox::Screen& screen, int16_t x, int16_t y, int16_t cell,
              const uint8_t cells[knucklebones::kColumns][knucklebones::kRows], bool bracketed);

namespace {

// TEMP ART PASS, menu candidate 1: the front-door band order the design
// language documents and jaipur ships. Record line, rule, the last match as
// the ornament in the middle, doors anchored to the bottom with PLAY loudest.
void buildMenuBands(toybox::Screen& screen, const MenuModel& model) {
  artChrome(screen, "KNUCKLEBONES");

  char record[48];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON", model.wins + model.losses + model.draws, model.wins);
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
  rows[static_cast<int>(MenuRow::PlayNearby)].label = "PLAY NEARBY";
  rows[static_cast<int>(MenuRow::PlayNearby)].subtitle = model.nearbyName;
  rows[static_cast<int>(MenuRow::PlayNearby)].actionValue = static_cast<int16_t>(MenuRow::PlayNearby);
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
  toybox::iconAtRowRight(screen, listBand, static_cast<int>(MenuRow::PlayNearby), 0, linkui::nearbyMark(),
                         selected == static_cast<int>(MenuRow::PlayNearby));

  if (!model.hasHistory) return;

  // The last match, stacked the way the board stacks it, with the running
  // tally between the two grids where the die normally sits.
  const fui::DeviceContext device = screen.device();
  constexpr int16_t kMini = 44;
  constexpr int16_t kMiniGap = 4;
  const int16_t gridH = static_cast<int16_t>(3 * kMini + 2 * kMiniGap);
  const int16_t stackH = static_cast<int16_t>(gridH * 2 + 12 + 24 + 12);
  const int16_t areaTop = static_cast<int16_t>(line.bottom() + 6 + toybox::kRule);
  const int16_t room = static_cast<int16_t>(listBand.y - areaTop);
  const int16_t top = static_cast<int16_t>(areaTop + (room > stackH ? (room - stackH) / 2 : 0));
  const int16_t width = static_cast<int16_t>(kMini * kb::kColumns + kMiniGap * (kb::kColumns - 1));
  const int16_t left = static_cast<int16_t>((device.width - width) / 2);

  miniGrid(screen, left, top, kMini, model.lastTheirs.cell, false);
  char tally[32];
  std::snprintf(tally, sizeof(tally), "%d W   %d L   %d D", model.wins, model.losses, model.draws);
  fui::TextStyle mid;
  mid.font = toybox::kTileFont;
  mid.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(content.x, static_cast<int16_t>(top + gridH + 12), content.width, 24), tally, mid);
  miniGrid(screen, left, static_cast<int16_t>(top + gridH + 12 + 24 + 12), kMini, model.lastYours.cell, false);
}

// TEMP ART PASS, menu candidate 2: the board's own composition echoed at menu
// scale. The last match sits where the game sits, grids left with their totals
// loud in the gutter; the doors are the dungeon's bar, one solid PLAY and the
// lesser pair sharing a row.
void buildMenuScoreboard(toybox::Screen& screen, const MenuModel& model) {
  artChrome(screen, "KNUCKLEBONES");

  fui::ButtonProps play;
  play.label = "PLAY";
  play.action = ActionMenuRow;
  play.value = static_cast<int16_t>(MenuRow::Play);
  play.text = toybox::buttonText(screen.theme());
  play.radius = toybox::kPillRadius;
  screen.button(play, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect lesser = screen.takeBottom(toybox::kRowHeight, toybox::kGutter);
  const int16_t half = static_cast<int16_t>((lesser.width - toybox::kGutter) / 2);
  fui::ButtonProps nearby;
  // Half a bar does not hold the canonical "PLAY NEARBY"; the short word plus
  // the mark every link game wears carries the same meaning.
  nearby.label = "NEARBY";
  nearby.action = ActionMenuRow;
  nearby.value = static_cast<int16_t>(MenuRow::PlayNearby);
  nearby.styles = toybox::rowStyles();
  screen.button(nearby, fui::makeRect(lesser.x, lesser.y, half, lesser.height));
  fui::ButtonProps how;
  how.label = "HOW TO PLAY";
  how.action = ActionMenuRow;
  how.value = static_cast<int16_t>(MenuRow::HowTo);
  how.styles = toybox::rowStyles();
  screen.button(how,
                fui::makeRect(static_cast<int16_t>(lesser.x + half + toybox::kGutter), lesser.y, half, lesser.height));

  const fui::Rect area = screen.body();
  if (!model.hasHistory) {
    // Nothing saved yet: say so rather than leaving a hole.
    fui::TextStyle empty;
    empty.font = toybox::kTileFont;
    empty.align = fui::TextAlign::Center;
    screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + (area.height - 24) / 2), area.width, 24),
                         "YOUR FIRST MATCH STARTS THE RECORD", empty);
    return;
  }

  constexpr int16_t kMini = 52;
  constexpr int16_t kMiniGap = 4;
  const int16_t gridH = static_cast<int16_t>(3 * kMini + 2 * kMiniGap);
  const int16_t width = static_cast<int16_t>(kMini * kb::kColumns + kMiniGap * (kb::kColumns - 1));

  // The whole block centred in the body, so the slack splits above and below
  // instead of pooling under the record line.
  const int16_t blockH = static_cast<int16_t>(36 + gridH * 2 + 18 + 16 + 24);
  const int16_t blockTop = static_cast<int16_t>(area.y + (area.height > blockH ? (area.height - blockH) / 2 : 0));

  fui::TextStyle label;
  label.font = toybox::kTileFont;
  label.align = fui::TextAlign::Left;
  screen.target().text(fui::makeRect(area.x, blockTop, area.width, 24), "LAST MATCH", label);

  const int16_t gridsTop = static_cast<int16_t>(blockTop + 36);
  miniGrid(screen, area.x, gridsTop, kMini, model.lastTheirs.cell, false);
  miniGrid(screen, area.x, static_cast<int16_t>(gridsTop + gridH + 18), kMini, model.lastYours.cell, false);

  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Center;
  const int16_t gx = static_cast<int16_t>(area.x + width + toybox::kGutter);
  const int16_t gw = static_cast<int16_t>(area.right() - gx);
  char total[8];
  std::snprintf(total, sizeof(total), "%d", kb::score(model.lastTheirs));
  screen.target().text(fui::makeRect(gx, static_cast<int16_t>(gridsTop + (gridH - 48) / 2), gw, 48), total, big);
  std::snprintf(total, sizeof(total), "%d", kb::score(model.lastYours));
  screen.target().text(fui::makeRect(gx, static_cast<int16_t>(gridsTop + gridH + 18 + (gridH - 48) / 2), gw, 48), total,
                       big);

  char tally[48];
  std::snprintf(tally, sizeof(tally), "%d W   %d L   %d D", model.wins, model.losses, model.draws);
  fui::TextStyle rec;
  rec.font = toybox::kTileFont;
  rec.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(gridsTop + gridH * 2 + 18 + 16), area.width, 24),
                       tally, rec);
}

}  // namespace

fui::Rect columnRect(const fui::DeviceContext& device, const int column, const bool yours) {
  // The whole column, not one cell: a player aims at a column, and the rules
  // take a column. Derived from the same cellRect the pixels came from, so the
  // target cannot drift from the drawing.
  const fui::Rect top = cellRect(device, column, kb::kRows - 1, yours);
  const fui::Rect bottom = cellRect(device, column, 0, yours);
  const int16_t y = top.y < bottom.y ? top.y : bottom.y;
  return fui::makeRect(top.x, y, bCellW(), bGridH());
}

int howToPages() { return 3; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  // TEMP ART PASS: candidate layouts, picked by env var, deleted with the
  // losers once Mario chooses.
  if (menuVariant() == 1) {
    buildMenuBands(screen, model);
    return;
  }
  if (menuVariant() == 2) {
    buildMenuScoreboard(screen, model);
    return;
  }

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
  // topIndex is zero: this menu is three rows and never scrolls. Passing it
  // explicitly is the point -- the parameter exists so a list that DOES scroll
  // cannot silently paint its icons against the wrong rows.
  toybox::iconAtRowRight(screen, band, static_cast<int>(MenuRow::PlayNearby), 0, linkui::nearbyMark(),
                         model.selected == static_cast<int>(MenuRow::PlayNearby));

  if (!model.hasHistory) return;

  // The board the last match ended on, drawn small, with the running tally
  // beneath it. Ornament made of the app's own material and the app's own data,
  // which is the only kind this fork allows: two devices show different
  // pictures here, and the same device shows a different one after every game.
  const fui::DeviceContext device = screen.device();
  constexpr int16_t kMini = 44;
  const int16_t width = kMini * knucklebones::kColumns + 4 * (knucklebones::kColumns - 1);
  const int16_t left = static_cast<int16_t>((device.width - width) / 2);
  const int16_t rowsBottom = static_cast<int16_t>(band.y + toybox::kRowHeight * static_cast<int>(MenuRow::Count) +
                                                  toybox::kGutter * static_cast<int>(MenuRow::Count));
  const int16_t top = static_cast<int16_t>(rowsBottom + 40);

  miniGrid(screen, left, top, kMini, model.lastTheirs.cell, false);
  miniGrid(screen, left, static_cast<int16_t>(top + 3 * kMini + 8 + 14), kMini, model.lastYours.cell, false);

  char record[32];
  std::snprintf(record, sizeof(record), "%d W  %d L  %d D", model.wins, model.losses, model.draws);
  fui::TextStyle tally;
  tally.font = toybox::kSmallFont;
  tally.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(band.x, static_cast<int16_t>(top + 6 * kMini + 8 + 14 + 12), band.width, 24),
                       record, tally);
}

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

// TEMP ART PASS, how-to candidate 2: jaipur's tutorial shape. A titled page,
// the whole page as the button, dots at the foot, the caption centred in what
// the diagram leaves. Declared above buildHowTo, defined after it.
void buildHowToGuide(toybox::Screen& screen, const HowToModel& model);

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= howToPages() ? howToPages() - 1 : model.page);

  if (howToVariant() == 2) {
    buildHowToGuide(screen, model);
    return;
  }

  // TEMP ART PASS, how-to candidate 1: the page counter moves into the black
  // band, jaipur's way, and the body starts where the counter used to be.
  const bool banded = howToVariant() == 1;
  char progress[16];
  if (banded) {
    std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, howToPages());
    artChrome(screen, "HOW TO PLAY", progress);
  } else {
    fui::HeaderProps header;
    header.title = "HOW TO PLAY";
    header.borderEdges = fui::EdgesNone;
    screen.header(header);
    screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
  }

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
  // (Candidate 1 pays subtitleText white and moves it into the band instead.)
  if (!banded) {
    char counterText[8];
    std::snprintf(counterText, sizeof(counterText), "%d/%d", page + 1, howToPages());
    fui::TextStyle counter;
    counter.font = toybox::kSmallFont;
    counter.align = fui::TextAlign::Right;
    screen.target().text(fui::makeRect(area.x, area.y, area.width, 20), counterText, counter);
  }

  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 3;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + (banded ? 0 : 20)), area.width, 130),
                       kLines[page], body);

  const fui::DeviceContext device = screen.device();
  constexpr uint8_t kNone = kb::kEmpty;
  const int16_t diagramTop = static_cast<int16_t>(area.y + (banded ? 140 : 170));

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

void buildHowToGuide(toybox::Screen& screen, const HowToModel& model) {
  const int pages = howToPages();
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);

  char progress[16];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, pages);
  artChrome(screen, "HOW TO PLAY", progress);
  const fui::Rect body = screen.body();
  // The whole page is the button, jaipur's one gesture.
  screen.frame().hit(body, ActionHowToNext, 0);

  // The foot: dots and the tap affordance.
  constexpr int16_t kDot = 14;
  constexpr int16_t kDotGap = 10;
  const int16_t dotRow = static_cast<int16_t>(pages * kDot + (pages - 1) * kDotGap);
  const int16_t dotX = static_cast<int16_t>(body.x + (body.width - dotRow) / 2);
  const int16_t dotY = static_cast<int16_t>(body.bottom() - kDot);
  for (int i = 0; i < pages; ++i) {
    const fui::Rect at = fui::makeRect(static_cast<int16_t>(dotX + i * (kDot + kDotGap)), dotY, kDot, kDot);
    if (i == page) {
      screen.target().fill(at, fui::Paint::solid(fui::Color::Black), 7);
    } else {
      screen.target().stroke(at, fui::Paint::dither(fui::Color::DarkGray), toybox::kHairline, 7);
    }
  }
  fui::TextStyle tapLine;
  tapLine.font = toybox::kTileFont;
  tapLine.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(dotY - 34), body.width, 22),
                       page + 1 == pages ? "TAP TO FINISH" : "TAP TO CONTINUE", tapLine);

  // A titled page: the display cut names the rule, the caption below the
  // diagram states it, the picture in between shows it.
  static const char* const kTitles[] = {"THE TABLE", "MATCHING MULTIPLIES", "FIVES DESTROY"};
  fui::TextStyle title;
  title.font = toybox::kDisplayFont;
  title.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, body.y, body.width, 46), kTitles[page], title);

  static const char* const kLines[] = {
      "TWO BOARDS FACING. ROLL A DIE, DROP IT IN ONE OF YOUR THREE COLUMNS.",
      "MATCHING DICE MULTIPLY. THREE 4s SCORE 36, NOT 12.",
      "YOUR 5 DESTROYS EVERY 5 IN THEIR FACING COLUMN.",
  };
  fui::TextStyle cap;
  cap.font = toybox::kUiFont;
  cap.align = fui::TextAlign::Center;
  cap.maxLines = 3;
  // Three body-cut lines are ~150px; the band is sized for the worst caption
  // so no page's third line can reach the tap line below it.
  const int16_t capTop = static_cast<int16_t>(body.bottom() - 56 - 154);
  screen.target().text(fui::makeRect(body.x, capTop, body.width, 150), kLines[page], cap);

  const fui::DeviceContext device = screen.device();
  constexpr uint8_t kNone = kb::kEmpty;
  const int16_t bandTop = static_cast<int16_t>(body.y + 92);

  if (page == 0) {
    constexpr int16_t kMini = 46;
    const int16_t width = kMini * kb::kColumns + 4 * (kb::kColumns - 1);
    const int16_t left = static_cast<int16_t>((device.width - width) / 2);
    const int16_t stack = static_cast<int16_t>(7 * kMini + 36);
    const int16_t top = static_cast<int16_t>(bandTop + (capTop - bandTop - stack) / 2);
    const uint8_t theirs[kb::kColumns][kb::kRows] = {{3, kNone, kNone}, {kNone, kNone, kNone}, {6, 2, kNone}};
    const uint8_t yours[kb::kColumns][kb::kRows] = {{kNone, kNone, kNone}, {5, kNone, kNone}, {kNone, kNone, kNone}};
    miniGrid(screen, left, top, kMini, theirs, false);
    const fui::Rect die = fui::makeRect(static_cast<int16_t>((device.width - kMini) / 2),
                                        static_cast<int16_t>(top + 3 * kMini + 8 + 14), kMini, kMini);
    screen.target().stroke(die, fui::Paint::solid(fui::Color::Black), 4);
    drawFace(screen, die, 4, false);
    miniGrid(screen, left, static_cast<int16_t>(top + 3 * kMini + 8 + kMini + 28), kMini, yours, true);
    return;
  }

  if (page == 1) {
    constexpr int16_t kMini = 40;
    const uint8_t stacked[kb::kColumns][kb::kRows] = {{4, 4, 4}, {kNone, kNone, kNone}, {kNone, kNone, kNone}};
    const uint8_t spread[kb::kColumns][kb::kRows] = {{4, kNone, kNone}, {4, kNone, kNone}, {4, kNone, kNone}};
    const int16_t width = kMini * kb::kColumns + 4 * (kb::kColumns - 1);
    const int16_t left = static_cast<int16_t>((device.width - width) / 2);
    const int16_t stack = static_cast<int16_t>(6 * kMini + 124);
    const int16_t top = static_cast<int16_t>(bandTop + (capTop - bandTop - stack) / 2);
    fui::TextStyle score;
    score.font = toybox::kDisplayFont;
    score.align = fui::TextAlign::Center;
    miniGrid(screen, left, top, kMini, stacked, false);
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(top + 3 * kMini + 14), body.width, 44), "36",
                         score);
    const int16_t secondTop = static_cast<int16_t>(top + 3 * kMini + 66);
    miniGrid(screen, left, secondTop, kMini, spread, false);
    screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(secondTop + 3 * kMini + 14), body.width, 44), "12",
                         score);
    return;
  }

  constexpr int16_t kMini = 42;
  const uint8_t theirs[kb::kColumns][kb::kRows] = {{5, 5, kNone}, {kNone, kNone, kNone}, {kNone, kNone, kNone}};
  const uint8_t yours[kb::kColumns][kb::kRows] = {{5, kNone, kNone}, {kNone, kNone, kNone}, {kNone, kNone, kNone}};
  const int16_t width = kMini * kb::kColumns + 4 * (kb::kColumns - 1);
  const int16_t left = static_cast<int16_t>((device.width - width) / 2);
  const int16_t stack = static_cast<int16_t>(6 * kMini + 104);
  const int16_t top = static_cast<int16_t>(bandTop + (capTop - bandTop - stack) / 2);
  miniGrid(screen, left, top, kMini, theirs, false);
  fui::TextStyle arrow;
  arrow.font = toybox::kDisplayFont;
  arrow.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(body.x, static_cast<int16_t>(top + 3 * kMini + 10), body.width, 44), "^", arrow);
  miniGrid(screen, left, static_cast<int16_t>(top + 3 * kMini + 60), kMini, yours, true);
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

  // TEMP ART PASS: every candidate gives the one screen in this file with no
  // bottom margin the same 16px every other screen here has, so the capsule
  // sits off the bezel instead of in it.
  if (boardLayout() != 0) {
    screen.insetContent(fui::Insets{0, 0, toybox::kMargin, 0});
  }

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
  // Where the die in hand sits, and how big it is, per layout: in the strip on
  // the facing layouts, large in the freed band on the side-by-side one.
  int16_t dieSize = 73;
  int16_t dieY = 360;
  switch (boardLayout()) {
    case 1:
      dieSize = 64;
      dieY = 372;
      break;
    case 2:
      dieSize = 84;
      dieY = 549;
      break;
    case 3:
      dieSize = 73;
      dieY = 371;
      break;
    default:
      break;
  }
  if (model.die != kb::kEmpty) {
    // Centred on the panel, not on a grid. It belongs to neither player: it is
    // the one object both are looking at.
    const fui::Rect dieRect = fui::makeRect(static_cast<int16_t>((device.width - dieSize) / 2), dieY, dieSize, dieSize);
    screen.target().fill(dieRect, fui::Paint::solid(fui::Color::White));
    screen.target().stroke(dieRect, fui::Paint::solid(fui::Color::Black), 4);
    drawFace(screen, dieRect, model.die, false);
  }

  // TEMP ART PASS: named totals for the candidates. The shipping layout keeps
  // its gutter totals (drawn in drawGrid); the candidates name each number so
  // neither position nor order has to be remembered.
  if (boardLayout() != 0) {
    char mineText[8];
    char theirsText[8];
    std::snprintf(mineText, sizeof(mineText), "%d", kb::score(model.yours));
    std::snprintf(theirsText, sizeof(theirsText), "%d", kb::score(model.theirs));
    fui::TextStyle nameStyle;
    nameStyle.font = toybox::kTileFont;
    fui::TextStyle numberStyle;
    numberStyle.font = toybox::kDisplayFont;

    if (boardLayout() == 2) {
      // Above each grid, centred on the grid it counts.
      nameStyle.align = fui::TextAlign::Center;
      numberStyle.align = fui::TextAlign::Center;
      for (int side = 0; side < 2; ++side) {
        const bool mine = side == 0;
        const int16_t x = gridLeftOf(device, mine);
        screen.target().text(fui::makeRect(x, 100, bGridW(), 22), mine ? "YOU" : "THEM", nameStyle);
        screen.target().text(fui::makeRect(x, 124, bGridW(), 52), mine ? mineText : theirsText, numberStyle);
      }
    } else {
      // In the strip, flanking the die: THEM on the left because their grid is
      // above, YOU on the right because yours is below -- and named, because
      // that mapping should not have to be remembered.
      const int16_t stripY = boardLayout() == 1 ? 368 : 371;
      const int16_t dieLeft = static_cast<int16_t>((device.width - dieSize) / 2);
      const int16_t zoneW = static_cast<int16_t>(dieLeft - toybox::kMargin - toybox::kGutter);
      nameStyle.align = fui::TextAlign::Left;
      numberStyle.align = fui::TextAlign::Left;
      screen.target().text(fui::makeRect(toybox::kMargin, stripY, zoneW, 20), "THEM", nameStyle);
      screen.target().text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(stripY + 22), zoneW, 50), theirsText,
                           numberStyle);
      const int16_t rightX = static_cast<int16_t>(dieLeft + dieSize + toybox::kGutter);
      const int16_t rightW = static_cast<int16_t>(device.width - toybox::kMargin - rightX);
      nameStyle.align = fui::TextAlign::Right;
      numberStyle.align = fui::TextAlign::Right;
      screen.target().text(fui::makeRect(rightX, stripY, rightW, 20), "YOU", nameStyle);
      screen.target().text(fui::makeRect(rightX, static_cast<int16_t>(stripY + 22), rightW, 50), mineText, numberStyle);
    }
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
