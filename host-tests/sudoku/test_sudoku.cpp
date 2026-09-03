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
#include "SudokuGame.h"

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

// ---------------------------------------------------------------------------
// The game the player touches.
// ---------------------------------------------------------------------------

sudoku::Puzzle aPuzzle(const sudoku::Level level, uint32_t& rng, sudoku::Workspace& work) {
  sudoku::Puzzle puzzle;
  check(sudoku::generate(puzzle, level, work, rng, kAttempts), "generated a puzzle for the game tests");
  return puzzle;
}

// The promise is that no tap on any cell is ever a no-op, because on a panel
// with no press feedback a dead tap and a missed tap look identical. Asserted
// over every cell of a real puzzle, in both the empty and the occupied case,
// rather than on a couple of examples.
void testNoTapOnAnyCellIsEverDead() {
  sudoku::Workspace work;
  uint32_t rng = 0x11223344u;
  const sudoku::Puzzle puzzle = aPuzzle(sudoku::Level::Medium, rng, work);

  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    for (int armed = 1; armed <= sudoku::kSize; ++armed) {
      sudoku::Game game;
      sudoku::startGame(game, puzzle);
      game.armed = static_cast<uint8_t>(armed);

      // Empty (or clued) cell.
      const uint8_t beforeArmed = game.armed;
      const uint8_t beforeEntry = game.entry[cell];
      sudoku::tapCell(game, cell);
      if (sudoku::isGiven(game, cell)) {
        checkEq(game.armed, puzzle.given[cell], "tapping a clue picks its digit up");
        checkEq(game.entry[cell], 0, "tapping a clue writes nothing");
      } else {
        check(game.entry[cell] != beforeEntry, "tapping an empty cell writes the armed digit");
        checkEq(game.entry[cell], beforeArmed, "and writes the armed digit specifically");
      }

      // The same cell again, now occupied.
      if (sudoku::isGiven(game, cell)) continue;
      sudoku::tapCell(game, cell);
      checkEq(game.entry[cell], 0, "tapping your own digit again clears it");

      // Occupied by something else.
      game.entry[cell] = static_cast<uint8_t>(armed == 9 ? 1 : armed + 1);
      sudoku::tapCell(game, cell);
      checkEq(game.entry[cell], beforeArmed, "tapping a different digit overwrites it");
    }
  }
}

void testHoldPencilsAndRubsOut() {
  sudoku::Workspace work;
  uint32_t rng = 0x55667788u;
  const sudoku::Puzzle puzzle = aPuzzle(sudoku::Level::Easy, rng, work);
  sudoku::Game game;
  sudoku::startGame(game, puzzle);

  int open = -1;
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    if (!sudoku::isGiven(game, cell)) {
      open = cell;
      break;
    }
  }
  check(open >= 0, "a puzzle has an empty cell");

  game.armed = 4;
  sudoku::holdCell(game, open);
  check((game.note[open] & sudoku::bitFor(4)) != 0, "a hold pencils the armed digit");
  sudoku::holdCell(game, open);
  checkEq(game.note[open], 0, "holding again rubs it out");

  // A hold on a cell you had filled clears the digit and pencils instead.
  sudoku::tapCell(game, open);
  check(game.entry[open] != 0, "the cell has a digit to displace");
  game.armed = 7;
  sudoku::holdCell(game, open);
  checkEq(game.entry[open], 0, "a hold clears the digit it replaces");
  check((game.note[open] & sudoku::bitFor(7)) != 0, "and leaves the mark");

  // A clue never changes, however it is touched.
  int clue = -1;
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    if (sudoku::isGiven(game, cell)) {
      clue = cell;
      break;
    }
  }
  check(clue >= 0, "a puzzle has a clue");
  const uint8_t was = puzzle.given[clue];
  sudoku::tapCell(game, clue);
  sudoku::holdCell(game, clue);
  checkEq(game.puzzle.given[clue], was, "a clue survives being tapped and held");
  checkEq(game.entry[clue], 0, "and nothing is written over it");
  checkEq(game.note[clue], 0, "and nothing is pencilled on it");
}

// Undo has to restore the board EXACTLY, marks included, or it is a second way
// to lose work rather than a way to recover it. Driven with a long random walk
// and compared against snapshots rather than spot-checked.
void testUndoWalksTheBoardBackExactly() {
  sudoku::Workspace work;
  uint32_t rng = 0x0BADF00Du;
  const sudoku::Puzzle puzzle = aPuzzle(sudoku::Level::Hard, rng, work);
  sudoku::Game game;
  sudoku::startGame(game, puzzle);

  constexpr int kSteps = sudoku::kUndoDepth;
  uint8_t entrySnapshot[kSteps][sudoku::kCells];
  sudoku::Mask noteSnapshot[kSteps][sudoku::kCells];

  for (int step = 0; step < kSteps; ++step) {
    std::memcpy(entrySnapshot[step], game.entry, sizeof(game.entry));
    std::memcpy(noteSnapshot[step], game.note, sizeof(game.note));
    const int cell = static_cast<int>(sudoku::nextRandom(rng) % sudoku::kCells);
    game.armed = static_cast<uint8_t>(1 + sudoku::nextRandom(rng) % sudoku::kSize);
    if (sudoku::isGiven(game, cell)) {
      // A clue changes nothing, so it pushes no undo either: re-snapshot.
      --step;
      continue;
    }
    if (sudoku::nextRandom(rng) % 2 == 0) {
      sudoku::tapCell(game, cell);
    } else {
      sudoku::holdCell(game, cell);
    }
  }

  for (int step = kSteps - 1; step >= 0; --step) {
    check(sudoku::undoOnce(game), "undo has something to give back");
    checkEq(std::memcmp(game.entry, entrySnapshot[step], sizeof(game.entry)), 0, "undo restores every digit");
    checkEq(std::memcmp(game.note, noteSnapshot[step], sizeof(game.note)), 0, "undo restores every mark");
  }
  check(!sudoku::undoOnce(game), "an exhausted undo says so");
}

void testVisibleNotesHideWhatAPeerHasTaken() {
  sudoku::Workspace work;
  uint32_t rng = 0x2A2A2A2Au;
  const sudoku::Puzzle puzzle = aPuzzle(sudoku::Level::Medium, rng, work);
  sudoku::Game game;
  sudoku::startGame(game, puzzle);

  // Pencil every digit into every empty cell, which is the worst case.
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    if (!sudoku::isGiven(game, cell)) game.note[cell] = sudoku::kAllDigits;
  }
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    const sudoku::Mask shown = sudoku::visibleNotes(game, cell);
    if (sudoku::valueAt(game, cell) != 0) {
      checkEq(shown, 0, "a filled cell shows no marks");
      continue;
    }
    for (int digit = 1; digit <= sudoku::kSize; ++digit) {
      if (!(shown & sudoku::bitFor(digit))) continue;
      for (int other = 0; other < sudoku::kCells; ++other) {
        if (!sudoku::arePeers(cell, other)) continue;
        check(sudoku::valueAt(game, other) != digit, "a shown mark is not already taken by a peer");
      }
    }
  }

  // And the stored marks were never touched, which is what keeps undo one cell
  // wide. A version that struck peers' notes on placement would fail here.
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    if (sudoku::isGiven(game, cell)) continue;
    checkEq(game.note[cell], sudoku::kAllDigits, "the pencil marks are exactly as pencilled");
  }
}

void testClashesAreFoundAndSolvingIsRecognised() {
  sudoku::Workspace work;
  uint32_t rng = 0x9E3779B9u;
  const sudoku::Puzzle puzzle = aPuzzle(sudoku::Level::Easy, rng, work);
  sudoku::Game game;
  sudoku::startGame(game, puzzle);

  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    check(!sudoku::isClashing(game, cell), "a fresh puzzle has no clash");
  }
  checkEq(sudoku::emptyCount(game), sudoku::kCells - puzzle.clues, "the empty count matches the clue count");
  check(!sudoku::isSolved(game), "a fresh puzzle is not solved");

  // Fill it correctly and it should settle, with the digit counts landing at
  // nine apiece.
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    if (sudoku::isGiven(game, cell)) continue;
    game.armed = puzzle.solution[cell];
    sudoku::tapCell(game, cell);
  }
  check(sudoku::isSolved(game), "filling in the answer solves it");
  checkEq(game.solvedFlag, 1, "and the game says so");
  checkEq(sudoku::emptyCount(game), 0, "with nothing left empty");
  for (int digit = 1; digit <= sudoku::kSize; ++digit) {
    checkEq(sudoku::placedCount(game, digit), 9, "every digit is placed nine times");
  }
  checkEq(sudoku::firstWrong(game), sudoku::kNoCell, "and nothing is wrong");

  // Now break it: two of a digit in one row must both read as clashing.
  int a = -1;
  int b = -1;
  for (int cell = 0; cell < sudoku::kCells; ++cell) {
    if (sudoku::isGiven(game, cell)) continue;
    for (int other = cell + 1; other < sudoku::kCells; ++other) {
      if (sudoku::isGiven(game, other) || !sudoku::arePeers(cell, other)) continue;
      a = cell;
      b = other;
      break;
    }
    if (a >= 0) break;
  }
  check(a >= 0 && b >= 0, "found two of the player's own cells that are peers");
  game.armed = game.entry[a];
  sudoku::tapCell(game, b);
  check(sudoku::isClashing(game, a), "the first of a clashing pair is marked");
  check(sudoku::isClashing(game, b), "and so is the second");
  check(!sudoku::isSolved(game), "a clashing board is not solved");
  check(sudoku::firstWrong(game) != sudoku::kNoCell, "and a wrong digit is findable");
}

void testTheRecordOnlyTimesUnhintedSolves() {
  sudoku::Record record;
  checkEq(sudoku::totalSolved(record), 0, "a fresh record is empty");

  sudoku::recordSolve(record, sudoku::Level::Hard, 600000, 0);
  checkEq(record.solved[static_cast<int>(sudoku::Level::Hard)], 1, "a solve is counted");
  checkEq(static_cast<long>(record.bestMs[static_cast<int>(sudoku::Level::Hard)]), 600000, "and timed");

  sudoku::recordSolve(record, sudoku::Level::Hard, 300000, 2);
  checkEq(record.solved[static_cast<int>(sudoku::Level::Hard)], 2, "a hinted solve is still counted");
  checkEq(static_cast<long>(record.bestMs[static_cast<int>(sudoku::Level::Hard)]), 600000,
          "but a hinted run cannot set a best time");
  checkEq(record.hintsTaken, 2, "hints are tallied");

  sudoku::recordSolve(record, sudoku::Level::Hard, 300000, 0);
  checkEq(static_cast<long>(record.bestMs[static_cast<int>(sudoku::Level::Hard)]), 300000,
          "an unhinted run does set one");
  checkEq(sudoku::totalSolved(record), 3, "the total counts every level");
}

// ---------------------------------------------------------------------------
// The front door under a saved grid.
//
// A data-loss bug shipped here, and its shape is why these are driven rather
// than sampled. DIFFICULTY set a one-way latch that only entering the app or
// starting a game cleared, and the row steps (level + 1) % 4 -- so FOUR taps
// put the menu back on the saved puzzle's own level with the latch still set.
// The grid was still drawn, cell for cell, and the door under it had quietly
// become the one that overwrites it. No confirmation, no timing window: anyone
// curious enough to look at what the levels were lost their puzzle.
//
// Every case below runs from every starting level, because the bug was
// invisible from the one place anybody looks -- the level you started on.
// ---------------------------------------------------------------------------

// The menu row's own step. Kept identical to SudokuActivity's on purpose: a
// test that walked the levels some other way would not be walking the taps.
sudoku::Level nextMenuLevel(const sudoku::Level level) {
  return static_cast<sudoku::Level>((static_cast<int>(level) + 1) % sudoku::kLevelCount);
}

sudoku::Game gameCarvedAt(const sudoku::Level level) {
  sudoku::Game game;
  game.puzzle.level = level;
  return game;
}

void testCyclingTheLevelRowBackReadsAsResumeAgain() {
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto saved = static_cast<sudoku::Level>(i);
    const sudoku::Game game = gameCarvedAt(saved);
    check(sudoku::canResume(game, true, saved), "the menu opens resumable on the saved puzzle's level");

    sudoku::Level menu = saved;
    for (int tap = 1; tap < sudoku::kLevelCount; ++tap) {
      menu = nextMenuLevel(menu);
      check(!sudoku::canResume(game, true, menu), "another level offers a new puzzle");
      check(sudoku::switchesLevel(game, true, menu), "and the caption says it is starting fresh");
    }

    menu = nextMenuLevel(menu);
    checkEq(static_cast<int>(menu), static_cast<int>(saved), "four taps return the row to where it started");
    check(sudoku::canResume(game, true, menu), "and the door is RESUME again, not NEW PUZZLE");
    check(!sudoku::switchesLevel(game, true, menu), "with nothing to start fresh from");
  }
}

void testTheDoorIsHonestWithNoSaveAndWithAFinishedOne() {
  // The empty card, and the reason `hasGame` cannot be dropped from the
  // predicate: an unset Puzzle grades as Easy and so does an unset menu, so the
  // two levels MATCH here. Only `hasGame` stands between a fresh card and
  // RESUME over nothing at all.
  const sudoku::Game none;
  checkEq(static_cast<int>(none.puzzle.level), static_cast<int>(sudoku::Level::Easy),
          "an unset puzzle default-grades as Easy");
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto menu = static_cast<sudoku::Level>(i);
    check(!sudoku::canResume(none, false, menu), "nothing saved is never resumable");
    check(!sudoku::switchesLevel(none, false, menu), "and has no level to be switched away from");
  }

  // A finished grid stays on the panel wearing SOLVED; the menu's door is the
  // one that replaces it. Resuming it would open a board with no move in it.
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto saved = static_cast<sudoku::Level>(i);
    sudoku::Game solved = gameCarvedAt(saved);
    solved.solvedFlag = 1;
    check(!sudoku::canResume(solved, true, saved), "a solved puzzle is not resumed");
    check(!sudoku::switchesLevel(solved, true, saved), "and reads as solved rather than as a level change");
    check(sudoku::switchesLevel(solved, true, nextMenuLevel(saved)), "picking another level after a solve does");
  }
}

void testAPuzzleJustCarvedIsImmediatelyResumable() {
  // The predicate reads `puzzle.level`, which the generator writes by GRADING
  // rather than by copying the request. If those two ever came apart, the front
  // door would offer NEW PUZZLE the instant a board was made -- over the board
  // it had just made.
  sudoku::Workspace work;
  uint32_t rng = 0x51D0C0DEu;
  int made = 0;
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    const auto level = static_cast<sudoku::Level>(i);
    sudoku::Puzzle puzzle;
    if (!sudoku::generate(puzzle, level, work, rng, kAttempts)) continue;
    ++made;
    sudoku::Game game;
    sudoku::startGame(game, puzzle);
    check(sudoku::canResume(game, true, level), "a puzzle just carved resumes at the level it was asked for");
    check(!sudoku::switchesLevel(game, true, level), "and is not a level change");
  }
  checkEq(made, sudoku::kLevelCount, "one puzzle carved at every level");
}

// Every string that can land in the board's status capsule, against the width
// that capsule actually has.
//
// This is pinned as a character count rather than measured, and that is honest
// about its limits: the real constraint is 244px in the display cut, and the
// number 11 came off a render where 14 characters truncated. What makes it
// worth having is the FAILURE MODE it guards. The capsule ellipsizes with
// U+2026, the Toybox face is subset to ASCII, and a glyph the font does not
// have draws as NOTHING -- so an over-long notice loses a letter and gains no
// mark at all. "LAST FREE CELL" shipped for one render as "LAST FREE CEL" and
// looked like a typo rather than a layout bug.
void testEveryNoticeFitsTheCapsule() {
  for (int rung = 0; rung < sudoku::kTechniqueCount; ++rung) {
    const char* name = sudoku::techniqueName(static_cast<sudoku::Technique>(rung));
    check(static_cast<int>(std::strlen(name)) <= sudoku::kMaxNoticeChars, "a technique name fits the capsule");
    check(name[0] != '\0', "a technique name is not empty");
  }
  // The activity's own notices, which share the capsule.
  const char* const kNotices[] = {"MAKING ONE", "SOLVED", "WRONG DIGIT", "NOTHING YET"};
  for (const char* notice : kNotices) {
    check(static_cast<int>(std::strlen(notice)) <= sudoku::kMaxNoticeChars, "an activity notice fits the capsule");
  }
  // And the level names, which ride in the header band beside the title.
  for (int i = 0; i < sudoku::kLevelCount; ++i) {
    check(static_cast<int>(std::strlen(sudoku::levelName(static_cast<sudoku::Level>(i)))) <= 6,
          "a level name is short");
  }
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
  testEveryNoticeFitsTheCapsule();
  std::printf("Sudoku game\n");
  testNoTapOnAnyCellIsEverDead();
  testHoldPencilsAndRubsOut();
  testUndoWalksTheBoardBackExactly();
  testVisibleNotesHideWhatAPeerHasTaken();
  testClashesAreFoundAndSolvingIsRecognised();
  testTheRecordOnlyTimesUnhintedSolves();
  testCyclingTheLevelRowBackReadsAsResumeAgain();
  testTheDoorIsHonestWithNoSaveAndWithAFinishedOne();
  testAPuzzleJustCarvedIsImmediatelyResumable();
  std::printf("Generation cost\n");
  reportGenerationCost();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
