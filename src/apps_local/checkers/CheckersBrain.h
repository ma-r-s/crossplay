#pragma once

// The built-in opponent. Freestanding, like the rules it plays by.
//
// Two ply with a material evaluation: play every legal move, let the opponent
// reply with their best, keep the move whose worst case is best. Checkers
// punishes one-ply greed badly -- a free capture is very often a trap, because
// the mandatory-capture rule means the reply is forced and can be a multi-jump
// -- so one ply here would produce an opponent that hangs pieces constantly.
// That is a different judgement from Knucklebones, where one ply was right
// because the die makes deeper search worthless.
//
// It cannot cheat: `Game` holds no hidden state and no generator. That is a
// property of the design, not a promise -- there is nothing here to peek at.

#include <cstdint>

#include "CheckersCore.h"

namespace checkers {

// A king is worth more than a man, and pieces are worth more than position on a
// board this size. Kept deliberately crude: a heavier evaluation would make the
// opponent stronger and the search slower, and this one already sees traps.
constexpr int kManValue = 10;
constexpr int kKingValue = 18;

inline int material(const Game& game, const uint8_t seat) {
  int score = 0;
  for (int index = 0; index < kCells; ++index) {
    if (!occupied(game, index)) continue;
    const int value = isKing(game, index) ? kKingValue : kManValue;
    score += ownerOf(game, index) == seat ? value : -value;
  }
  return score;
}

// The opponent's best reply to a position, as a score for `seat`.
inline int worstReply(const Game& game, const uint8_t seat) {
  Move list[kMaxMoves];
  const int count = moves(game, list);
  // They have nothing: this position wins for whoever is not to move.
  if (count == 0) return game.turn == seat ? -1000 : 1000;

  int worst = 2000;
  for (int i = 0; i < count; ++i) {
    Game trial = game;
    if (!play(trial, list[i])) continue;
    const int score = material(trial, seat);
    if (score < worst) worst = score;
  }
  return worst;
}

// The move the opponent would play, or false when it has none.
//
// Ties break toward the earlier move in generation order, which is
// deterministic: a random tiebreak would make the same position play
// differently on two devices, and a match has to stay reconstructible.
inline bool chooseMove(const Game& game, Move& out) {
  Move list[kMaxMoves];
  const int count = moves(game, list);
  if (count == 0) return false;

  const uint8_t seat = game.turn;
  int bestScore = -3000;
  int best = -1;
  for (int i = 0; i < count; ++i) {
    Game trial = game;
    if (!play(trial, list[i])) continue;
    const int score = worstReply(trial, seat);
    if (score > bestScore) {
      bestScore = score;
      best = i;
    }
  }
  if (best < 0) return false;
  out = list[best];
  return true;
}

}  // namespace checkers
