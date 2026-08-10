#pragma once

// Minesweeper on screen. Freestanding builders over plain models: these know
// FreeInkUI and Toybox tokens and nothing else, so host-tests/ui can build every
// screen against a fake draw target and ask what it drew and what it made
// tappable.

#include "../ui/ToyboxScreen.h"
#include "MinesweeperCore.h"
#include "MinesweeperFlow.h"

namespace mineui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  // The two halves of the bottom capsule. Separate actions rather than one
  // toggle, so tapping the tool you are already holding is a no-op instead of
  // silently switching you to the other one.
  ActionHowToNext = 5,
  ActionAgain = 6,
  ActionDone = 7,
  ActionToggleMode = 8,
  // The verdict capsule on a settled board: the door to the stats.
  ActionSeeResult = 9,
};

enum class MenuRow : int { Play = 0, HowTo, Count };

struct MenuModel {
  int selected = -1;
  // The last finished board and the tally, drawn as the front door's ornament.
  bool hasHistory = false;
  minesweeper::Game lastBoard{};
  int wins = 0;
  int losses = 0;
};

struct HowToModel {
  int page = 0;
};

struct BoardModel {
  minesweeper::Game game{};
  // The cell a finger is resting on, or -1. Drawn as a bracket so a hold shows
  // it is being registered before it fires: on a panel this slow, an unmarked
  // hold is indistinguishable from a tap that missed.
  int holdColumn = -1;
  int holdRow = -1;
  // True once the game is settled, which reveals the mines the player never
  // found. The board is still drawn: a finished minefield is the thing you want
  // to look at.
  bool showMines = false;
  // What a tap on a cell does. Flagging used to be reachable only by holding,
  // which is the one gesture of its kind in the whole fork and the only control
  // anywhere in it that nothing on screen mentions. On a panel that repaints in
  // a second there is also no press feedback to learn it from. So the mode is
  // drawn, and a tap on it switches: the board can then say what a tap will do
  // instead of the player having to remember.
  bool flagMode = false;
};

struct ResultModel {
  bool won = false;
  int revealed = 0;
  int flagsRight = 0;
};

// The rect of one cell, and its exact inverse.
//
// The grid is NOT registered as eighty tappable buttons, and cannot be: the
// interaction buffer holds twenty-four. That is not a limit to raise -- it is
// sized for screens made of discrete controls, and a regular grid is not one.
// A grid is hit-tested arithmetically from the same geometry that drew it,
// which costs no interactions at all and is the rule this fork already has:
// hit-testing must be derived from the pixels, never computed a second time.
//
// `cellAt` returns false when the point is outside the board. The pair is
// tested against each other so they cannot drift.
fui::Rect cellRect(const fui::DeviceContext& device, int column, int row);
bool cellAt(const fui::DeviceContext& device, int x, int y, int& column, int& row);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

int howToPages();

}  // namespace mineui
