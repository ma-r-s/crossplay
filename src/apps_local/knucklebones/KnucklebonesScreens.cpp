#include "KnucklebonesScreens.h"

#include <cstdio>
#include <cstdlib>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxFormat.h"

namespace knuckleui {

namespace {

namespace kb = knucklebones;

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
  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

// The board, centred on the panel's own axis. The first shipping layout
// anchored everything left so the right gutter could carry one big total per
// grid; Mario rejected that wholesale (the composition hugged the edge and the
// column scores sat in the smallest cut), and of the round-two candidates the
// centred one won. Totals now live in the strip flanking the die, named, so
// neither position nor order has to be remembered.
//
// The budget: header (76), air, their grid (231), their column scores (28),
// the strip with the die and both totals (73), your grid (231), your column
// scores (28), then the capsule and its 16px margin. Their grid sits at 90
// rather than 100 so their score row clears the die below it -- at 100 the
// centre column's score looked like it was touching the die, which Mario
// called out on the render.
constexpr int16_t kCell = 73;
constexpr int16_t kCellGap = 6;
constexpr int16_t kGridSide = kCell * kb::kColumns + kCellGap * (kb::kColumns - 1);
constexpr int16_t kScoreBand = 28;
constexpr int16_t kTheirsTop = 90;
constexpr int16_t kTheirsScoreTop = kTheirsTop + kGridSide + 4;  // 325
constexpr int16_t kDieTop = 371;
constexpr int16_t kYoursTop = 452;
constexpr int16_t kYoursScoreTop = kYoursTop + kGridSide + 4;  // 687

// The die drawn inside every cell.
constexpr int16_t kPipSize = 9;

int16_t gridLeftOf(const fui::DeviceContext& device, const bool) {
  return static_cast<int16_t>((device.width - kGridSide) / 2);
}

int16_t gridTopOf(const bool yours) { return yours ? kYoursTop : kTheirsTop; }

int16_t scoreTopOf(const bool yours) { return yours ? kYoursScoreTop : kTheirsScoreTop; }

// One cell's rect. Row 0 is the near end for whoever owns the grid, so your
// own grid stacks upward from the bottom and the opponent's stacks downward
// from the top -- the two grids face each other across the strip, the way two
// players face each other across a table.
fui::Rect cellRect(const fui::DeviceContext& device, const int column, const int row, const bool yours) {
  const int16_t left = static_cast<int16_t>(gridLeftOf(device, yours) + column * (kCell + kCellGap));
  const int16_t top = gridTopOf(yours);
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
                      static_cast<int16_t>(kGridSide + 8), static_cast<int16_t>(kGridSide + 8));
    screen.target().fill(ground, fui::Paint::dither(fui::Color::LightGray));
  }

  for (int column = 0; column < kb::kColumns; ++column) {
    for (int row = 0; row < kb::kRows; ++row) {
      const fui::Rect cell = cellRect(device, column, row, yours);
      const uint8_t value = grid.cell[column][row];
      // Dice that are multiplying draw inverted, so the game's one mechanic is
      // visible on the board rather than only in the column total. Solid black
      // in the play surface is what the ink-budget rule frowns at; Mario saw
      // the render and chose it anyway -- the mechanic earns the ink.
      bool matched = false;
      if (value != kb::kEmpty) {
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
    // The body cut, not the small one: "the numbers are so little" was half of
    // what sank round one, and a column score is a number the game is played
    // for.
    style.font = toybox::kUiFont;
    style.align = fui::TextAlign::Center;
    const int16_t left = static_cast<int16_t>(gridLeftOf(device, yours) + column * (kCell + kCellGap));
    // inkCentred because the band is 28px and the ui cut's line box is 42:
    // handed the band itself, the target's clamp drops the total 6px out of it.
    screen.target().text(toybox::inkCentred(fui::makeRect(left, scoreTopOf(yours), kCell, kScoreBand), toybox::kUiCut),
                         label, style);
  }
}

}  // namespace

// Both are defined further down, beside the board that is their main caller.
// Declared here because the menu's ornament and the how-to's diagrams draw the
// same grid and the same "your columns" mark that the board does: a signal
// taught with one shape and used with another would be worse than none.
void bracket(toybox::Screen& screen, const fui::Rect& box);
void miniGrid(toybox::Screen& screen, int16_t x, int16_t y, int16_t cell,
              const uint8_t cells[knucklebones::kColumns][knucklebones::kRows], bool bracketed);

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
  toyboxChrome(screen, "KNUCKLEBONES");

  // The front door in the documented band order: record, rule, the last match
  // as the ornament, doors anchored to the bottom with PLAY loudest.
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
  // tally between the two grids where the die normally sits. Ornament made of
  // the app's own material and the app's own data, the only kind this fork
  // allows.
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

  // The page counter lives in the black band, jaipur's way, so it costs no
  // body space.
  char progress[toybox::kOfCounterChars];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, howToPages());
  toyboxChrome(screen, "HOW TO PLAY", progress);

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

  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 3;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 130), kLines[page], body);

  const fui::DeviceContext device = screen.device();
  constexpr uint8_t kNone = kb::kEmpty;
  const int16_t diagramTop = static_cast<int16_t>(area.y + 140);

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
    // column against the same three spread out. Each score sits BESIDE its
    // grid at the grid's own middle, the way the board carries totals beside
    // the play surface. Below the grid, the 36 sat nearer the next grid than
    // its own and read as a caption for the wrong picture -- and the 12
    // crowded the NEXT capsule.
    constexpr int16_t kMini = 58;
    const uint8_t stacked[kb::kColumns][kb::kRows] = {{4, 4, 4}, {kNone, kNone, kNone}, {kNone, kNone, kNone}};
    const uint8_t spread[kb::kColumns][kb::kRows] = {{4, kNone, kNone}, {4, kNone, kNone}, {4, kNone, kNone}};
    const int16_t width = kMini * kb::kColumns + 4 * (kb::kColumns - 1);
    const int16_t left = static_cast<int16_t>((device.width - width) / 2);
    const int16_t gridH = static_cast<int16_t>(3 * kMini + 8);
    const int16_t zoneX = static_cast<int16_t>(left + width + toybox::kGutter);
    const int16_t zoneW = static_cast<int16_t>(device.width - toybox::kMargin - zoneX);
    fui::TextStyle score;
    score.font = toybox::kDisplayFont;
    score.align = fui::TextAlign::Center;
    miniGrid(screen, left, diagramTop, kMini, stacked, false);
    screen.target().text(
        toybox::inkCentred(fui::makeRect(zoneX, static_cast<int16_t>(diagramTop + (gridH - 44) / 2), zoneW, 44),
                           toybox::kDisplayCut),
        "36", score);
    const int16_t secondTop = static_cast<int16_t>(diagramTop + gridH + 40);
    miniGrid(screen, left, secondTop, kMini, spread, false);
    screen.target().text(
        toybox::inkCentred(fui::makeRect(zoneX, static_cast<int16_t>(secondTop + (gridH - 44) / 2), zoneW, 44),
                           toybox::kDisplayCut),
        "12", score);
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
  screen.target().text(
      toybox::inkCentred(fui::makeRect(area.x, static_cast<int16_t>(diagramTop + 3 * kMini + 10), area.width, 44),
                         toybox::kDisplayCut),
      "^", arrow);
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
  toybox::headerBand(screen, header);

  // The same 16px bottom margin every other screen here has, so the capsule
  // sits off the bezel instead of in it. This screen shipped without one.
  screen.insetContent(fui::Insets{0, 0, toybox::kMargin, 0});

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
  constexpr int16_t kDieSize = kCell;
  if (model.die != kb::kEmpty) {
    // Centred on the panel, not on a grid. It belongs to neither player: it is
    // the one object both are looking at.
    const fui::Rect dieRect =
        fui::makeRect(static_cast<int16_t>((device.width - kDieSize) / 2), kDieTop, kDieSize, kDieSize);
    screen.target().fill(dieRect, fui::Paint::solid(fui::Color::White));
    screen.target().stroke(dieRect, fui::Paint::solid(fui::Color::Black), 4);
    drawFace(screen, dieRect, model.die, false);
  }

  // The totals, in the strip flanking the die, each named: THEM on the left
  // because their grid is above, YOU on the right because yours is below --
  // and named precisely so that mapping never has to be remembered.
  {
    char mineText[8];
    char theirsText[8];
    std::snprintf(mineText, sizeof(mineText), "%d", kb::score(model.yours));
    std::snprintf(theirsText, sizeof(theirsText), "%d", kb::score(model.theirs));
    fui::TextStyle nameStyle;
    nameStyle.font = toybox::kTileFont;
    fui::TextStyle numberStyle;
    numberStyle.font = toybox::kDisplayFont;
    const int16_t dieLeft = static_cast<int16_t>((device.width - kDieSize) / 2);
    const int16_t zoneW = static_cast<int16_t>(dieLeft - toybox::kMargin - toybox::kGutter);
    nameStyle.align = fui::TextAlign::Left;
    numberStyle.align = fui::TextAlign::Left;
    screen.target().text(fui::makeRect(toybox::kMargin, kDieTop, zoneW, 20), "THEM", nameStyle);
    screen.target().text(
        toybox::inkCentred(fui::makeRect(toybox::kMargin, static_cast<int16_t>(kDieTop + 22), zoneW, 50),
                           toybox::kDisplayCut),
        theirsText, numberStyle);
    const int16_t rightX = static_cast<int16_t>(dieLeft + kDieSize + toybox::kGutter);
    const int16_t rightW = static_cast<int16_t>(device.width - toybox::kMargin - rightX);
    nameStyle.align = fui::TextAlign::Right;
    numberStyle.align = fui::TextAlign::Right;
    screen.target().text(fui::makeRect(rightX, kDieTop, rightW, 20), "YOU", nameStyle);
    screen.target().text(
        toybox::inkCentred(fui::makeRect(rightX, static_cast<int16_t>(kDieTop + 22), rightW, 50), toybox::kDisplayCut),
        mineText, numberStyle);
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
  toybox::headerBand(screen, header);
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
