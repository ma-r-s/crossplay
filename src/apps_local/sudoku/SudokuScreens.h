#pragma once

// Sudoku on screen. Freestanding builders over plain models: these know
// FreeInkUI and Toybox tokens and nothing else, so host-tests/ui can build every
// screen against a fake draw target and ask what it drew and what it made
// tappable.

#include "../ui/ToyboxScreen.h"
#include "SudokuFlow.h"
#include "SudokuGame.h"

namespace sudokuui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  // The headline: the largest thing on the front door is also the commonest tap.
  ActionPlay = 2,
  ActionHowToNext = 3,
  ActionUndo = 4,
  ActionHint = 5,
  // The status capsule. Inert while solving; the door to the stats once the
  // grid is finished.
  ActionSeeResult = 6,
  ActionAgain = 7,
  ActionDone = 8,
  // A level chosen straight off the front door's ladder, value = the Level.
  ActionPickLevel = 9,
};

enum class MenuRow : int { Level = 0, HowTo, Count };

struct MenuModel {
  sudoku::Level level = sudoku::Level::Easy;
  // A puzzle left half solved, drawn as the ornament and resumed by the
  // headline. Without one the headline starts a fresh puzzle instead.
  bool hasGame = false;
  sudoku::Game game{};
  sudoku::Record record{};
  int selected = -1;
};

struct HowToModel {
  int page = 0;
};

struct BoardModel {
  sudoku::Game game{};
  // The cell a finger is resting on, or kNoCell. Drawn as a bracket so a hold
  // is visibly being registered before it fires: on a panel this slow, an
  // unmarked hold is indistinguishable from a tap that missed.
  int holdCell = sudoku::kNoCell;
  // True while a puzzle is being carved. The grid is empty then, and the
  // capsule says so rather than the app looking frozen.
  bool generating = false;
  // What the capsule says instead of the count: the rule a hint cited, or that
  // one of the player's own digits disagrees with the answer.
  const char* notice = nullptr;
};

struct ResultModel {
  // The grid you just finished, drawn as the ornament: clues as full squares
  // and your own digits as small ones, so the picture is how much of it was
  // yours. Different every time, and identical on nobody else's device.
  sudoku::Game game{};
  sudoku::Level level = sudoku::Level::Easy;
  sudoku::Technique hardest = sudoku::Technique::None;
  uint32_t elapsedMs = 0;
  uint32_t bestMs = 0;
  bool newBest = false;
  int hintsUsed = 0;
  int clues = 0;
  int solvedAtThisLevel = 0;
};

// The grid's geometry, and its exact inverse.
//
// The 81 cells are NOT registered as tappable controls, and cannot be: the
// interaction buffer holds twenty-four. That is not a limit to raise. It is
// sized for screens made of discrete controls, and a regular grid is not one,
// so a grid is hit-tested arithmetically from the same numbers that drew it.
// The digit pad is a regular grid too and is handled the same way, which leaves
// the board screen spending three interactions in total.
//
// Each `...At` returns false outside its own area. The pairs are tested against
// each other in host-tests/ui so they cannot drift apart.
fui::Rect cellRect(const fui::DeviceContext& device, int cell);
bool cellAt(const fui::DeviceContext& device, int x, int y, int& cell);
fui::Rect padKeyRect(const fui::DeviceContext& device, int digit);
bool padKeyAt(const fui::DeviceContext& device, int x, int y, int& digit);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

int howToPages();

// "12:04", or "1:02:03" past the hour. Shared so the result screen and the
// front door's record line cannot format the same number two ways.
void formatClock(uint32_t ms, char* out, int size);

}  // namespace sudokuui
