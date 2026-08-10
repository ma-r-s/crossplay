#pragma once

// The 58 physical cards: what face each one has, and what colour it is printed
// in. Freestanding data, no logic.
//
// Card ids are grouped by face in `Kind` order, so `kCardKind` is a run-length
// expansion of `kKindSupply` and the two can never disagree -- the static_assert
// at the bottom checks exactly that.
//
// Colour is the one thing here that cannot be derived, because it is a property
// of the printed deck. Two rules read it: the mermaid, and the end-of-round
// colour bonus.

#include <cstdint>

#include "SeaSaltCore.h"

namespace seasalt {

// clang-format off
constexpr uint8_t kCardKind[kCards] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9 crab
    1, 1, 1, 1, 1, 1, 1, 1,     // 8 boat
    2, 2, 2, 2, 2, 2, 2,        // 7 fish
    3, 3, 3, 3, 3,              // 5 swimmer
    4, 4, 4, 4, 4,              // 5 shark
    5, 5, 5, 5, 5, 5,           // 6 shell
    6, 6, 6, 6, 6,              // 5 octopus
    7, 7, 7,                    // 3 penguin
    8, 8,                       // 2 sailor
    9, 9, 9, 9,                 // 4 mermaid
    10,                         // lighthouse
    11,                         // shoal of fish
    12,                         // penguin colony
    13,                         // captain
};
// clang-format on

// The eleven colours, in descending order of how many cards carry them. Names
// are the official ones from the distribution card that ships in the box, not
// the community's (which calls black "dark grey" and purple "mauve").
//
// Eleven is more than it looks like it should be, and three of the groups are
// tiny: light pink is two cards and orange is one. That is not an oddity to
// design around, it is the game -- a one-card colour can never carry a colour
// bonus, and the deck is built so the big groups are the ones worth chasing.
enum class Colour : uint8_t {
  DarkBlue = 0,
  LightBlue,
  Black,
  Yellow,
  LightGreen,
  White,
  Purple,
  LightGrey,
  LightOrange,
  LightPink,
  Orange,
};
constexpr int kColourCount = 11;

// How many cards carry each colour, straight off the distribution card. This is
// the number the expansion below is checked against, so a mistyped card cannot
// pass unnoticed.
constexpr uint8_t kColourSupply[kColourCount] = {9, 9, 8, 8, 6, 4, 4, 4, 3, 2, 1};

// clang-format off
constexpr uint8_t kCardColour[kCards] = {
    0, 0, 1, 1, 2, 3, 3, 4, 7,  // crab:     2 dark blue, 2 light blue, black, 2 yellow, light green, light grey
    0, 0, 1, 1, 2, 2, 3, 3,     // boat:     2 of each of the four big colours
    0, 0, 1, 2, 2, 3, 4,        // fish
    0, 1, 2, 3, 8,              // swimmer
    0, 1, 2, 4, 6,              // shark
    0, 1, 2, 3, 4, 7,           // shell
    1, 3, 4, 6, 7,              // octopus
    6, 8, 9,                    // penguin:  purple, light orange, light pink
    9, 10,                      // sailor:   light pink, orange
    5, 5, 5, 5,                 // mermaid:  all four are white, and white is nothing else
    6,                          // lighthouse:     purple
    7,                          // shoal of fish:  light grey
    4,                          // penguin colony: light green
    8,                          // captain:        light orange
};
// clang-format on

// The first card id of each face, so a screen can name a face without scanning.
constexpr int firstCardOf(const Kind kind) {
  int at = 0;
  for (int k = 0; k < static_cast<int>(kind); ++k) at += kKindSupply[k];
  return at;
}

namespace detail {

// Both tables are hand-entered expansions of a count, so both are checked
// against their count at compile time rather than by a test that could be
// forgotten. 58 cards spread over 14 faces and 11 colours is exactly the sort
// of table where one wrong digit is invisible.
constexpr bool tableMatches(const uint8_t* table, const uint8_t* supply, const int groups) {
  int counted[16] = {};
  for (int c = 0; c < kCards; ++c) {
    if (table[c] >= groups) return false;
    ++counted[table[c]];
  }
  for (int g = 0; g < groups; ++g) {
    if (counted[g] != supply[g]) return false;
  }
  return true;
}

}  // namespace detail

static_assert(detail::tableMatches(kCardKind, kKindSupply, kKindCount), "kCardKind must expand kKindSupply exactly");
static_assert(detail::tableMatches(kCardColour, kColourSupply, kColourCount),
              "kCardColour must expand kColourSupply exactly");

}  // namespace seasalt
