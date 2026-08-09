#include "ConnectFourScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>

#include "../link/LinkScreens.h"
#include "../ui/ToyboxIcons.h"

namespace c4ui {

namespace {

namespace c4 = connectfour;

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

// The slot row above the grid: seven empty places, one per column, where the
// disc you are about to drop would go in.
//
// It exists because a Connect Four board has no "here" until a disc lands. The
// grid is the history; the slot row is the move, and it is also what makes the
// column read as a target rather than the cell you happen to touch.
constexpr int16_t kSlotHeight = 56;

int16_t gridLeft(const fui::DeviceContext& device) { return static_cast<int16_t>((device.width - kGridWidth) / 2); }

// The assembly is slot row, gutter, frame, grid, frame.
constexpr int16_t kAssemblyHeight = kSlotHeight + toybox::kGutter + toybox::kBoardFrame * 2 + kGridHeight;

// Where the BOARD screen's grid starts, as a pure function of the device.
//
// Pure on purpose. cellRect, columnAt and the drawing all call this, so a tap
// lands on the cell the player is looking at by construction rather than by two
// pieces of arithmetic happening to agree. Deriving it from the laid-out body
// rect instead would give the hit-test a different answer from the drawing the
// moment either is called at a different point in the layout.
//
// Centred between the header and the status capsule, so the slack is spread
// rather than left in one lump at the bottom.
int16_t boardGridTop(const fui::DeviceContext& device) {
  const int16_t top = static_cast<int16_t>(toybox::kHeaderHeight + toybox::kGutter * 2);
  const int16_t bottom = static_cast<int16_t>(device.height - toybox::kMargin - toybox::kPillHeight - toybox::kGutter);
  const int16_t slack = static_cast<int16_t>(bottom - top - kAssemblyHeight);
  const int16_t assembly = static_cast<int16_t>(top + (slack > 0 ? slack / 2 : 0));
  return static_cast<int16_t>(assembly + kSlotHeight + toybox::kGutter + toybox::kBoardFrame);
}

int16_t boardSlotTop(const fui::DeviceContext& device) {
  return static_cast<int16_t>(boardGridTop(device) - toybox::kBoardFrame - toybox::kGutter - kSlotHeight);
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
  fui::HeaderProps header;
  header.title = "CONNECT FOUR";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
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

  // topIndex is zero: this menu is three rows and never scrolls, so the row is
  // always where its index says. Passing it explicitly is the point -- the
  // parameter is required precisely so a list that DOES scroll cannot silently
  // paint its icons against the wrong rows.
  toybox::iconAtRowRight(screen, band, static_cast<int>(MenuRow::PlayNearby), 0, linkui::nearbyMark(),
                         model.selected == static_cast<int>(MenuRow::PlayNearby));
}

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int page = model.page < 0 ? 0 : (model.page >= howToPages() ? howToPages() - 1 : model.page);

  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  fui::ButtonProps next;
  next.label = page + 1 < howToPages() ? "NEXT" : "GOT IT";
  next.action = ActionHowToNext;
  screen.button(next, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  char progress[8];
  std::snprintf(progress, sizeof(progress), "%d/%d", page + 1, howToPages());
  fui::TextStyle counter;
  counter.font = toybox::kSmallFont;
  counter.align = fui::TextAlign::Right;
  screen.target().text(fui::makeRect(area.x, area.y, area.width, 20), progress, counter);

  static const char* const kLines[] = {
      "TAP A COLUMN. YOUR DISC FALLS TO THE LOWEST FREE PLACE IN IT.",
      "FOUR IN A ROW WINS. ACROSS, UP, OR EITHER DIAGONAL.",
      "A FULL COLUMN TAKES NO MORE. A FULL BOARD WITH NO FOUR IS A DRAW.",
  };
  fui::TextStyle body;
  body.font = toybox::kBodyFont;
  body.align = fui::TextAlign::Center;
  body.maxLines = 3;
  screen.target().text(fui::makeRect(area.x, static_cast<int16_t>(area.y + 24), area.width, 130), kLines[page], body);

  // A fragment of the real board at the real size, showing the page. Four wide
  // and two deep, which is exactly enough to hold the thing being taught.
  const fui::DeviceContext device = screen.device();
  const int16_t top = static_cast<int16_t>(area.y + 180);
  const int16_t left = static_cast<int16_t>((device.width - kCell * 4) / 2);
  const fui::Rect frame = fui::makeRect(static_cast<int16_t>(left - toybox::kBoardFrame),
                                        static_cast<int16_t>(top - toybox::kBoardFrame),
                                        static_cast<int16_t>(kCell * 4 + toybox::kBoardFrame * 2),
                                        static_cast<int16_t>(kCell * 2 + toybox::kBoardFrame * 2));
  screen.target().stroke(frame, fui::Paint::solid(fui::Color::Black), toybox::kBoardFrame);
  screen.target().fill(fui::makeRect(left, top, static_cast<int16_t>(kCell * 4), static_cast<int16_t>(kCell * 2)),
                       fui::Paint::dither(fui::Color::LightGray));

  // Page 0: a disc on its way into a column that already holds one, so
  // "falls to the lowest free place" is a picture rather than a claim.
  // Page 1: four in a row, marked the way the result screen marks it.
  // Page 2: a full column beside an empty one.
  static const char* const kBoards[][2] = {
      {"....", ".L.."},          // top row, bottom row; '.' empty
      {"....", "LLLL"},
      {"D...", "D..."},
  };
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 4; ++column) {
      const int16_t cx = static_cast<int16_t>(left + column * kCell + kCell / 2);
      const int16_t cy = static_cast<int16_t>(top + row * kCell + kCell / 2);
      const char c = kBoards[page][row][column];
      toybox::disc(screen, cx, cy, kDiscRadius, fui::Color::White);
      if (c == '.') continue;
      drawDisc(screen, cx, cy, c == 'L' ? c4::kLight : c4::kDark);
      if (page == 1) {
        toybox::bracket(screen,
                        fui::makeRect(static_cast<int16_t>(left + column * kCell),
                                      static_cast<int16_t>(top + row * kCell), kCell, kCell),
                        18, 4);
      }
    }
  }
  if (page == 0) {
    // The disc above the board, in the slot, mid-drop.
    drawDisc(screen, static_cast<int16_t>(left + kCell + kCell / 2),
             static_cast<int16_t>(top - toybox::kBoardFrame - kSlotHeight / 2), c4::kLight);
  }
}

// The grid, its frame, and the discs in it. Shared by the board and the result
// screen so the position cannot be drawn two different ways.
namespace {

void drawGrid(toybox::Screen& screen, const int16_t top, const c4::Game& game, const int* markLine) {
  const int16_t left = gridLeft(screen.device());

  const fui::Rect frame =
      fui::makeRect(static_cast<int16_t>(left - toybox::kBoardFrame), static_cast<int16_t>(top - toybox::kBoardFrame),
                    static_cast<int16_t>(kGridWidth + toybox::kBoardFrame * 2),
                    static_cast<int16_t>(kGridHeight + toybox::kBoardFrame * 2));
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
  screen.target().fill(fui::makeRect(left, top, kGridWidth, kGridHeight), fui::Paint::dither(fui::Color::LightGray));

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

  // The last disc played, ringed inside. Forty-two identical discs do not say
  // what just changed, and "what did they do" is the first thing a player asks
  // on their turn. Inside the disc rather than around the cell, so it cannot be
  // confused with the winning-line mark.
  if (markLine == nullptr && game.lastColumn != c4::kNoColumn) {
    const int drawRow = c4::kRows - 1 - game.lastRow;
    const int16_t cx = static_cast<int16_t>(left + game.lastColumn * kCell + kCell / 2);
    const int16_t cy = static_cast<int16_t>(top + drawRow * kCell + kCell / 2);
    const bool solid = game.cell[game.lastColumn][game.lastRow] == c4::kDark;
    toybox::ring(screen, cx, cy, 12, 3, solid ? fui::Color::White : fui::Color::Black,
                 solid ? fui::Color::Black : fui::Color::White);
  }

  // The four that ended it. Corner marks, so the discs themselves stay legible
  // underneath -- which colour won is the point.
  if (markLine != nullptr) {
    for (int i = 0; i < c4::kLine; ++i) {
      if (markLine[i] < 0) continue;
      const int column = markLine[i] / c4::kRows;
      const int row = markLine[i] % c4::kRows;
      const int drawRow = c4::kRows - 1 - row;
      toybox::bracket(screen,
                      fui::makeRect(static_cast<int16_t>(left + column * kCell),
                                    static_cast<int16_t>(top + drawRow * kCell), kCell, kCell),
                      20, 4);
    }
  }
}

}  // namespace

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "CONNECT FOUR";
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
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

  // The slot row. On your turn every open column holds your disc, waiting: the
  // board shows what a tap would do before you make it, which is the only
  // preview an e-ink screen can offer without a hover it does not have.
  //
  // A FULL column shows nothing at all rather than a crossed-out mark. There is
  // no ambiguity to resolve -- the column below is visibly full to the brim --
  // and a negative mark would be the only ink on this screen that says "no".
  if (model.yourTurn) {
    const int16_t left = gridLeft(device);
    const int16_t top = boardSlotTop(device);
    for (int column = 0; column < c4::kColumns; ++column) {
      if ((model.open & (1 << column)) == 0) continue;
      toybox::ring(screen, static_cast<int16_t>(left + column * kCell + kCell / 2),
                   static_cast<int16_t>(top + kSlotHeight / 2), 22, 3, fui::Color::Black,
                   model.seat == c4::kDark ? fui::Color::Black : fui::Color::White);
    }
  }

  drawGrid(screen, boardGridTop(device), model.game, nullptr);
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const bool won = (model.seat == c4::kLight && model.outcome == c4::Outcome::LightWins) ||
                   (model.seat == c4::kDark && model.outcome == c4::Outcome::DarkWins);

  fui::HeaderProps header;
  header.title = model.outcome == c4::Outcome::Draw ? "A DRAW" : (won ? "YOU WIN" : "THEY WIN");
  header.borderEdges = fui::EdgesNone;
  screen.header(header);
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
  drawGrid(screen, static_cast<int16_t>(area.y + (slack > 0 ? slack / 2 : 0) + toybox::kBoardFrame), model.game,
           model.outcome == c4::Outcome::Draw ? nullptr : model.line);
}

}  // namespace c4ui
