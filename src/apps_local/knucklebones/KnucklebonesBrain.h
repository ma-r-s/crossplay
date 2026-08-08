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
// Ties break toward the lower column index rather than randomly. A random
// tiebreak would make the same position play differently on two devices, and
// this has to stay replayable: the whole match is reconstructible from a seed.
inline int chooseColumn(const Game& game) {
  const int seat = game.turn;
  int best = -1;
  int bestValue = 0;

  for (int column = 0; column < kColumns; ++column) {
    if (!canPlace(game, column)) continue;

    // Played on a copy. The real game is never speculatively mutated, so a
    // brain that returned early or threw could not leave a half-applied move
    // behind -- the bug class simply is not reachable.
    Game trial = game;
    if (!place(trial, column)) continue;

    const int value = advantage(trial, seat);
    if (best < 0 || value > bestValue) {
      best = column;
      bestValue = value;
    }
  }
  return best;
}

}  // namespace knucklebones
