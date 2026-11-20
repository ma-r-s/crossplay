// Train Tracks, the rules and the solver, checked without a panel.
//
// The solver is held to an independent brute force on small grids: every
// simple path between the gates is enumerated directly, and every subset of
// squares is fed to the win check. Neither shares code with the solver, so a
// propagation rule that is unsound shows up as a count that disagrees.

#include <cstdio>
#include <cstring>

#include "TrainTracksCore.h"
#include "TrainTracksSolver.h"

using namespace traintracks;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

Solver solver;  // ~8KB; static, as the device will hold it

// testRatingIsPinned: tier 0..3 solved, then unsolved.
constexpr int kPinned[5] = {0, 226, 14, 120, 40};

int sideOutOf(const int size, const int r, const int c, Rng& rng) {
  int sides[4];
  int count = 0;
  if (r == 0) sides[count++] = kNorth;
  if (c == size - 1) sides[count++] = kEast;
  if (r == size - 1) sides[count++] = kSouth;
  if (c == 0) sides[count++] = kWest;
  return count ? sides[rng.below(count)] : -1;
}

bool onEdge(const int size, const int cell) {
  const int r = rowOf(cell);
  const int c = colOf(cell);
  return r == 0 || c == 0 || r == size - 1 || c == size - 1;
}

// A random self-avoiding walk from an edge square, cut at its last edge
// square. Test scaffolding only: Milestone 2 brings the real path builder.
bool randomPuzzle(Puzzle& p, const int size, Rng& rng, const int minLength) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    uint8_t path[kMaxCells];
    bool used[kMaxCells] = {};
    int r;
    int c;
    do {
      r = rng.below(size);
      c = rng.below(size);
    } while (!onEdge(size, cellIndex(r, c)));
    int length = 0;
    path[length++] = static_cast<uint8_t>(cellIndex(r, c));
    used[cellIndex(r, c)] = true;
    for (;;) {
      int options[4];
      int count = 0;
      for (int d = 0; d < 4; ++d) {
        const int r2 = r + kDRow[d];
        const int c2 = c + kDCol[d];
        if (r2 < 0 || c2 < 0 || r2 >= size || c2 >= size || used[cellIndex(r2, c2)]) continue;
        options[count++] = d;
      }
      if (count == 0 || rng.below(size * size) == 0) break;
      const int d = options[rng.below(count)];
      r += kDRow[d];
      c += kDCol[d];
      used[cellIndex(r, c)] = true;
      path[length++] = static_cast<uint8_t>(cellIndex(r, c));
    }
    while (length > 1 && !onEdge(size, path[length - 1])) --length;
    if (length < minLength) continue;
    const int startSide = sideOutOf(size, rowOf(path[0]), colOf(path[0]), rng);
    const int endSide = sideOutOf(size, rowOf(path[length - 1]), colOf(path[length - 1]), rng);
    if (buildFromPath(p, size, path, length, startSide, endSide)) return true;
  }
  return false;
}

void giveRandom(Puzzle& p, Rng& rng, const int percent) {
  for (int i = 0; i < kMaxCells; ++i) {
    p.given[i] = (p.solution[i] && rng.below(100) < percent) ? p.solution[i] : 0;
  }
}

void boardFromSolution(Board& b, const Puzzle& p) {
  resetBoard(b, p);
  for (int i = 0; i < kMaxCells; ++i) {
    if (p.solution[i]) b.cell[i] = kTrack;
  }
}

// --- the brute force -------------------------------------------------------

struct Brute {
  const Puzzle* p;
  bool used[kMaxCells];
  uint8_t path[kMaxCells];
  uint32_t sets[1 << 14];
  int setCount;
  bool overflow;
};

uint32_t setBit(const Puzzle& p, const int cell) { return 1u << (rowOf(cell) * p.size + colOf(cell)); }

void bruteAccept(Brute& b, const int length) {
  const Puzzle& p = *b.p;
  int rows[kMaxSize] = {};
  int cols[kMaxSize] = {};
  uint32_t set = 0;
  for (int i = 0; i < length; ++i) {
    ++rows[rowOf(b.path[i])];
    ++cols[colOf(b.path[i])];
    set |= setBit(p, b.path[i]);
  }
  for (int k = 0; k < p.size; ++k) {
    if (rows[k] != p.rowClue[k] || cols[k] != p.colClue[k]) return;
  }
  // Givens: present, with their exact shape.
  for (int cell = 0; cell < kMaxCells; ++cell) {
    if (!p.given[cell]) continue;
    int at = -1;
    for (int i = 0; i < length; ++i) {
      if (b.path[i] == cell) at = i;
    }
    if (at < 0) return;
    uint8_t mask = 0;
    if (at == 0) mask |= dirBit(p.start.side);
    if (at == length - 1) mask |= dirBit(p.end.side);
    for (const int j : {at - 1, at + 1}) {
      if (j < 0 || j >= length) continue;
      for (int d = 0; d < 4; ++d) {
        if (rowOf(b.path[j]) - rowOf(cell) == kDRow[d] && colOf(b.path[j]) - colOf(cell) == kDCol[d]) mask |= dirBit(d);
      }
    }
    if (mask != p.given[cell]) return;
  }
  for (int i = 0; i < b.setCount; ++i) {
    if (b.sets[i] == set) return;
  }
  if (b.setCount == static_cast<int>(sizeof(b.sets) / sizeof(b.sets[0]))) {
    b.overflow = true;
    return;
  }
  b.sets[b.setCount++] = set;
}

void bruteWalk(Brute& b, const int length) {
  const Puzzle& p = *b.p;
  const int cell = b.path[length - 1];
  if (cell == cellIndex(p.end.row, p.end.col)) {
    bruteAccept(b, length);
    return;
  }
  for (int d = 0; d < 4; ++d) {
    const int r = rowOf(cell) + kDRow[d];
    const int c = colOf(cell) + kDCol[d];
    if (!inside(p, r, c) || b.used[cellIndex(r, c)]) continue;
    b.used[cellIndex(r, c)] = true;
    b.path[length] = static_cast<uint8_t>(cellIndex(r, c));
    bruteWalk(b, length + 1);
    b.used[cellIndex(r, c)] = false;
  }
}

Brute brute;

int bruteCount(const Puzzle& p) {
  std::memset(&brute, 0, sizeof(brute));
  brute.p = &p;
  const int start = cellIndex(p.start.row, p.start.col);
  brute.used[start] = true;
  brute.path[0] = static_cast<uint8_t>(start);
  bruteWalk(brute, 1);
  return brute.overflow ? -1 : brute.setCount;
}

// --- tests -----------------------------------------------------------------

void testBuildFromPath() {
  // 4x4: in at the top of column 1, down, right, down to the bottom of col 2.
  //   . S . .
  //   . # # .
  //   . . # .
  //   . . E .
  const uint8_t path[] = {static_cast<uint8_t>(cellIndex(0, 1)), static_cast<uint8_t>(cellIndex(1, 1)),
                          static_cast<uint8_t>(cellIndex(1, 2)), static_cast<uint8_t>(cellIndex(2, 2)),
                          static_cast<uint8_t>(cellIndex(3, 2))};
  Puzzle p;
  CHECK(buildFromPath(p, 4, path, 5, kNorth, kSouth));
  CHECK(p.rowClue[0] == 1 && p.rowClue[1] == 2 && p.rowClue[2] == 1 && p.rowClue[3] == 1);
  CHECK(p.colClue[0] == 0 && p.colClue[1] == 2 && p.colClue[2] == 3 && p.colClue[3] == 0);
  CHECK(p.solution[cellIndex(0, 1)] == (dirBit(kNorth) | dirBit(kSouth)));
  CHECK(p.solution[cellIndex(1, 1)] == (dirBit(kNorth) | dirBit(kEast)));
  CHECK(p.solution[cellIndex(1, 2)] == (dirBit(kWest) | dirBit(kSouth)));
  CHECK(p.solution[cellIndex(3, 2)] == (dirBit(kNorth) | dirBit(kSouth)));
  CHECK(totalTrack(p) == 5);

  // A gate side pointing into the grid is not a gate.
  CHECK(!buildFromPath(p, 4, path, 5, kSouth, kSouth));
  // Not adjacent.
  const uint8_t gap[] = {static_cast<uint8_t>(cellIndex(0, 0)), static_cast<uint8_t>(cellIndex(0, 2))};
  CHECK(!buildFromPath(p, 4, gap, 2, kNorth, kNorth));
}

void testTapCycleAndGivens() {
  const uint8_t path[] = {static_cast<uint8_t>(cellIndex(0, 0)), static_cast<uint8_t>(cellIndex(0, 1)),
                          static_cast<uint8_t>(cellIndex(0, 2))};
  Puzzle p;
  CHECK(buildFromPath(p, 3, path, 3, kWest, kEast));
  p.given[cellIndex(0, 1)] = p.solution[cellIndex(0, 1)];
  CHECK(validPuzzle(p));
  Board b;
  resetBoard(b, p);
  CHECK(b.cell[cellIndex(0, 1)] == kTrack);
  CHECK(b.cell[cellIndex(1, 1)] == kEmpty);

  CHECK(tapCell(b, p, 1, 1) && b.cell[cellIndex(1, 1)] == kTrack);
  CHECK(tapCell(b, p, 1, 1) && b.cell[cellIndex(1, 1)] == kCross);
  CHECK(tapCell(b, p, 1, 1) && b.cell[cellIndex(1, 1)] == kEmpty);
  CHECK(!tapCell(b, p, 0, 1) && b.cell[cellIndex(0, 1)] == kTrack);  // given: locked
  CHECK(!tapCell(b, p, 3, 0));                                       // outside a 3x3
  CHECK(!tapCell(b, p, -1, 0));
  CHECK(b.history.count == 3);
}

void testUndoRedo() {
  Puzzle p;
  Rng rng{7};
  CHECK(randomPuzzle(p, 8, rng, 6));
  Board b;
  resetBoard(b, p);
  CHECK(!canUndo(b) && !canRedo(b) && undo(b) < 0 && redo(b) < 0);

  tapCell(b, p, 2, 3);
  tapCell(b, p, 2, 3);
  tapCell(b, p, 5, 5);
  CHECK(b.cell[cellIndex(2, 3)] == kCross && b.cell[cellIndex(5, 5)] == kTrack);
  CHECK(undo(b) == cellIndex(5, 5) && b.cell[cellIndex(5, 5)] == kEmpty);
  CHECK(undo(b) == cellIndex(2, 3) && b.cell[cellIndex(2, 3)] == kTrack);
  CHECK(canRedo(b));
  CHECK(redo(b) == cellIndex(2, 3) && b.cell[cellIndex(2, 3)] == kCross);
  // A new move drops the redo branch.
  CHECK(tapCell(b, p, 0, 7));
  CHECK(!canRedo(b) && redo(b) < 0);
  CHECK(b.cell[cellIndex(5, 5)] == kEmpty);
  // ...and the dropped move is gone from the history, not hidden in it.
  CHECK(undo(b) == cellIndex(0, 7));
  CHECK(undo(b) == cellIndex(2, 3) && b.cell[cellIndex(2, 3)] == kTrack);
  CHECK(undo(b) == cellIndex(2, 3) && b.cell[cellIndex(2, 3)] == kEmpty);
  CHECK(undo(b) < 0);

  // The ring keeps the newest kHistoryDepth moves. 100 moves on squares that
  // are never givens: undoing all the way must stop after 64 and leave the
  // board as it stood after move 36.
  Puzzle open;
  const uint8_t edgePath[] = {static_cast<uint8_t>(cellIndex(0, 0)), static_cast<uint8_t>(cellIndex(0, 1))};
  CHECK(buildFromPath(open, 10, edgePath, 2, kWest, kNorth));
  resetBoard(b, open);
  Board snapshot{};
  for (int m = 0; m < 100; ++m) {
    const int r = 2 + (m * 7) % 8;
    const int c = (m * 3) % 10;
    CHECK(tapCell(b, open, r, c));
    if (m == 35) snapshot = b;
  }
  int undone = 0;
  while (undo(b) >= 0) ++undone;
  CHECK(undone == kHistoryDepth);
  CHECK(std::memcmp(b.cell, snapshot.cell, sizeof(b.cell)) == 0);
  int redone = 0;
  while (redo(b) >= 0) ++redone;
  CHECK(redone == kHistoryDepth);
}

void testClueStates() {
  CHECK(clueState(1, 2) == ClueState::Under);
  CHECK(clueState(2, 2) == ClueState::Met);
  CHECK(clueState(3, 2) == ClueState::Over);
  Puzzle p;
  Rng rng{11};
  CHECK(randomPuzzle(p, 6, rng, 8));
  Board b;
  boardFromSolution(b, p);
  CHECK(allCluesMet(b, p));
  for (int k = 0; k < p.size; ++k) {
    CHECK(rowCount(b, p, k) == p.rowClue[k] && colCount(b, p, k) == p.colClue[k]);
  }
}

void testInferShapes() {
  // A straight run, a corner, and the gate stubs.
  //   S # +
  //   . . #
  //   . . E
  const uint8_t path[] = {static_cast<uint8_t>(cellIndex(0, 0)), static_cast<uint8_t>(cellIndex(0, 1)),
                          static_cast<uint8_t>(cellIndex(0, 2)), static_cast<uint8_t>(cellIndex(1, 2)),
                          static_cast<uint8_t>(cellIndex(2, 2))};
  Puzzle p;
  CHECK(buildFromPath(p, 3, path, 5, kWest, kSouth));
  Board b;
  boardFromSolution(b, p);
  uint8_t mask[kMaxCells];
  inferShapes(b, p, mask);
  for (int i = 0; i < kMaxCells; ++i) CHECK(mask[i] == p.solution[i]);

  // A U through a 2x2 block, gates on top. Every square touches two others,
  // and the two top squares also have a gate, so they start with three
  // candidates. Saturation resolves them: each has its gate plus the settled
  // square below, so the sideways candidate between them is dropped.
  Puzzle q;
  const uint8_t u[] = {static_cast<uint8_t>(cellIndex(0, 0)), static_cast<uint8_t>(cellIndex(1, 0)),
                       static_cast<uint8_t>(cellIndex(1, 1)), static_cast<uint8_t>(cellIndex(0, 1))};
  CHECK(buildFromPath(q, 4, u, 4, kWest, kNorth));
  Board block;
  boardFromSolution(block, q);
  inferShapes(block, q, mask);
  for (int i = 0; i < kMaxCells; ++i) CHECK(mask[i] == q.solution[i]);

  // A plus and a T: the centre has more than two settled neighbours, so it
  // stays ambiguous and draws as a block.
  Board plus;
  resetBoard(plus, q);
  for (const int i : {cellIndex(2, 2), cellIndex(1, 2), cellIndex(3, 2), cellIndex(2, 1), cellIndex(2, 3)}) {
    plus.cell[i] = kTrack;
  }
  inferShapes(plus, q, mask);
  CHECK(popcount4(mask[cellIndex(2, 2)]) == 4);
  plus.cell[cellIndex(2, 3)] = kEmpty;
  inferShapes(plus, q, mask);
  CHECK(popcount4(mask[cellIndex(2, 2)]) == 3);
  CHECK(mask[cellIndex(2, 1)] == dirBit(kEast));  // an arm draws its one stub

  // A given's shape cuts a neighbour's candidate toward it.
  //   . . . .
  //   . . . .
  //   . # G .     G is a given vertical straight; # beside it must not join it
  //   . . # .
  Puzzle r = q;
  r.given[cellIndex(2, 2)] = dirBit(kNorth) | dirBit(kSouth);
  Board beside;
  resetBoard(beside, r);
  beside.cell[cellIndex(2, 1)] = kTrack;
  beside.cell[cellIndex(3, 2)] = kTrack;
  inferShapes(beside, r, mask);
  CHECK(mask[cellIndex(2, 1)] == 0);
  CHECK(mask[cellIndex(3, 2)] == dirBit(kNorth));

  // Stubs: a lone mark with no neighbours draws nothing; a mark beside the
  // start gate square draws one connection toward it.
  Board lone;
  resetBoard(lone, p);
  lone.cell[cellIndex(2, 0)] = kTrack;
  inferShapes(lone, p, mask);
  CHECK(mask[cellIndex(2, 0)] == 0);
}

void testWinOnSolutionsAndNearMisses() {
  Rng rng{2026};
  for (int trial = 0; trial < 300; ++trial) {
    Puzzle p;
    const int size = 4 + trial % 7;
    CHECK(randomPuzzle(p, size, rng, size));
    giveRandom(p, rng, 20);
    Board b;
    boardFromSolution(b, p);
    CHECK(isWin(p, b, solver));

    // Remove one non-given track square: a clue goes short.
    for (int i = 0; i < kMaxCells; ++i) {
      if (p.solution[i] && !p.given[i]) {
        b.cell[i] = kCross;
        CHECK(!isWin(p, b, solver));
        b.cell[i] = kTrack;
        break;
      }
    }
    // Crosses are notes only: crossing every non-track square still wins.
    for (int r = 0; r < size; ++r) {
      for (int c = 0; c < size; ++c) {
        if (!p.solution[cellIndex(r, c)]) b.cell[cellIndex(r, c)] = kCross;
      }
    }
    CHECK(isWin(p, b, solver));
  }
}

void testGivenShapeIsARule() {
  // A U through a 2x3 block, in at the top left, out at the top right:
  //   S . E
  //   # # #
  // The same squares carry only one path, so instead pin a given whose shape
  // disagrees with that path: the puzzle then has no solution and the
  // solution board must not win.
  const uint8_t path[] = {static_cast<uint8_t>(cellIndex(0, 0)), static_cast<uint8_t>(cellIndex(1, 0)),
                          static_cast<uint8_t>(cellIndex(1, 1)), static_cast<uint8_t>(cellIndex(1, 2)),
                          static_cast<uint8_t>(cellIndex(0, 2))};
  Puzzle p;
  CHECK(buildFromPath(p, 3, path, 5, kNorth, kNorth));
  Board b;
  boardFromSolution(b, p);
  CHECK(isWin(p, b, solver));
  p.given[cellIndex(1, 1)] = dirBit(kNorth) | dirBit(kSouth);  // a vertical straight where the track runs across
  CHECK(!validPuzzle(p) || !isWin(p, b, solver));
  p.given[cellIndex(1, 1)] = dirBit(kWest) | dirBit(kNorth);
  CHECK(validPuzzle(p));
  CHECK(!isWin(p, b, solver));
}

// Every subset of squares on a small grid against the brute force: the win
// check accepts exactly the sets the enumeration found.
void testWinAgainstEverySubset() {
  Rng rng{99};
  int checked = 0;
  int ambiguousSeen = 0;
  for (int trial = 0; trial < 2000 && (checked < 30 || ambiguousSeen < 5); ++trial) {
    Puzzle p;
    CHECK(randomPuzzle(p, 4, rng, 4));
    if (trial % 2) giveRandom(p, rng, 25);
    const int count = bruteCount(p);
    CHECK(count >= 1);
    // Every puzzle until 30 are done, then only ambiguous ones until 5 of
    // those have been through.
    if (checked >= 30 && count < 2) continue;
    ++checked;
    if (count > 1) ++ambiguousSeen;
    int wins = 0;
    for (uint32_t subset = 0; subset < (1u << 16); ++subset) {
      Board b;
      resetBoard(b, p);
      bool skip = false;
      for (int k = 0; k < 16; ++k) {
        const int cell = cellIndex(k / 4, k % 4);
        const bool on = subset & (1u << k);
        if (p.given[cell] && !on) skip = true;
        b.cell[cell] = on ? kTrack : kEmpty;
      }
      if (skip || !allCluesMet(b, p)) continue;
      const bool win = isWin(p, b, solver);
      bool inBrute = false;
      for (int i = 0; i < brute.setCount; ++i) inBrute = inBrute || brute.sets[i] == subset;
      CHECK(win == inBrute);
      wins += win;
    }
    CHECK(wins == count);
  }
  // Any valid solution wins, not only the generated one: make sure the sample
  // actually contained puzzles with more than one.
  CHECK(ambiguousSeen > 0);
}

// countSolutions against the brute force on 4x4 and 5x5, with and without
// givens.
void testCountAgainstBruteForce() {
  Rng rng{31337};
  int unique = 0;
  int multiple = 0;
  for (int trial = 0; trial < 400; ++trial) {
    Puzzle p;
    const int size = trial < 200 ? 4 : 5;
    CHECK(randomPuzzle(p, size, rng, size));
    giveRandom(p, rng, trial % 3 == 0 ? 0 : 15);
    const int expected = bruteCount(p);
    CHECK(expected >= 1);
    CHECK(solver.load(p));
    const int got = solver.countSolutions(100000);
    if (got != expected) std::printf("  trial %d size %d: solver %d brute %d\n", trial, size, got, expected);
    CHECK(got == expected);
    unique += expected == 1;
    multiple += expected > 1;
  }
  CHECK(unique > 0 && multiple > 0);
}

// Deductions are sound: whatever the tiers decide agrees with the generated
// solution, at every tier and every size. And tiers are monotonic.
void testDeductionsAreSound() {
  Rng rng{424242};
  int byTier[4] = {};
  int unsolved = 0;
  for (int trial = 0; trial < 600; ++trial) {
    Puzzle p;
    const int size = 4 + trial % 9;
    CHECK(randomPuzzle(p, size, rng, size + size / 2));
    giveRandom(p, rng, trial % 4 == 0 ? 0 : 5 + trial % 30);
    bool solvedAt[4] = {};
    for (int tier = 1; tier <= 3; ++tier) {
      CHECK(solver.load(p));
      const Rating rating = solver.deduce(tier);
      solvedAt[tier] = rating.solved;
      CHECK(rating.tier <= tier);
      for (int r = 0; r < size; ++r) {
        for (int c = 0; c < size; ++c) {
          const uint8_t s = solver.square(r, c);
          if (s == kUnknown) continue;
          const bool track = p.solution[cellIndex(r, c)] != 0;
          // A sound deduction holds for every solution, so it holds for the
          // generated one even when the puzzle is not unique.
          CHECK((s == kYes) == track);
        }
      }
    }
    CHECK(!solvedAt[1] || solvedAt[2]);
    CHECK(!solvedAt[2] || solvedAt[3]);
    CHECK(solver.load(p));
    const Rating rating = solver.deduce(3);
    if (rating.solved) {
      ++byTier[rating.tier];
      // Solved by deduction means the track set is unique.
      CHECK(solver.load(p));
      CHECK(solver.countSolutions(2) == 1);
    } else {
      ++unsolved;
    }
  }
  std::printf("  deduce: tier0 %d, tier1 %d, tier2 %d, tier3 %d, unsolved %d\n", byTier[0], byTier[1], byTier[2],
              byTier[3], unsolved);
  // Each tier is reachable, so a rating of 2 or 3 can be produced at all.
  CHECK(byTier[1] > 0 && byTier[2] > 0 && byTier[3] > 0);
}

// The rating is part of what a seed means: the generator keeps or rejects a
// puzzle by it, so a seed saved under one rater regenerates a different puzzle
// under another. Weakening or strengthening any rule moves these numbers. When
// that is intended, update them AND bump the save format version.
void testRatingIsPinned() {
  Rng rng{8086};
  int byTier[4] = {};
  int unsolved = 0;
  for (int trial = 0; trial < 400; ++trial) {
    Puzzle p;
    const int size = 5 + trial % 6;
    CHECK(randomPuzzle(p, size, rng, size + size / 2));
    giveRandom(p, rng, trial % 3 == 0 ? 0 : 8);
    CHECK(solver.load(p));
    const Rating rating = solver.deduce(3);
    if (rating.solved) {
      ++byTier[rating.tier];
    } else {
      ++unsolved;
    }
  }
  std::printf("  pinned: tier1 %d, tier2 %d, tier3 %d, unsolved %d\n", byTier[1], byTier[2], byTier[3], unsolved);
  CHECK(byTier[0] == kPinned[0] && byTier[1] == kPinned[1] && byTier[2] == kPinned[2] && byTier[3] == kPinned[3] &&
        unsolved == kPinned[4]);
}

void testFullyGivenIsTierOne() {
  Rng rng{5};
  for (int trial = 0; trial < 100; ++trial) {
    Puzzle p;
    const int size = 3 + trial % 10;
    CHECK(randomPuzzle(p, size, rng, size));
    giveRandom(p, rng, 100);
    CHECK(solver.load(p));
    const Rating rating = solver.deduce(1);
    CHECK(rating.solved && rating.tier <= 1);
    CHECK(solver.load(p));
    CHECK(solver.countSolutions(2) == 1);
  }
}

void testContradictoryPuzzle() {
  // Clues that no path can meet: a row clue larger than the grid allows once
  // the column clues are zero.
  const uint8_t path[] = {static_cast<uint8_t>(cellIndex(0, 0)), static_cast<uint8_t>(cellIndex(0, 1)),
                          static_cast<uint8_t>(cellIndex(0, 2))};
  Puzzle p;
  CHECK(buildFromPath(p, 3, path, 3, kWest, kEast));
  p.rowClue[0] = 2;
  p.rowClue[1] = 1;
  CHECK(solver.load(p));
  CHECK(solver.countSolutions(10) == 0);
  CHECK(!solver.deduce(3).solved);
}

void testDeterministicRng() {
  Rng a{12345};
  Rng b{12345};
  for (int i = 0; i < 1000; ++i) CHECK(a.next() == b.next());
  Rng c{1};
  for (int i = 0; i < 10000; ++i) {
    const int x = c.below(7);
    CHECK(x >= 0 && x < 7);
  }
  // Pinned values: a change here changes every saved game's puzzle.
  Rng d{0};
  CHECK(d.next() == 0x92CA2F0Eu);
}

}  // namespace

int main() {
  testBuildFromPath();
  testTapCycleAndGivens();
  testUndoRedo();
  testClueStates();
  testInferShapes();
  testWinOnSolutionsAndNearMisses();
  testGivenShapeIsARule();
  testWinAgainstEverySubset();
  testCountAgainstBruteForce();
  testDeductionsAreSound();
  testRatingIsPinned();
  testFullyGivenIsTierOne();
  testContradictoryPuzzle();
  testDeterministicRng();
  std::printf("traintracks: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
