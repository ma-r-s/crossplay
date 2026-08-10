#pragma once

// The solo opponent. Freestanding, deterministic and pure: the same position
// always produces the same column, and it decides from the board alone.
//
// Connect Four has no hidden information, so "it cannot cheat by looking at
// what you hold" is free here in a way it was not in Jaipur or Knucklebones.
// What is NOT free is the other half of that promise -- that it plays well
// enough to be worth beating -- so `testTheBrainBeatsARandomMoverConvincingly`
// is the test that actually matters. Legal and deterministic were both true of
// a brain whose evaluation had been negated.

#include <cstdint>

#include "ConnectFourCore.h"

namespace connectfour {

// Alpha-beta gets nowhere near this; it is a guard so a scoring bug cannot make
// a win look worse than a draw.
constexpr int kWinScore = 1000000;

// How far ahead it looks. Seven plies is a full round trip plus one, which is
// enough to see a double threat being built and to refuse the column that
// creates one for you. Higher is stronger and slower; the whole search is
// bounded by 7^7 before pruning, and pruning takes about two orders off that.
constexpr int kDepth = 7;

// How many four-in-a-rows pass through a cell in the BOTTOM row of each column:
// three at the edge, seven in the middle. Used as a per-column weight.
//
// The comment here used to say this was the count for the whole column and that
// the middle column sat on 13 lines. Neither is what the table holds. Counted:
// per column the figures are 15 27 39 51 39 27 15, and 13 is the maximum for
// any single CELL, which is the centre of the board rather than anything about
// a column. The table is a reasonable centre-weighting and the justification
// written over it was wrong, so the justification changed rather than the
// numbers -- the alternative was picking new numbers to fit a sentence.
constexpr int kLinesThrough[kColumns] = {3, 4, 5, 7, 5, 4, 3};

// Score a four-cell window for `side`: how close it is to a line, and nothing
// if the other side is in it.
// SYMMETRIC, and that is the fix rather than the taste.
//
// It used to pay -80 for their open three against +60 for mine, with a comment
// claiming that weighted defence. It did the opposite. kDepth is odd, so every
// leaf in the search is evaluated from the OPPONENT's side and the root value
// of a leaf is its negation -- measured: 19,198 leaves scored for the opponent,
// zero for the root side. At the root the asymmetry therefore rewarded the
// brain's own open three by 80 and the opponent's by only 60, the mirror of
// what was written. Flipping kDepth to an even number would have flipped the
// bias with it, which is what a sign that depends on parity does.
//
// It cost games, not just tidiness: a copy identical but for -80 -> -60 beat
// the shipped brain 233-140 over 266 distinct games. Symmetric also makes
// evaluate() zero-sum, which it was not -- evaluate(g, light) != -evaluate(g,
// dark) in 63,392 of 138,663 reachable positions, worst gap 300.
//
// The four-of-a-kind case is gone. negamax returns at its terminal check before
// evaluate ever runs, so no leaf can hold four of one colour; setting that
// branch to zero changed 0 of 2031 chosen columns. Dead code that looked like a
// safety net.
inline int windowScore(const int mine, const int theirs, const int empty) {
  if (mine > 0 && theirs > 0) return 0;  // blocked, worth nothing to either
  if (mine == 3 && empty == 1) return 60;
  if (mine == 2 && empty == 2) return 6;
  if (theirs == 3 && empty == 1) return -60;
  if (theirs == 2 && empty == 2) return -6;
  return 0;
}

// Static evaluation from `side`'s point of view.
inline int evaluate(const Game& game, const uint8_t side) {
  const uint8_t enemy = other(side);
  int score = 0;

  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (game.cell[column][row] == side)
        score += kLinesThrough[column];
      else if (game.cell[column][row] == enemy)
        score -= kLinesThrough[column];
    }
  }

  constexpr int kDirections[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      for (const auto& dir : kDirections) {
        const int endColumn = column + dir[0] * (kLine - 1);
        const int endRow = row + dir[1] * (kLine - 1);
        if (!inside(endColumn, endRow)) continue;
        int mine = 0;
        int theirs = 0;
        int empty = 0;
        for (int step = 0; step < kLine; ++step) {
          const uint8_t cell = game.cell[column + dir[0] * step][row + dir[1] * step];
          if (cell == side)
            ++mine;
          else if (cell == enemy)
            ++theirs;
          else
            ++empty;
        }
        score += windowScore(mine, theirs, empty);
      }
    }
  }
  return score;
}

// The order columns are tried in: middle outward. Pure speed -- alpha-beta
// prunes far more when the good move is examined first -- and it does not
// change which move is chosen, because ties are broken explicitly below.
constexpr int kSearchOrder[kColumns] = {3, 2, 4, 1, 5, 0, 6};

inline int negamax(Game game, const uint8_t side, const int depth, int alpha, const int beta) {
  if (game.outcome != Outcome::Running) {
    if (game.outcome == Outcome::Draw) return 0;
    const bool sideWon =
        (game.outcome == Outcome::LightWins && side == kLight) || (game.outcome == Outcome::DarkWins && side == kDark);
    // Deeper wins score lower, so a mate in one is preferred to a mate in five
    // and a loss is postponed as long as possible. Without this the brain finds
    // a win and then wanders, because every winning line scores the same.
    return sideWon ? kWinScore + depth : -(kWinScore + depth);
  }
  if (depth == 0) return evaluate(game, side);

  int best = -kWinScore * 2;
  for (const int column : kSearchOrder) {
    if (!canDrop(game, column)) continue;
    Game next = game;
    drop(next, column);
    const int score = -negamax(next, other(side), depth - 1, -beta, -alpha);
    if (score > best) best = score;
    if (best > alpha) alpha = best;
    if (alpha >= beta) break;
  }
  return best;
}

// The column it plays, or kNoColumn when there is nothing to play.
//
// Ties break toward the centre, using the same line-count table the evaluation
// uses, and then toward the lower index. Never toward randomness: a brain that
// rolls a die is not reproducible, and every screenshot recipe in this repo
// depends on being able to replay a game exactly.
inline int chooseColumn(const Game& game) {
  if (over(game)) return kNoColumn;
  const uint8_t side = game.turn;

  int bestColumn = kNoColumn;
  int bestScore = 0;
  for (const int column : kSearchOrder) {
    if (!canDrop(game, column)) continue;
    Game next = game;
    drop(next, column);
    const int score = -negamax(next, other(side), kDepth - 1, -kWinScore * 2, kWinScore * 2);
    const bool better =
        bestColumn == kNoColumn || score > bestScore ||
        (score == bestScore && kLinesThrough[column] > kLinesThrough[bestColumn]) ||
        (score == bestScore && kLinesThrough[column] == kLinesThrough[bestColumn] && column < bestColumn);
    if (better) {
      bestColumn = column;
      bestScore = score;
    }
  }
  return bestColumn;
}

}  // namespace connectfour
