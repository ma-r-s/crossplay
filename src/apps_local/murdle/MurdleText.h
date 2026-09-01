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

// "ALREADY TICKED: SPADE/FARM AND PAN/GARDEN. CLEAR THEM TO TICK HERE." -- what
// the grid says when a tap could only have been honoured by deleting a tick the
// player made. Empty when the tap was not blocked. Names the squares rather
// than restating the rule: the rule is obvious and the squares are not.
void blockedLine(const murdle::Puzzle& puzzle, int catA, int itemA, int catB, int itemB,
                 const murdle::TapResult& result, char* out, int cap);

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
