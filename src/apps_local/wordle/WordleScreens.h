#pragma once

// Wordle's screens: free functions over plain models, so host-tests/ui can
// build them without a device. The archive calendar and the download screen
// are Connections' own (connectionsui::buildCalendar, buildImport), drawn in
// its faces, because Wordle keeps Connections' shape.

#include <Icon.h>

#include "../ui/ToyboxScreen.h"
#include "WordleCore.h"

namespace wordleui {

namespace fui = freeink::ui;
using wordle::Mark;

// Above connectionsui's ids, which the reused calendar registers.
enum : fui::ActionId {
  ActionKeyboard = 101,  // one region for every key; keyAt() says which
  ActionMenu = 103,      // value: 0 today, 1 how to play, 2 archive, 3 get puzzles
  ActionArchive = 104,   // a finished game: the calendar, on its month
  ActionNext = 105,      // a finished game: the newest day not yet finished
};

// Keys that are not letters.
constexpr char kEnter = '\n';
constexpr char kErase = '\b';

// Where the keys are, computed once and used by both the drawing and the tap,
// so a key cannot be drawn in one place and hit in another.
struct KeyboardLayout {
  int16_t top = 0;
  int16_t keyW = 0;
  int16_t keyH = 0;
  int16_t gap = 0;
  int16_t wide = 0;         // ENTER and delete
  int16_t rowLeft[3] = {};  // x of each row's first key
};

KeyboardLayout keyboardLayout(const fui::Rect& safe, int16_t top);
// The key under (x, y): 'A'-'Z', kEnter, kErase, or 0 for a gap or outside.
char keyAt(const KeyboardLayout& layout, int x, int y);

struct GameModel {
  const wordle::Game* game = nullptr;
  const char* date = nullptr;     // "SEP 28", the header's right label while playing
  const char* message = nullptr;  // one line under the header, or null
  bool canNext = false;           // a finished game: is there a day left to play
};

// Returns the keyboard it drew, for keyAt(); an empty one once the game is
// over, when the keyboard gives way to the result and ARCHIVE / NEXT.
KeyboardLayout buildGame(toybox::Screen& screen, const GameModel& model);

struct MenuModel {
  const char* date = nullptr;  // "28 SEP 2026", or null before the first download
  const char* state = "NOT STARTED";
  wordle::Stats stats;
  int puzzles = 0;
  bool upToDate = false;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildHowTo(toybox::Screen& screen);

}  // namespace wordleui
