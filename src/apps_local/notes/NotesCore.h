#pragma once

// A note is one card of text, and a line beginning "- [ ] " is tickable.
//
// Freestanding C++17 -- no Arduino, no renderer, no SD card -- so that
// host-tests/notes can drive every rule on a laptop. The device half
// (NotesLibrary) does nothing but read and write the bytes these functions
// hand it.
//
// ---------------------------------------------------------------------------
// One data type, and one byte
// ---------------------------------------------------------------------------
//
// There is no separate to-do list. A note is text; a shopping list is a note
// whose lines all happen to be tasks, and a recipe is a note with none. That
// keeps one parser, one file format and one screen, and it means the ONLY edit
// the device itself has to perform is a tick.
//
// A tick flips exactly one byte. `Line::markAt` is the offset of the ' ' or
// 'x' between the brackets, so `toggle()` writes a single character back into
// the buffer and every other byte of the file -- indentation, prose,
// punctuation, the user's own trailing spaces -- is preserved BY CONSTRUCTION
// rather than by a careful re-serialiser that can drift from the parser. It
// also means a file rewritten after a tick differs from the original in one
// place, so a torn write cannot turn prose into something else.
//
// The task syntax is therefore STRICT: "- [ ] ", "- [x] " or "- [X] " after
// optional leading whitespace, with '*' and '+' accepted as the bullet because
// Markdown accepts them. "- [] milk" is prose. Being permissive there would
// mean a tick had to INSERT a byte, which is how the one-byte guarantee above
// would be lost for the sake of a typo.
//
// ---------------------------------------------------------------------------
// Why clearing is the primitive
// ---------------------------------------------------------------------------
//
// The real cycle of a list is add -> tick -> clear, not add -> tick -> keep.
// `clearChecked()` removes the ticked lines in one pass, which is the only
// bulk edit the device performs and the reason a list does not have to be
// emptied a line at a time on a panel that repaints in 0.3s.

#include <cstddef>
#include <cstdint>  // Kind's underlying type; clang gets it transitively, GCC does not
#include <string>
#include <vector>

namespace notes {

// One line, as a byte range into the document it was parsed from. Ranges, not
// copies: a note is read far more often than it is edited.
struct Line {
  size_t begin = 0;      // first byte of the line
  size_t end = 0;        // one past its last byte, excluding the newline
  size_t textBegin = 0;  // first byte of the visible text ("- [ ] " skipped)
  size_t markAt = 0;     // the ' ' or 'x' between the brackets; tasks only
  bool isTask = false;
  bool checked = false;
};

std::vector<Line> parse(const std::string& doc);

// Flips one byte and updates `line`. False if the line is not a task, which is
// the only way it can fail.
bool toggle(std::string& doc, Line& line);

struct Counts {
  int done = 0;
  int total = 0;   // every non-empty line, because every one is an item
  int marked = 0;  // of those, the ones carrying a tick box
};
Counts counts(const std::vector<Line>& lines);

// The note's NAME is its filename, not any line inside it. One source of truth:
// renaming is a file rename, and nothing has to decide which of two titles
// wins.
std::string textOf(const std::string& doc, const Line& line);

// Removes every checked line and returns their texts in file order.
std::vector<std::string> clearChecked(std::string& doc);

// Every non-empty line that is not already a tick box becomes one, in place.
// This is what a person typing on their phone gets, and it is why the page has
// no syntax to teach: they write "Milk" and the reader shows a tick box.
//
// It runs on the way IN, not on the way out, so the file on the card stays
// ordinary Markdown that a desktop editor understands. Lines that are already
// markers are left byte-for-byte alone, so a save from the phone cannot disturb
// what was ticked on the device. Returns true when anything changed.
bool coerceToList(std::string& doc);

// A note is a LIST or a PAGE, and the kind belongs to the NOTE, never to the
// line. That distinction is the whole lesson of the first version: when a line
// could be one of two kinds, the fast path produced the wrong one and the two
// were drawn differently. Here the kind is one answer for the whole file, so
// nothing inside it can disagree.
//
// It is read off the content -- a file with any tick box is a list -- so it
// survives being edited on a computer and needs nothing stored beside the note.
// An EMPTY note has no evidence either way, which is what `emptyIsList` is for:
// the app remembers what was chosen when it was made, and the first line
// written settles it for good.
enum class Kind : uint8_t { List, Page };

// Strips the tick box off every line, turning a list back into words. The
// counterpart of coerceToList, and the reason neither is a one-way door: the
// kind is INFERRED from the file, so a note that should have been a list is
// fixed by writing the markers rather than by storing a flag somewhere the
// file cannot see.
bool stripMarkers(std::string& doc);
Kind kindOf(const std::vector<Line>& lines, bool emptyIsList = true);

}  // namespace notes
