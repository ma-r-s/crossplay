#pragma once

// Picross (nonogram): the board, and nothing that needs hardware.
//
// No Arduino, no renderer, no heap -- so host-tests/picross/ runs the whole
// thing on a laptop. See docs/building-apps.md; every app in this fork is split
// this way and it is the only reason the rules get real coverage.
//
// The rules the player is solving for: the numbers beside a row or column are
// the lengths of the runs of filled cells in it, in order. Deduce which cells
// are filled. Every puzzle in the bank was proved UNIQUE and LINE-SOLVABLE by
// tools_local/picross/gen_picross.py, so a finished board is checked by asking
// whether every solid cell has been filled -- there is exactly one way to do
// that. See docs/apps/picross.md.

#include <cstdint>

#include "PicrossPuzzles.h"

namespace picross {

// What the player has done to a cell.
//
// Filled and Mistake are the two committing outcomes of a FILL, and which one
// you get is the whole game: FILL a solid cell and it is Filled; FILL an empty
// cell and it is a Mistake. Crossed is a free annotation ("I think this is
// empty") that is never penalised and always reversible. Blank is untouched.
enum class Cell : uint8_t { Blank = 0, Filled = 1, Crossed = 2, Mistake = 3 };

// How a wrong FILL is treated. Punish is the shipped rule (the classic Nintendo
// Picross behaviour): a wrong fill LOCKS IN as a mistake and is counted. This
// enum is the whole of the alternative Mario can pick later: FreeErase turns a
// wrong fill into a harmless cross with no lock and no count, which is the only
// thing that changes. Everything downstream -- rendering, saving, win -- is the
// same either way. See DungeonCore for the sibling pattern.
enum class Rules : uint8_t { Punish = 0, FreeErase = 1 };

// Run lengths of the set bits in `bits`, low bit (leftmost / topmost) first,
// written into out[] which must hold at least kMaxSize entries. Returns the
// number of runs; a line with no set bits has zero runs and shows as "0".
//
// A free function so the generator's cross-check and the host test can derive
// clues straight from a raw bitmap, exactly as the device does. The clue is
// never stored, only the picture, so the two cannot disagree.
int lineRuns(uint16_t bits, int size, uint8_t* out);

class Board {
 public:
  // Loads puzzle `index` with an empty grid and no mistakes. Out of range loads
  // the first.
  void load(int index);

  int index() const { return index_; }
  const Puzzle& puzzle() const { return kPuzzles[index_]; }
  int size() const { return kPuzzles[index_].size; }

  void setRules(Rules rules) { rules_ = rules; }
  Rules rules() const { return rules_; }

  Cell cell(int row, int col) const;
  // The solution: is (row, col) filled in the finished picture?
  bool solid(int row, int col) const;

  // The clues for a row or column, derived from the solution. Writes into out[]
  // (>= kMaxSize) and returns the run count.
  int rowClues(int row, uint8_t* out) const;
  int colClues(int col, uint8_t* out) const;

  // A line is satisfied when every solid cell in it has been filled. Because a
  // wrong fill becomes a Mistake rather than a Filled cell, a Filled cell is
  // always a correct one, so this is honest: it is true only when the line is
  // actually right, never merely when its count happens to match. It is the
  // signal the clue numbers dim on.
  bool rowSatisfied(int row) const;
  bool colSatisfied(int col) const;

  // The two player actions, one per input mode. Each returns true if the board
  // changed, so the caller repaints only when something moved.
  //
  // fill(): Blank/Crossed -> Filled if the cell is solid; otherwise a wrong
  // fill, which under Punish LOCKS as a Mistake and bumps the count, and under
  // FreeErase becomes a plain Crossed. A Filled cell toggles back to Blank (a
  // correct fill is never locked, so a mis-tap on geometry is recoverable). A
  // Mistake is locked and refuses.
  //
  // A fill that COMPLETES a line also auto-marks it: see autoMark().
  bool fill(int row, int col);
  // mark(): toggles Blank <-> Crossed. Never a mistake, never counted -- it
  // asserts nothing about the cell being filled. Refuses a Filled or Mistake
  // cell, which are decisions rather than notes.
  bool mark(int row, int col);

  int mistakes() const { return mistakes_; }
  // Every solid cell is filled. Equivalent to "correct", because a Filled cell
  // is always a correct one and the puzzle's solution is unique.
  bool solved() const;
  // Any cell touched or any mistake made: a board worth resuming rather than a
  // fresh one, which is what the menu offers RESUME on.
  bool touched() const;

  // Clears every mark and the mistake count. The puzzle stays loaded.
  void reset();

  // The save is the whole cell grid plus the mistake count and the index.
  // cells() points at kMaxSize*kMaxSize bytes in row-major order.
  const uint8_t* cells() const { return &cell_[0][0]; }
  void restore(int index, const uint8_t* cells, int mistakes);

 private:
  // Cross every still-blank cell of every satisfied line. A satisfied line has
  // all of its solid cells filled, so every remaining blank cell in it is
  // PROVABLY empty and the cross is always right -- this is a chore the player
  // would otherwise do by hand, never a guess made on their behalf.
  //
  // It is safe against this game's central invariant because it only ever
  // writes Crossed. A MARK is a free, reversible annotation that asserts
  // nothing; a FILL is the committing move that can lock as a Mistake. So
  // auto-marking cannot manufacture a mistake, cannot alter the mistake count,
  // and cannot make solved() true -- "a filled cell is always correct" is
  // untouched.
  //
  // AN AUTO-PLACED MARK IS AN ORDINARY MARK. It is stored as Cell::Crossed with
  // no flag saying who put it there, so mark() erases it exactly like one the
  // player made, and the save carries it like one. That is deliberate: a mark
  // the game placed and the player cannot remove would be the only
  // uneraseable annotation in the game, and telling the two apart would need a
  // fifth cell state, a save format that carries it, and a rule for what
  // happens when the line stops being satisfied. Erasing one is never
  // punished, so nothing is lost by letting them.
  //
  // Only called from a fill that LANDED (a fill is the only move that can make
  // a line satisfied), never from load() or restore(): a board that arrives
  // pre-crossed would read as touched(), and a fresh puzzle would offer to
  // RESUME itself.
  void autoMark();

  bool inside(int row, int col) const;
  int filledInRow(int row) const;
  int filledInCol(int col) const;
  int solidInRow(int row) const;
  int solidInCol(int col) const;

  int index_ = 0;
  Rules rules_ = Rules::Punish;
  int mistakes_ = 0;
  uint8_t cell_[kMaxSize][kMaxSize] = {};  // Cell values, row-major
};

// True for a puzzle index the player may open.
constexpr bool isPlayable(const int index) { return index >= 0 && index < kPuzzleCount; }

// Which puzzles are solved, one bit each, packed into a bitset wide enough for
// the whole bank. One uint32_t held the original 17; the bank is far larger
// now, so it is an array of words sized from kPuzzleCount -- grow the bank and
// the bitset grows with it, no reader or writer changes. kMaxSize has nothing
// to do with it.
constexpr int kProgressWords = (kPuzzleCount + 31) / 32;

struct Progress {
  uint32_t solved[kProgressWords] = {};

  bool isSolved(int index) const;
  void markSolved(int index);
  int solvedCount() const;
  // The first unsolved puzzle, or the last when every one is done. This is what
  // PLAY opens, so the player never has to pick before playing.
  int nextUnsolved() const;
};

}  // namespace picross
