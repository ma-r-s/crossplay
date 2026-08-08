#pragma once

// Knucklebones, the rules. Freestanding: no renderer, no Activity, no storage.
//
// Two players, a three by three grid each, and one die. On your turn you roll,
// then drop the die into one of your three columns. Placing a value into a
// column destroys every copy of that value in the *opponent's* matching column.
// A column scores each value as v * n * n, where n is how many copies of v it
// holds -- so three fours are 36 rather than 12, and that multiplier is the
// whole game. It ends the moment either grid fills; the higher total wins.
//
// The shape follows Jaipur's, for the same reasons:
//
//   * `Game` is the whole shared state AND the wire format, so two devices
//     cannot drift. It is a POD with no pointers and no padding worth hiding.
//   * Everything derivable is derived: scores, whether a column has room, who
//     is winning, whether it is over. A stored total that disagrees with the
//     grid is a bug class that does not exist here.
//   * The die is part of the state, not a return value. Both devices have to
//     agree on what was rolled before it is placed, and a roll that lived only
//     in a local would have to be sent separately and could go missing.

#include <cstdint>

namespace knucklebones {

constexpr int kColumns = 3;
// Slots per column. Index 0 is the near end, and dice stack away from it, so a
// column is always full from 0 upward with no holes -- see compact().
constexpr int kRows = 3;
constexpr int kSeats = 2;
constexpr int kFaces = 6;

// Die values are 1..6, so zero reads as "no die here" without a second array or
// a sentinel that could collide with a real face.
constexpr uint8_t kEmpty = 0;

struct Grid {
  uint8_t cell[kColumns][kRows];
};

struct Game {
  Grid grid[kSeats];
  // Whose turn it is, and the value they have already rolled and must now
  // place. Rolling happens when a turn begins, not when a placement is made,
  // because the player has to see the die before choosing a column.
  uint8_t turn;
  uint8_t die;
  uint32_t rng;
};

// xorshift32. Seeded by the caller and never by a clock in here: a core that
// reaches for the time cannot be replayed, and both devices in a match have to
// deal the same game from the same seed.
inline uint32_t nextRandom(uint32_t& state) {
  if (state == 0) state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

inline uint8_t roll(uint32_t& state) { return static_cast<uint8_t>(nextRandom(state) % kFaces + 1); }

// How many dice a column holds. Because columns compact, this is also the index
// the next die lands in.
inline int columnCount(const Grid& grid, const int column) {
  int used = 0;
  for (int row = 0; row < kRows; ++row) {
    if (grid.cell[column][row] != kEmpty) ++used;
  }
  return used;
}

inline bool columnHasRoom(const Grid& grid, const int column) { return columnCount(grid, column) < kRows; }

inline bool full(const Grid& grid) {
  for (int column = 0; column < kColumns; ++column) {
    if (columnHasRoom(grid, column)) return false;
  }
  return true;
}

// What one column is worth. Each distinct value scores v * n * n, so the pair
// in [3,3,5] is worth 12 and the loose 5 is worth 5.
//
// Counted by face rather than by scanning for neighbours, because matching dice
// score together wherever they sit in the column -- gravity is a layout rule,
// not a scoring one.
inline int columnScore(const Grid& grid, const int column) {
  int seen[kFaces + 1] = {};
  for (int row = 0; row < kRows; ++row) {
    const uint8_t value = grid.cell[column][row];
    if (value != kEmpty) ++seen[value];
  }
  int total = 0;
  for (int face = 1; face <= kFaces; ++face) {
    total += face * seen[face] * seen[face];
  }
  return total;
}

inline int score(const Grid& grid) {
  int total = 0;
  for (int column = 0; column < kColumns; ++column) total += columnScore(grid, column);
  return total;
}

// Pull the dice in a column back down to index 0, preserving their order.
//
// Needed because destruction removes dice from the middle. A column with a hole
// in it would make columnCount lie about where the next die lands, and would
// draw a die floating above a gap.
inline void compact(Grid& grid, const int column) {
  uint8_t packed[kRows] = {};
  int write = 0;
  for (int row = 0; row < kRows; ++row) {
    if (grid.cell[column][row] != kEmpty) packed[write++] = grid.cell[column][row];
  }
  for (int row = 0; row < kRows; ++row) grid.cell[column][row] = row < write ? packed[row] : kEmpty;
}

inline void start(Game& game, const uint32_t seed) {
  for (int seat = 0; seat < kSeats; ++seat) {
    for (int column = 0; column < kColumns; ++column) {
      for (int row = 0; row < kRows; ++row) game.grid[seat].cell[column][row] = kEmpty;
    }
  }
  game.turn = 0;
  game.rng = seed;
  game.die = roll(game.rng);
}

inline bool over(const Game& game) { return full(game.grid[0]) || full(game.grid[1]); }

inline bool canPlace(const Game& game, const int column) {
  if (over(game) || column < 0 || column >= kColumns) return false;
  return columnHasRoom(game.grid[game.turn], column);
}

// Place the rolled die, destroy the opponent's matching dice in the same
// column, then hand over with a fresh roll.
//
// Returns false and changes nothing if the move is not legal, so a caller can
// route a tap straight here without pre-checking and cannot desync a match by
// applying half of an illegal move.
inline bool place(Game& game, const int column) {
  if (!canPlace(game, column)) return false;

  const int seat = game.turn;
  const int other = 1 - seat;
  const uint8_t value = game.die;

  game.grid[seat].cell[column][columnCount(game.grid[seat], column)] = value;

  // The destruction is what makes a column a contest rather than two solitaire
  // boards side by side: it only ever reaches the *same* column, and only that
  // one value.
  for (int row = 0; row < kRows; ++row) {
    if (game.grid[other].cell[column][row] == value) game.grid[other].cell[column][row] = kEmpty;
  }
  compact(game.grid[other], column);

  if (over(game)) {
    // No roll: there is nobody to place it. Leaving a stale die in the state
    // would draw a die nobody can put anywhere on the final screen.
    game.die = kEmpty;
    return true;
  }
  game.turn = static_cast<uint8_t>(other);
  game.die = roll(game.rng);
  return true;
}

// The seat that won, or -1 for a draw. Only meaningful once over().
inline int winner(const Game& game) {
  const int a = score(game.grid[0]);
  const int b = score(game.grid[1]);
  if (a == b) return -1;
  return a > b ? 0 : 1;
}

}  // namespace knucklebones
