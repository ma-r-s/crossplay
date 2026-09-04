#include "ConnectFourScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxIcons.h"

namespace c4ui {

namespace {

namespace c4 = connectfour;

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

// The slab at an arbitrary cell size, drawn from the saved final board, for
// the menu's ornament: frame, dither, holes, discs, and the winning four
// marked the way the result screen marks them. `cells` is column-major
// bottom-up; `line` holds four flat indices or -1s.
void miniSlab(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t cell, const uint8_t* cells,
              const int* line) {
  const int16_t width = static_cast<int16_t>(cell * c4::kColumns);
  const int16_t height = static_cast<int16_t>(cell * c4::kRows);
  screen.target().stroke(
      fui::makeRect(static_cast<int16_t>(left - toybox::kFrame), static_cast<int16_t>(top - toybox::kFrame),
                    static_cast<int16_t>(width + toybox::kFrame * 2),
                    static_cast<int16_t>(height + toybox::kFrame * 2)),
      fui::Paint::solid(fui::Color::Black), toybox::kFrame);
  screen.target().fill(fui::makeRect(left, top, width, height), fui::Paint::dither(fui::Color::LightGray));
  const int16_t radius = static_cast<int16_t>(cell * 26 / 64);
  for (int column = 0; column < c4::kColumns; ++column) {
    for (int row = 0; row < c4::kRows; ++row) {
      const int16_t cx = static_cast<int16_t>(left + column * cell + cell / 2);
      const int16_t cy = static_cast<int16_t>(top + (c4::kRows - 1 - row) * cell + cell / 2);
      toybox::disc(screen, cx, cy, radius, fui::Color::White);
      const uint8_t piece = cells[column * c4::kRows + row];
      if (piece == c4::kEmpty) continue;
      toybox::ring(screen, cx, cy, radius, 3, fui::Color::Black,
                   piece == c4::kDark ? fui::Color::Black : fui::Color::White);
    }
  }
  if (line == nullptr || line[0] < 0) return;
  // The mark scales with the cell. At full size the result screen's mark
  // covers about forty percent of a disc; a fixed-size mark at ornament scale
  // swallowed the whole disc, and a marked light disc read as a dark one.
  const int16_t weight = static_cast<int16_t>(cell / 10 < 2 ? 2 : cell / 10);
  for (int i = 0; i < c4::kLine; ++i) {
    const int column = line[i] / c4::kRows;
    const int row = line[i] % c4::kRows;
    const int16_t cx = static_cast<int16_t>(left + column * cell + cell / 2);
    const int16_t cy = static_cast<int16_t>(top + (c4::kRows - 1 - row) * cell + cell / 2);
    screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - weight * 2), static_cast<int16_t>(cy - weight * 2),
                                       static_cast<int16_t>(weight * 4), static_cast<int16_t>(weight * 4)),
                         fui::Paint::solid(fui::Color::Black), weight);
  }
}

// 64px, so seven cells are exactly the 448px between the margins. Bigger than
// Checkers' 56 because there are fewer of them, and a bigger cell is a bigger
// disc, not more air.
constexpr int16_t kCell = 64;
constexpr int16_t kGridWidth = kCell * c4::kColumns;
constexpr int16_t kGridHeight = kCell * c4::kRows;

// The disc radius inside a cell. 26 of 64 leaves 6px of clearance, close to the
// ratio chess uses (48 of 53) and tighter than Checkers ended up: on a grid
// this open the discs have to carry the weight.
constexpr int16_t kDiscRadius = 26;
constexpr int16_t kRingWeight = 4;
// The waiting disc in the lip. Smaller than a played one, so the channel cannot
// be mistaken for a row.
constexpr int16_t kLipRadius = 16;
// Twenty-one discs a colour, which is what is in the box.
constexpr int kDiscsPerSide = c4::kCells / 2;

// The lip: a shallow channel across the top of the board, holding the disc you
// are about to drop.
//
// It exists because a Connect Four board has no "here" until a disc lands. The
// grid is the history; the lip is the move, and it is also what makes the
// column read as a target rather than the cell you happen to touch.
//
// SHALLOWER than a row, and its discs are smaller, because at full height they
// were pixel-identical to a row of played light pieces and the 3px rule was
// the only thing saying otherwise. That is not enough separation for a board
// whose top row will one day be full of exactly those pieces.
constexpr int16_t kSlotHeight = 42;

int16_t gridLeft(const fui::DeviceContext& device) { return static_cast<int16_t>((device.width - kGridWidth) / 2); }

// Where the BOARD screen's grid starts, as a pure function of the device.
//
// Pure on purpose. cellRect, columnAt and the drawing all call this, so a tap
// lands on the cell the player is looking at by construction rather than by two
// pieces of arithmetic happening to agree. Deriving it from the laid-out body
// rect instead would give the hit-test a different answer from the drawing the
// moment either is called at a different point in the layout.
//
// ANCHORED under the header, not centred.
//
// It was centred, with a comment claiming that beat leaving the slack in one
// lump. The design language disagrees by name: a block floating with equal
// slack above and below reads as unresolved, and the measurement bore that out
// -- 112px of white above the board and 100px below, a quarter of the panel
// split into two symmetric nothings. Chess and Checkers both anchor within
// 15-33px of the header. The band that frees up goes to the disc strips below,
// which is the same answer Checkers reached for the same gap.
int16_t boardGridTop(const fui::DeviceContext& device) {
  (void)device;
  const int16_t top = static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter * 2);
  return static_cast<int16_t>(top + toybox::kBoardFrame + kSlotHeight);
}

// The lip sits directly on top of the grid now, inside the same frame, so
// there is no gutter between them.
int16_t boardSlotTop(const fui::DeviceContext& device) {
  return static_cast<int16_t>(boardGridTop(device) - kSlotHeight);
}

// A disc: dark solid, light outlined, the same convention as the board next
// door in Checkers. Filled means WHOSE, never whose turn.
void drawDisc(toybox::Screen& screen, const int16_t cx, const int16_t cy, const uint8_t side) {
  toybox::ring(screen, cx, cy, kDiscRadius, kRingWeight, fui::Color::Black,
               side == c4::kDark ? fui::Color::Black : fui::Color::White);
}

}  // namespace

fui::Rect cellRect(const fui::DeviceContext& device, const int column, const int row) {
  // Row 0 is the bottom in the rules, because that is the way gravity works.
  // Here it is the last row drawn. This one line is the whole flip.
  const int drawRow = c4::kRows - 1 - row;
  return fui::makeRect(static_cast<int16_t>(gridLeft(device) + column * kCell),
                       static_cast<int16_t>(boardGridTop(device) + drawRow * kCell), kCell, kCell);
}

fui::Rect slotRect(const fui::DeviceContext& device, const int column) {
  return fui::makeRect(static_cast<int16_t>(gridLeft(device) + column * kCell), boardSlotTop(device), kCell,
                       kSlotHeight);
}

int columnAt(const fui::DeviceContext& device, const int x, const int y) {
  // Slot row and grid alike: the whole column is one target, top to bottom.
  // A seven-wide row of 64px strips 500px tall is the most forgiving target
  // this panel can offer, and aiming at one 64px cell would be the only hard
  // tap in the game for no gain -- the row a disc lands on is not the player's
  // to choose.
  const int16_t top = boardSlotTop(device);
  const int16_t bottom = static_cast<int16_t>(boardGridTop(device) + kGridHeight + toybox::kBoardFrame);
  if (y < top || y >= bottom) return c4::kNoColumn;
  const int16_t left = gridLeft(device);
  if (x < left || x >= left + kGridWidth) return c4::kNoColumn;
  return (x - left) / kCell;
}

int howToPages() { return 3; }

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "CONNECT FOUR");

  // The front door in the documented band order: record, rule, the last
  // game's final slab with its winning four as the ornament, doors anchored
  // bottom.
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

  if (!model.hasHistory || model.lastCells == nullptr) return;

  // The last game, held where the eye rests: the final slab with the four
  // that ended it still marked. Ornament made of the app's own material and
  // the app's own data, the only kind this fork allows.
  constexpr int16_t kMini = 30;
  const int16_t slabW = static_cast<int16_t>(kMini * c4::kColumns);
  const int16_t slabH = static_cast<int16_t>(kMini * c4::kRows);
  const int16_t areaTop = static_cast<int16_t>(line.bottom() + 6 + toybox::kRule);
  const int16_t room = static_cast<int16_t>(listBand.y - areaTop);
  const int16_t blockH = static_cast<int16_t>(slabH + 14 + 24);
  const int16_t top = static_cast<int16_t>(areaTop + (room > blockH ? (room - blockH) / 2 : 12));
  const fui::DeviceContext device = screen.device();
  miniSlab(screen, static_cast<int16_t>((device.width - slabW) / 2), top, kMini, model.lastCells, model.lastLine);

  const char* caption = "LAST GAME: A DRAW";
  if (model.lastOutcome == 0) caption = "LAST GAME: YOU WON";
  if (model.lastOutcome == 1) caption = "LAST GAME: THEY WON";
  fui::TextStyle cap;
  cap.font = toybox::kTileFont;
  cap.align = fui::TextAlign::Center;
  screen.target().text(fui::makeRect(content.x, static_cast<int16_t>(top + slabH + 14), content.width, 24), caption,
                       cap);
}

namespace {

// The fragment the how-to pages draw, lifted out so every layout candidate
// places the same picture. `top` is the fragment's own top edge.
void howToFragment(toybox::Screen& screen, const int16_t top, const int page);

}  // namespace

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= howToPages() ? howToPages() - 1 : model.page);

  // The page counter lives in the black band, jaipur's way, so it costs no
  // body space; the fragment centres in the room that frees, with headroom
  // for page 0's lip disc above the frame.
  char progress[16];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, howToPages());
  toyboxChrome(screen, "HOW TO PLAY", progress);

  fui::ButtonProps next;
  next.label = page + 1 < howToPages() ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();

  static const char* const kLines[] = {
      "TAP A COLUMN. YOUR DISC FALLS TO THE LOWEST FREE PLACE IN IT.",
      "FOUR IN A LINE WINS. ACROSS, UP, OR ON EITHER DIAGONAL LIKE THIS ONE.",
      "A FULL COLUMN STOPS TAKING DISCS, AND ITS SLOT AT THE TOP GOES GREY.",
  };
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 3;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 120), kLines[page], body);

  constexpr int16_t kFragment = kCell * 4;
  const int16_t bandTop = static_cast<int16_t>(area.y + 150 + 46);
  int16_t top = static_cast<int16_t>(bandTop + (area.bottom() - bandTop - kFragment) / 2);
  if (top < bandTop) top = bandTop;
  howToFragment(screen, top, page);
}

namespace {

void howToFragment(toybox::Screen& screen, const int16_t top, const int page) {
  // A fragment of the real board at the real size. FOUR deep, not two: at 4x2
  // the picture on page two was geometrically incapable of showing a vertical
  // or a diagonal four, so it showed the one win everybody already knows and
  // filled the other row with empty holes.
  const fui::DeviceContext device = screen.device();
  constexpr int kFragmentSide = 4;
  const int16_t left = static_cast<int16_t>((device.width - kCell * kFragmentSide) / 2);
  const fui::Rect frame =
      fui::makeRect(static_cast<int16_t>(left - toybox::kBoardFrame), static_cast<int16_t>(top - toybox::kBoardFrame),
                    static_cast<int16_t>(kCell * kFragmentSide + toybox::kBoardFrame * 2),
                    static_cast<int16_t>(kCell * kFragmentSide + toybox::kBoardFrame * 2));
  screen.target().stroke(frame, fui::Paint::solid(fui::Color::Black), toybox::kBoardFrame);
  screen.target().fill(fui::makeRect(left, top, static_cast<int16_t>(kCell * kFragmentSide),
                                     static_cast<int16_t>(kCell * kFragmentSide)),
                       fui::Paint::dither(fui::Color::LightGray));

  // Page 0: a column with two in it and a third landing, so "falls to the
  //         lowest free place" is a picture of the place rather than a claim.
  // Page 1: a DIAGONAL four, which is the win a new player misses and the one
  //         the old 4x2 fragment could not draw.
  // Page 2: one column full to the brim beside one that is not, which is the
  //         thing the board's own signal -- a dimmed slot -- is about.
  static const char* const kBoards[3][kFragmentSide] = {
      {"....", "....", ".L..", ".D.."},
      {"...L", "..LD", ".LDD", "LDLD"},
      {"D...", "L...", "D...", "LD.."},
  };
  for (int row = 0; row < kFragmentSide; ++row) {
    for (int column = 0; column < kFragmentSide; ++column) {
      const int16_t cx = static_cast<int16_t>(left + column * kCell + kCell / 2);
      const int16_t cy = static_cast<int16_t>(top + row * kCell + kCell / 2);
      toybox::disc(screen, cx, cy, kDiscRadius, fui::Color::White);
      const char c = kBoards[page][row][column];
      if (c == '.') continue;
      drawDisc(screen, cx, cy, c == 'L' ? c4::kLight : c4::kDark);
    }
  }
  if (page == 0) {
    // The disc on its way in, sitting in the lip where a real one would.
    toybox::ring(screen, static_cast<int16_t>(left + kCell + kCell / 2),
                 static_cast<int16_t>(top - toybox::kBoardFrame - kSlotHeight / 2), kLipRadius, kRingWeight,
                 fui::Color::Black, fui::Color::White);
  }
  if (page == 1) {
    // The same line mark the result screen uses, in the same ink it would use:
    // opposite to the winning discs, which here are light. Teaching a white
    // line and then drawing a black one would make the picture a lie about the
    // one screen it is preparing you for.
    constexpr int16_t kWeight = 7;
    const int16_t x0 = static_cast<int16_t>(left + kCell / 2);
    const int16_t y0 = static_cast<int16_t>(top + 3 * kCell + kCell / 2);
    for (int step = 0; step <= 3 * kCell; ++step) {
      screen.target().fill(fui::makeRect(static_cast<int16_t>(x0 + step - kWeight / 2),
                                         static_cast<int16_t>(y0 - step - kWeight / 2), kWeight, kWeight),
                           fui::Paint::solid(fui::Color::Black));
    }
  }
}

}  // namespace

// The grid, its frame, and the discs in it. Shared by the board and the result
// screen so the position cannot be drawn two different ways.
namespace {

// `lip` is the height of the band above the grid that belongs to the board --
// the top of the frame, where a disc goes in. Zero on the result screen, which
// has no move to make.
void drawGrid(toybox::Screen& screen, const int16_t top, const int16_t lip, const c4::Game& game, const uint8_t waiting,
              const uint8_t seat, const int* markLine) {
  const int16_t left = gridLeft(screen.device());
  const int16_t lipTop = static_cast<int16_t>(top - lip);

  const fui::Rect frame = fui::makeRect(static_cast<int16_t>(left - toybox::kBoardFrame),
                                        static_cast<int16_t>(lipTop - toybox::kBoardFrame),
                                        static_cast<int16_t>(kGridWidth + toybox::kBoardFrame * 2),
                                        static_cast<int16_t>(kGridHeight + lip + toybox::kBoardFrame * 2));
  screen.target().stroke(frame, fui::Paint::solid(fui::Color::Black), toybox::kBoardFrame);

  // The slab, then the holes punched in it, then the discs sitting in them.
  //
  // Dithered ground is how this fork already draws a playing surface (the dark
  // squares of a checkers board), and here it earns its keep twice: it makes
  // the grid a physical object with forty-two holes in it, and it gives the
  // three states of a place three genuinely different textures rather than
  // three ring weights nobody could tell apart at 220ppi.
  //
  //   empty  white disc on dithered ground -- a hole
  //   light  white disc with a heavy black rim
  //   dark   solid black
  screen.target().fill(fui::makeRect(left, lipTop, kGridWidth, static_cast<int16_t>(kGridHeight + lip)),
                       fui::Paint::dither(fui::Color::LightGray));

  // The lip: the board's own top edge, where a disc goes in, with your disc
  // waiting over every column that will still take one.
  //
  // It is INSIDE the frame and on the same dithered slab, and that is the whole
  // point. Drawn on white paper above the board, a light disc is a ring on
  // nothing and reads as one more empty hole; on the slab it reads as the same
  // piece it will be a moment later. It is also physically what a Connect Four
  // frame looks like: you drop from the top of the object, not from the air
  // above it.
  //
  // A FULL column DIMS. It does not vanish.
  //
  // It used to vanish, and that is the design language's named failure: a
  // control that disappears takes its space with it, so you cannot tell whether
  // the action is unavailable or whether you misremembered the screen. Six
  // rings and a gap in a row of seven is not something anyone notices. The
  // defence written here -- that the column below is visibly full to the brim --
  // does not survive a full column whose topmost disc is light, which is the
  // quietest state on the board.
  //
  // Dither is this fork's disabled treatment, so the slot keeps its size and
  // its place and loses only its solidity.
  if (lip > 0) {
    for (int column = 0; column < c4::kColumns; ++column) {
      const int16_t cx = static_cast<int16_t>(left + column * kCell + kCell / 2);
      const int16_t cy = static_cast<int16_t>(lipTop + lip / 2);
      if ((waiting & (1 << column)) != 0) {
        toybox::ring(screen, cx, cy, kLipRadius, kRingWeight, fui::Color::Black,
                     seat == c4::kDark ? fui::Color::Black : fui::Color::White);
        continue;
      }
      screen.target().fill(fui::makeRect(static_cast<int16_t>(cx - kLipRadius), static_cast<int16_t>(cy - kLipRadius),
                                         static_cast<int16_t>(kLipRadius * 2), static_cast<int16_t>(kLipRadius * 2)),
                           fui::Paint::dither(fui::Color::DarkGray));
    }
    // And a rule under it, so the lip is not mistaken for a seventh row of
    // places you could win in.
    screen.target().fill(fui::makeRect(left, static_cast<int16_t>(top - toybox::kRule), kGridWidth, toybox::kRule),
                         fui::Paint::solid(fui::Color::Black));
  }

  for (int column = 0; column < c4::kColumns; ++column) {
    for (int row = 0; row < c4::kRows; ++row) {
      const int drawRow = c4::kRows - 1 - row;
      const int16_t cx = static_cast<int16_t>(left + column * kCell + kCell / 2);
      const int16_t cy = static_cast<int16_t>(top + drawRow * kCell + kCell / 2);
      toybox::disc(screen, cx, cy, kDiscRadius, fui::Color::White);
      const uint8_t cell = game.cell[column][row];
      if (cell != c4::kEmpty) drawDisc(screen, cx, cy, cell);
    }
  }

  // The last disc played, DOTTED inside. Forty-two identical discs do not say
  // what just changed, and "what did they do" is the first thing a player asks
  // on their turn.
  //
  // A dot, not a ring. The ring was radius 12 weight 3 in contrasting ink,
  // which is Checkers' king mark pixel for pixel: the same glyph meaning "this
  // piece is promoted" on one board of discs and "this was the last move" on
  // the one next door. It also read differently by colour -- concentric rings
  // on a light disc are a bullseye, heavier than the same mark on a dark one --
  // so the weight of the mark depended on whose move it was. A filled dot is
  // one shape at one weight on both.
  //
  // Suppressed on the result screen, a draw included: nothing is going to
  // change again, so "what just changed" has nothing left to say.
  if (markLine == nullptr && game.lastColumn != c4::kNoColumn) {
    const int drawRow = c4::kRows - 1 - game.lastRow;
    const bool solid = game.cell[game.lastColumn][game.lastRow] == c4::kDark;
    toybox::disc(screen, static_cast<int16_t>(left + game.lastColumn * kCell + kCell / 2),
                 static_cast<int16_t>(top + drawRow * kCell + kCell / 2), 8,
                 solid ? fui::Color::White : fui::Color::Black);
  }

  // The four that ended it, as a LINE through their centres.
  //
  // Corner brackets were the obvious reuse and they were wrong twice over. On
  // four CONTIGUOUS cells the arms abut into an 8px rung and the top rail runs
  // 62% black across 256px: not four marks, a ladder, and the loudest object on
  // the screen. Worse, an arm sits 4px inside a cell edge and a disc has 6px of
  // clearance, so 2px of paper separates a black disc from a black cage -- at
  // 220ppi they fuse and you can no longer count four discs. The mark ate the
  // thing it was marking, on both colours.
  //
  // A line also says the right word. Chess and Checkers bracket ONE isolated
  // thing; this is a game named for connecting four, and the shape that means
  // that is a line. Drawn in the opposite ink to the winning discs, which are
  // all one colour by definition, so it reads on either.
  if (markLine != nullptr && markLine[0] >= 0 && markLine[c4::kLine - 1] >= 0) {
    const int firstColumn = markLine[0] / c4::kRows;
    const int firstRow = markLine[0] % c4::kRows;
    const int lastColumn = markLine[c4::kLine - 1] / c4::kRows;
    const int lastRow = markLine[c4::kLine - 1] % c4::kRows;
    const fui::Paint ink =
        fui::Paint::solid(game.cell[firstColumn][firstRow] == c4::kDark ? fui::Color::White : fui::Color::Black);

    const int16_t x0 = static_cast<int16_t>(left + firstColumn * kCell + kCell / 2);
    const int16_t y0 = static_cast<int16_t>(top + (c4::kRows - 1 - firstRow) * kCell + kCell / 2);
    const int16_t x1 = static_cast<int16_t>(left + lastColumn * kCell + kCell / 2);
    const int16_t y1 = static_cast<int16_t>(top + (c4::kRows - 1 - lastRow) * kCell + kCell / 2);

    // A fill-only target has no line primitive, so this is a square brush
    // stepped along the run. The brush is what gives it weight on the
    // diagonals, where a single-pixel trace would be a third as heavy as the
    // same line drawn horizontally.
    constexpr int16_t kWeight = 7;
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int steps = dx > dy ? dx : dy;
    for (int step = 0; step <= steps; ++step) {
      const int16_t x = static_cast<int16_t>(x0 + (x1 - x0) * step / (steps == 0 ? 1 : steps));
      const int16_t y = static_cast<int16_t>(y0 + (y1 - y0) * step / (steps == 0 ? 1 : steps));
      screen.target().fill(
          fui::makeRect(static_cast<int16_t>(x - kWeight / 2), static_cast<int16_t>(y - kWeight / 2), kWeight, kWeight),
          ink);
    }
  }
}

}  // namespace

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "CONNECT FOUR";
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 2, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // The capsule first, so the board can never grow into it.
  fui::ButtonProps status;
  status.label = model.yourTurn ? "YOUR DROP" : "THEIR DROP";
  status.action = fui::NO_ACTION;
  status.borderEdges = fui::EdgesNone;
  const fui::Rect capsule = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  screen.button(
      status, model.opponentName != nullptr ? linkui::withOpponentFace(screen, capsule, model.opponentName) : capsule);

  const fui::DeviceContext device = screen.device();
  // The lip carries your waiting discs only on your turn. On theirs it is plain
  // slab, which says "not yours" with the same ink the capsule spends words on.
  drawGrid(screen, boardGridTop(device), kSlotHeight, model.game, model.yourTurn ? model.open : 0, model.seat, nullptr);

  // What each side has LEFT to play, in the band the anchored board frees up.
  //
  // Twenty-one discs a colour is the physical game, and the count is real
  // information late on: a player who can see they have four left plays
  // differently. Remaining rather than played, for the same reason Checkers
  // shows remaining pieces -- two strips of what has been used are both empty
  // exactly when the game begins.
  //
  // Yours is the LOWER strip, nearest you, and it also answers the question the
  // capsule never did: which colour am I. Before this, nothing on any screen
  // said so, and in a link game the coin toss decides your seat, so a player
  // could not even fall back on "I am always light".
  {
    // Pinned above the CAPSULE, not under the board. Anchoring the board alone
    // just moved the white from two lumps into one big one at the bottom; two
    // anchored blocks with the slack between them is what Chess does and what
    // the space actually wants.
    const int16_t left = gridLeft(device);
    int played[2] = {0, 0};
    for (int column = 0; column < c4::kColumns; ++column) {
      for (int row = 0; row < c4::kRows; ++row) {
        const uint8_t cell = model.game.cell[column][row];
        if (cell == c4::kLight)
          ++played[0];
        else if (cell == c4::kDark)
          ++played[1];
      }
    }

    // Each tray row led by its count, so the tray reads as a number with a
    // picture rather than as texture. The dots stay as the honest
    // disc-per-disc picture of what is left in the box.
    constexpr int16_t kPitch = 18;
    constexpr int16_t kSmall = 8;
    const int16_t rowPitch = static_cast<int16_t>(kSmall * 2 + toybox::kGutter);
    const int16_t bandTop = static_cast<int16_t>(device.height - toybox::kMargin - toybox::kPillHeight -
                                                 toybox::kGutter * 2 - rowPitch * 2);
    for (int strip = 0; strip < 2; ++strip) {
      // Strip 0 is theirs, nearest the board; strip 1 is yours, nearest you.
      const uint8_t side = strip == 0 ? c4::other(model.seat) : model.seat;
      const int held = kDiscsPerSide - played[side == c4::kLight ? 0 : 1];
      const int16_t cy = static_cast<int16_t>(bandTop + strip * rowPitch + kSmall);
      char lead[8];
      std::snprintf(lead, sizeof(lead), "%d", held);
      fui::TextStyle label;
      label.font = toybox::kTileFont;
      label.align = fui::TextAlign::Left;
      screen.target().text(fui::makeRect(left, static_cast<int16_t>(cy - 11), 40, 22), lead, label);
      for (int i = 0; i < held; ++i) {
        toybox::ring(screen, static_cast<int16_t>(left + 44 + kSmall + i * kPitch), cy, kSmall, 2, fui::Color::Black,
                     side == c4::kDark ? fui::Color::Black : fui::Color::White);
      }
    }
  }
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const bool won = (model.seat == c4::kLight && model.outcome == c4::Outcome::LightWins) ||
                   (model.seat == c4::kDark && model.outcome == c4::Outcome::DarkWins);

  // Name them, if they have a name. They had a face and a name on the capsule
  // for the whole game and became an anonymous "THEY" at the one moment it
  // mattered: opponentName was carried into this model and never read.
  char headline[40];
  if (model.outcome != c4::Outcome::Draw && !won && model.opponentName != nullptr) {
    std::snprintf(headline, sizeof(headline), "%s WINS", model.opponentName);
  } else {
    std::snprintf(headline, sizeof(headline), "%s",
                  model.outcome == c4::Outcome::Draw ? "A DRAW" : (won ? "YOU WIN" : "THEY WIN"));
  }

  fui::HeaderProps header;
  header.title = headline;
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  screen.button(done, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  screen.button(again, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  // The final board, with the four that ended it marked.
  //
  // Not a score. Connect Four has no score, and the question a player has at
  // the end is "where was it" -- which four, and how long had it been there.
  // A number would be an invented answer to a question nobody asked.
  // Drawn WITHOUT the slot row's space: there is no move to make, so reserving
  // room for one would push the grid into the two buttons this screen gained.
  // The board moving is right here -- the header, the furniture and the marks
  // all changed too, so this is a different screen rather than the same one
  // twitching.
  const fui::Rect area = screen.body();
  const int16_t slack = static_cast<int16_t>(area.height - kGridHeight - toybox::kBoardFrame * 2);
  drawGrid(screen, static_cast<int16_t>(area.y + (slack > 0 ? slack / 2 : 0) + toybox::kBoardFrame), 0, model.game, 0,
           model.seat, model.outcome == c4::Outcome::Draw ? nullptr : model.line);
}

}  // namespace c4ui
