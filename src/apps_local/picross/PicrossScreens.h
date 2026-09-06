#pragma once

// Picross on screen. Freestanding builders over plain models: these know
// FreeInkUI and Toybox tokens and nothing else, so host-tests/ui can build
// every screen against a fake draw target and ask what it drew and what it made
// tappable. See ToyboxScreen.h.

#include "../ui/ToyboxScreen.h"
#include "PicrossCore.h"

namespace picrossui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  // The whole grid is one target: a 10x10 is a hundred cells against a
  // twenty-four slot interaction buffer, so it is hit-tested arithmetically
  // through Layout below, from the same geometry that drew it -- the rule this
  // fork already keeps (Minesweeper does the same).
  ActionBoard = 1,
  ActionButton = 2,
  ActionPick = 3,  // value is a puzzle index, or -1 to resolve through the picker
  ActionMode = 4,  // value is Mode: which input mode a tap selects
  ActionPage = 5,  // value is a page index the picker should jump to
};

enum Button : int {
  ButtonPlay = 0,
  ButtonRestart = 1,
  ButtonPuzzles = 2,
  ButtonNext = 3,
};

// The active input mode. FILL is the committing action (a wrong fill LOCKS as a
// mistake); MARK is a free annotation. The two physical side keys select these
// directly (Up = FILL, Down = MARK) and the on-screen capsule mirrors them, so
// the player always knows which they are in without reading the panel.
enum Mode : int { ModeFill = 0, ModeMark = 1 };

// Where the board grid was drawn. Filled by the builder as it lays out the
// cells, read by the activity to turn a tap into a cell -- hit-testing sharing
// the geometry that placed the pixels.
struct Layout {
  fui::Rect board = {};  // the NxN cell grid, excluding the clue gutters
  int16_t cell = 0;
  int16_t size = 0;

  // The cell under logical (x, y), or false if the tap missed the grid.
  bool cellAt(int x, int y, int& row, int& col) const;
};

struct BoardModel {
  const picross::Board* board = nullptr;
  int mode = ModeFill;  // which input mode is active, drawn on the capsule
  int solvedCount = 0;
  int total = 0;
};

// One tile on the picker. A solved puzzle shows its finished picture and name;
// an unsolved one shows only its size and number, because the reveal is the
// whole reward and a thumbnail of the answer would spoil it.
struct MenuModel {
  const picross::Progress* progress = nullptr;
  // The puzzle PLAY would open: the one you last tapped, or the next unsolved.
  int selectedIndex = 0;
  // The in-progress puzzle (touched, not solved), or -1. Drawn with a resume
  // affordance so RESUME is discoverable from the grid.
  int inProgressIndex = -1;
  int solvedCount = 0;
  int total = 0;
  // RESUME rather than PLAY on the selected tile, when it is the in-progress one.
  bool hasProgress = false;
  // Which page of the picker to draw. The activity owns it and the picker
  // clamps it, reporting back what it actually drew in PickerLayout.
  int page = 0;
  // Ignore `page` and open on the page holding `selectedIndex`. Set when the
  // picker appears, so the highlighted tile is on screen without the activity
  // having to know how many tiles a page holds -- that number is derived from
  // the panel inside buildMenu, and a second copy of it elsewhere is a copy
  // that goes stale the next time the layout moves.
  bool followSelection = false;
};

// Where the picker drew its tile grid. Same discipline as the board's Layout:
// filled while drawing, read to resolve a tap.
struct PickerLayout {
  fui::Rect grid = {};
  int16_t cell = 0;
  int16_t gap = 0;
  int16_t cols = 0;
  int16_t rows = 0;
  int16_t count = 0;         // tiles actually drawn on this page
  int16_t firstIndex = 0;    // global puzzle index of the first tile on the page
  int16_t rowHeight = 0;     // list layouts: the per-row pitch (0 for grids)
  int16_t pageCount = 1;     // total pages, for the activity to clamp paging
  int16_t pageOnScreen = 0;  // the page this layout drew

  // The puzzle index under logical (x, y), or -1.
  int indexAt(int x, int y) const;
};

struct WinModel {
  const picross::Puzzle* cleared = nullptr;
  int mistakes = 0;
  int solvedCount = 0;
  int total = 0;
  bool moreToPlay = true;
};

// The board. Fills `layout` as it draws.
void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout);

// The front door: the picker grid. Fills `layout` for the tap resolver.
void buildMenu(toybox::Screen& screen, const MenuModel& model, PickerLayout& layout);

// The payoff: the finished picture, revealed clean and named, graded by
// mistakes. A puzzle nobody has named yet is revealed without a name band.
void buildWin(toybox::Screen& screen, const WinModel& model);

// One page step, clamped to [0, pageCount). `delta` is -1 or +1.
//
// A free function, and the ONLY reason it is one is that it can be tested. The
// two physical side keys page the picker, and the simulator never runs
// InputManager and never sees a button at all -- so nothing about the key
// press itself is provable off-device. What IS provable is the decision the
// press feeds: this. Clamping rather than wrapping, because the keys are the
// case's moulded page-turn keys and the reader they were made for stops at the
// ends of a book.
int stepPage(int page, int pageCount, int delta);

}  // namespace picrossui
