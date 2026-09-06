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
#include <cstring>
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

  // Mark is a free, reversible annotation and never a mistake. The cell has to
  // be BLANK, not merely empty: a correct fill can satisfy its line, and a
  // satisfied line is auto-crossed, so "the first empty cell" is often already
  // Crossed and marking it would toggle it OFF -- which is the opposite of what
  // this is checking.
  int er2 = -1, ec2 = -1;
  for (int r = 0; r < board.size() && er2 < 0; ++r)
    for (int c = 0; c < board.size() && er2 < 0; ++c)
      if (!board.solid(r, c) && board.cell(r, c) == picross::Cell::Blank) {
        er2 = r;
        ec2 = c;
      }
  CHECK(er2 >= 0);
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

// Every picture must USE the grid it claims: no empty first or last row, no
// empty first or last column. An empty edge line means the drawing was never
// cropped to its bounding box, so a puzzle labelled 15x15 can really be a 13x13
// -- and once was a 15x8 (WEDGE). The SIZE LABEL is what tells the player the
// difficulty tier, so a picture that does not fill its grid is a label that
// lies. Interior empty lines stay legal: a picture may genuinely have a gap.
// The generator refuses these; this re-proves it over the header that ships.
void bankFillsItsGrid() {
  for (int p = 0; p < picross::kPuzzleCount; ++p) {
    const picross::Puzzle& z = picross::kPuzzles[p];
    const int n = z.size;
    const uint16_t mask = static_cast<uint16_t>((1u << n) - 1u);
    const bool firstRow = (z.rows[0] & mask) != 0;
    const bool lastRow = (z.rows[n - 1] & mask) != 0;
    bool firstCol = false;
    bool lastCol = false;
    for (int r = 0; r < n; ++r) {
      if (z.rows[r] & uint16_t{1}) firstCol = true;
      if (z.rows[r] & static_cast<uint16_t>(uint16_t{1} << (n - 1))) lastCol = true;
    }
    if (!firstRow || !lastRow || !firstCol || !lastCol)
      std::printf("  %s %dx%d does not fill its grid (row0=%d rowN=%d col0=%d colN=%d)\n", z.name, n, n,
                  static_cast<int>(firstRow), static_cast<int>(lastRow), static_cast<int>(firstCol),
                  static_cast<int>(lastCol));
    CHECK(firstRow);
    CHECK(lastRow);
    CHECK(firstCol);
    CHECK(lastCol);
  }
}

// The picker recovers its size tabs by RUN-SCANNING the bank: a new group
// starts wherever the size differs from the previous entry, into
// kSizeGroupCount slots, and it breaks out when they are full. That is only
// correct while each size is ONE contiguous run. An unsorted bank -- which is
// exactly what appending an import produces -- makes every alternation a fresh
// group, fills the slots, and leaves every puzzle after that point unreachable
// from the tabs. Nothing renders wrong, nothing logs, nothing fails: the
// puzzles are simply not there any more.
//
// So the sortedness is asserted here rather than trusted. The generator
// constructs it (gen_picross.sort_by_size) and this re-proves it over the
// header that ships, which is the only copy the device reads.
void bankIsSizeSorted() {
  int runs = 0;
  for (int p = 0; p < picross::kPuzzleCount; ++p) {
    if (p == 0 || picross::kPuzzles[p].size != picross::kPuzzles[p - 1].size) ++runs;
    if (p > 0 && picross::kPuzzles[p].size < picross::kPuzzles[p - 1].size)
      std::printf("  bank is not size-sorted: puzzle %d is %dx%d after %dx%d\n", p, picross::kPuzzles[p].size,
                  picross::kPuzzles[p].size, picross::kPuzzles[p - 1].size, picross::kPuzzles[p - 1].size);
    CHECK(p == 0 || picross::kPuzzles[p].size >= picross::kPuzzles[p - 1].size);
  }
  // One run per distinct size, and exactly as many as the picker has slots for.
  // Counting the runs is the half that matters: sorted-ascending alone would
  // still pass with a size that appears twice, which is the shape that breaks
  // the scan.
  if (runs != picross::kSizeGroupCount)
    std::printf("  bank has %d size runs but kSizeGroupCount is %d\n", runs, picross::kSizeGroupCount);
  CHECK(runs == picross::kSizeGroupCount);
}

// Every puzzle names a provenance row that exists, and no row is blank where a
// blank would read as a claim. An empty `license` and "all rights reserved" are
// the same fact, so the empty string must never stand in for one: a picture
// whose licence was never recorded has to SAY that, or the next reader assumes
// it was cleared. `source` is the one field legitimately empty -- artwork drawn
// for this fork came from nowhere.
// The bank ships ONE size, and the picker's static_assert is the other half of
// this: it draws no size tabs, so a second tier would run together with the
// first in one flat sequence of pages with nothing saying so.
void bankShipsOneSize() {
  CHECK(picross::kSizeGroupCount == 1);
  for (int p = 0; p < picross::kPuzzleCount; ++p) {
    if (picross::kPuzzles[p].size != picross::kPuzzles[0].size)
      std::printf("  puzzle %d is %dx%d, the bank's first is %dx%d\n", p, picross::kPuzzles[p].size,
                  picross::kPuzzles[p].size, picross::kPuzzles[0].size, picross::kPuzzles[0].size);
    CHECK(picross::kPuzzles[p].size == picross::kPuzzles[0].size);
  }
  // And kMaxSize is that size, not a leftover from a wider bank: it sizes the
  // save's cell grid and every clue buffer, so a stale one is silent waste in
  // flash, in RAM and in every SaveState written to the card.
  CHECK(picross::kMaxSize == picross::kPuzzles[0].size);
}

// The name is the reveal, and it is the only string the bank carries now that
// the designer, the rights line and the source URL have left the firmware for
// PROVENANCE.md. It is written by hand, so a bank is normally PART named and an
// empty name is not a fault -- what would be a fault is a name that cannot be
// drawn.
//
// The generator refuses a bad one (gen_picross.load_names), and this asserts the
// same properties over the header that actually ships, so a hand-edit cannot
// slip one past: within kMaxNameLen, and made only of glyphs the display cut
// has. A glyph it lacks is a HOLE in the word, not a box, so this is the
// difference between a reveal reading "R  BIT" and reading "RABBIT".
void bankNamesAreDrawable() {
  const char* allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -'";
  int named = 0;
  int longest = 0;
  for (int p = 0; p < picross::kPuzzleCount; ++p) {
    const char* name = picross::kPuzzles[p].name;
    CHECK(name != nullptr);
    if (name == nullptr || name[0] == '\0') continue;
    ++named;
    const int len = static_cast<int>(std::strlen(name));
    if (len > longest) longest = len;
    if (len > picross::kMaxNameLen)
      std::printf("  puzzle %d is named %s, %d characters against kMaxNameLen %d\n", p, name, len,
                  picross::kMaxNameLen);
    CHECK(len <= picross::kMaxNameLen);
    for (const char* ch = name; *ch != '\0'; ++ch) {
      if (std::strchr(allowed, *ch) == nullptr)
        std::printf("  puzzle %d is named %s, which uses %c -- not in the display cut\n", p, name, *ch);
      CHECK(std::strchr(allowed, *ch) != nullptr);
    }
  }
  // kMaxNameLen is DERIVED by the generator from the names it emitted. If it
  // disagrees with the bank, something hand-edited one of the two.
  CHECK(longest == picross::kMaxNameLen);
  std::printf("  (%d of %d puzzles named, longest %d)\n", named, picross::kPuzzleCount, longest);
}

// A line whose solid cells are all filled has every REMAINING cell provably
// empty, so the game crosses them off rather than making the player do it by
// hand. Mario asked for it; this is the proof it happens, that it never places
// ink anywhere it could be wrong, and that it cannot disturb the invariant the
// whole game rests on.
void autoMarksSatisfiedLines() {
  for (int p = 0; p < picross::kPuzzleCount; ++p) {
    picross::Board board;
    board.load(p);
    const int n = board.size();

    // Pick a row with at least one solid cell AND at least one empty one, so
    // "the rest of the line" is not the empty set.
    int target = -1;
    for (int r = 0; r < n && target < 0; ++r) {
      int solid = 0;
      for (int c = 0; c < n; ++c)
        if (board.solid(r, c)) ++solid;
      if (solid > 0 && solid < n) target = r;
    }
    if (target < 0) continue;  // a bank of solid stripes; nothing to prove here

    for (int c = 0; c < n; ++c)
      if (board.solid(target, c)) board.fill(target, c);
    CHECK(board.rowSatisfied(target));
    for (int c = 0; c < n; ++c) {
      const picross::Cell got = board.cell(target, c);
      if (board.solid(target, c)) {
        CHECK(got == picross::Cell::Filled);
      } else {
        // Crossed, never Mistake: an auto mark is an annotation, so it can
        // never add to the count or lock a cell.
        if (got != picross::Cell::Crossed)
          std::printf("  puzzle %d row %d col %d is %d, not Crossed\n", p, target, c, static_cast<int>(got));
        CHECK(got == picross::Cell::Crossed);
      }
    }
    // It cost nothing and claimed nothing: no mistake, and not solved unless
    // the picture really is finished.
    CHECK(board.mistakes() == 0);
  }

  picross::Board board;
  board.load(0);
  const int n = board.size();

  // AN AUTO MARK IS AN ORDINARY MARK. The player can rub it out exactly like
  // one they placed, because it is stored as the same thing with no flag saying
  // who put it there. A mark the game placed and the player could not remove
  // would be the only uneraseable annotation in the game.
  int row = -1;
  for (int r = 0; r < n && row < 0; ++r) {
    int solid = 0;
    for (int c = 0; c < n; ++c)
      if (board.solid(r, c)) ++solid;
    if (solid > 0 && solid < n) row = r;
  }
  CHECK(row >= 0);
  for (int c = 0; c < n; ++c)
    if (board.solid(row, c)) board.fill(row, c);
  int auto_ = -1;
  for (int c = 0; c < n && auto_ < 0; ++c)
    if (board.cell(row, c) == picross::Cell::Crossed) auto_ = c;
  CHECK(auto_ >= 0);
  CHECK(board.mark(row, auto_));
  CHECK(board.cell(row, auto_) == picross::Cell::Blank);

  // And a cell it crossed can still be FILLED afterwards -- which, on a
  // satisfied line, is a wrong fill and must still lock as a mistake. Auto
  // marking must not quietly immunise the player against their own taps.
  picross::Board second;
  second.load(0);
  for (int c = 0; c < second.size(); ++c)
    if (second.solid(row, c)) second.fill(row, c);
  int crossed = -1;
  for (int c = 0; c < second.size() && crossed < 0; ++c)
    if (second.cell(row, c) == picross::Cell::Crossed) crossed = c;
  CHECK(crossed >= 0);
  CHECK(second.fill(row, crossed));
  CHECK(second.cell(row, crossed) == picross::Cell::Mistake);
  CHECK(second.mistakes() == 1);

  // A FRESH board is never pre-marked. autoMark runs only from a fill that
  // landed, so a puzzle nobody has touched reports touched() == false -- and
  // that matters beyond tidiness: touched() is what makes the picker offer
  // RESUME, so a board that auto-marked on load would offer to resume itself.
  picross::Board fresh;
  fresh.load(0);
  CHECK(!fresh.touched());
  for (int r = 0; r < fresh.size(); ++r)
    for (int c = 0; c < fresh.size(); ++c) CHECK(fresh.cell(r, c) == picross::Cell::Blank);
}

// rowSatisfied and colSatisfied are ONE question asked along two axes, and this
// fork's most expensive recurring bug is repairing one of a symmetric pair and
// leaving its twin. On a fully solved board every row AND every column must be
// satisfied. With exactly one solid cell left unfilled, exactly the row and the
// column through that cell must be the only unsatisfied lines -- which is the
// state a half-solved board renders, and the one a reader mistook for a bug.
void satisfiedAgreesOnBothAxes() {
  for (int p = 0; p < picross::kPuzzleCount; ++p) {
    picross::Board board;
    board.load(p);
    const int n = board.size();
    for (int r = 0; r < n; ++r)
      for (int c = 0; c < n; ++c)
        if (board.solid(r, c)) board.fill(r, c);
    CHECK(board.solved());
    for (int r = 0; r < n; ++r) CHECK(board.rowSatisfied(r));
    for (int c = 0; c < n; ++c) CHECK(board.colSatisfied(c));
  }

  picross::Board board;
  board.load(picross::kPuzzleCount - 1);
  const int n = board.size();
  // The hole must sit OFF the diagonal. With row == col a swapped axis gives the
  // same answer, so the obvious choice (the first solid cell, usually on the
  // diagonal) makes this test blind to exactly the bug it exists to catch --
  // confirmed by mutating colSatisfied to use the row counts and watching a
  // diagonal hole still pass.
  int hr = -1;
  int hc = -1;
  for (int r = 0; r < n && hr < 0; ++r)
    for (int c = 0; c < n && hr < 0; ++c)
      if (r != c && board.solid(r, c)) {
        hr = r;
        hc = c;
      }
  CHECK(hr >= 0);
  CHECK(hr != hc);
  for (int r = 0; r < n; ++r)
    for (int c = 0; c < n; ++c)
      if (board.solid(r, c) && !(r == hr && c == hc)) board.fill(r, c);
  CHECK(!board.solved());
  for (int r = 0; r < n; ++r) CHECK(board.rowSatisfied(r) == (r != hr));
  for (int c = 0; c < n; ++c) CHECK(board.colSatisfied(c) == (c != hc));
}

}  // namespace

int main() {
  validateBank();
  bankFillsItsGrid();
  bankIsSizeSorted();
  bankShipsOneSize();
  bankNamesAreDrawable();
  autoMarksSatisfiedLines();
  satisfiedAgreesOnBothAxes();
  clueDerivation();
  mistakeAndWin();
  freeEraseMode();
  satisfiedHonesty();
  progressAndRestore();

  std::printf("picross: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
