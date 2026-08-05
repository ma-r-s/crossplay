#pragma once

// Who this device is: a name in three parts, and the face those parts describe.
//
// Device-wide on purpose, and this is the part worth getting right: a DS asked
// you for a nickname once, in System Settings, and then every game it ever ran
// used it. No game asked, no game stored its own, and no game had a naming
// screen. Any app in this fork gets the same deal -- call name() and you are
// done.
//
// There is no keyboard, and that is a feature rather than a gap. Typing on a
// 1-bit panel with a 500ms refresh is a chore, and a name is not information
// the player is trying to communicate; it is a label they need to recognise.
// So it is rolled from word lists, the way a console hands you one. That also
// means it can never be empty, never be rude, and never need validating.
//
// ---------------------------------------------------------------------------
// The three words ARE the face.
//
// Slot 0 is the hair, slot 1 the eyes, slot 2 the mouth. SPIKY GRIM BEARD is a
// name and it is also a drawing instruction, which is what makes rolling one
// worth doing: you tap the middle word and you watch the eyes change. A single
// reroll button gave you a name to accept or reject; three give you something
// you can steer.
//
// The consequence that pays for the whole design: **an avatar costs no wire
// bytes.** The name already travels to the other device (LinkProtocol's Hello
// and Join carry it), so the far side rebuilds the face by reading the same
// three words. Nothing was added to the protocol, nothing can disagree, and a
// build with a different word list draws a plain head rather than the wrong
// face. See PlayerAvatar.h for that half.
// ---------------------------------------------------------------------------
//
// The composing half is freestanding so host-tests can exercise the lists;
// only the load/save half touches storage.

#include <cstddef>
#include <cstdint>

namespace player {

// Hair, eyes, mouth. Three because it is the most a person can steer before the
// screen becomes a form, and because three is what fits across the width of the
// panel as three tappable words.
constexpr int kSlotCount = 3;
enum Slot : int { SlotHair = 0, SlotEyes = 1, SlotMouth = 2 };

// A word this build does not know. Only ever comes from parsing: a name rolled
// here is always three known words, and a name off the wire may not be.
constexpr uint8_t kUnknownWord = 0xFF;

// The widest triple the lists can produce, spaces included. Exact rather than
// generous, so adding a seven-letter word fails the exhaustive test in
// host-tests/player instead of being quietly truncated in a packet header. It
// must stay <= linkplay::kMaxNameBytes; LinkActivity.cpp static_asserts that.
constexpr size_t kMaxNameLength = 20;

// Which word each slot has landed on. Not the string: the string is derived,
// and keeping one source of truth is what stops a name and a face disagreeing.
struct Name {
  uint8_t word[kSlotCount] = {kUnknownWord, kUnknownWord, kUnknownWord};

  // Every slot names a word this build can draw. False for a name from a build
  // whose lists differ, which is the only way an unknown word arrives.
  bool known() const;
};

// How many words each slot can take. constexpr because PlayerAvatar.cpp holds
// the matching artwork in its own tables and static_asserts these lengths
// against them: a word without a drawing, or a drawing without a word, is a
// build failure rather than a face with a hole in it. That the two lists are
// also in the same *order* is not something a static_assert can see, so
// host-tests/player checks it word by word.
constexpr size_t kWordCount[kSlotCount] = {14, 14, 14};

// What word `index` of slot `slot` is. Returns nullptr for anything out of
// range, including kUnknownWord.
size_t wordCount(int slot);
const char* word(int slot, uint8_t index);

// Writes "HAIR EYES MOUTH" into `out`, truncating rather than overrunning. An
// unknown slot is skipped, which is only reachable through a hand-built Name;
// rolled names always have three.
void compose(char* out, size_t capacity, const Name& name);

// Reads a name back into slots. Positional: the first word is only looked up in
// the hair list, so two lists could share a word without ambiguity. Anything
// unrecognised becomes kUnknownWord rather than a guess.
Name parse(const char* text);

// The longest single word any list holds. What a caller needs to size a buffer
// for shortName().
constexpr size_t kMaxShortNameLength = 6;

// What to call somebody in a sentence: their first word.
//
// A full name is three words and up to twenty characters, which is right on a
// seat card and far too long anywhere it has to sit inside a line of prose.
// "SHAGGY SLEEPY GOATEE'S MOVE" is four characters past the width of chess's
// status capsule, and what got dropped was "MOVE" -- so the one word carrying
// the meaning of the label was the word that went, silently, with no ellipsis.
// Battleship has the same shape in "%s SANK YOUR %s".
//
// Calling them SHAGGY is also just better. It is what a person would do, it can
// never overflow, and the full name is still on the screen where you met them.
//
// Copies up to the first space, truncating rather than overrunning. Works on a
// name this build cannot parse, since it reads the text and not the slots.
void shortName(const char* name, char* out, size_t capacity);

// A whole fresh name, for a device that has never had one. Same seed, same
// name, which is what makes it testable. This is the only random draw left --
// once you have a name, changing it is a walk, not another roll.
Name roll(uint32_t seed);

// Steps one slot to the next word, wrapping at the end.
//
// A walk, not a reroll, and the difference matters at fourteen options: random
// showed you the same three faces over and over and hid the rest, and you could
// not go back to the one two taps ago. Stepping means fourteen taps is the whole
// list, the order is the one in PlayerName.cpp, and the word you just left is
// thirteen taps away rather than lost. It also needs no seed, so what a tap does
// is now a fact rather than a distribution.
//
// A slot on a word this build does not know steps to the first one.
Name nextWord(const Name& current, int slot);

// --- the half that touches storage ------------------------------------------

// The stored name, generating and persisting one on first use so a device is
// never nameless. Stable for the life of the process, and always three known
// words: a saved name that does not parse (a two-word one from before the face
// existed) is discarded and rolled again rather than half-understood.
const char* name();

// The same thing as slots, for anything that needs to draw or change it.
Name parts();

// Steps slot `slot` to its next word and stores the result. Out of range is a
// no-op.
void stepSlot(int slot);

}  // namespace player
