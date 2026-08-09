#pragma once

// Connect Four on screen. Freestanding builders over plain models.

#include "../ui/ToyboxScreen.h"
#include "ConnectFourCore.h"
#include "ConnectFourFlow.h"

namespace c4ui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionHowToNext = 2,
  ActionAgain = 3,
  ActionDone = 4,
};

enum class MenuRow : int { Play = 0, PlayNearby, HowTo, Count };

struct MenuModel {
  const char* nearbyName = nullptr;
  int selected = -1;
};

struct HowToModel {
  int page = 0;
};

struct BoardModel {
  connectfour::Game game{};
  // Which columns will take a disc, from the rules rather than recomputed here.
  uint8_t open = 0;
  // Which colour this device plays. Light in a solo game; the coin toss
  // decides it in a match.
  uint8_t seat = connectfour::kLight;
  bool yourTurn = true;
  const char* opponentName = nullptr;
};

struct ResultModel {
  // The final board. The result screen shows the position, not a score:
  // Connect Four has no score, and the question at the end is "where was it".
  connectfour::Game game{};
  connectfour::Outcome outcome = connectfour::Outcome::Draw;
  uint8_t seat = connectfour::kLight;
  // The four cells that ended it, as flat indices, or -1 when it was a draw.
  // Taken from the rules' own search, so the marked cells ARE the winning line
  // rather than a second opinion about where it was.
  int line[connectfour::kLine] = {-1, -1, -1, -1};
  const char* opponentName = nullptr;
};

// A cell's rect, and the inverse a tap goes through. Forty-two cells against a
// twenty-four slot interaction buffer, so the grid is hit-tested arithmetically
// from the geometry that drew it rather than registered cell by cell.
//
// Rank 0 is the BOTTOM row, matching the rules, and the drawing flips it.
// Neither takes the seat: unlike a checkers board, a Connect Four grid looks
// the same from both ends, and flipping it would only move the discs around
// for no gain.
fui::Rect cellRect(const fui::DeviceContext& device, int column, int row);
// The slot above a column, which is where a drop is aimed.
fui::Rect slotRect(const fui::DeviceContext& device, int column);
// Which column a tap falls in, or kNoColumn. The whole column is the target,
// slot and grid alike: a seven-wide row of 64px strips is the most forgiving
// target this panel can offer, and aiming at a single 64px cell would be the
// only hard tap in the game.
int columnAt(const fui::DeviceContext& device, int x, int y);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

int howToPages();

}  // namespace c4ui
