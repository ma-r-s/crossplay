#pragma once

// Turning a clue into a sentence.
//
// This is the only place in the game that knows any English, and it is separate
// from MurdleCore for a reason: the logic must never depend on how something is
// worded, and the wording must never be able to change what a clue means. A
// clue is a mask; a sentence is a rendering of that mask; the solver reads the
// mask and the player reads the sentence, and the test asserts they agree.
//
// Freestanding, so host-tests/murdle/ can build every sentence this can produce
// out of the whole cast table and check that each one comes out as correct
// English rather than as a mad lib.

#include <cstdint>

#include "MurdleCast.h"
#include "MurdleCore.h"

namespace murdletext {

// Long enough for the worst case, which is an either/or over two locations with
// a trait-worded anchor.
constexpr int kLineMax = 160;

// The longest a `label()` can come back. The names live in runtime tables,
// so nothing here can static_assert it; host-tests/murdle walks all four
// tables against this, which is the same gate one step later. Anything that
// quotes a label sizes its buffer from this rather than from today's cast --
// the longest name is seven characters and that is not a fact to build on.
constexpr int kLabelMax = 16;

// The short uppercase label: what the accusation sheet lists and what a grid
// axis falls back to when there is room for a word.
const char* label(const murdle::Puzzle& puzzle, int cat, int item);

// One clue, as a sentence, with its full stop. Always null terminated.
void clueLine(const murdle::Puzzle& puzzle, int clueIndex, char* out, int cap);

// "LEFT-HANDED, GREEN EYES, BLACK HAIR, 5'8"" -- the suspect card, which is
// what makes an attribute clue usable.
void suspectAttributes(const murdle::Puzzle& puzzle, int item, char* out, int cap);

// The trait a fixture is known by, for the case file listing. Empty for
// suspects and motives, which have none.
const char* trait(const murdle::Puzzle& puzzle, int cat, int item);

// "IT WAS DOCTOR ASHE WITH THE BONE SAW IN THE LIGHTHOUSE" -- picks are one
// item per live category, indexed by category.
void accusationLine(const murdle::Puzzle& puzzle, const uint8_t picks[murdle::kMaxCats], char* out, int cap);

// The name of a category, for a heading.
const char* categoryName(int cat);

// What the grid says when a tap could only have been honoured by deleting a
// tick the player made.
//
// THREE WORDS, and the band above the grid is sized from this one string.
// It used to name the squares in the way -- "ALREADY TICKED: SPADE/FARM AND
// PAN/GARDEN. CLEAR THEM TO TICK HERE." -- which is two wrapped lines of the
// tile cut at real cast names, and the band that has to hold it is reserved on
// every frame so the grid never moves. That spent a paragraph of the board's
// room permanently to say something on a handful of frames. The squares are
// already on screen with the player's own ticks in them, and "clear them" is
// the next thing they try; ELSEWHERE is the only part they cannot see, because
// the square they tapped is not itself ticked.
constexpr const char* kBlockedNotice = "ALREADY TICKED ELSEWHERE";

// `kBlockedNotice`, or empty when the tap was not blocked. The puzzle is not a
// parameter any more: the wording names no fixture, so nothing here can depend
// on the cast.
void blockedLine(const murdle::TapResult& result, char* out, int cap);

// One distinct letter per item of a category, for the grid's axes, plus a
// terminating null. A 34px column header cannot hold a word and this renderer
// does not rotate type, so the axes carry letters and the legend carries the
// names.
//
// Mnemonic where it can be: an item's own initial, and the next unused letter
// of its name when two of the drawn items start alike. Distinctness is the
// hard requirement -- two rows labelled the same is a grid that cannot be read
// -- so it falls back to digits rather than ever repeating.
void axisLetters(const murdle::Puzzle& puzzle, int cat, char out[murdle::kMaxItems + 1]);

}  // namespace murdletext
