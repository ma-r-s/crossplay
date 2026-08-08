#pragma once

// Minesweeper, the rules. Freestanding: no renderer, no Activity, no storage.
//
// Eight by ten with ten mines, which is 12.5% density -- between the classic
// beginner board's 12.3% and its intermediate 15.6%. The shape is set by the
// panel rather than by tradition: 448px of usable width over eight columns is a
// 56px cell, which is a thumb, and ten rows is what fits under the header with
// the status capsule still on screen.
//
// Two properties are built in rather than checked for, which is this project's
// rule about promised properties:
//
//   * **The first tap is always safe.** Mines are laid AFTER it, excluding that
//     cell and its neighbours, so the opening is always a flood rather than a
//     coin toss. Re-rolling until the first tap missed would be the same result
//     bolted on, and would loop forever on a dense board.
//   * **A revealed zero opens its neighbours.** The flood is the game; a version
//     that made the player tap every empty cell would be a chore with the same
//     rules.

#include <cstdint>

namespace minesweeper {

constexpr int kColumns = 8;
constexpr int kRows = 10;
constexpr int kMines = 10;
constexpr int kCells = kColumns * kRows;

// One cell, as bit flags in a byte. A struct of three bools would be three
// bytes and this state is also a save file.
constexpr uint8_t kMine = 1 << 0;
constexpr uint8_t kRevealed = 1 << 1;
constexpr uint8_t kFlagged = 1 << 2;

enum class Status : uint8_t {
  // Nothing tapped yet. Mines do not exist until the first reveal, which is
  // what makes that reveal safe.
  Fresh,
  Playing,
  Won,
  Lost,
};

struct Game {
  uint8_t cell[kColumns][kRows];
  uint32_t rng;
  Status status;
};

inline uint32_t nextRandom(uint32_t& state) {
  if (state == 0) state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

inline bool inside(const int column, const int row) {
  return column >= 0 && column < kColumns && row >= 0 && row < kRows;
}

inline bool has(const Game& game, const int column, const int row, const uint8_t flag) {
  return inside(column, row) && (game.cell[column][row] & flag) != 0;
}

inline void start(Game& game, const uint32_t seed) {
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) game.cell[column][row] = 0;
  }
  game.rng = seed;
  game.status = Status::Fresh;
}

// How many mines touch a cell, counting the eight neighbours.
inline int neighbouringMines(const Game& game, const int column, const int row) {
  int count = 0;
  for (int dc = -1; dc <= 1; ++dc) {
    for (int dr = -1; dr <= 1; ++dr) {
      if (dc == 0 && dr == 0) continue;
      if (has(game, column + dc, row + dr, kMine)) ++count;
    }
  }
  return count;
}

// Lay the mines, avoiding `safeColumn`/`safeRow` and everything touching it.
//
// Called once, from the first reveal. Excluding the neighbours as well as the
// cell itself is what guarantees the opening is a flood: a first tap with a
// mine beside it would reveal a single numbered cell and leave the player
// guessing, which is the failure classic Minesweeper is famous for.
inline void layMines(Game& game, const int safeColumn, const int safeRow) {
  // Nine cells are excluded at most, so the board must be big enough to hold
  // the mines outside them. 80 - 9 = 71 against 10 mines, with room to spare.
  int placed = 0;
  while (placed < kMines) {
    const uint32_t roll = nextRandom(game.rng) % static_cast<uint32_t>(kCells);
    const int column = static_cast<int>(roll) % kColumns;
    const int row = static_cast<int>(roll) / kColumns;
    if (game.cell[column][row] & kMine) continue;
    const int dc = column - safeColumn;
    const int dr = row - safeRow;
    if (dc >= -1 && dc <= 1 && dr >= -1 && dr <= 1) continue;
    game.cell[column][row] |= kMine;
    ++placed;
  }
}

inline int flagCount(const Game& game) {
  int count = 0;
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (game.cell[column][row] & kFlagged) ++count;
    }
  }
  return count;
}

// Every cell that is not a mine has been revealed. Derived rather than counted
// as we go: a stored tally that disagrees with the board is a bug class that
// does not exist here.
inline bool allSafeCellsRevealed(const Game& game) {
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      const uint8_t cell = game.cell[column][row];
      if ((cell & kMine) == 0 && (cell & kRevealed) == 0) return false;
    }
  }
  return true;
}

// Reveal a cell, flooding outward through everything a zero touches.
//
// Iterative rather than recursive: a full-board flood is 80 frames deep on a
// device whose activity stacks are 2KB.
//
// **A cell is marked revealed when it is PUSHED, not when it is popped**, and
// that is the whole correctness of this function. The first version marked at
// pop and deduplicated at push, so a cell touched by several zeroes was
// enqueued several times: pushes were not bounded by the eighty slots, the
// overflow guard silently dropped the excess, and 41.8% of real first taps
// opened a truncated region. The symptom is a blank revealed cell sitting next
// to a covered one, which cannot happen in this game, and the tests missed it
// because they asserted "more than ten cells opened" against a true answer of
// about forty.
//
// Marking at push makes each cell enqueue at most once, so the queue provably
// holds: eighty cells, eighty slots.
inline void floodFrom(Game& game, const int startColumn, const int startRow) {
  uint8_t queue[kCells][2];
  int head = 0;
  int tail = 0;

  // A flag is the player saying "I believe there is a mine here". The flood
  // stops at it rather than tearing it off: losing a flag you placed to a
  // cascade you did not aim is the most annoying thing this game can do.
  if (game.cell[startColumn][startRow] & (kRevealed | kFlagged)) return;
  game.cell[startColumn][startRow] |= kRevealed;
  queue[tail][0] = static_cast<uint8_t>(startColumn);
  queue[tail][1] = static_cast<uint8_t>(startRow);
  ++tail;

  while (head < tail) {
    const int column = queue[head][0];
    const int row = queue[head][1];
    ++head;

    if (neighbouringMines(game, column, row) != 0) continue;
    for (int dc = -1; dc <= 1; ++dc) {
      for (int dr = -1; dr <= 1; ++dr) {
        if (dc == 0 && dr == 0) continue;
        const int nc = column + dc;
        const int nr = row + dr;
        if (!inside(nc, nr)) continue;
        if (game.cell[nc][nr] & (kRevealed | kFlagged)) continue;
        game.cell[nc][nr] |= kRevealed;
        queue[tail][0] = static_cast<uint8_t>(nc);
        queue[tail][1] = static_cast<uint8_t>(nr);
        ++tail;
      }
    }
  }
}

// Whether a tap on this cell would do anything.
inline bool canReveal(const Game& game, const int column, const int row) {
  if (!inside(column, row)) return false;
  if (game.status == Status::Won || game.status == Status::Lost) return false;
  return (game.cell[column][row] & (kRevealed | kFlagged)) == 0;
}

// Reveal, and settle the outcome. Returns false and changes nothing when the
// tap is not legal, so a caller can route a tap straight here.
inline bool reveal(Game& game, const int column, const int row) {
  if (!canReveal(game, column, row)) return false;

  if (game.status == Status::Fresh) {
    layMines(game, column, row);
    game.status = Status::Playing;
  }

  if (game.cell[column][row] & kMine) {
    game.cell[column][row] |= kRevealed;
    game.status = Status::Lost;
    return true;
  }

  floodFrom(game, column, row);
  if (allSafeCellsRevealed(game)) game.status = Status::Won;
  return true;
}

// Plant or lift a flag. Never on a revealed cell, and never once the game is
// settled.
inline bool toggleFlag(Game& game, const int column, const int row) {
  if (!inside(column, row)) return false;
  if (game.status == Status::Won || game.status == Status::Lost) return false;
  if (game.cell[column][row] & kRevealed) return false;
  game.cell[column][row] ^= kFlagged;
  return true;
}

// Mines minus flags planted. Goes negative when the player over-flags, which is
// information rather than an error: it says the board disagrees with them.
inline int minesRemaining(const Game& game) { return kMines - flagCount(game); }

inline bool over(const Game& game) { return game.status == Status::Won || game.status == Status::Lost; }

}  // namespace minesweeper
