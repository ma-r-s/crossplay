#pragma once

// Which shelf items the folders do not show, as a pure set plus a parse/format
// pair.
//
// Split out of Shelf.cpp for the same reason ShelfState is: the file outlives
// the firmware that wrote it, so the part that has to survive a truncated
// write, a game renamed since, and a card written by a newer build is the part
// a host test must be able to reach without a device. See
// host-tests/shelfhidden/.
//
// The file is /.crosspoint/shelf-hidden.cfg and is one item TITLE per line:
//
//   MINESWEEPER
//   XKCD
//
// Only the hidden ones are written, so the common file is empty or absent and
// the common parse is nothing at all.
//
// Keyed by TITLE and not by row index, for the reason the wake-resume already
// learned: indices move whenever a game is added or removed, and this card
// outlives firmware updates. An index would hide a DIFFERENT game after the
// next release, silently, and the person it happened to would have no way to
// tell what had gone wrong.
//
// A title in the file that matches no item today is kept rather than dropped.
// That is what makes a downgrade safe: a build that does not know about a game
// must not be the reason its setting is forgotten by the build after it.
//
// THE WHOLE FORMAT FAILS OPEN. A file that is missing, empty, truncated
// mid-line, or full of junk hides nothing, because hiding is what the lines
// SAY and a line that says nothing recognisable hides nothing recognisable.
// That direction is the one that matters: a game that appears when it should
// not is a list a person can fix in four taps, and a game that vanishes for a
// reason held in a file they cannot see is a device that broke.

#include <cstddef>
#include <string>
#include <vector>

// For MAX_ITEM_TITLE, which is part of this file's contract and not an
// implementation detail: a line longer than a title can be is dropped, and how
// long a title can be is the other file's rule.
#include "ShelfState.h"

namespace shelf {

// The titles the shelf is hiding. Order is insertion order and means nothing;
// this is a set with a stable file representation, not a list.
class HiddenSet {
 public:
  bool contains(const char* title) const;

  // Hide or show `title`. Returns true when the set actually changed, which is
  // what keeps a no-op tap from writing to a card with a finite erase count.
  bool set(const char* title, bool hidden);

  size_t size() const { return titles_.size(); }
  const std::string& at(const size_t index) const { return titles_[index]; }

 private:
  std::vector<std::string> titles_;
};

// The most lines parseHidden will take from one file. Far more than a registry
// can hold, and the only thing it is really bounding is a corrupt card: this is
// read into RAM on a device with 8MB of it.
constexpr size_t MAX_HIDDEN = 128;

// Replace `out` with the titles in `text` (NUL-terminated file contents).
//
// Everything is dropped quietly, because there is nothing a person could do
// about any of it: blank lines, surrounding whitespace, a stray carriage
// return, a duplicate, a line longer than any title can be (MAX_ITEM_TITLE --
// it can match no item, so keeping it would only cost memory), and every title
// past the MAX_HIDDEN'th one KEPT (a file of junk is still walked to the end;
// what is bounded is the memory, not the reading). A file that is missing entirely never gets here and
// means the same thing as an empty one: nothing is hidden.
void parseHidden(const char* text, HiddenSet& out);

// One title per line, each line terminated. Empty for an empty set, which is
// a file of zero bytes rather than a file holding a blank line.
std::string formatHidden(const HiddenSet& set);

// ---------------------------------------------------------------------------
// A folder's rows, given what it is hiding.
//
// A ROW is a position in the list a folder is drawing; an ITEM is a position in
// the registry. They are the same number until something is hidden, and after
// that they are two small ints in the same range that nothing distinguishes --
// so crossing them is a mistake no compiler and no range check can report, and
// the symptom is the shelf's oldest one: the header says page 2, the rows sit
// where page 2's rows sit, and the tap opens page 1's game.
//
// They live HERE, away from the registry, because the registry drags in every
// activity in the fork and cannot be built on a host. The three functions below
// are the whole conversion and they take the titles as a callable, so the
// folder passes its own and a test passes an array. See host-tests/shelfhidden.
// ---------------------------------------------------------------------------

// How many of `count` items are on the list.
template <typename TitleAt>
int shownCountIn(const HiddenSet& hidden, const int count, TitleAt titleAt) {
  int shown = 0;
  for (int i = 0; i < count; ++i) {
    if (!hidden.contains(titleAt(i))) ++shown;
  }
  return shown;
}

// The item at shown row `row`, or -1 when the list is not that long. The one
// place a row becomes an item.
template <typename TitleAt>
int shownItemIn(const HiddenSet& hidden, const int count, const int row, TitleAt titleAt) {
  if (row < 0) return -1;
  int seen = 0;
  for (int i = 0; i < count; ++i) {
    if (hidden.contains(titleAt(i))) continue;
    if (seen == row) return i;
    ++seen;
  }
  return -1;
}

// How many shown items sit BEFORE item `item` -- which is the shown row of
// `item` itself when it is shown, and the row of the next surviving item when
// it is not.
//
// Deliberately unclamped, so it can return the whole shown count when
// everything from `item` on is hidden. The clamp is shelfui::resumeRowFor()'s
// job and is the same rule a folder that SHRANK already uses: pin to the last
// row, not back to the top. Two rules, one each, rather than two clamps that
// have to agree.
template <typename TitleAt>
int shownRowForIn(const HiddenSet& hidden, const int count, const int item, TitleAt titleAt) {
  if (item <= 0) return 0;
  int before = 0;
  for (int i = 0; i < item && i < count; ++i) {
    if (!hidden.contains(titleAt(i))) ++before;
  }
  return before;
}

}  // namespace shelf
