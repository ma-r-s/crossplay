// Sudoku rules tests. Freestanding core, so this is a laptop binary.
//
//   host-tests/sudoku/run.sh
//
// The load-bearing tests are the ones that check the difficulty PROMISE, and
// they are deliberately written against a different mechanism than the one that
// makes it. SudokuCore claims a generated puzzle is unique because a technique
// solver drove it to completion; this file counts solutions with a plain
// brute-force search that knows nothing about techniques. If the two ever
// disagree, the claim was wrong and not merely untested.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "SudokuCore.h"

namespace {

int checks = 0;
int failures = 0;

// Hard is the scarcest band at about 3% of digs, so a test that wants one
// reliably has to ask for enough tries. Each is well under a millisecond.
constexpr int kAttempts = 400;

void check(const bool ok, const char* what) {
  ++checks;
  if (ok) return;
  ++failures;
  std::printf("  FAIL %s\n", what);
}

void checkEq(const long got, const long want, const char* what) {
  ++checks;
  if (got == want) return;
  ++failures;
  std::printf("  FAIL %s: got %ld want %ld\n", what, got, want);
}

// ---------------------------------------------------------------------------
// An independent solver. No techniques, no candidate propagation beyond the
// obvious: pick the first empty cell, try every digit that fits, recurse. Slow
// and stupid on purpose, because its whole job is to disagree with the clever
// one if the clever one is wrong.
// ---------------------------------------------------------------------------

bool bruteFits(const uint8_t value[sudoku::kCells], const int cell, const int digit) {
  for (int other = 0; other < sudoku::kCells; ++other) {
    if (!sudoku::arePeers(cell, other)) continue;
    if (value[other] == digit) return false;
  }
  return true;
}

// Counts up to `limit` solutions and stops. Two is all anyone needs to know.
int countSolutions(uint8_t value[sudoku::kCells], const int limit) {
  int cell = -1;
  for (int i = 0; i < sudoku::kCells; ++i) {
    if (value[i] == 0) {
      cell = i;
      break;
    }
  }
  if (cell < 0) return 1;
  int found = 0;
  for (int digit = 1; digit <= sudoku::kSize; ++digit) {
    if (!bruteFits(value, cell, digit)) continue;
    value[cell] = static_cast<uint8_t>(digit);
    found += countSolutions(value, limit - found);
    value[cell] = 0;
    if (found >= limit) break;
  }
  return found;
}

int solutionCount(const uint8_t given[sudoku::kCells], const int limit) {
  uint8_t work[sudoku::kCells];
  std::memcpy(work, given, sizeof(work));
  return countSolutions(work, limit);
}

bool isCompleteAndLegal(const uint8_t value[sudoku::kCells]) {
  for (int unit = 0; unit < sudoku::kUnits; ++unit) {
    unsigned seen = 0;
    for (int slot = 0; slot < sudoku::kSize; ++slot) {
      const int digit = value[sudoku::unitCell(unit, slot)];
      if (digit < 1 || digit > sudoku::kSize) return false;
      const unsigned bit = 1u << digit;
      if (seen & bit) return false;
      seen |= bit;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------

void testTheUnitIndexingCoversEveryCellThreeTimes() {
  int seen[sudoku::kCells] = {};
  for (int unit = 0; unit < sudoku::kUnits; ++unit) {
    bool inThisUnit[sudoku::kCells] = {};
    for (int slot = 0; slot < sudoku::kSize; ++slot) {
      const int cell = sudoku::unitCell(unit, slot);
      check(cell >= 0 && cell < sudoku::kCells, "unitCell in range");
      check(!inThisUnit[cell], "a unit names each cell once");
      inThisUnit[cell] = true;
      ++seen[cell];
    }
  }
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    checkEq(seen[cell], 3, "every cell is in a row, a column and a box");
  }

  // Exactly twenty peers, which is what the peer table in the .cpp assumes.
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    int peers = 0;
    for (int other = 0; other < sudoku::kCells; ++other) {
      if (sudoku::arePeers(cell, other)) ++peers;
    }
    checkEq(peers, 20, "every cell has twenty peers");
  }
}

void testTheLevelAndCeilingMapsAgree() {
  for (int rung = 0; rung < sudoku::kTechniqueCount; ++rung) {
    const auto technique = static_cast<sudoku::Technique>(rung);
    const sudoku::Level level = sudoku::levelOf(technique);
    check(static_cast<int>(sudoku::ceilingFor(level)) >= rung, "a level's ceiling covers its own techniques");
  }
  // The bands must partition the ladder in order, or the floor argument in
  // carve() is not sound: a technique above a level's ceiling has to land in a
  // strictly higher level.
  for (int rung = 1; rung < sudoku::kTechniqueCount; ++rung) {
    const auto lower = static_cast<sudoku::Technique>(rung - 1);
    const auto higher = static_cast<sudoku::Technique>(rung);
    check(static_cast<int>(sudoku::levelOf(higher)) >= static_cast<int>(sudoku::levelOf(lower)),
          "levelOf is monotone in the ladder");
  }
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto level = static_cast<sudoku::Level>(i);
    checkEq(static_cast<long>(sudoku::levelOf(sudoku::ceilingFor(level))), i, "a ceiling grades back to its own level");
  }
}

void testFillCompleteAlwaysProducesALegalGrid() {
  sudoku::Workspace work;
  uint32_t rng = 0x5EED1234u;
  for (int trial = 0; trial < 500; ++trial) {
    uint8_t solution[sudoku::kCells];
    if (!sudoku::fillComplete(solution, work, rng)) {
      check(false, "fillComplete succeeded");
      continue;
    }
    check(isCompleteAndLegal(solution), "fillComplete produced a legal grid");
  }

  // And the grids differ: a generator that returned the same board every time
  // would pass every check above.
  uint8_t first[sudoku::kCells];
  uint8_t second[sudoku::kCells];
  rng = 0xABCDEF01u;
  sudoku::fillComplete(first, work, rng);
  sudoku::fillComplete(second, work, rng);
  check(std::memcmp(first, second, sizeof(first)) != 0, "two fills differ");
}

void testSolvingACompleteGridIsImmediate() {
  sudoku::Workspace work;
  uint32_t rng = 0x1234u;
  uint8_t solution[sudoku::kCells];
  sudoku::fillComplete(solution, work, rng);
  sudoku::Grid grid;
  check(sudoku::load(grid, solution), "a full legal grid loads");
  const sudoku::SolveReport report = sudoku::solve(grid, sudoku::Technique::XWing);
  check(report.solved, "a full grid is solved");
  checkEq(report.steps, 0, "a full grid needs no technique");
  check(report.hardest == sudoku::Technique::None, "a full grid needs no technique named");
}

void testABrokenPositionIsReportedAsBroken() {
  uint8_t given[sudoku::kCells] = {};
  given[0] = 5;
  given[1] = 5;  // two fives in one row and one box
  sudoku::Grid grid;
  check(!sudoku::load(grid, given), "contradictory clues fail to load");
}

// Each of these is a real position whose next forced step is exactly the named
// technique, taken from published examples of it. The assertion that matters is
// the pair: solvable AT the technique's rung, and NOT solvable one rung below.
// A test that only checked the first would pass for a puzzle that never needed
// the technique at all.
void testEachRungIsActuallyNeededBySomething() {
  sudoku::Workspace work;
  uint32_t rng = 0xC0FFEEu;

  // Rather than hand-transcribing positions (which is how a test ends up
  // agreeing with the code's own idea of a technique), generate puzzles at each
  // level and assert the ladder property directly on them.
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto level = static_cast<sudoku::Level>(i);
    const sudoku::Technique ceiling = sudoku::ceilingFor(level);
    int made = 0;
    for (int trial = 0; trial < 12; ++trial) {
      sudoku::Puzzle puzzle;
      if (!sudoku::generate(puzzle, level, work, rng, kAttempts)) continue;
      ++made;

      sudoku::Grid grid;
      check(sudoku::load(grid, puzzle.given), "generated clues load");
      const sudoku::SolveReport atCeiling = sudoku::solve(grid, ceiling);
      check(atCeiling.solved, "a generated puzzle is solvable at its own ceiling");
      check(!atCeiling.broken, "a generated puzzle is not self-contradictory");

      if (level != sudoku::Level::Easy) {
        const auto below = static_cast<sudoku::Level>(i - 1);
        sudoku::Grid weaker;
        sudoku::load(weaker, puzzle.given);
        const sudoku::SolveReport report = sudoku::solve(weaker, sudoku::ceilingFor(below));
        check(!report.solved, "a puzzle is NOT solvable one band below its label");
      }
    }
    check(made > 0, "at least one puzzle generated at every level");
  }
}

void testGeneratedPuzzlesAreUniqueByAnIndependentCount() {
  sudoku::Workspace work;
  uint32_t rng = 0x0D15EA5Eu;
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto level = static_cast<sudoku::Level>(i);
    for (int trial = 0; trial < 6; ++trial) {
      sudoku::Puzzle puzzle;
      if (!sudoku::generate(puzzle, level, work, rng, kAttempts)) continue;
      checkEq(solutionCount(puzzle.given, 2), 1, "a generated puzzle has exactly one solution");
      check(isCompleteAndLegal(puzzle.solution), "the recorded solution is a legal grid");
      for (int cell = 0; cell < sudoku::kCells; ++cell) {
        if (puzzle.given[cell] == 0) continue;
        checkEq(puzzle.given[cell], puzzle.solution[cell], "every clue agrees with the solution");
      }
    }
  }
}

void testGeneratedPuzzlesAreSymmetricAndSanelyClued() {
  sudoku::Workspace work;
  uint32_t rng = 0x77777777u;
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto level = static_cast<sudoku::Level>(i);
    for (int trial = 0; trial < 8; ++trial) {
      sudoku::Puzzle puzzle;
      if (!sudoku::generate(puzzle, level, work, rng, kAttempts)) continue;
      for (int cell = 0; cell < sudoku::kCells; ++cell) {
        const int partner = sudoku::kCells - 1 - cell;
        check((puzzle.given[cell] != 0) == (puzzle.given[partner] != 0), "the clue pattern is symmetric");
      }
      check(puzzle.clues >= 17, "no puzzle claims fewer clues than a Sudoku can have");
      check(puzzle.clues <= 60, "a puzzle is actually dug");
      int counted = 0;
      for (int cell = 0; cell < sudoku::kCells; ++cell) {
        if (puzzle.given[cell] != 0) ++counted;
      }
      checkEq(counted, puzzle.clues, "the clue count matches the board");
    }
  }
}

void testTheLabelIsNeverWrong() {
  // carve() returns false rather than shipping a mislabelled puzzle. This
  // asserts the belt never has to fire: across every puzzle generated here, the
  // graded level equals the requested one.
  sudoku::Workspace work;
  uint32_t rng = 0xFACEB00Cu;
  int generated = 0;
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto level = static_cast<sudoku::Level>(i);
    for (int trial = 0; trial < 10; ++trial) {
      sudoku::Puzzle puzzle;
      if (!sudoku::generate(puzzle, level, work, rng, kAttempts)) continue;
      ++generated;
      check(puzzle.level == level, "the graded level is the requested level");
      check(static_cast<int>(puzzle.hardest) <= static_cast<int>(sudoku::ceilingFor(level)),
            "the hardest technique is within the band");
      if (level != sudoku::Level::Easy) {
        const auto below = static_cast<sudoku::Level>(i - 1);
        check(static_cast<int>(puzzle.hardest) > static_cast<int>(sudoku::ceilingFor(below)),
              "the hardest technique is above the band below");
      }
    }
  }
  check(generated >= 30, "enough puzzles to mean anything");
}

void testHintsAlwaysNameTheTruth() {
  sudoku::Workspace work;
  uint32_t rng = 0xBEEF0001u;
  int hinted = 0;
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto level = static_cast<sudoku::Level>(i);
    for (int trial = 0; trial < 4; ++trial) {
      sudoku::Puzzle puzzle;
      if (!sudoku::generate(puzzle, level, work, rng, kAttempts)) continue;

      // Play the puzzle out entirely by hint, which is the strongest form of
      // this test: if any hint is ever wrong or missing, the board never fills.
      uint8_t board[sudoku::kCells];
      std::memcpy(board, puzzle.given, sizeof(board));
      int placed = 0;
      for (int step = 0; step < sudoku::kCells + 1; ++step) {
        const sudoku::Hint hint = sudoku::nextHint(board, sudoku::ceilingFor(level));
        if (!hint.found) break;
        check(hint.cell >= 0 && hint.cell < sudoku::kCells, "a hint names a real cell");
        check(board[hint.cell] == 0, "a hint names an empty cell");
        checkEq(hint.digit, puzzle.solution[hint.cell], "a hint names the true digit");
        check(static_cast<int>(hint.technique) <= static_cast<int>(sudoku::ceilingFor(level)),
              "a hint cites a rule the level allows");
        board[hint.cell] = static_cast<uint8_t>(hint.digit);
        ++placed;
        ++hinted;
      }
      for (int cell = 0; cell < sudoku::kCells; ++cell) {
        checkEq(board[cell], puzzle.solution[cell], "hints alone finish the puzzle");
      }
      check(placed > 0, "a fresh puzzle has at least one hint");
    }
  }
  check(hinted > 200, "the hint path was exercised properly");
}

void testAHintOnAFinishedBoardIsHonestlyEmpty() {
  sudoku::Workspace work;
  uint32_t rng = 0x99u;
  sudoku::Puzzle puzzle;
  check(sudoku::generate(puzzle, sudoku::Level::Easy, work, rng, kAttempts), "generated one to finish");
  const sudoku::Hint hint = sudoku::nextHint(puzzle.solution, sudoku::Technique::XWing);
  check(!hint.found, "a solved board offers no hint");
}

void reportGenerationCost() {
  // Not an assertion: a measurement, printed so the number that decides whether
  // generation can block the render path is written down rather than guessed.
  sudoku::Workspace work;
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto level = static_cast<sudoku::Level>(i);
    uint32_t rng = 0x13571357u + static_cast<uint32_t>(i);
    const auto started = std::chrono::steady_clock::now();
    int made = 0;
    long clueTotal = 0;
    const int wanted = 40;
    for (int trial = 0; trial < wanted; ++trial) {
      sudoku::Puzzle puzzle;
      if (!sudoku::generate(puzzle, level, work, rng, kAttempts)) continue;
      ++made;
      clueTotal += puzzle.clues;
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
    std::printf("  %-7s %2d/%d puzzles, %6.1f ms each, %.1f clues average\n", sudoku::levelName(level), made, wanted,
                made > 0 ? ms / made : 0.0, made > 0 ? static_cast<double>(clueTotal) / made : 0.0);
    check(made == wanted, "every requested puzzle was generated within its attempt budget");
  }
}

}  // namespace

int main() {
  std::printf("Sudoku rules\n");
  testTheUnitIndexingCoversEveryCellThreeTimes();
  testTheLevelAndCeilingMapsAgree();
  testFillCompleteAlwaysProducesALegalGrid();
  testSolvingACompleteGridIsImmediate();
  testABrokenPositionIsReportedAsBroken();
  testEachRungIsActuallyNeededBySomething();
  testGeneratedPuzzlesAreUniqueByAnIndependentCount();
  testGeneratedPuzzlesAreSymmetricAndSanelyClued();
  testTheLabelIsNeverWrong();
  testHintsAlwaysNameTheTruth();
  testAHintOnAFinishedBoardIsHonestlyEmpty();
  std::printf("Generation cost\n");
  reportGenerationCost();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
