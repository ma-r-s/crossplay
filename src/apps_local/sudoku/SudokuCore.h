#pragma once

// Sudoku, the rules. Freestanding: no renderer, no Activity, no storage, no
// heap. Everything here runs on a laptop under host-tests/sudoku/run.sh.
//
// The whole file exists to serve one promise: **a puzzle is exactly as hard as
// its label says, by construction.** Not sampled for, not estimated from the
// clue count. Two facts make that possible.
//
//   * A puzzle that a technique solver drives to completion is UNIQUE by
//     definition, because every step it took was forced. So there is no
//     separate solution counter anywhere in this file: uniqueness is a
//     by-product of the thing we already have to run.
//   * Difficulty is the hardest technique the puzzle REQUIRES, and that is
//     computed FROM the finished puzzle rather than hoped for. The ceiling is
//     constructed: the dig is driven by a bounded solver, so a puzzle can never
//     need a technique above its band. The floor is proved: `carve` grades what
//     it produced and the label is that measurement. A puzzle whose grade does
//     not match what was asked for is discarded, never relabelled, so nothing
//     unverified ever reaches a player.
//
// Nothing here knows what a screen is. The player's own marks, undo and clock
// live in SudokuGame.h on top of this.

#include <cstdint>

namespace sudoku {

constexpr int kSize = 9;
constexpr int kCells = kSize * kSize;
constexpr int kBoxSize = 3;
constexpr int kUnits = 27;  // 9 rows, 9 columns, 9 boxes

// A set of candidate digits. Bit 0 is the digit 1. Nine bits used, so a cell's
// candidates and a digit's positions inside a unit are the same type and the
// same operations, which is most of why the harder techniques stay short.
using Mask = uint16_t;
constexpr Mask kAllDigits = 0x1FF;

constexpr Mask bitFor(const int digit) { return static_cast<Mask>(1u << (digit - 1)); }

constexpr int popcount(Mask mask) {
  int count = 0;
  while (mask != 0) {
    mask = static_cast<Mask>(mask & (mask - 1));
    ++count;
  }
  return count;
}

// The lowest digit in a mask, or 0 for an empty one.
constexpr int lowestDigit(const Mask mask) {
  for (int digit = 1; digit <= kSize; ++digit) {
    if (mask & bitFor(digit)) return digit;
  }
  return 0;
}

constexpr int rowOf(const int cell) { return cell / kSize; }
constexpr int columnOf(const int cell) { return cell % kSize; }
constexpr int boxOf(const int cell) { return (rowOf(cell) / kBoxSize) * kBoxSize + columnOf(cell) / kBoxSize; }
constexpr int cellAt(const int row, const int column) { return row * kSize + column; }

// Unit `u` is row u for u < 9, column u-9 for u < 18, and box u-18 above that.
// One indexing scheme for all three is what lets every technique below be
// written once instead of three times.
constexpr int unitCell(const int unit, const int slot) {
  if (unit < kSize) return cellAt(unit, slot);
  if (unit < 2 * kSize) return cellAt(slot, unit - kSize);
  const int box = unit - 2 * kSize;
  return cellAt((box / kBoxSize) * kBoxSize + slot / kBoxSize, (box % kBoxSize) * kBoxSize + slot % kBoxSize);
}

constexpr int rowUnit(const int row) { return row; }
constexpr int columnUnit(const int column) { return kSize + column; }
constexpr int boxUnit(const int box) { return 2 * kSize + box; }

// Two cells are peers when they share a row, a column or a box. A cell is not
// its own peer.
constexpr bool arePeers(const int a, const int b) {
  if (a == b) return false;
  return rowOf(a) == rowOf(b) || columnOf(a) == columnOf(b) || boxOf(a) == boxOf(b);
}

// The ladder. Ordered by how hard a human finds it, because that order IS the
// difficulty scale: `solve()` always tries the cheapest technique that works,
// so the hardest one it ever needed is a real measurement rather than a label.
//
// Adding a rung means inserting it in difficulty order and extending
// `apply()`; the Level mapping below is the only other place that cares.
enum class Technique : uint8_t {
  None = 0,
  NakedSingle,       // this cell has one candidate left
  HiddenSingle,      // this digit has one home left in its row, column or box
  LockedCandidates,  // pointing and claiming: a digit confined to an intersection
  NakedPair,         // two cells in a unit sharing the same two candidates
  HiddenPair,        // two digits in a unit confined to the same two cells
  NakedTriple,       // three cells in a unit whose candidates span three digits
  HiddenTriple,      // three digits in a unit confined to the same three cells
  XWing,             // a digit boxed into a rectangle across two lines
  XYWing,            // a two-candidate pivot with two pincers agreeing on a third
  Swordfish,         // the three-line fish
};
constexpr int kTechniqueCount = 11;

// Never longer than kMaxNoticeChars: these are drawn into the board's status
// capsule, which truncates with a glyph this face does not have.
constexpr int kMaxNoticeChars = 11;

const char* techniqueName(Technique technique);

enum class Level : uint8_t { Easy = 0, Medium, Hard, Expert };
constexpr int kLevelCount = 4;

const char* levelName(Level level);

// The hardest technique a level is allowed to need. The bands are the
// conventional ones: singles alone are easy, an intersection is the first thing
// that feels like technique, subsets are the middle, and everything above them
// is the top.
//
// Expert is DELIBERATELY the widest band, and the width is a measurement rather
// than taste. A level is only reachable when its floor technique is genuinely
// needed by real puzzles, and each of triples, X-Wing, XY-Wing and Swordfish is
// individually uncommon. With Expert holding only NakedTriple and XWing the
// generator carved one puzzle in forty attempts at 939ms a puzzle; the fix was
// to give the band more floors to land on, not to weaken what EXPERT promises.
// host-tests/sudoku prints the rate, so a future narrowing shows up there.
constexpr Technique ceilingFor(const Level level) {
  switch (level) {
    case Level::Easy:
      return Technique::HiddenSingle;
    case Level::Medium:
      return Technique::LockedCandidates;
    case Level::Hard:
      return Technique::HiddenPair;
    case Level::Expert:
      return Technique::Swordfish;
  }
  return Technique::HiddenSingle;
}

// The inverse: which level a puzzle lands in, given the hardest technique it
// actually required. `ceilingFor(levelOf(t)) >= t` for every t, and the two
// agree at the band edges; host tests pin both directions.
constexpr Level levelOf(const Technique technique) {
  switch (technique) {
    case Technique::None:
    case Technique::NakedSingle:
    case Technique::HiddenSingle:
      return Level::Easy;
    case Technique::LockedCandidates:
      return Level::Medium;
    case Technique::NakedPair:
    case Technique::HiddenPair:
      return Level::Hard;
    case Technique::NakedTriple:
    case Technique::HiddenTriple:
    case Technique::XWing:
    case Technique::XYWing:
    case Technique::Swordfish:
      return Level::Expert;
  }
  return Level::Easy;
}

// The solver's working position: what is known, and what is still possible.
// `candidate[cell]` is meaningless where `value[cell]` is set, and is kept at 0
// there so a stale bit cannot be read by mistake.
struct Grid {
  uint8_t value[kCells];
  Mask candidate[kCells];
};

struct SolveReport {
  bool solved = false;
  // True when the position contradicted itself: a cell with no candidates, or
  // two of a digit in one unit. Distinct from "ran out of technique", which is
  // an ordinary outcome for a puzzle above its ceiling.
  bool broken = false;
  Technique hardest = Technique::None;
  // How many times a technique fired. Deterministic, so a test can assert on
  // it, and it is the honest unit for a work budget: see docs/apps/sudoku.md.
  uint16_t steps = 0;
};

// Empty grid: every cell open, every digit possible.
void clear(Grid& grid);

// Place `digit` in `cell` and strike it from every peer. Returns false when
// that contradicts the position, which leaves the grid unusable: callers either
// abandon it or restore from a copy.
bool assign(Grid& grid, int cell, int digit);

// Build a position from a clue array (0 for an empty cell). Returns false when
// the clues already contradict each other.
bool load(Grid& grid, const uint8_t given[kCells]);

bool isComplete(const Grid& grid);

// Drive the position as far as techniques up to `ceiling` will take it.
//
// Always applies the CHEAPEST technique that fires, restarting the ladder after
// each one, so `hardest` is the hardest technique the puzzle genuinely needed
// and never merely the hardest one that happened to be tried. That property is
// what the whole difficulty scale rests on.
SolveReport solve(Grid& grid, Technique ceiling);

// A generated puzzle and its answer.
struct Puzzle {
  uint8_t given[kCells] = {};     // 0 where the player has to fill
  uint8_t solution[kCells] = {};  // never 0
  Level level = Level::Easy;
  Technique hardest = Technique::None;
  uint8_t clues = 0;
  uint32_t seed = 0;
};

// Scratch for the generator. Owned by the caller so nothing here needs the heap
// and nothing sits in .bss: about 1.1KB, which is a member of the Activity.
struct Workspace {
  uint8_t order[kCells][kSize];
  uint8_t next[kCells];
  uint8_t value[kCells];
  uint8_t dig[kCells];
  Grid grid;
  Grid probe;
};

// Fill an empty grid at random. The base solution every puzzle is carved out of.
bool fillComplete(uint8_t solution[kCells], Workspace& work, uint32_t& rng);

// Carve one puzzle out of a full grid and grade it. The level is an OUTPUT: you
// do not ask for a difficulty here, you find out what you made.
//
// Clues come out in 180-degree rotational pairs, which is what every printed
// Sudoku does and what makes the grid worth leaving on a desk. A pair is
// removed only when the remaining clues still drive the bounded solver to a
// finish, so the dig is maximal: once it stops, no further pair can come out.
// (Re-sweeping in another order finds nothing, because removing clues only ever
// makes a puzzle harder, so a pair rejected once stays rejected.)
//
// The dig always runs at the FULL ceiling rather than at some target's ceiling.
// That costs nothing and wastes nothing: a dig that lands below the level you
// wanted is a perfectly good puzzle for the level it did land in, and running
// the dig any narrower only throws away information.
bool carve(Puzzle& out, const uint8_t solution[kCells], Workspace& work, uint32_t& rng);

// fillComplete plus carve, discarding puzzles that do not grade as `level`.
//
// The measured distribution of a symmetric maximal dig, over 1500 grids, is
// roughly 83% Easy, 10% Medium, 3% Hard and 4% Expert: singles dominate because
// a symmetric dig stops at about 28 clues. So Easy costs about one attempt and
// Hard about thirty, at well under a millisecond each. The numbers are printed
// by host-tests/sudoku on every run, so a change that wrecks them is visible
// rather than silent.
//
// `attempts` is a hard bound and the caller is expected to loop across render
// passes rather than raise it: see SudokuActivity, which keeps GENERATING on
// screen instead of pinning the main loop.
bool generate(Puzzle& out, Level level, Workspace& work, uint32_t& rng, int attempts = 24);

// Naming a cell the player could fill right now, and the rule that proves it.
// The hint system and the difficulty grader are the same solver, which is why
// hints cost almost nothing once the ladder exists.
struct Hint {
  bool found = false;
  int cell = -1;
  int digit = 0;
  Technique technique = Technique::None;
  // The unit the deduction was made in, for the screen to outline. -1 when the
  // technique is not about one unit (a naked single is about the cell alone).
  int unit = -1;
};

// The next thing a solver would do to `given`, starting from the clues plus
// whatever the player has already entered correctly.
Hint nextHint(const uint8_t given[kCells], Technique ceiling);

// xorshift32. Same core as every other generator in this fork, kept here so the
// rules stay freestanding.
inline uint32_t nextRandom(uint32_t& state) {
  if (state == 0) state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

}  // namespace sudoku
