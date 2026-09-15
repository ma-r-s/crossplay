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
// Why clearing is the primitive, and where OFTEN comes from
// ---------------------------------------------------------------------------
//
// The real cycle of a list is add -> tick -> clear, not add -> tick -> keep.
// So `clearChecked()` removes the ticked lines and RETURNS THEIR TEXT, and the
// caller files that text into a small per-note history. `often::suggest()`
// then offers the things this list has held before and is not holding now,
// which is the row of one-tap pills at the bottom of the screen.
//
// That row exists because the complaint in every list app is re-entering the
// same forty items, not slow typing. It is the rung of the input ladder that
// keeps a person off the keyboard entirely.
//
// Matching is case-insensitive and ignores surrounding whitespace throughout:
// "Milk", "milk" and " milk " are one item, or the history fills with
// near-duplicates that push the useful pills off the row.

#include <cstddef>
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
  int total = 0;  // task lines only; prose is not counted
};
Counts counts(const std::vector<Line>& lines);

// The card's name in the deck: the first line with anything on it, with a
// leading "# " removed so a Markdown heading reads as a title rather than as
// punctuation. Empty when the note is empty.
std::string title(const std::string& doc, const std::vector<Line>& lines);

std::string textOf(const std::string& doc, const Line& line);

// Removes every checked task line and returns their texts in file order, for
// the caller to record. Prose is never removed, whatever it says.
std::vector<std::string> clearChecked(std::string& doc);

// The text of every task currently in the note, ticked or not. This is what
// `often::suggest` must exclude.
std::vector<std::string> taskTexts(const std::string& doc);

// Trimmed and lowercased; the identity used for every comparison here.
std::string fold(const std::string& text);

namespace often {

// One remembered item. `count` is how many times it has been cleared from this
// note, `lastSeen` a monotonic stamp so that ties -- and at first everything
// ties on count 1 -- fall back to recency rather than to whatever order the
// file happened to be in.
struct Entry {
  std::string text;  // as the user last wrote it, not folded
  int count = 0;
  long lastSeen = 0;
};

// Files freshly cleared texts into the history, bumping counts and stamps.
// `stamp` should increase between calls; the caller keeps it beside the file.
void record(std::vector<Entry>& history, const std::vector<std::string>& texts, long stamp);

// The pills: most-used first, ties broken by most-recent, never offering
// something the note already holds.
std::vector<std::string> suggest(const std::vector<Entry>& history, const std::vector<std::string>& present,
                                 size_t max);

}  // namespace often

}  // namespace notes
