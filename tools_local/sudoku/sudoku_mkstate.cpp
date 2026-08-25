// Writes a Sudoku save file to stdout, so a screenshot can be reproduced.
//
//   c++ -std=c++17 -O2 -Isrc/apps_local/sudoku \
//     src/apps_local/sudoku/SudokuCore.cpp tools_local/sudoku/sudoku_mkstate.cpp -o /tmp/sudoku_mkstate
//   /tmp/sudoku_mkstate midway > fs_agent/.crosspoint/sudoku.sav
//
// The seed is fixed, so the same argument gives the same grid every time. That
// is the whole point: a layout judged against an empty save is a layout nobody
// will ever see, and a screenshot no one can reproduce rots the moment the
// generator changes.
#include <cstdio>
#include <cstring>
#include <string>

#include "SudokuCore.h"

namespace {

sudoku::Workspace work;

void emit(const sudoku::Puzzle& puzzle, const uint8_t entry[sudoku::kCells], const sudoku::Mask note[sudoku::kCells],
          const int menuLevel, const uint32_t elapsedMs, const int hintsUsed, const int armed, const int hintCell,
          const int solvedFlag, const int solved[4], const uint32_t best[4]) {
  std::printf("1 %d %u %d %d %d %d %d %d", menuLevel, elapsedMs, hintsUsed, armed, hintCell, 0, solvedFlag, 1);
  for (int i = 0; i < 4; ++i) std::printf(" %d", solved[i]);
  for (int i = 0; i < 4; ++i) std::printf(" %u", best[i]);
  std::printf(" %d\n", hintsUsed);
  for (int cell = 0; cell < sudoku::kCells; ++cell) std::printf("%d", puzzle.given[cell]);
  std::printf("\n");
  for (int cell = 0; cell < sudoku::kCells; ++cell) std::printf("%d", entry[cell]);
  std::printf("\n");
  for (int cell = 0; cell < sudoku::kCells; ++cell) std::printf("%03X", note[cell]);
  std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string what = argc > 1 ? argv[1] : "midway";
  const sudoku::Level level = what == "expert" ? sudoku::Level::Expert : sudoku::Level::Medium;

  uint32_t rng = 0x5D0C0FFEu;
  sudoku::Puzzle puzzle;
  if (!sudoku::generate(puzzle, level, work, rng, 400)) {
    std::fprintf(stderr, "could not carve a %s puzzle\n", sudoku::levelName(level));
    return 1;
  }

  uint8_t entry[sudoku::kCells] = {};
  sudoku::Mask note[sudoku::kCells] = {};

  // Solved by hint order rather than by cell order, so the filled cells are
  // scattered the way a real solve leaves them instead of banding down the top.
  const int wanted = what == "nearly" ? 46 : (what == "solved" ? 81 : 18);
  uint8_t board[sudoku::kCells];
  std::memcpy(board, puzzle.given, sizeof(board));
  int placed = 0;
  while (placed < wanted) {
    const sudoku::Hint hint = sudoku::nextHint(board, sudoku::ceilingFor(sudoku::Level::Expert));
    if (!hint.found) break;
    board[hint.cell] = static_cast<uint8_t>(hint.digit);
    entry[hint.cell] = static_cast<uint8_t>(hint.digit);
    ++placed;
  }

  // A few pencilled cells, from the real candidates so the marks are ones a
  // player could plausibly have written.
  sudoku::Grid grid;
  sudoku::load(grid, board);
  sudoku::solve(grid, sudoku::Technique::HiddenSingle);
  int pencilled = 0;
  for (int cell = 0; cell < sudoku::kCells && pencilled < 7; ++cell) {
    if (board[cell] != 0) continue;
    sudoku::Grid fresh;
    sudoku::load(fresh, board);
    if (sudoku::popcount(fresh.candidate[cell]) < 2) continue;
    note[cell] = fresh.candidate[cell];
    ++pencilled;
  }

  const int solved[4] = {6, 3, 1, 0};
  const uint32_t best[4] = {241000, 512000, 1804000, 0};
  const int solvedFlag = what == "solved" ? 1 : 0;
  emit(puzzle, entry, note, static_cast<int>(level), 754000, 0, 7, sudoku::kCells, solvedFlag, solved, best);
  std::fprintf(stderr, "%s %s puzzle, %d clues, %d filled in, hardest %s\n", what.c_str(), sudoku::levelName(level),
               puzzle.clues, placed, sudoku::techniqueName(puzzle.hardest));
  return 0;
}
