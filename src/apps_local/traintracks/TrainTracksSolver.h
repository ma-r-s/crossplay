#pragma once

// Train Tracks, the solver. Freestanding C++17, no heap, no recursion.
//
// The unknowns are every square (track or not) and every edge between two
// adjacent squares (connected or not). Edges on the grid boundary are
// constants: connected at a gate, not connected anywhere else. Deductions are
// tiered, so the same code rates puzzles and will drive hints:
//
//   Tier 1  Line counts (a full line closes, a line with just enough room
//           fills); a non-track square has no connections; a connected edge
//           makes both squares track; a track square takes exactly two
//           connections (forced corners and straights).
//   Tier 2  Dead ends (a square with fewer than two possible connections is
//           not track) and chain ends (an edge that would close a loop, or
//           join the start to the end while track is still missing, is cut).
//   Tier 3  One-step lookahead: assume a value, propagate tiers 1-2, and keep
//           the opposite on contradiction.
//
// `countSolutions` is a bounded exhaustive search that counts distinct sets of
// track squares, which is what the player marks and what must be unique. Two
// paths over the same squares count once.
//
// Undo is a trail: every assignment records the old value, and backtracking
// unwinds to a mark. The search keeps its own decision stack rather than
// recursing, so its stack use is flat on the device.

#include <cstdint>
#include <initializer_list>

#include "TrainTracksCore.h"

namespace traintracks {

enum Value : uint8_t {
  kUnknown = 0,
  kYes = 1,
  kNo = 2,
};

// The hardest tier a deduction needed. Tier numbers map to difficulty:
// 1 = Easy, 2 = Medium, 3 = Hard.
struct Rating {
  bool solved;   // every square decided by deduction alone
  uint8_t tier;  // hardest tier used (0 if nothing was needed)
};

class Solver {
 public:
  static constexpr int kVars = 3 * kMaxCells;

  // Loads clues, gates and givens. False if the givens contradict each other
  // or the gates.
  bool load(const Puzzle& puzzle) {
    p = &puzzle;
    n = puzzle.size;
    total = totalTrack(puzzle);
    trailLen = 0;
    lookCursor = 0;
    for (int i = 0; i < kVars; ++i) v[i] = kNo;
    for (int r = 0; r < n; ++r) {
      for (int c = 0; c < n; ++c) {
        v[cellIndex(r, c)] = kUnknown;
        if (c + 1 < n) v[kMaxCells + cellIndex(r, c)] = kUnknown;
        if (r + 1 < n) v[2 * kMaxCells + cellIndex(r, c)] = kUnknown;
      }
    }
    for (int r = 0; r < n; ++r) {
      for (int c = 0; c < n; ++c) {
        const uint8_t g = puzzle.given[cellIndex(r, c)];
        if (!g) continue;
        if (!set(cellIndex(r, c), kYes)) return false;
        for (int d = 0; d < 4; ++d) {
          const int e = edgeVar(r, c, d);
          if (e >= 0 && !set(e, (g & dirBit(d)) ? kYes : kNo)) return false;
        }
      }
    }
    trailLen = 0;
    clearQueue();
    for (int item = 0; item < kWork; ++item) {
      const bool live = item < kMaxCells ? (rowOf(item) < n && colOf(item) < n) : (item - kMaxCells) % kMaxSize < n;
      if (live) enqueue(item);
    }
    return true;
  }

  // Fixes every square to the player's marks: track is track, anything else
  // (empty or crossed) is not.
  bool applyBoard(const Board& board) {
    for (int r = 0; r < n; ++r) {
      for (int c = 0; c < n; ++c) {
        const int i = cellIndex(r, c);
        if (!set(i, board.cell[i] == kTrack ? kYes : kNo)) return false;
      }
    }
    return true;
  }

  // Fixes only the squares the player has decided: track is track, a cross is
  // not, and an empty square stays open. For hints, which reason forward from
  // what the player already knows rather than from a finished board.
  bool applyMarks(const Board& board) {
    for (int r = 0; r < n; ++r) {
      for (int c = 0; c < n; ++c) {
        const int i = cellIndex(r, c);
        if (board.cell[i] == kEmpty) continue;
        if (!set(i, board.cell[i] == kTrack ? kYes : kNo)) return false;
      }
    }
    return true;
  }

  // Deduces as far as tiers up to maxTier allow, always reaching for the
  // lowest tier that makes progress. Leaves the deductions in place.
  Rating deduce(const int maxTier) {
    Rating rating{false, 0};
    for (;;) {
      progress = false;
      if (!propagateTier1()) return rating;
      if (progress && rating.tier < 1) rating.tier = 1;
      if (allSquaresKnown()) {
        rating.solved = true;
        return rating;
      }
      if (maxTier >= 2) {
        progress = false;
        if (!tier2Pass()) return rating;
        if (progress) {
          if (rating.tier < 2) rating.tier = 2;
          continue;
        }
      }
      if (maxTier >= 3) {
        const int step = lookaheadStep();
        if (step < 0) return rating;
        if (step > 0) {
          rating.tier = 3;
          continue;
        }
      }
      return rating;
    }
  }

  // Distinct track-square sets consistent with the loaded state, up to limit.
  // Leaves the state as it found it.
  int countSolutions(const int limit) {
    const int base = trailLen;
    // Every tier is sound, so deducing first changes no count. It only leaves
    // the search far less to branch on.
    progress = false;
    const Rating root = deduce(3);
    (void)root;
    const int found = search(true, limit);
    unwind(base);
    return found;
  }

  // Square and edge state after load / deduce, for tests and hints.
  uint8_t square(const int row, const int col) const { return v[cellIndex(row, col)]; }
  uint8_t edge(const int row, const int col, const int dir) const {
    const int e = edgeVar(row, col, dir);
    if (e >= 0) return v[e];
    return gateSide(*p, row, col) == dir ? kYes : kNo;
  }
  int mark() const { return trailLen; }
  void unwind(const int to) {
    while (trailLen > to) {
      --trailLen;
      v[trail[trailLen].var] = trail[trailLen].old;
    }
    clearQueue();
  }

 private:
  struct Entry {
    uint16_t var;
    uint8_t old;
  };
  struct Decision {
    uint16_t var;
    uint16_t mark;
    uint8_t triedNo;
  };

  const Puzzle* p = nullptr;
  int n = 0;
  int total = 0;
  bool progress = false;
  int lookCursor = 0;
  uint8_t v[kVars] = {};
  Entry trail[kVars] = {};
  int trailLen = 0;
  Decision cellStack[kVars] = {};
  Decision edgeStack[kVars] = {};

  // Tier 1 worklist. Items are squares (0..kMaxCells-1), then rows, then
  // columns. An assignment queues only what it can affect: a square queues
  // itself and its two lines, an edge queues its two squares. Each item is in
  // the queue at most once, so a ring of kWork never overflows.
  static constexpr int kWork = kMaxCells + 2 * kMaxSize;
  uint8_t queue[kWork] = {};
  bool queued[kWork] = {};
  int qHead = 0;
  int qCount = 0;

  void enqueue(const int item) {
    if (queued[item]) return;
    queued[item] = true;
    queue[(qHead + qCount) % kWork] = static_cast<uint8_t>(item);
    ++qCount;
  }

  void clearQueue() {
    while (qCount > 0) {
      queued[queue[qHead]] = false;
      qHead = (qHead + 1) % kWork;
      --qCount;
    }
  }

  // The variable for the edge leaving (row, col) in dir, or -1 on the boundary.
  int edgeVar(const int row, const int col, const int dir) const {
    const int r2 = row + kDRow[dir];
    const int c2 = col + kDCol[dir];
    if (r2 < 0 || c2 < 0 || r2 >= n || c2 >= n) return -1;
    switch (dir) {
      case kEast:
        return kMaxCells + cellIndex(row, col);
      case kWest:
        return kMaxCells + cellIndex(row, c2);
      case kSouth:
        return 2 * kMaxCells + cellIndex(row, col);
      default:
        return 2 * kMaxCells + cellIndex(r2, col);
    }
  }

  bool set(const int var, const uint8_t value) {
    if (v[var] == value) return true;
    if (v[var] != kUnknown) return false;
    trail[trailLen++] = Entry{static_cast<uint16_t>(var), v[var]};
    v[var] = value;
    progress = true;
    if (var < kMaxCells) {
      enqueue(var);
      enqueue(kMaxCells + rowOf(var));
      enqueue(kMaxCells + kMaxSize + colOf(var));
      return true;
    }
    const int a = var < 2 * kMaxCells ? var - kMaxCells : var - 2 * kMaxCells;
    enqueue(a);
    enqueue(var < 2 * kMaxCells ? a + 1 : a + kMaxSize);
    if (value == kYes) return chainStillLegal(var);
    return true;
  }

  struct ChainEnd {
    int cell;
    int length;   // squares from the starting square to here, inclusive
    bool gate;    // the chain leaves the grid through a gate here
    bool loop;    // the walk came back to `stop`
    bool branch;  // a square with three connections; propagation rejects it
  };

  // Follows connected edges from `cell`, not going back through `cameFrom`
  // (-1 for none), until the chain ends. Arriving at `stop` is a loop.
  ChainEnd walk(int cell, int cameFrom, const int stop) const {
    ChainEnd end{cell, 1, false, false, false};
    for (int steps = 0; steps <= kMaxCells; ++steps) {
      const int r = rowOf(cell);
      const int c = colOf(cell);
      int next = -1;
      int count = 0;
      for (int d = 0; d < 4; ++d) {
        if (d == cameFrom || edge(r, c, d) != kYes) continue;
        ++count;
        next = d;
      }
      end.cell = cell;
      if (count == 0) return end;
      if (count > 1) {
        end.branch = true;
        return end;
      }
      if (edgeVar(r, c, next) < 0) {
        end.gate = true;
        return end;
      }
      cell = cellIndex(r + kDRow[next], c + kDCol[next]);
      if (cell == stop) {
        end.loop = true;
        return end;
      }
      cameFrom = opposite(next);
      ++end.length;
    }
    end.loop = true;
    return end;
  }

  // After connecting an edge: no loop, and the start and end may only be
  // joined by a chain that holds every track square.
  bool chainStillLegal(const int var) const {
    int a;
    int dir;
    if (var < 2 * kMaxCells) {
      a = var - kMaxCells;
      dir = kEast;
    } else {
      a = var - 2 * kMaxCells;
      dir = kSouth;
    }
    const int b = cellIndex(rowOf(a) + kDRow[dir], colOf(a) + kDCol[dir]);
    // A loop through the new edge comes back to `a`; the walk the other way
    // would only find the same loop, so it is used for its length alone.
    const ChainEnd fromB = walk(b, opposite(dir), a);
    if (fromB.loop) return false;
    const ChainEnd fromA = walk(a, dir, -1);
    if (fromA.gate && fromB.gate && fromA.length + fromB.length != total) return false;
    return true;
  }

  struct Tally {
    int yes;
    int unknown;
  };

  Tally edgeTally(const int r, const int c) const {
    Tally t{0, 0};
    for (int d = 0; d < 4; ++d) {
      const uint8_t e = edge(r, c, d);
      t.yes += e == kYes;
      t.unknown += e == kUnknown;
    }
    return t;
  }

  bool setUnknownEdges(const int r, const int c, const uint8_t value) {
    for (int d = 0; d < 4; ++d) {
      const int e = edgeVar(r, c, d);
      if (e >= 0 && v[e] == kUnknown && !set(e, value)) return false;
    }
    return true;
  }

  bool lineRule(const bool isRow, const int index) {
    int yes = 0;
    int unknown = 0;
    for (int k = 0; k < n; ++k) {
      const uint8_t s = v[isRow ? cellIndex(index, k) : cellIndex(k, index)];
      yes += s == kYes;
      unknown += s == kUnknown;
    }
    const int clue = isRow ? p->rowClue[index] : p->colClue[index];
    if (yes > clue || yes + unknown < clue) return false;
    if (unknown == 0) return true;
    uint8_t fill;
    if (yes == clue) {
      fill = kNo;
    } else if (yes + unknown == clue) {
      fill = kYes;
    } else {
      return true;
    }
    for (int k = 0; k < n; ++k) {
      const int i = isRow ? cellIndex(index, k) : cellIndex(k, index);
      if (v[i] == kUnknown && !set(i, fill)) return false;
    }
    return true;
  }

  bool squareRule(const int r, const int c) {
    const int i = cellIndex(r, c);
    const Tally t = edgeTally(r, c);
    if (v[i] == kNo) {
      if (t.yes > 0) return false;
      return setUnknownEdges(r, c, kNo);
    }
    if (t.yes > 0 && !set(i, kYes)) return false;
    if (v[i] != kYes) return true;
    if (t.yes > 2 || t.yes + t.unknown < 2) return false;
    if (t.unknown == 0) return true;
    if (t.yes == 2) return setUnknownEdges(r, c, kNo);
    if (t.yes + t.unknown == 2) return setUnknownEdges(r, c, kYes);
    return true;
  }

  // Runs tier 1 to a fixpoint by draining the worklist. Leaves `progress`
  // true if anything changed.
  bool propagateTier1() {
    progress = false;
    while (qCount > 0) {
      const int item = queue[qHead];
      qHead = (qHead + 1) % kWork;
      --qCount;
      queued[item] = false;
      bool ok;
      if (item < kMaxCells) {
        ok = squareRule(rowOf(item), colOf(item));
      } else {
        ok = lineRule(item < kMaxCells + kMaxSize, (item - kMaxCells) % kMaxSize);
      }
      if (!ok) {
        clearQueue();
        return false;
      }
    }
    return true;
  }

  bool tier2Pass() {
    for (int r = 0; r < n; ++r) {
      for (int c = 0; c < n; ++c) {
        const int i = cellIndex(r, c);
        if (v[i] != kUnknown) continue;
        const Tally t = edgeTally(r, c);
        if (t.yes + t.unknown < 2 && !set(i, kNo)) return false;
      }
    }
    // Chain ends facing each other across an open edge.
    for (int r = 0; r < n; ++r) {
      for (int c = 0; c < n; ++c) {
        for (const int dir : {kEast, kSouth}) {
          const int e = edgeVar(r, c, dir);
          if (e < 0 || v[e] != kUnknown) continue;
          const int a = cellIndex(r, c);
          const int b = cellIndex(r + kDRow[dir], c + kDCol[dir]);
          if (v[a] == kNo || v[b] == kNo) continue;
          if (edgeTally(r, c).yes != 1 || edgeTally(rowOf(b), colOf(b)).yes != 1) continue;
          const ChainEnd fromA = walk(a, -1, -1);
          if (fromA.branch) continue;
          if (fromA.cell == b) {
            if (!set(e, kNo)) return false;
            continue;
          }
          const ChainEnd fromB = walk(b, -1, -1);
          if (fromA.gate && fromB.gate && fromA.length + fromB.length != total && !set(e, kNo)) return false;
        }
      }
    }
    return true;
  }

  bool propagate() {
    for (;;) {
      if (!propagateTier1()) return false;
      progress = false;
      if (!tier2Pass()) {
        clearQueue();
        return false;
      }
      if (!progress) return true;
    }
  }

  bool allSquaresKnown() const {
    for (int r = 0; r < n; ++r) {
      for (int c = 0; c < n; ++c) {
        if (v[cellIndex(r, c)] == kUnknown) return false;
      }
    }
    return true;
  }

  // One tier-3 deduction: 1 made, 0 none available, -1 contradiction.
  // Probing resumes where the last deduction was found rather than at 0: the
  // variables before it were just probed and gave nothing. A full lap with no
  // deduction means there is none. Order changes speed, never the result,
  // because the closure under lookahead is the same whatever order finds it.
  int lookaheadStep() {
    for (int k = 0; k < kVars; ++k) {
      const int var = lookCursor;
      lookCursor = (lookCursor + 1) % kVars;
      if (v[var] != kUnknown) continue;
      for (const uint8_t guess : {kYes, kNo}) {
        const int m = trailLen;
        const bool ok = set(var, guess) && propagate();
        unwind(m);
        if (!ok) return set(var, guess == kYes ? kNo : kYes) ? 1 : -1;
      }
    }
    return 0;
  }

  // Next variable to branch on: a square (or, when squaresOnly is false, an
  // edge) next to a track square still missing a connection; else the first
  // unknown. -1 when none is left.
  int pickVar(const bool squaresOnly) const {
    int fallback = -1;
    for (int r = 0; r < n; ++r) {
      for (int c = 0; c < n; ++c) {
        const int i = cellIndex(r, c);
        if (v[i] == kUnknown) {
          if (fallback < 0) fallback = i;
          continue;
        }
        if (v[i] != kYes || edgeTally(r, c).yes >= 2) continue;
        for (int d = 0; d < 4; ++d) {
          const int e = edgeVar(r, c, d);
          if (e < 0 || v[e] != kUnknown) continue;
          const int j = cellIndex(r + kDRow[d], c + kDCol[d]);
          if (v[j] == kUnknown) return j;
          if (!squaresOnly) return e;
        }
      }
    }
    if (fallback >= 0 || squaresOnly) return fallback;
    for (int var = kMaxCells; var < kVars; ++var) {
      if (v[var] == kUnknown) return var;
    }
    return -1;
  }

  // Every square and edge decided: is it one path from gate to gate through
  // every track square?
  bool completePath() const {
    const ChainEnd end = walk(cellIndex(p->start.row, p->start.col), p->start.side, -1);
    return end.gate && !end.branch && end.cell == cellIndex(p->end.row, p->end.col) && end.length == total;
  }

  // Depth-first search. With squaresOnly, branches on squares and accepts a
  // leaf if some edge assignment completes it; otherwise branches on edges too
  // and accepts a complete path. Returns the leaves accepted, up to limit.
  int search(const bool squaresOnly, const int limit) {
    Decision* stack = squaresOnly ? cellStack : edgeStack;
    int depth = 0;
    int found = 0;
    bool ok = propagate();
    for (;;) {
      if (ok) {
        const int var = pickVar(squaresOnly);
        if (var < 0) {
          bool accept;
          if (squaresOnly) {
            const int m = trailLen;
            accept = search(false, 1) > 0;
            unwind(m);
          } else {
            accept = completePath();
          }
          if (accept && ++found >= limit) return found;
          ok = false;
          continue;
        }
        stack[depth++] = Decision{static_cast<uint16_t>(var), static_cast<uint16_t>(trailLen), 0};
        ok = set(var, kYes) && propagate();
        continue;
      }
      while (depth > 0 && stack[depth - 1].triedNo) --depth;
      if (depth == 0) return found;
      Decision& d = stack[depth - 1];
      unwind(d.mark);
      d.triedNo = 1;
      ok = set(d.var, kNo) && propagate();
    }
  }
};

// The rule-based win: every clue met, and some single path from the start gate
// to the end gate runs through exactly the marked squares, keeping every
// given's shape. `scratch` is the caller's so the solver's ~8KB is not on the
// stack.
inline bool isWin(const Puzzle& puzzle, const Board& board, Solver& scratch) {
  if (!allCluesMet(board, puzzle)) return false;
  if (!scratch.load(puzzle) || !scratch.applyBoard(board)) return false;
  return scratch.countSolutions(1) == 1;
}

}  // namespace traintracks
