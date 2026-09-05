#pragma once

// The shelf's remembered position, as a pure parse/format pair.
//
// Split out of Shelf.cpp so the file format can be tested on a host with no
// card and no device: the parse is the part that has to survive a truncated
// write, a file written by an older firmware, and a game that has since been
// renamed or removed. See host-tests/shelfstate/.
//
// The file is /.crosspoint/shelf.cfg and has one or two lines:
//
//   <lastFolder> <resumeRow0> <resumeRow1> ...
//   <title of the item that was open>
//
// The second line is what wake reads to return to the game you were in; it is
// absent when nothing was open, and absent in every file written before that
// existed. An older firmware reading a newer file stops after the first line
// and is unaffected, which is why the title went on a line of its own rather
// than at the end of the first.
//
// The item is keyed by TITLE and not by its row index. Indices move whenever a
// game is added or removed, and the file outlives firmware updates -- an index
// would silently resume into a different game.
//
// A resume row is a row and not a title for the opposite reason: it stands for a
// PAGE rather than for a game, and a page is a position in the list. See
// shelfui::rowForPage().

#include <cstddef>

namespace shelf {

// "KNUCKLEBONES" is the longest title in the registry today. Shelf.cpp
// static_asserts every title against this, so a longer one does not build.
constexpr size_t MAX_ITEM_TITLE = 24;

// Two folders today (GAMES, APPS). A third needs only this raised.
constexpr int MAX_FOLDERS = 4;

struct State {
  int lastFolder = -1;  // shelf row Home lands on, -1 for none
  // Per folder, the row it reopens on -- which is to say the page it reopens on,
  // stored as a row. Written by the two things that leave a folder standing
  // somewhere: opening an item (that item's row) and turning the page (the new
  // page's first row). It is where you WERE, not what you last played.
  int resumeRow[MAX_FOLDERS] = {};
  char openTitle[MAX_ITEM_TITLE + 1] = {};  // item open when the device slept; empty for none
};

constexpr size_t constexprLength(const char* s) {
  size_t n = 0;
  while (s[n] != '\0') ++n;
  return n;
}

// Parse `text` (NUL-terminated file contents) into `out`.
//
// Returns false and leaves `out` untouched when the first line is not a
// complete position, so a truncated or corrupt file keeps the caller's
// defaults rather than half of them. `itemLimits[i]` is the highest valid row
// of folder i as the registry stands NOW, not as it stood when the file was
// written: a game removed since then would otherwise select a row that no
// longer exists. Pinning to the last row rather than to the first is the same
// choice shelfui::resumeRowFor() makes and for the same reason -- a folder that
// has shrunk under you reopens on its last page, which is nearer where you were
// than the top is. A title too long to hold is dropped rather than truncated --
// a truncated title matches no item, and dropping says so.
bool parseState(const char* text, int folderCount, const int* itemLimits, State& out);

// Render `state` into `out`. Returns the byte count written (excluding the
// terminator), or 0 if it did not fit, in which case `out` is not usable.
size_t formatState(const State& state, int folderCount, char* out, size_t outSize);

}  // namespace shelf
