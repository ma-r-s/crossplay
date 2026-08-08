#pragma once

// The built-in opponent. Freestanding, like the rules it plays by, so it can be
// soaked on a laptop against thousands of games.
//
// It is a one-ply search and deliberately not more. Knucklebones has three
// legal moves at most and a die you cannot predict, so a deeper search buys
// very little and costs a slow panel its responsiveness. What it does buy is an
// opponent that never misses the obvious: it always sees a stack it can build
// and a stack it can wreck, which is what a person notices too.
//
// It takes the whole Game because it is the same information a player has -- the
// board is face up. There is nothing hidden in this game, so there is nothing
// for it to cheat with, and that is a property of the design rather than a
// promise in a comment.

#include <cstdint>

#include "KnucklebonesCore.h"

namespace knucklebones {

// How much a seat's position is worth, from that seat's point of view: your
// score minus theirs. Whole-board rather than per-column, because destroying
// their stack and building your own are the same currency and the opponent
// should be free to trade one for the other.
inline int advantage(const Game& game, const int seat) { return score(game.grid[seat]) - score(game.grid[1 - seat]); }

// The column the opponent would play, or -1 when it cannot move.
//
// Ties break toward the column with the most room, and that is not a detail: a
// cold review measured 43% of its moves as one-ply ties, so the tiebreak is
// nearly half of how it plays. Breaking toward the lowest index -- the obvious
// choice, and the first one here -- meant every column evaluated identically on
// an empty board, so its first three dice went into column 0 in 100% of games.
// It buried its opening in one column and could not start a second stack until
// that one filled. Preferring room spreads the opening and wins 59% against the
// version that did not.
//
// Still deterministic, which is the part that matters structurally: a random
// tiebreak would make the same position play differently on two devices, and a
// match has to stay reconstructible from its seed.
// A number derived from the position a player can SEE -- both grids, the turn
// and the die on the table -- and deliberately not from `rng`.
//
// It exists to break ties that survive everything else. On an empty board all
// three columns evaluate identically AND have identical room, so every earlier
// tiebreak fell through to "lowest index" and the opening went to column 0 every
// single game. Mixing the visible position in spreads the opening across the
// die that is actually showing, while staying a pure function of state: two
// devices replaying the same match still make the same choice.
inline uint32_t positionHash(const Game& game) {
  uint32_t hash = 2166136261u;
  for (int seat = 0; seat < kSeats; ++seat) {
    for (int column = 0; column < kColumns; ++column) {
      for (int row = 0; row < kRows; ++row) {
        hash = (hash ^ game.grid[seat].cell[column][row]) * 16777619u;
      }
    }
  }
  hash = (hash ^ game.turn) * 16777619u;
  hash = (hash ^ game.die) * 16777619u;
  return hash;
}

inline int chooseColumn(const Game& game) {
  const int seat = game.turn;
  int best = -1;
  int bestValue = 0;
  int bestRoom = -1;

  // Rotated by the position, so a tie that nothing else separates lands
  // somewhere the die decides rather than always on the left.
  const uint32_t offset = positionHash(game) % static_cast<uint32_t>(kColumns);

  for (int step = 0; step < kColumns; ++step) {
    const int column = static_cast<int>((offset + static_cast<uint32_t>(step)) % kColumns);
    if (!canPlace(game, column)) continue;

    // Played on a copy, with the die stream wiped. The real game is never
    // speculatively mutated, and zeroing rng is what makes "it cannot cheat"
    // structural rather than a promise: the state it is handed carries the
    // generator, and the generator advances once per placement, so that field
    // IS the complete sequence of future dice.
    Game trial = game;
    trial.rng = 0;
    if (!place(trial, column)) continue;

    const int value = advantage(trial, seat);
    // Room first among equal-value moves: a cold review measured the greedy
    // brain losing 59/39 to one that preferred the emptiest column, because
    // stacking into a nearly-full column forecloses its own next move.
    const int room = kRows - columnCount(game.grid[seat], column);
    if (best < 0 || value > bestValue || (value == bestValue && room > bestRoom)) {
      best = column;
      bestValue = value;
      bestRoom = room;
    }
  }
  return best;
}

}  // namespace knucklebones
