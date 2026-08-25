#include "SudokuCore.h"

namespace sudoku {
namespace {

// Every cell has exactly twenty peers: eight along the row, eight down the
// column, and the four remaining cells of its box. Built once at compile time
// because `assign` walks it 81 times per solve and a solve runs 80-odd times per
// generated puzzle, which turns an `arePeers` scan of all 81 cells into the
// hottest loop in the app for no reason. 1620 bytes of flash.
constexpr int kPeerCount = 20;

struct PeerTable {
  uint8_t peer[kCells][kPeerCount];
};

constexpr PeerTable makePeerTable() {
  PeerTable table{};
  for (int cell = 0; cell < kCells; ++cell) {
    int found = 0;
    for (int other = 0; other < kCells; ++other) {
      if (arePeers(cell, other)) table.peer[cell][found++] = static_cast<uint8_t>(other);
    }
  }
  return table;
}

constexpr PeerTable kPeers = makePeerTable();

// A belt rather than a mechanism. Every technique below strictly reduces the
// total candidate count or places a digit, so the ladder terminates on its own;
// this only bounds what a corrupted position can cost. There are at most 81
// placements and 81*8 eliminations available, so nothing real comes near it.
constexpr int kStepBudget = 4096;

// Plain backtracking over an empty grid rarely needs more than a few thousand
// probes. Bounded so a caller can never be pinned, and `host-tests/sudoku`
// prints the real worst case so this number is not a guess.
constexpr uint32_t kFillBudget = 200000;

constexpr int lowestIndex(const Mask mask) {
  for (int index = 0; index < kSize; ++index) {
    if (mask & static_cast<Mask>(1u << index)) return index;
  }
  return -1;
}

// Which slots of `unit` could still hold `digit`.
Mask positionsOf(const Grid& grid, const int unit, const int digit) {
  Mask positions = 0;
  for (int slot = 0; slot < kSize; ++slot) {
    const int cell = unitCell(unit, slot);
    if (grid.value[cell] == 0 && (grid.candidate[cell] & bitFor(digit))) {
      positions = static_cast<Mask>(positions | (1u << slot));
    }
  }
  return positions;
}

bool digitPlacedIn(const Grid& grid, const int unit, const int digit) {
  for (int slot = 0; slot < kSize; ++slot) {
    if (grid.value[unitCell(unit, slot)] == digit) return true;
  }
  return false;
}

// Strike `bits` from a cell's candidates. Returns whether anything actually
// went, which is what every elimination technique reports as progress: a
// technique that "fires" without removing a candidate would spin the ladder
// forever.
bool eliminate(Grid& grid, const int cell, const Mask bits, bool& broken) {
  if (grid.value[cell] != 0) return false;
  const Mask before = grid.candidate[cell];
  const Mask after = static_cast<Mask>(before & ~bits);
  if (after == before) return false;
  grid.candidate[cell] = after;
  if (after == 0) broken = true;
  return true;
}

// Walk k-combinations of [0, n) in place. `index` starts as {0, 1, ... k-1}.
bool nextCombination(int* index, const int k, const int n) {
  int i = k - 1;
  while (i >= 0 && index[i] == n - k + i) --i;
  if (i < 0) return false;
  ++index[i];
  for (int j = i + 1; j < k; ++j) index[j] = index[j - 1] + 1;
  return true;
}

bool findNakedSingle(const Grid& grid, int& cell, int& digit) {
  for (int candidateCell = 0; candidateCell < kCells; ++candidateCell) {
    if (grid.value[candidateCell] != 0) continue;
    if (popcount(grid.candidate[candidateCell]) != 1) continue;
    cell = candidateCell;
    digit = lowestDigit(grid.candidate[candidateCell]);
    return true;
  }
  return false;
}

bool findHiddenSingle(const Grid& grid, int& cell, int& digit, int& unit) {
  for (int candidateUnit = 0; candidateUnit < kUnits; ++candidateUnit) {
    for (int candidateDigit = 1; candidateDigit <= kSize; ++candidateDigit) {
      if (digitPlacedIn(grid, candidateUnit, candidateDigit)) continue;
      const Mask positions = positionsOf(grid, candidateUnit, candidateDigit);
      if (popcount(positions) != 1) continue;
      const int only = unitCell(candidateUnit, lowestIndex(positions));
      // A cell down to one candidate is a naked single, and calling it hidden
      // would mis-grade the puzzle one rung up. The ladder exhausts naked
      // singles before reaching here, so this only guards the direct callers.
      if (popcount(grid.candidate[only]) == 1) continue;
      cell = only;
      digit = candidateDigit;
      unit = candidateUnit;
      return true;
    }
  }
  return false;
}

bool applyNakedSingle(Grid& grid, bool& broken) {
  bool progress = false;
  int cell = -1;
  int digit = 0;
  while (findNakedSingle(grid, cell, digit)) {
    if (!assign(grid, cell, digit)) {
      broken = true;
      return true;
    }
    progress = true;
  }
  return progress;
}

bool applyHiddenSingle(Grid& grid, bool& broken) {
  int cell = -1;
  int digit = 0;
  int unit = -1;
  if (!findHiddenSingle(grid, cell, digit, unit)) return false;
  if (!assign(grid, cell, digit)) broken = true;
  return true;
}

// Pointing and claiming, which are the same observation read from either side:
// a digit's remaining homes lie in the intersection of a box and a line, so it
// can be struck from the rest of whichever of the two it did not come from.
bool applyLockedCandidates(Grid& grid, bool& broken) {
  for (int box = 0; box < kSize; ++box) {
    const int unit = boxUnit(box);
    for (int digit = 1; digit <= kSize; ++digit) {
      if (digitPlacedIn(grid, unit, digit)) continue;
      const Mask positions = positionsOf(grid, unit, digit);
      if (popcount(positions) < 2) continue;
      int row = -1;
      int column = -1;
      bool sameRow = true;
      bool sameColumn = true;
      for (int slot = 0; slot < kSize; ++slot) {
        if (!(positions & static_cast<Mask>(1u << slot))) continue;
        const int cell = unitCell(unit, slot);
        if (row < 0) {
          row = rowOf(cell);
        } else if (rowOf(cell) != row) {
          sameRow = false;
        }
        if (column < 0) {
          column = columnOf(cell);
        } else if (columnOf(cell) != column) {
          sameColumn = false;
        }
      }
      bool progress = false;
      if (sameRow) {
        for (int c = 0; c < kSize; ++c) {
          const int cell = cellAt(row, c);
          if (boxOf(cell) == box) continue;
          progress |= eliminate(grid, cell, bitFor(digit), broken);
        }
      }
      if (sameColumn) {
        for (int r = 0; r < kSize; ++r) {
          const int cell = cellAt(r, column);
          if (boxOf(cell) == box) continue;
          progress |= eliminate(grid, cell, bitFor(digit), broken);
        }
      }
      if (progress) return true;
    }
  }

  for (int line = 0; line < 2 * kSize; ++line) {
    const bool isRow = line < kSize;
    for (int digit = 1; digit <= kSize; ++digit) {
      if (digitPlacedIn(grid, line, digit)) continue;
      const Mask positions = positionsOf(grid, line, digit);
      if (popcount(positions) < 2) continue;
      int box = -1;
      bool sameBox = true;
      for (int slot = 0; slot < kSize; ++slot) {
        if (!(positions & static_cast<Mask>(1u << slot))) continue;
        const int cell = unitCell(line, slot);
        if (box < 0) {
          box = boxOf(cell);
        } else if (boxOf(cell) != box) {
          sameBox = false;
        }
      }
      if (!sameBox || box < 0) continue;
      bool progress = false;
      for (int slot = 0; slot < kSize; ++slot) {
        const int cell = unitCell(boxUnit(box), slot);
        const bool onTheLine = isRow ? (rowOf(cell) == line) : (columnOf(cell) == line - kSize);
        if (onTheLine) continue;
        progress |= eliminate(grid, cell, bitFor(digit), broken);
      }
      if (progress) return true;
    }
  }
  return false;
}

// `size` cells in one unit whose candidates span exactly `size` digits. Those
// digits are spoken for, so they leave every other cell of the unit.
bool applyNakedSubset(Grid& grid, const int size, bool& broken) {
  for (int unit = 0; unit < kUnits; ++unit) {
    int open[kSize];
    int openCount = 0;
    for (int slot = 0; slot < kSize; ++slot) {
      const int cell = unitCell(unit, slot);
      if (grid.value[cell] == 0) open[openCount++] = cell;
    }
    if (openCount <= size) continue;

    int index[3] = {0, 1, 2};
    for (int i = 0; i < size; ++i) index[i] = i;
    do {
      Mask spanned = 0;
      bool usable = true;
      for (int i = 0; i < size; ++i) {
        const Mask cellCandidates = grid.candidate[open[index[i]]];
        const int count = popcount(cellCandidates);
        if (count < 2 || count > size) {
          usable = false;
          break;
        }
        spanned = static_cast<Mask>(spanned | cellCandidates);
      }
      if (!usable || popcount(spanned) != size) continue;

      bool progress = false;
      for (int other = 0; other < openCount; ++other) {
        bool inSubset = false;
        for (int i = 0; i < size; ++i) {
          if (index[i] == other) inSubset = true;
        }
        if (inSubset) continue;
        progress |= eliminate(grid, open[other], spanned, broken);
      }
      if (progress) return true;
    } while (nextCombination(index, size, openCount));
  }
  return false;
}

// The mirror of the above: `size` digits in one unit confined to exactly `size`
// cells. Those cells are spoken for, so everything else leaves them.
bool applyHiddenSubset(Grid& grid, const int size, bool& broken) {
  for (int unit = 0; unit < kUnits; ++unit) {
    int digits[kSize];
    Mask where[kSize];
    int digitCount = 0;
    for (int digit = 1; digit <= kSize; ++digit) {
      if (digitPlacedIn(grid, unit, digit)) continue;
      const Mask positions = positionsOf(grid, unit, digit);
      const int count = popcount(positions);
      if (count < 2 || count > size) continue;
      digits[digitCount] = digit;
      where[digitCount] = positions;
      ++digitCount;
    }
    if (digitCount < size) continue;

    int index[3] = {0, 1, 2};
    for (int i = 0; i < size; ++i) index[i] = i;
    do {
      Mask covered = 0;
      Mask wanted = 0;
      for (int i = 0; i < size; ++i) {
        covered = static_cast<Mask>(covered | where[index[i]]);
        wanted = static_cast<Mask>(wanted | bitFor(digits[index[i]]));
      }
      if (popcount(covered) != size) continue;

      bool progress = false;
      for (int slot = 0; slot < kSize; ++slot) {
        if (!(covered & static_cast<Mask>(1u << slot))) continue;
        progress |= eliminate(grid, unitCell(unit, slot), static_cast<Mask>(kAllDigits & ~wanted), broken);
      }
      if (progress) return true;
    } while (nextCombination(index, size, digitCount));
  }
  return false;
}

// A fish: `size` lines on which a digit's remaining homes span exactly `size`
// crossing lines. The digit is then used up in those crossings, so it leaves
// them everywhere else. Size two is the X-Wing and size three the Swordfish;
// they are one observation at two widths, and writing them twice is how the
// second one ends up subtly different from the first.
bool applyFish(Grid& grid, const int size, bool& broken) {
  for (int orientation = 0; orientation < 2; ++orientation) {
    const bool byRow = orientation == 0;
    for (int digit = 1; digit <= kSize; ++digit) {
      int line[kSize];
      Mask cross[kSize];
      int found = 0;
      for (int i = 0; i < kSize; ++i) {
        const int unit = byRow ? rowUnit(i) : columnUnit(i);
        if (digitPlacedIn(grid, unit, digit)) continue;
        const Mask positions = positionsOf(grid, unit, digit);
        const int count = popcount(positions);
        if (count < 2 || count > size) continue;
        line[found] = i;
        cross[found] = positions;
        ++found;
      }
      if (found < size) continue;

      int index[3] = {0, 1, 2};
      for (int i = 0; i < size; ++i) index[i] = i;
      do {
        Mask covered = 0;
        for (int i = 0; i < size; ++i) covered = static_cast<Mask>(covered | cross[index[i]]);
        if (popcount(covered) != size) continue;

        bool progress = false;
        for (int slot = 0; slot < kSize; ++slot) {
          if (!(covered & static_cast<Mask>(1u << slot))) continue;
          for (int i = 0; i < kSize; ++i) {
            bool inFish = false;
            for (int k = 0; k < size; ++k) {
              if (line[index[k]] == i) inFish = true;
            }
            if (inFish) continue;
            const int cell = byRow ? cellAt(i, slot) : cellAt(slot, i);
            progress |= eliminate(grid, cell, bitFor(digit), broken);
          }
        }
        if (progress) return true;
      } while (nextCombination(index, size, found));
    }
  }
  return false;
}

// A pivot holding {a, b} and two of its peers holding {a, c} and {b, c}.
// Whichever way the pivot falls, one pincer becomes c, so c cannot survive in
// any cell that sees both pincers. The first rung that reasons about a chain
// rather than about one unit, which is why it sits above the subsets.
bool applyXYWing(Grid& grid, bool& broken) {
  for (int pivot = 0; pivot < kCells; ++pivot) {
    if (grid.value[pivot] != 0) continue;
    if (popcount(grid.candidate[pivot]) != 2) continue;
    const int a = lowestDigit(grid.candidate[pivot]);
    const int b = lowestDigit(static_cast<Mask>(grid.candidate[pivot] & ~bitFor(a)));

    for (int i = 0; i < kPeerCount; ++i) {
      const int first = kPeers.peer[pivot][i];
      if (grid.value[first] != 0 || popcount(grid.candidate[first]) != 2) continue;
      const bool hasA = (grid.candidate[first] & bitFor(a)) != 0;
      const bool hasB = (grid.candidate[first] & bitFor(b)) != 0;
      if (hasA == hasB) continue;  // needs exactly one of the pivot's pair
      const int shared = hasA ? a : b;
      const int other = hasA ? b : a;
      const int third = lowestDigit(static_cast<Mask>(grid.candidate[first] & ~bitFor(shared)));
      if (third == other || third == 0) continue;
      const Mask wanted = static_cast<Mask>(bitFor(other) | bitFor(third));

      for (int j = 0; j < kPeerCount; ++j) {
        const int second = kPeers.peer[pivot][j];
        if (second == first) continue;
        if (grid.value[second] != 0 || grid.candidate[second] != wanted) continue;

        bool progress = false;
        for (int cell = 0; cell < kCells; ++cell) {
          if (cell == pivot || cell == first || cell == second) continue;
          if (!arePeers(cell, first) || !arePeers(cell, second)) continue;
          progress |= eliminate(grid, cell, bitFor(third), broken);
        }
        if (progress) return true;
      }
    }
  }
  return false;
}

bool apply(Grid& grid, const Technique technique, bool& broken) {
  switch (technique) {
    case Technique::None:
      return false;
    case Technique::NakedSingle:
      return applyNakedSingle(grid, broken);
    case Technique::HiddenSingle:
      return applyHiddenSingle(grid, broken);
    case Technique::LockedCandidates:
      return applyLockedCandidates(grid, broken);
    case Technique::NakedPair:
      return applyNakedSubset(grid, 2, broken);
    case Technique::HiddenPair:
      return applyHiddenSubset(grid, 2, broken);
    case Technique::NakedTriple:
      return applyNakedSubset(grid, 3, broken);
    case Technique::HiddenTriple:
      return applyHiddenSubset(grid, 3, broken);
    case Technique::XWing:
      return applyFish(grid, 2, broken);
    case Technique::XYWing:
      return applyXYWing(grid, broken);
    case Technique::Swordfish:
      return applyFish(grid, 3, broken);
  }
  return false;
}

bool fits(const uint8_t value[kCells], const int cell, const int digit) {
  for (int i = 0; i < kPeerCount; ++i) {
    if (value[kPeers.peer[cell][i]] == digit) return false;
  }
  return true;
}

bool solvableWith(const uint8_t given[kCells], const Technique ceiling, Grid& probe) {
  if (!load(probe, given)) return false;
  const SolveReport report = solve(probe, ceiling);
  return report.solved && !report.broken;
}

}  // namespace

const char* techniqueName(const Technique technique) {
  switch (technique) {
    case Technique::None:
      return "NOTHING";
    case Technique::NakedSingle:
      return "LAST FREE CELL";
    case Technique::HiddenSingle:
      return "LAST FREE HOME";
    case Technique::LockedCandidates:
      return "LOCKED DIGIT";
    case Technique::NakedPair:
      return "PAIR OF CELLS";
    case Technique::HiddenPair:
      return "PAIR OF DIGITS";
    case Technique::NakedTriple:
      return "TRIPLE OF CELLS";
    case Technique::HiddenTriple:
      return "TRIPLE OF DIGITS";
    case Technique::XWing:
      return "RECTANGLE";
    case Technique::XYWing:
      return "Y CHAIN";
    case Technique::Swordfish:
      return "SWORDFISH";
  }
  return "NOTHING";
}

const char* levelName(const Level level) {
  switch (level) {
    case Level::Easy:
      return "EASY";
    case Level::Medium:
      return "MEDIUM";
    case Level::Hard:
      return "HARD";
    case Level::Expert:
      return "EXPERT";
  }
  return "EASY";
}

void clear(Grid& grid) {
  for (int cell = 0; cell < kCells; ++cell) {
    grid.value[cell] = 0;
    grid.candidate[cell] = kAllDigits;
  }
}

bool assign(Grid& grid, const int cell, const int digit) {
  if (cell < 0 || cell >= kCells || digit < 1 || digit > kSize) return false;
  if (grid.value[cell] == 0 && !(grid.candidate[cell] & bitFor(digit))) return false;
  grid.value[cell] = static_cast<uint8_t>(digit);
  grid.candidate[cell] = 0;
  const Mask bit = bitFor(digit);
  bool ok = true;
  for (int i = 0; i < kPeerCount; ++i) {
    const int peer = kPeers.peer[cell][i];
    if (grid.value[peer] == digit) {
      ok = false;
      continue;
    }
    if (grid.value[peer] != 0) continue;
    grid.candidate[peer] = static_cast<Mask>(grid.candidate[peer] & ~bit);
    if (grid.candidate[peer] == 0) ok = false;
  }
  return ok;
}

bool load(Grid& grid, const uint8_t given[kCells]) {
  clear(grid);
  bool ok = true;
  for (int cell = 0; cell < kCells; ++cell) {
    if (given[cell] == 0) continue;
    if (!assign(grid, cell, given[cell])) ok = false;
  }
  return ok;
}

bool isComplete(const Grid& grid) {
  for (int cell = 0; cell < kCells; ++cell) {
    if (grid.value[cell] == 0) return false;
  }
  return true;
}

SolveReport solve(Grid& grid, const Technique ceiling) {
  SolveReport report;
  while (report.steps < kStepBudget) {
    if (isComplete(grid)) {
      report.solved = true;
      return report;
    }
    bool progress = false;
    for (int rung = 1; rung <= static_cast<int>(ceiling); ++rung) {
      bool broken = false;
      if (!apply(grid, static_cast<Technique>(rung), broken)) continue;
      ++report.steps;
      if (rung > static_cast<int>(report.hardest)) report.hardest = static_cast<Technique>(rung);
      if (broken) {
        report.broken = true;
        return report;
      }
      progress = true;
      break;
    }
    if (!progress) return report;
  }
  return report;
}

bool fillComplete(uint8_t solution[kCells], Workspace& work, uint32_t& rng) {
  for (int cell = 0; cell < kCells; ++cell) {
    for (int i = 0; i < kSize; ++i) work.order[cell][i] = static_cast<uint8_t>(i + 1);
    for (int i = kSize - 1; i > 0; --i) {
      const int j = static_cast<int>(nextRandom(rng) % static_cast<uint32_t>(i + 1));
      const uint8_t swap = work.order[cell][i];
      work.order[cell][i] = work.order[cell][j];
      work.order[cell][j] = swap;
    }
    work.next[cell] = 0;
    work.value[cell] = 0;
  }

  int cell = 0;
  uint32_t guard = 0;
  while (cell < kCells) {
    if (++guard > kFillBudget) return false;
    bool placed = false;
    while (work.next[cell] < kSize) {
      const int digit = work.order[cell][work.next[cell]++];
      if (fits(work.value, cell, digit)) {
        work.value[cell] = static_cast<uint8_t>(digit);
        placed = true;
        break;
      }
    }
    if (placed) {
      ++cell;
      continue;
    }
    work.next[cell] = 0;
    work.value[cell] = 0;
    if (cell == 0) return false;
    --cell;
    work.value[cell] = 0;
  }

  for (int i = 0; i < kCells; ++i) solution[i] = work.value[i];
  return true;
}

bool carve(Puzzle& out, const uint8_t solution[kCells], Workspace& work, uint32_t& rng) {
  for (int cell = 0; cell < kCells; ++cell) {
    out.given[cell] = solution[cell];
    out.solution[cell] = solution[cell];
  }

  int leader[(kCells + 1) / 2];
  int leaderCount = 0;
  for (int cell = 0; cell < kCells; ++cell) {
    if (cell <= kCells - 1 - cell) leader[leaderCount++] = cell;
  }
  for (int i = leaderCount - 1; i > 0; --i) {
    const int j = static_cast<int>(nextRandom(rng) % static_cast<uint32_t>(i + 1));
    const int swap = leader[i];
    leader[i] = leader[j];
    leader[j] = swap;
  }

  const Technique ceiling = ceilingFor(Level::Expert);
  for (int i = 0; i < leaderCount; ++i) {
    const int a = leader[i];
    const int b = kCells - 1 - a;
    const uint8_t keptA = out.given[a];
    const uint8_t keptB = out.given[b];
    out.given[a] = 0;
    out.given[b] = 0;
    if (!solvableWith(out.given, ceiling, work.probe)) {
      out.given[a] = keptA;
      out.given[b] = keptB;
    }
  }

  if (!load(work.grid, out.given)) return false;
  const SolveReport report = solve(work.grid, ceiling);
  if (!report.solved || report.broken) return false;

  out.hardest = report.hardest;
  out.level = levelOf(report.hardest);
  out.clues = 0;
  for (int cell = 0; cell < kCells; ++cell) {
    if (out.given[cell] != 0) ++out.clues;
  }
  return true;
}

bool generate(Puzzle& out, const Level level, Workspace& work, uint32_t& rng, const int attempts) {
  for (int attempt = 0; attempt < attempts; ++attempt) {
    const uint32_t attemptSeed = rng;
    uint8_t solution[kCells];
    if (!fillComplete(solution, work, rng)) continue;
    if (!carve(out, solution, work, rng)) continue;
    if (out.level != level) continue;
    out.seed = attemptSeed;
    return true;
  }
  return false;
}

Hint nextHint(const uint8_t given[kCells], const Technique ceiling) {
  Hint hint;
  Grid grid;
  if (!load(grid, given)) return hint;

  // The hint always names a cell and a digit, never an elimination: "you can
  // rule 4 out of this row" is true and useless to someone stuck. So the loop
  // pushes eliminations forward silently and reports the first PLACEMENT,
  // labelled with the hardest rule it needed to get there.
  Technique hardest = Technique::None;
  for (int guard = 0; guard < kStepBudget; ++guard) {
    if (isComplete(grid)) return hint;

    int cell = -1;
    int digit = 0;
    int unit = -1;
    Technique via = Technique::None;
    if (findNakedSingle(grid, cell, digit)) {
      via = Technique::NakedSingle;
    } else if (findHiddenSingle(grid, cell, digit, unit)) {
      via = Technique::HiddenSingle;
    }
    if (via != Technique::None) {
      hint.found = true;
      hint.cell = cell;
      hint.digit = digit;
      hint.technique = static_cast<int>(hardest) > static_cast<int>(via) ? hardest : via;
      hint.unit = unit;
      return hint;
    }

    bool progress = false;
    for (int rung = static_cast<int>(Technique::LockedCandidates); rung <= static_cast<int>(ceiling); ++rung) {
      bool broken = false;
      if (!apply(grid, static_cast<Technique>(rung), broken)) continue;
      if (broken) return hint;
      if (rung > static_cast<int>(hardest)) hardest = static_cast<Technique>(rung);
      progress = true;
      break;
    }
    if (!progress) return hint;
  }
  return hint;
}

}  // namespace sudoku
