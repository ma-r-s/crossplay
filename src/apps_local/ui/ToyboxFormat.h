#pragma once

// How wide a snprintf buffer has to be, derived rather than guessed.
//
// A tile must show the whole text, no exceptions. snprintf does not overrun --
// it CUTS -- so a buffer that is one byte short is not a crash, it is a
// shortened string on the panel with nothing to say it was shortened. That is
// exactly the defect these constants exist to prevent, and card 256 found it
// twenty-two times over twelve apps, every one of them a number that looked
// roomy at the time it was typed.
//
// So: size a buffer as (the ints it can carry * kIntChars) + the format's own
// literal characters + the terminator. Written that way the size IS the
// derivation, so changing the format changes the buffer, and a format that
// outgrows its buffer stops compiling instead of quietly truncating.
//
// The class was invisible on this Mac entirely: clang has no
// -Wformat-truncation in any form, and the three GCC suites that could have
// seen it all passed -Wno-format-truncation. Those flags are gone; keep them
// gone. See host-tests/ui/run.sh, and card 270 for the -O level it still needs.
//
// Freestanding, like the rest of ui/: no renderer, no device, no Arduino.

#include <limits>

namespace toybox {

// What a `%d` can print, sign included: "-2147483648" is eleven characters.
constexpr int kIntChars = 11;
static_assert(kIntChars >= std::numeric_limits<int>::digits10 + 2, "an int must fit kIntChars with its sign");

// The literal characters of a format: everything the directives do not supply.
// constexpr, so a buffer derived with it is a compile-time fact.
constexpr int literalChars(const char* text) {
  int n = 0;
  while (text[n] != '\0') ++n;
  return n;
}

// The three formats this fork's chrome writes over and over. Named here rather
// than re-derived in eleven files, because eleven copies of an arithmetic fact
// is eleven chances for one of them to drift.

// "%d OF %d" -- the how-to page counter in the black band.
constexpr int kOfCounterChars = 2 * kIntChars + literalChars(" OF ") + 1;
// "%d/%d" -- the same counter, the shelf's and the dungeon's spelling.
constexpr int kSlashCounterChars = 2 * kIntChars + literalChars("/") + 1;
// "%d" -- a bare number: a page pip, a tray count, a clue's index.
constexpr int kIntTextChars = kIntChars + 1;

}  // namespace toybox
