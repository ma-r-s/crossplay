// Host tests for the Picross rules and the puzzle bank. No device, no
// PlatformIO: PicrossCore is freestanding C++17 precisely so this runs on a
// laptop. See run.sh.
//
// The centrepiece is validateBank(): an independent, brute-force implementation
// of "unique" and "line-solvable", run over every stored picture. That is
// this app's perft. The Python generator asserts both properties at build time;
// this asserts them again in C++, over the header that actually ships, so a
// hand-edit to the generated file or a bad merge cannot slip an ambiguous or
// un-line-solvable puzzle onto the device. Two implementations, two languages,
// agreeing on every board.

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "../../src/apps_local/picross/PicrossCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s\n", "test_picross.cpp", line, what);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

using Line = std::vector<int>;  // cells: -1 unknown, 0 empty, 1 filled
using Clue = std::vector<int>;  // run lengths

// Run lengths of the set bits in `pattern`, low bit first.
Clue runsOf(const unsigned pattern, const int n) {
  Clue runs;
  int run = 0;
  for (int i = 0; i < n; ++i) {
    if (pattern & (1u << i)) {
      ++run;
    } else if (run) {
      runs.push_back(run);
      run = 0;
    }
  }
  if (run) runs.push_back(run);
  return runs;
}

// The generator writes zero runs for an empty line; PicrossCore does the same.
// Compare on that footing: an all-empty pattern has an empty clue here.
bool clueEqual(const Clue& a, const Clue& b) { return a == b; }

// Tighten `known` using every full pattern that both matches `clue` and is
// consistent with what is already known. Brute force over 2^n patterns, which
// for n <= 10 is at most 1024 and is obviously correct -- no placement
// enumeration to get wrong. Returns false if the clue cannot be satisfied.
bool solveLine(const Clue& clue, const int n, Line& known) {
  unsigned forcedOne = (n >= 32) ? ~0u : ((1u << n) - 1u);
  unsigned forcedZero = forcedOne;
  bool any = false;
  for (unsigned pat = 0; pat < (1u << n); ++pat) {
    bool ok = true;
    for (int i = 0; i < n && ok; ++i) {
      const int bit = (pat >> i) & 1u;
      if (known[i] != -1 && known[i] != bit) ok = false;
    }
    if (!ok) continue;
    if (!clueEqual(runsOf(pat, n), clue)) continue;
    any = true;
    forcedOne &= pat;
    forcedZero &= ~pat;
  }
  if (!any) return false;
  for (int i = 0; i < n; ++i) {
    if (forcedOne & (1u << i)) known[i] = 1;
    if (forcedZero & (1u << i)) known[i] = 0;
  }
  return true;
}

// Solve using only single-line deductions. Returns the grid; caller checks
// whether it is fully determined and equals the source.
std::vector<Line> lineSolve(const std::vector<Clue>& rows, const std::vector<Clue>& cols, const int n,
                            bool& contradiction) {
  contradiction = false;
  std::vector<Line> grid(n, Line(n, -1));
  bool changed = true;
  while (changed) {
    changed = false;
    for (int r = 0; r < n; ++r) {
      Line before = grid[r];
      if (!solveLine(rows[r], n, grid[r])) {
        contradiction = true;
        return grid;
      }
      if (grid[r] != before) changed = true;
    }
    for (int c = 0; c < n; ++c) {
      Line col(n);
      for (int r = 0; r < n; ++r) col[r] = grid[r][c];
      Line before = col;
      if (!solveLine(cols[c], n, col)) {
        contradiction = true;
        return grid;
      }
      if (col != before) {
        for (int r = 0; r < n; ++r) grid[r][c] = col[r];
        changed = true;
      }
    }
  }
  return grid;
}

// Every full pattern of length n whose runs match `clue`.
std::vector<unsigned> patternsFor(const Clue& clue, const int n) {
  std::vector<unsigned> out;
  for (unsigned pat = 0; pat < (1u << n); ++pat)
    if (clueEqual(runsOf(pat, n), clue)) out.push_back(pat);
  return out;
}

// Exhaustive solution count, up to `limit`. Independent of lineSolve above:
// rows are chosen from their matching patterns and a partial column is pruned
// the instant its prefix matches no legal column pattern.
int countSolutions(const std::vector<Clue>& rows, const std::vector<Clue>& cols, const int n, const int limit) {
  std::vector<std::vector<unsigned>> rowOpts(n), colPats(n);
  for (int r = 0; r < n; ++r) rowOpts[r] = patternsFor(rows[r], n);
  for (int c = 0; c < n; ++c) colPats[c] = patternsFor(cols[c], n);

  std::vector<unsigned> chosen(n, 0);
  int count = 0;

  // Columns feasible given rows 0..r are chosen.
  auto prefixOk = [&](const int r) {
    for (int c = 0; c < n; ++c) {
      const unsigned mask = (r + 1 >= 32) ? ~0u : ((1u << (r + 1)) - 1u);
      unsigned prefix = 0;
      for (int i = 0; i <= r; ++i)
        if (chosen[i] & (1u << c)) prefix |= (1u << i);
      bool feasible = false;
      for (const unsigned pat : colPats[c]) {
        if ((pat & mask) == prefix) {
          feasible = true;
          break;
        }
      }
      if (!feasible) return false;
    }
    return true;
  };

  std::function<void(int)> dfs = [&](int r) {
    if (count >= limit) return;
    if (r == n) {
      ++count;
      return;
    }
    for (const unsigned opt : rowOpts[r]) {
      chosen[r] = opt;
      if (prefixOk(r)) dfs(r + 1);
      chosen[r] = 0;
      if (count >= limit) return;
    }
  };
  dfs(0);
  return count;
}

Clue clueFromBits(const uint16_t bits, const int n) {
  Clue runs;
  int run = 0;
  for (int i = 0; i < n; ++i) {
    if (bits & (uint16_t{1} << i)) {
      ++run;
    } else if (run) {
      runs.push_back(run);
      run = 0;
    }
  }
  if (run) runs.push_back(run);
  return runs;
}

void validateBank() {
  for (int p = 0; p < picross::kPuzzleCount; ++p) {
    const picross::Puzzle& puzzle = picross::kPuzzles[p];
    const int n = puzzle.size;
    CHECK(n == 5 || n == 10 || n == 15);

    std::vector<Clue> rows(n), cols(n);
    for (int r = 0; r < n; ++r) rows[r] = clueFromBits(puzzle.rows[r], n);
    for (int c = 0; c < n; ++c) {
      uint16_t bits = 0;
      for (int r = 0; r < n; ++r)
        if (puzzle.rows[r] & (uint16_t{1} << c)) bits |= (uint16_t{1} << r);
      cols[c] = clueFromBits(bits, n);
    }

    // Line-solvable AND unique: single-line reasoning determines every cell and
    // lands back on the stored picture.
    bool contradiction = false;
    std::vector<Line> solved = lineSolve(rows, cols, n, contradiction);
    CHECK(!contradiction);
    bool fullyDetermined = true;
    bool matchesSource = true;
    for (int r = 0; r < n; ++r) {
      for (int c = 0; c < n; ++c) {
        if (solved[r][c] == -1) fullyDetermined = false;
        const int want = (puzzle.rows[r] & (uint16_t{1} << c)) ? 1 : 0;
        if (solved[r][c] != want) matchesSource = false;
      }
    }
    if (!fullyDetermined) std::printf("  %s is not line-solvable\n", puzzle.name);
    if (!matchesSource) std::printf("  %s line-solves to a different grid\n", puzzle.name);
    CHECK(fullyDetermined);
    CHECK(matchesSource);

    // Independently: exactly one solution.
    const int count = countSolutions(rows, cols, n, 2);
    if (count != 1) std::printf("  %s has %d solutions\n", puzzle.name, count);
    CHECK(count == 1);
  }
}

// PicrossCore must derive the same clues the oracle does, straight from the
// bitmap, since the device never stores them.
void clueDerivation() {
  for (int p = 0; p < picross::kPuzzleCount; ++p) {
    picross::Board board;
    board.load(p);
    const int n = board.size();
    for (int r = 0; r < n; ++r) {
      uint8_t out[picross::kMaxSize] = {};
      const int cnt = board.rowClues(r, out);
      const Clue want = clueFromBits(picross::kPuzzles[p].rows[r], n);
      // An empty line derives to zero runs; the oracle's clue is empty too.
      CHECK(cnt == static_cast<int>(want.size()));
      for (int i = 0; i < cnt; ++i) CHECK(out[i] == want[i]);
    }
  }
  // lineRuns spot checks.
  uint8_t out[picross::kMaxSize] = {};
  CHECK(picross::lineRuns(0b10101, 5, out) == 3 && out[0] == 1 && out[1] == 1 && out[2] == 1);
  CHECK(picross::lineRuns(0b11100, 5, out) == 1 && out[0] == 3);
  CHECK(picross::lineRuns(0, 5, out) == 0);
  CHECK(picross::lineRuns(0b1111111111, 10, out) == 1 && out[0] == 10);
}

// Find a puzzle's first solid and first empty cell, for the action tests.
void firstCells(const picross::Board& board, int& solidR, int& solidC, int& emptyR, int& emptyC) {
  solidR = solidC = emptyR = emptyC = -1;
  for (int r = 0; r < board.size(); ++r) {
    for (int c = 0; c < board.size(); ++c) {
      if (board.solid(r, c) && solidR < 0) {
        solidR = r;
        solidC = c;
      }
      if (!board.solid(r, c) && emptyR < 0) {
        emptyR = r;
        emptyC = c;
      }
    }
  }
}

void mistakeAndWin() {
  picross::Board board;
  board.load(0);  // HEART 5x5
  int sr, sc, er, ec;
  firstCells(board, sr, sc, er, ec);
  CHECK(sr >= 0 && er >= 0);

  // A correct fill fills, does not count, and toggles back off.
  CHECK(board.fill(sr, sc));
  CHECK(board.cell(sr, sc) == picross::Cell::Filled);
  CHECK(board.mistakes() == 0);
  CHECK(board.fill(sr, sc));  // toggle off
  CHECK(board.cell(sr, sc) == picross::Cell::Blank);
  CHECK(board.fill(sr, sc));  // and back on for the win below

  // A wrong fill LOCKS as a mistake, counts, and refuses every later action.
  CHECK(board.fill(er, ec));
  CHECK(board.cell(er, ec) == picross::Cell::Mistake);
  CHECK(board.mistakes() == 1);
  CHECK(!board.fill(er, ec));  // locked: no change
  CHECK(!board.mark(er, ec));  // locked: no change
  CHECK(board.cell(er, ec) == picross::Cell::Mistake);
  CHECK(board.mistakes() == 1);  // not double counted

  // Mark is a free, reversible annotation and never a mistake.
  int er2 = -1, ec2 = -1;
  for (int r = 0; r < board.size() && er2 < 0; ++r)
    for (int c = 0; c < board.size() && er2 < 0; ++c)
      if (!board.solid(r, c) && !(r == er && c == ec)) {
        er2 = r;
        ec2 = c;
      }
  CHECK(board.mark(er2, ec2));
  CHECK(board.cell(er2, ec2) == picross::Cell::Crossed);
  CHECK(board.mistakes() == 1);
  CHECK(board.mark(er2, ec2));  // toggles off
  CHECK(board.cell(er2, ec2) == picross::Cell::Blank);

  // Fill every solid cell -> solved, regardless of the mistake and the marks.
  for (int r = 0; r < board.size(); ++r)
    for (int c = 0; c < board.size(); ++c)
      if (board.solid(r, c) && board.cell(r, c) != picross::Cell::Filled) CHECK(board.fill(r, c));
  CHECK(board.solved());
}

void freeEraseMode() {
  picross::Board board;
  board.load(0);
  board.setRules(picross::Rules::FreeErase);
  int sr, sc, er, ec;
  firstCells(board, sr, sc, er, ec);
  // A wrong fill under FreeErase is a harmless cross: no lock, no count.
  CHECK(board.fill(er, ec));
  CHECK(board.cell(er, ec) == picross::Cell::Crossed);
  CHECK(board.mistakes() == 0);
  // And it is reversible, unlike a Punish mistake.
  CHECK(board.mark(er, ec));
  CHECK(board.cell(er, ec) == picross::Cell::Blank);
}

void satisfiedHonesty() {
  picross::Board board;
  board.load(0);  // HEART 5x5
  // Row 4 of HEART is a single centre cell (clue [1]).
  // Pick a row with exactly one solid cell and verify satisfied flips only when
  // that exact cell is filled, never on a wrong-but-same-count fill (which
  // cannot even happen, since wrong fills never become Filled).
  for (int r = 0; r < board.size(); ++r) {
    int solidCount = 0;
    for (int c = 0; c < board.size(); ++c)
      if (board.solid(r, c)) ++solidCount;
    if (solidCount == 0) {
      CHECK(board.rowSatisfied(r));  // an empty row is satisfied from the start
      continue;
    }
    CHECK(!board.rowSatisfied(r));
  }
  // Fill exactly the solid cells of row 0 -> satisfied.
  for (int c = 0; c < board.size(); ++c)
    if (board.solid(0, c)) CHECK(board.fill(0, c));
  CHECK(board.rowSatisfied(0));
}

void progressAndRestore() {
  picross::Progress prog;
  CHECK(prog.solvedCount() == 0);
  CHECK(prog.nextUnsolved() == 0);
  prog.markSolved(0);
  prog.markSolved(3);
  CHECK(prog.isSolved(0) && prog.isSolved(3) && !prog.isSolved(1));
  CHECK(prog.solvedCount() == 2);
  CHECK(prog.nextUnsolved() == 1);
  prog.markSolved(-1);                     // refused
  prog.markSolved(picross::kPuzzleCount);  // refused
  CHECK(prog.solvedCount() == 2);

  // The widened bitset: a bank past 32 puzzles spills into more than one word,
  // so mark bits either side of every 32-bit boundary and confirm each is read
  // back from its own word and counted once. This is the storage the save
  // persists, so a boundary bug here is lost or phantom progress on device.
  {
    picross::Progress wide;
    int marked = 0;
    for (int i = 0; i < picross::kPuzzleCount; ++i)
      if (i == 0 || i == 31 || i == 32 || i == 63 || i == 64 || i == picross::kPuzzleCount - 1) {
        wide.markSolved(i);
        ++marked;
      }
    CHECK(wide.solvedCount() == marked);
    CHECK(wide.isSolved(31) && wide.isSolved(32));
    CHECK(picross::kPuzzleCount <= 64 || (wide.isSolved(63) && wide.isSolved(64)));
    CHECK(wide.isSolved(picross::kPuzzleCount - 1));
    CHECK(!wide.isSolved(30));
    // Marking every bit but one leaves nextUnsolved on exactly that gap, even
    // when the gap is in a high word.
    picross::Progress full;
    const int gap = picross::kPuzzleCount - 1;
    for (int i = 0; i < picross::kPuzzleCount; ++i)
      if (i != gap) full.markSolved(i);
    CHECK(full.nextUnsolved() == gap);
    CHECK(full.solvedCount() == picross::kPuzzleCount - 1);
    full.markSolved(gap);
    CHECK(full.nextUnsolved() == picross::kPuzzleCount - 1);  // all done -> the last
    CHECK(full.solvedCount() == picross::kPuzzleCount);
  }

  // restore round-trips the cell grid and repairs a corrupt byte.
  picross::Board board;
  board.load(2);
  int sr, sc, er, ec;
  firstCells(board, sr, sc, er, ec);
  board.fill(sr, sc);  // a correct fill
  board.fill(er, ec);  // a mistake
  CHECK(board.mistakes() == 1);
  uint8_t saved[picross::kMaxSize * picross::kMaxSize];
  for (int i = 0; i < picross::kMaxSize * picross::kMaxSize; ++i) saved[i] = board.cells()[i];

  picross::Board back;
  back.restore(2, saved, 1);
  CHECK(back.cell(sr, sc) == picross::Cell::Filled);
  CHECK(back.cell(er, ec) == picross::Cell::Mistake);
  CHECK(back.mistakes() == 1);

  // A Filled byte on a cell that is not solid is dropped to Blank on restore.
  uint8_t corrupt[picross::kMaxSize * picross::kMaxSize];
  for (int i = 0; i < picross::kMaxSize * picross::kMaxSize; ++i) corrupt[i] = 0;
  corrupt[er * picross::kMaxSize + ec] = static_cast<uint8_t>(picross::Cell::Filled);
  picross::Board fixed;
  fixed.restore(2, corrupt, 0);
  CHECK(fixed.cell(er, ec) == picross::Cell::Blank);
}

}  // namespace

int main() {
  validateBank();
  clueDerivation();
  mistakeAndWin();
  freeEraseMode();
  satisfiedHonesty();
  progressAndRestore();

  std::printf("picross: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
