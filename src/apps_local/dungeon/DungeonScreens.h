#pragma once

// The dungeon's screens, as freestanding builders. See ToyboxScreen.h for why
// screens are written this way: a free function over a plain model, drawing
// into a DrawTarget it was handed, so host-tests/ui/ can run them with a fake
// target and no device.

#include "../ui/ToyboxScreen.h"
#include "DungeonCore.h"

namespace dungeonui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  // The whole board is one target. Sixty-four hit rects would not fit in the
  // interaction buffer (24), and they would be the wrong shape anyway: the
  // Layout below already knows where every cell was drawn, so resolving a tap
  // through it is the same geometry rather than a second copy of it.
  ActionBoard = 1,
  ActionButton = 2,
  ActionPick = 3,  // value is a puzzle index
  ActionPage = 4,  // value is -1 or +1
};

enum Button : int {
  ButtonPlay = 0,
  ButtonChoose = 1,
  ButtonReset = 2,
  ButtonMenu = 3,
  ButtonNext = 4,
  ButtonGuide = 5,
  ButtonGuideBack = 6,
  ButtonGuideNext = 7,
};

// Where the board was drawn. Filled by the builder as it draws and read by the
// activity to turn a tap into a cell -- hit-testing sharing the geometry that
// placed the pixels, which is the rule three separate bugs in this project came
// from breaking.
struct Layout {
  fui::Rect board = {};  // the play area, excluding the clue lane
  int16_t cell = 0;
  int16_t lane = 0;
  int16_t size = 0;

  // The cell under logical (x, y), or false if the tap missed the board.
  bool cellAt(int x, int y, int& row, int& col) const;
};

struct BoardModel {
  const dungeon::Board* board = nullptr;
  int solvedCount = 0;
  int total = 0;
  bool solved = false;
};

struct MenuModel {
  const char* dungeonName = "";
  int tier = 0;
  int slot = 0;
  int solvedCount = 0;
  int total = 0;
  bool hasProgress = false;
  // Which dungeons are done, drawn as the grid the original lays them out in.
  // The menu had a screenful of white space under the name, and the fork's rule
  // is that decoration has to carry data: this is the player's own record, so a
  // screenshot of it is different on every device.
  const dungeon::Progress* progress = nullptr;
};

// Where the picker drew the campaign grid, for the variants that have one.
// Same discipline as the board's Layout: filled while drawing, read to resolve
// a tap, so the hit region cannot drift from the pixels. Sixty-five separate
// hit rects would not fit the interaction buffer anyway.
struct PickerLayout {
  fui::Rect grid = {};
  int16_t cell = 0;
  int16_t gap = 0;
  int16_t cols = 0;
  int16_t rows = 0;

  // The dungeon index under logical (x, y), or -1.
  int indexAt(int x, int y) const;
};

struct PickerModel {
  // The tier being shown, 0 for the tutorial and 1..8 for the campaign.
  int tier = 1;
  int firstIndex = 0;
  int count = 0;
  int current = -1;
  // Bit i set when kPuzzles[firstIndex + i] is solved.
  uint16_t solved = 0;
  int solvedCount = 0;
  int total = 0;
  // The whole campaign: this screen shows all of it at once.
  const dungeon::Progress* progress = nullptr;
  // The dungeon PLAY would open, named at the foot of the screen so the map
  // answers "where am I" as well as "where can I go".
  int nextIndex = 0;
};

// The adventurer's guide: the rules, one at a time, each drawn rather than
// described. The last page hands you the tutorial dungeon.
//
// This exists because the tutorial used to be a sixty-fifth cell on the map,
// which said it was just another dungeon. It is not: it is where the rules are
// explained, and the rules are the whole game.
struct GuideModel {
  int page = 0;
  int pageCount = 0;
};

struct WinModel {
  const char* dungeonName = "";
  // The dungeon just finished, drawn as the map the player has this moment
  // finished carving. Same rule as the menu's grid: the payoff screen shows
  // their own work rather than an ornament.
  const dungeon::Puzzle* cleared = nullptr;
  int solvedCount = 0;
  int total = 0;
  bool moreToPlay = true;
};

// The board. Fills `layout` as it draws.
void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout);

// The front door, following the pattern in docs/design-language.md.
void buildMenu(toybox::Screen& screen, const MenuModel& model);

// The map: every dungeon at once, a tier per row, laid out the way the
// original arranges them. Fills `layout` as it draws, so a tap resolves through
// the same geometry that placed the cells.
void buildPicker(toybox::Screen& screen, const PickerModel& model, PickerLayout& layout);

// One page of the guide. Page pageCount - 1 offers the tutorial instead of a
// next page.
void buildGuide(toybox::Screen& screen, const GuideModel& model);

// How many pages the guide has. Lives here so the activity can bound its page
// counter without knowing what is on them.
int guidePageCount();

// The walls page `page` shows, for the host test that checks the guide teaches
// the real puzzle: every page must be a subset of the tutorial's solution, and
// the last page must be it exactly. False if the page is out of range.
bool guidePageWalls(int page, uint64_t& walls);

// The payoff.
void buildWin(toybox::Screen& screen, const WinModel& model);

}  // namespace dungeonui
