#include "SudokuScreens.h"

#include <cstdio>

namespace sudokuui {

namespace {

namespace sk = sudoku;

// ---------------------------------------------------------------------------
// The composition.
//
// Everything on the board screen is derived from four numbers, and they were
// chosen so the whole page lines up on two vertical rules rather than three:
// the grid, the digit pad and the control rail all start or end on the board's
// own edges, and the pad's bottom lands exactly on the page's bottom margin.
//
//   header 76 + rule + 16      -> the grid starts at 92
//   50px cells, 9 of them      -> 450, plus a 9px board frame -> 468 wide
//   468 wide on a 480 panel    -> a 6px margin, and the grid ends at 560
//   a 207px pad on the margin  -> 577 to 784, so the gap under the grid is 17
//
// The pad is three by three because a Sudoku box is three by three. It is the
// one control on the screen made of the game's own material, so it needs no
// label to say what it is, and the digits sit where a phone keypad puts them.
// ---------------------------------------------------------------------------

constexpr int16_t kCell = 50;
constexpr int16_t kGridSide = kCell * sk::kSize;
constexpr int16_t kBoardOuter = kGridSide + 2 * toybox::kBoardFrame;
constexpr int16_t kBoardTop = toybox::kHeaderHeight + toybox::kMargin;

// The keys abut, and the gap between two of them is their own two outlines.
// A real gap would be pixels that belong to no key, and `padKeyAt` would either
// have to reject them (a dead strip in the middle of the pad, on a panel with
// no press feedback) or claim them (a hit test that no longer matches what was
// drawn). Sharing an edge means the pair below is an exact inverse, which is
// what host-tests/ui asserts over every pixel of the pad.
constexpr int16_t kPadKey = 69;
constexpr int16_t kPadSide = 3 * kPadKey;
constexpr int16_t kRailRow = 61;
constexpr int16_t kRailRowGap = (kPadSide - 3 * kRailRow) / 2;

constexpr int16_t kPadTop = 800 - toybox::kMargin - kPadSide;
constexpr int16_t kRailGap = kPadTop - (kBoardTop + kBoardOuter);

static_assert(kPadSide == 207, "the pad is three keys wide");
static_assert(kPadTop + kPadSide == 800 - toybox::kMargin, "the pad ends on the page's bottom margin");
static_assert(kRailGap >= toybox::kGutter, "the pad needs air under the grid");
static_assert(3 * kRailRow + 2 * kRailRowGap == kPadSide, "the rail is exactly as tall as the pad");
static_assert(kRailRowGap > 0, "three rail rows have to fit beside the pad");

int16_t boardLeft(const fui::DeviceContext& device) { return static_cast<int16_t>((device.width - kBoardOuter) / 2); }

int16_t gridLeft(const fui::DeviceContext& device) {
  return static_cast<int16_t>(boardLeft(device) + toybox::kBoardFrame);
}

constexpr int16_t gridTop() { return kBoardTop + toybox::kBoardFrame; }

fui::Rect railRect(const fui::DeviceContext& device, const int row) {
  const int16_t left = static_cast<int16_t>(boardLeft(device) + kPadSide + kRailGap);
  const int16_t width = static_cast<int16_t>(boardLeft(device) + kBoardOuter - left);
  const int16_t top = static_cast<int16_t>(kPadTop + row * (kRailRow + kRailRowGap));
  return fui::makeRect(left, top, width, kRailRow);
}

fui::Paint ink(const bool paper) { return fui::Paint::solid(paper ? fui::Color::White : fui::Color::Black); }

// A rectangular outline of a given weight, drawn as four bars inside the rect.
void frame(toybox::Screen& screen, const fui::Rect& box, const int16_t weight, const bool paper = false) {
  const fui::Paint paint = ink(paper);
  screen.target().fill(fui::makeRect(box.x, box.y, box.width, weight), paint);
  screen.target().fill(fui::makeRect(box.x, static_cast<int16_t>(box.bottom() - weight), box.width, weight), paint);
  screen.target().fill(fui::makeRect(box.x, box.y, weight, box.height), paint);
  screen.target().fill(fui::makeRect(static_cast<int16_t>(box.right() - weight), box.y, weight, box.height), paint);
}

// Four brackets at the corners of a cell. The design language's answer to
// marking a cell rather than the thing standing in it: no digit reaches a
// corner at any size, so this is legible over a numeral where a dot or a shaded
// ground would fight it.
void cornerMarks(toybox::Screen& screen, const fui::Rect& box, const int16_t arm, const int16_t weight,
                 const bool paper) {
  const fui::Paint paint = ink(paper);
  const int16_t right = static_cast<int16_t>(box.right() - arm);
  const int16_t bottom = static_cast<int16_t>(box.bottom() - weight);
  const int16_t bottomArm = static_cast<int16_t>(box.bottom() - arm);
  const int16_t rightEdge = static_cast<int16_t>(box.right() - weight);
  screen.target().fill(fui::makeRect(box.x, box.y, arm, weight), paint);
  screen.target().fill(fui::makeRect(box.x, box.y, weight, arm), paint);
  screen.target().fill(fui::makeRect(right, box.y, arm, weight), paint);
  screen.target().fill(fui::makeRect(rightEdge, box.y, weight, arm), paint);
  screen.target().fill(fui::makeRect(box.x, bottom, arm, weight), paint);
  screen.target().fill(fui::makeRect(box.x, bottomArm, weight, arm), paint);
  screen.target().fill(fui::makeRect(right, bottom, arm, weight), paint);
  screen.target().fill(fui::makeRect(rightEdge, bottomArm, weight, arm), paint);
}

// The header band with the offset rule under it, as the other games wear it. A
// local copy rather than a shared helper, for the reason LinkScreens gives: a
// copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  // rightLabel is drawn with subtitleText, and the theme's default for it is
  // black, on the black band. Jaipur paid for this discovery.
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

void digitText(char* out, const int digit) {
  out[0] = static_cast<char>('0' + digit);
  out[1] = '\0';
}

// The nine pencil marks, laid out where the digit itself would go if it were
// the only one: 1 top-left through 9 bottom-right. A mark keeps its position
// whatever else is in the cell, so the pattern of marks is readable as a shape
// rather than as a list that reflows.
void drawNotes(toybox::Screen& screen, const fui::Rect& cell, const sk::Mask notes) {
  if (notes == 0) return;
  const int16_t inset = 4;
  const int16_t side = static_cast<int16_t>((cell.width - 2 * inset) / 3);
  fui::TextStyle mark;
  mark.font = toybox::kTileFont;
  mark.align = fui::TextAlign::Center;
  for (int digit = 1; digit <= sk::kSize; ++digit) {
    if (!(notes & sk::bitFor(digit))) continue;
    const int16_t column = static_cast<int16_t>((digit - 1) % 3);
    const int16_t row = static_cast<int16_t>((digit - 1) / 3);
    char text[2];
    digitText(text, digit);
    screen.target().text(
        toybox::inkCentred(fui::makeRect(static_cast<int16_t>(cell.x + inset + column * side),
                                         static_cast<int16_t>(cell.y + inset + row * side), side, side),
                           toybox::kTileCut),
        text, mark);
  }
}

void drawGrid(toybox::Screen& screen, const BoardModel& model) {
  const fui::DeviceContext& device = screen.device();
  const int16_t left = boardLeft(device);

  // The frame is knocked out rather than stroked: fill the whole board solid
  // and paint the playing surface back over it. Four bars would leave the
  // corners to arithmetic, which is where a 1px gap comes from.
  screen.target().fill(fui::makeRect(left, kBoardTop, kBoardOuter, kBoardOuter), fui::Paint::solid(fui::Color::Black));
  screen.target().fill(fui::makeRect(gridLeft(device), gridTop(), kGridSide, kGridSide),
                       fui::Paint::solid(fui::Color::White));

  for (int cell = 0; cell < sk::kCells; ++cell) {
    const fui::Rect box = cellRect(device, cell);
    const uint8_t value = sk::valueAt(model.game, cell);
    const bool clash = sk::isClashing(model.game, cell);
    const bool clue = sk::isGiven(model.game, cell);
    const bool held = cell == model.holdCell;

    const bool armedHere = value != 0 && value == model.game.armed;

    // One ground per cell, and the order is the order of urgency: a clash
    // shouts over everything, and a finger resting on a cell over the rest.
    if (clash) {
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
    } else if (held) {
      screen.target().fill(box, fui::Paint::dither(fui::Color::DarkGray));
    } else if (clue) {
      screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
    }

    if (value != 0) {
      char text[2];
      digitText(text, value);
      fui::TextStyle digit;
      digit.font = toybox::kDisplayFont;
      digit.align = fui::TextAlign::Center;
      digit.color = clash ? fui::Color::White : fui::Color::Black;
      // The cell's full width, but a rect built by inkCentred for the height:
      // the display cut's line box is 63px and a cell is 50, so handing the
      // component the plain cell makes its own centring clamp to zero and drops
      // the digit's foot through the bottom border. See ToyboxTokens.h.
      screen.target().text(toybox::inkCentred(box, toybox::kDisplayCut), text, digit);
      // Every copy of the armed digit is marked, which is what makes arming one
      // a way of reading the board rather than only a way of writing to it.
      if (armedHere) cornerMarks(screen, box, 13, toybox::kRule, clash);
    } else {
      drawNotes(screen, box, sk::visibleNotes(model.game, cell));
    }

    if (cell == model.game.hintCell) frame(screen, box, toybox::kFrame);
  }

  // The rules on top of the cells, so no cell can overdraw a line: hairlines
  // between cells, the heavier rule between boxes. Drawn after the loop for
  // the same reason the chess selection frame is: anything that crosses a cell
  // boundary needs its own pass.
  for (int index = 1; index < sk::kSize; ++index) {
    const bool boxEdge = index % sk::kBoxSize == 0;
    const int16_t weight = boxEdge ? toybox::kRule : toybox::kHairline;
    const int16_t offset = static_cast<int16_t>(index * kCell - weight / 2);
    screen.target().fill(fui::makeRect(static_cast<int16_t>(gridLeft(device) + offset), gridTop(), weight, kGridSide),
                         fui::Paint::solid(fui::Color::Black));
    screen.target().fill(fui::makeRect(gridLeft(device), static_cast<int16_t>(gridTop() + offset), kGridSide, weight),
                         fui::Paint::solid(fui::Color::Black));
  }
}

void drawPad(toybox::Screen& screen, const BoardModel& model) {
  const fui::DeviceContext& device = screen.device();
  for (int digit = 1; digit <= sk::kSize; ++digit) {
    const fui::Rect key = padKeyRect(device, digit);
    const int placed = sk::placedCount(model.game, digit);
    const bool spent = placed >= sk::kSize;
    const bool armed = digit == model.game.armed;

    // A key with nothing left to place dims rather than disappearing: the pad
    // is a fixed shape and a hole in it would move every other key.
    if (spent) screen.target().fill(key, fui::Paint::dither(fui::Color::LightGray));
    frame(screen, key, armed ? toybox::kFrame : toybox::kHairline);

    char text[2];
    digitText(text, digit);
    fui::TextStyle label;
    label.font = toybox::kDisplayFont;
    label.align = fui::TextAlign::Center;
    screen.target().text(toybox::inkCentred(key, toybox::kDisplayCut), text, label);

    // There is no progress bar under the digit. The first version drew one and
    // it read as an underline on every key -- nine cells that looked like
    // fill-in-the-blanks rather than buttons. How many of a digit are down is
    // already on the board, where arming it brackets every copy, so the key
    // only has to say when there is nothing left to place.
  }
}

void drawRail(toybox::Screen& screen, const BoardModel& model) {
  const fui::DeviceContext& device = screen.device();
  const bool solved = model.game.solvedFlag != 0;

  // The status capsule. Outlined while you solve and inverted once the grid is
  // finished, because that is the one moment it is worth spending solid black
  // on: it happens once and it is the payoff.
  const fui::Rect status = railRect(device, 0);
  char line[32];
  if (model.generating) {
    std::snprintf(line, sizeof(line), "MAKING ONE");
  } else if (solved) {
    std::snprintf(line, sizeof(line), "SOLVED");
  } else if (model.notice != nullptr) {
    std::snprintf(line, sizeof(line), "%s", model.notice);
  } else {
    std::snprintf(line, sizeof(line), "%d LEFT", sk::emptyCount(model.game));
  }

  fui::ButtonProps capsule;
  capsule.label = line;
  capsule.styles = toybox::rowStyles();
  capsule.state = solved ? fui::StateSelected : fui::StateNormal;
  // Registered only once it can do something. While solving it is a readout,
  // and a readout that answers a tap is a control the player has to learn is
  // not one.
  if (solved) capsule.action = ActionSeeResult;
  screen.button(capsule, status);

  fui::ButtonProps undo;
  undo.label = "UNDO";
  undo.action = ActionUndo;
  const bool canUndo = sk::canUndo(model.game) && !model.generating;
  undo.styles = canUndo ? toybox::rowStyles() : toybox::disabledStepperStyles();
  screen.button(undo, railRect(device, 1));

  fui::ButtonProps hint;
  hint.label = "HINT";
  hint.action = ActionHint;
  const bool canHint = !solved && !model.generating;
  hint.styles = canHint ? toybox::rowStyles() : toybox::disabledStepperStyles();
  screen.button(hint, railRect(device, 2));
}

// The front door's ornament: the puzzle you have open, at a tenth the size.
//
// It passes the design language's test by construction. A clue is a filled
// square, a digit you wrote is a hollow one, an empty cell is nothing, so the
// picture is the shape of your specific puzzle plus how far into it you are. It
// is different on every device and different every time you look.
void drawMiniature(toybox::Screen& screen, const fui::Rect& room, const sk::Game& game, const bool hasGame) {
  const int16_t side =
      static_cast<int16_t>((room.width < room.height ? room.width : room.height) - 2 * toybox::kMargin);
  const int16_t cell = static_cast<int16_t>(side / sk::kSize);
  const int16_t grid = static_cast<int16_t>(cell * sk::kSize);
  const int16_t left = static_cast<int16_t>(room.x + (room.width - grid) / 2);
  const int16_t top = static_cast<int16_t>(room.y + (room.height - grid) / 2);

  // The same brackets the board wears, so the two read as one device.
  const int16_t reach = static_cast<int16_t>(toybox::kMargin - 4);
  cornerMarks(screen,
              fui::makeRect(static_cast<int16_t>(left - reach), static_cast<int16_t>(top - reach),
                            static_cast<int16_t>(grid + 2 * reach), static_cast<int16_t>(grid + 2 * reach)),
              16, toybox::kRule, false);

  for (int index = 0; index <= sk::kSize; index += sk::kBoxSize) {
    const int16_t offset = static_cast<int16_t>(index * cell - (index == sk::kSize ? toybox::kHairline : 0));
    screen.target().fill(fui::makeRect(static_cast<int16_t>(left + offset), top, toybox::kHairline, grid),
                         fui::Paint::dither(fui::Color::DarkGray));
    screen.target().fill(fui::makeRect(left, static_cast<int16_t>(top + offset), grid, toybox::kHairline),
                         fui::Paint::dither(fui::Color::DarkGray));
  }
  if (!hasGame) return;

  for (int index = 0; index < sk::kCells; ++index) {
    const int16_t x = static_cast<int16_t>(left + (index % sk::kSize) * cell);
    const int16_t y = static_cast<int16_t>(top + (index / sk::kSize) * cell);
    if (sk::isGiven(game, index)) {
      const int16_t pad = 2;
      screen.target().fill(fui::makeRect(static_cast<int16_t>(x + pad), static_cast<int16_t>(y + pad),
                                         static_cast<int16_t>(cell - 2 * pad), static_cast<int16_t>(cell - 2 * pad)),
                           fui::Paint::solid(fui::Color::Black));
    } else if (game.entry[index] != 0) {
      const int16_t pad = static_cast<int16_t>(cell / 3);
      screen.target().fill(fui::makeRect(static_cast<int16_t>(x + pad), static_cast<int16_t>(y + pad),
                                         static_cast<int16_t>(cell - 2 * pad), static_cast<int16_t>(cell - 2 * pad)),
                           fui::Paint::solid(fui::Color::Black));
    }
  }
}

const char* const kLessonLines[] = {
    "FILL EVERY ROW, COLUMN AND BOX WITH ONE TO NINE. NO DIGIT TWICE IN ANY OF THEM.",
    "PICK A DIGIT ON THE PAD, THEN TAP CELLS. EVERY COPY OF IT ON THE BOARD IS MARKED, SO YOU CAN SEE WHERE IT CANNOT "
    "GO.",
    "TAP YOUR OWN DIGIT AGAIN TO CLEAR IT. TAP A PRINTED CLUE TO PICK ITS DIGIT UP.",
    "HOLD A CELL TO PENCIL THE ARMED DIGIT IN. HOLD AGAIN TO RUB IT OUT. MARKS FADE ON THEIR OWN ONCE A NEIGHBOUR "
    "TAKES THE DIGIT.",
    "A DIGIT THAT CLASHES WITH ONE IT CAN SEE TURNS BLACK. HINT NAMES A CELL YOU CAN SOLVE NOW, AND THE RULE THAT "
    "PROVES IT.",
};
constexpr int kLessonCount = static_cast<int>(sizeof(kLessonLines) / sizeof(kLessonLines[0]));

// A small worked fragment for each page, built from real rules rather than
// drawn: the diagram cannot disagree with the lesson beside it.
void lessonDiagram(toybox::Screen& screen, const int page, const int16_t top, const int16_t bottom,
                   const fui::Rect& area) {
  const int16_t cell = 44;
  const int16_t grid = static_cast<int16_t>(cell * 3);
  const int16_t left = static_cast<int16_t>(area.x + (area.width - grid) / 2);
  const int16_t room = static_cast<int16_t>(bottom - top);
  if (room < grid) return;
  const int16_t y = static_cast<int16_t>(top + (room - grid) / 2);

  // Page order matches kLessonLines: the box, an armed digit with its marks,
  // an overwrite, a pencilled cell, a clash.
  static const char* const kFaces[kLessonCount] = {"123456789", "5..3..7.5", "5..3..7..", "5..3..7..", "5..3..5.."};
  const char* face = kFaces[page];

  for (int index = 0; index < 9; ++index) {
    const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + (index % 3) * cell),
                                        static_cast<int16_t>(y + (index / 3) * cell), cell, cell);
    const char mark = face[index];
    const bool clash = page == 4 && (index == 0 || index == 6);
    if (clash) {
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
    } else if (page != 0 && mark != '.') {
      screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
    }
    frame(screen, box, toybox::kHairline);
    if (mark != '.') {
      char text[2] = {mark, '\0'};
      fui::TextStyle digit;
      digit.font = toybox::kDisplayFont;
      digit.align = fui::TextAlign::Center;
      digit.color = clash ? fui::Color::White : fui::Color::Black;
      screen.target().text(box, text, digit);
      if (page == 1 && mark == '5') cornerMarks(screen, box, 11, toybox::kRule, false);
    } else if (page == 3 && index == 4) {
      drawNotes(screen, box, static_cast<sk::Mask>(sk::bitFor(2) | sk::bitFor(6) | sk::bitFor(9)));
    }
  }
  frame(screen,
        fui::makeRect(static_cast<int16_t>(left - toybox::kRule), static_cast<int16_t>(y - toybox::kRule),
                      static_cast<int16_t>(grid + 2 * toybox::kRule), static_cast<int16_t>(grid + 2 * toybox::kRule)),
        toybox::kRule);
}

}  // namespace

fui::Rect cellRect(const fui::DeviceContext& device, const int cell) {
  return fui::makeRect(static_cast<int16_t>(gridLeft(device) + (cell % sk::kSize) * kCell),
                       static_cast<int16_t>(gridTop() + (cell / sk::kSize) * kCell), kCell, kCell);
}

bool cellAt(const fui::DeviceContext& device, const int x, const int y, int& cell) {
  const int16_t left = gridLeft(device);
  if (x < left || y < gridTop()) return false;
  const int column = (x - left) / kCell;
  const int row = (y - gridTop()) / kCell;
  if (column >= sk::kSize || row >= sk::kSize) return false;
  cell = row * sk::kSize + column;
  return true;
}

fui::Rect padKeyRect(const fui::DeviceContext& device, const int digit) {
  const int index = digit - 1;
  return fui::makeRect(static_cast<int16_t>(boardLeft(device) + (index % 3) * kPadKey),
                       static_cast<int16_t>(kPadTop + (index / 3) * kPadKey), kPadKey, kPadKey);
}

bool padKeyAt(const fui::DeviceContext& device, const int x, const int y, int& digit) {
  const int16_t left = boardLeft(device);
  if (x < left || y < kPadTop) return false;
  const int column = (x - left) / kPadKey;
  const int row = (y - kPadTop) / kPadKey;
  if (column >= 3 || row >= 3) return false;
  digit = row * 3 + column + 1;
  return true;
}

void formatClock(const uint32_t ms, char* out, const int size) {
  const uint32_t seconds = ms / 1000;
  const uint32_t hours = seconds / 3600;
  if (hours > 0) {
    std::snprintf(out, static_cast<size_t>(size), "%u:%02u:%02u", hours, (seconds / 60) % 60, seconds % 60);
    return;
  }
  std::snprintf(out, static_cast<size_t>(size), "%u:%02u", seconds / 60, seconds % 60);
}

int howToPages() { return kLessonCount; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "SUDOKU", sk::levelName(model.level));

  // The headline is the loudest thing on the page and it is also the main
  // action's hit target, so the commonest tap needs no button at all.
  const bool resuming = model.hasGame && !model.levelChanged && model.game.solvedFlag == 0;
  fui::ButtonProps headline;
  headline.label = resuming ? "RESUME" : "NEW PUZZLE";
  headline.action = ActionPlay;
  headline.styles = toybox::rowStyles();
  screen.button(headline, screen.takeTop(58));

  // Bands sized to the ink rather than to the number that looked about right.
  // The first version gave the state 28px and the record 26 and the two ran
  // into each other, because a 20px cut's line box is taller than its cap.
  char state[48];
  if (model.levelChanged) {
    std::snprintf(state, sizeof(state), "%s, STARTING FRESH", sk::levelName(model.level));
  } else if (resuming) {
    std::snprintf(state, sizeof(state), "%d LEFT", sk::emptyCount(model.game));
  } else if (model.hasGame) {
    std::snprintf(state, sizeof(state), "LAST ONE SOLVED");
  } else {
    std::snprintf(state, sizeof(state), "NOT STARTED");
  }
  fui::TextStyle stateStyle;
  stateStyle.font = toybox::kBodyFont;
  stateStyle.align = fui::TextAlign::Left;
  screen.target().text(screen.takeTop(38), state, stateStyle);

  const fui::Rect ruleBand = screen.takeTop(toybox::kRule + 10);
  screen.target().fill(fui::makeRect(ruleBand.x, static_cast<int16_t>(ruleBand.y + 6), ruleBand.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  // The doors carry the difficulty, which cycles in place.
  char levelRow[32];
  std::snprintf(levelRow, sizeof(levelRow), "%s", sk::levelName(model.level));
  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Level)].label = "DIFFICULTY";
  rows[static_cast<int>(MenuRow::Level)].value = levelRow;
  rows[static_cast<int>(MenuRow::Level)].actionValue = static_cast<int16_t>(MenuRow::Level);
  rows[static_cast<int>(MenuRow::HowTo)].label = "HOW TO PLAY";
  rows[static_cast<int>(MenuRow::HowTo)].actionValue = static_cast<int16_t>(MenuRow::HowTo);

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionMenuRow;
  const int count = static_cast<int>(MenuRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

  char record[56];
  const int index = static_cast<int>(model.level);
  if (model.record.bestMs[index] != 0) {
    char best[16];
    formatClock(model.record.bestMs[index], best, sizeof(best));
    std::snprintf(record, sizeof(record), "%d SOLVED   BEST %s", sk::totalSolved(model.record), best);
  } else {
    std::snprintf(record, sizeof(record), "%d SOLVED   NO %s TIME YET", sk::totalSolved(model.record),
                  sk::levelName(model.level));
  }
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  const fui::Rect recordBand = screen.takeTop(26);
  screen.target().text(recordBand, record, small);
  drawMiniature(screen,
                fui::makeRect(content.x, recordBand.bottom(), content.width,
                              static_cast<int16_t>(listBand.y - recordBand.bottom())),
                model.game, model.hasGame);
}

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= kLessonCount ? kLessonCount - 1 : model.page);
  char progress[16];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, kLessonCount);
  toyboxChrome(screen, "HOW TO PLAY", progress);

  // Taken before the page's own drawing, so no branch can skip the way forward.
  fui::ButtonProps next;
  next.label = page + 1 < kLessonCount ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 5;
  const int16_t textHeight = 160;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, textHeight), kLessonLines[page], body);
  lessonDiagram(screen, page, static_cast<int16_t>(area.y + textHeight + 10), area.bottom(), area);
}

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  char level[24];
  std::snprintf(level, sizeof(level), "%s", sk::levelName(model.game.puzzle.level));
  toyboxChrome(screen, "SUDOKU", level);
  drawGrid(screen, model);
  drawPad(screen, model);
  drawRail(screen, model);
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  toyboxChrome(screen, "SOLVED", sk::levelName(model.level));

  char clock[16];
  formatClock(model.elapsedMs, clock, sizeof(clock));
  const fui::Rect headline = screen.takeTop(72);
  fui::TextStyle big;
  big.font = toybox::kDisplayFont;
  big.align = fui::TextAlign::Left;
  screen.target().text(headline, clock, big);

  char under[48];
  if (model.hintsUsed > 0) {
    std::snprintf(under, sizeof(under), "%d HINT%s, SO NO TIME RECORDED", model.hintsUsed,
                  model.hintsUsed == 1 ? "" : "S");
  } else if (model.newBest) {
    std::snprintf(under, sizeof(under), "YOUR BEST %s YET", sk::levelName(model.level));
  } else if (model.bestMs != 0) {
    char best[16];
    formatClock(model.bestMs, best, sizeof(best));
    std::snprintf(under, sizeof(under), "YOUR BEST IS %s", best);
  } else {
    std::snprintf(under, sizeof(under), "UNAIDED");
  }
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Left;
  screen.target().text(screen.takeTop(34), under, body);

  const fui::Rect rule = screen.takeTop(toybox::kRule + 12);
  screen.target().fill(fui::makeRect(rule.x, static_cast<int16_t>(rule.y + 8), rule.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  // What the puzzle actually demanded of you, which is the honest measure of an
  // EXPERT label and the only place the ladder is ever named to a player.
  //
  // One small line, not three body-cut ones. Three lines at the same weight as
  // the sentence above them flattened the page: the clock, the comparison and
  // the footnote all shouted equally, and the block was the loudest thing under
  // the header. This is the front door's record line, in the same cut, doing
  // the same job.
  char detail[72];
  std::snprintf(detail, sizeof(detail), "%d CLUES   HARDEST %s   %d %s SOLVED", model.clues,
                sk::techniqueName(model.hardest), model.solvedAtThisLevel, sk::levelName(model.level));
  fui::TextStyle small;
  small.font = toybox::kTileFont;
  small.align = fui::TextAlign::Left;
  screen.target().text(screen.takeTop(26), detail, small);

  fui::ButtonProps again;
  again.label = "ANOTHER";
  again.action = ActionAgain;
  screen.button(again, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  fui::ButtonProps done;
  done.label = "BACK TO THE GRID";
  done.action = ActionDone;
  done.styles = toybox::rowStyles();
  screen.button(done, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  // Taken after both buttons, so what is left is exactly the slack between the
  // last stat and the first control. The first version left that band empty and
  // it was six hundred pixels of nothing under four lines of text.
  drawMiniature(screen, screen.body(), model.game, true);
}

}  // namespace sudokuui
