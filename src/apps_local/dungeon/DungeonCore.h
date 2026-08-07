#pragma once

// D&Diagrams: the board, and nothing that needs hardware.
//
// No Arduino, no renderer, no heap -- so host-tests/dungeon/ can run the whole
// thing on a laptop. See docs/building-apps.md; every app in this fork is split
// this way and it is the only reason the rules get real coverage.
//
// The rules the player is solving for, for reference, because nothing in this
// file states them:
//
//   1. The number beside a row or column is how many walls it holds.
//   2. Every dead end holds a monster, and every monster is in a dead end.
//   3. Every chest sits in a 3x3 room of floor with exactly one way in.
//   4. Corridors are one cell wide: no 2x2 of floor outside a treasure room.
//   5. All floor is connected.
//
// They are absent here on purpose. Each puzzle in the bank was proved to have
// exactly one solution by tools_local/gen_dungeons.py, which carries the rules,
// so a finished board is checked against that solution with one comparison.
// Implementing the rules a second time would mean two implementations that have
// to agree forever, and the device would get nothing for it -- the puzzle is
// solved when the walls are right, and only one arrangement of walls is.

#include <cstdint>

#include "DungeonPuzzles.h"

namespace dungeon {

// What the player has said about a cell. Floor is a note to themselves: it is
// never required, and a board with no Floor marks at all can be solved.
enum class Mark : uint8_t { Unknown = 0, Wall = 1, Floor = 2 };

class Board {
 public:
  // Loads puzzle `index` with an empty grid. Out of range loads the first.
  void load(int index);

  int index() const { return index_; }
  const Puzzle& puzzle() const { return kPuzzles[index_]; }
  int size() const { return kPuzzles[index_].size; }

  Mark mark(int row, int col) const;

  // One tap, one path: Unknown -> Wall -> Floor -> Unknown.
  //
  // No mode switch and no long press. A mode is a second thing to look at
  // before every tap, and a long press is invisible to anyone who has not been
  // told about it -- on a panel with no hover and no tooltip that makes it a
  // feature nobody finds. Three states in a ring costs at most two taps to
  // correct a mis-tap and needs no explaining.
  //
  // A cell holding a monster or a chest is floor by definition and refuses.
  void tap(int row, int col);

  // Clears every mark. The puzzle stays loaded.
  void reset();

  bool isMonster(int row, int col) const;
  bool isChest(int row, int col) const;

  int rowWalls(int row) const;
  int colWalls(int col) const;
  // Placed exactly as many walls as the clue asks for. Not "correct": a row can
  // be satisfied and wrong. It is the same thing the original dims the number
  // for, and it is the only progress signal the player gets.
  bool rowSatisfied(int row) const { return rowWalls(row) == kPuzzles[index_].rowClues[row]; }
  bool colSatisfied(int col) const { return colWalls(col) == kPuzzles[index_].colClues[col]; }

  bool solved() const { return walls_ == kPuzzles[index_].walls; }

  // Anything at all marked. Distinguishes a board worth resuming from a fresh
  // one, which is what the menu offers RESUME on.
  bool touched() const { return walls_ != 0 || floors_ != 0; }

  // The save is these two masks and the index. Sixteen bytes, and no replay:
  // rebuilding a position from a move list would be a second implementation of
  // the same state that has to agree with this one forever.
  uint64_t wallMask() const { return walls_; }
  uint64_t floorMask() const { return floors_; }
  void restore(int index, uint64_t walls, uint64_t floors);

 private:
  // NOT `bit`. Arduino defines bit(n) as a macro, so a two-argument bit() here
  // compiles on the host and fails only on the device, which is the worst
  // place to find out: `macro "bit" passed 2 arguments, but takes just 1`.
  static uint64_t cellBit(int row, int col) { return uint64_t{1} << (row * 8 + col); }
  bool inside(int row, int col) const;

  int index_ = 0;
  uint64_t walls_ = 0;
  uint64_t floors_ = 0;
};

// kPuzzles[0] is the tutorial and it is NOT a level. It exists to be explained:
// the adventurer's guide walks through it a step at a time, and nothing else in
// the app may open it, count it or offer it. The campaign is everything after
// it, and the number the player is shown is out of that.
constexpr int kCampaignFirst = 1;
constexpr int kCampaignCount = kPuzzleCount - kCampaignFirst;

// True for a puzzle the player is allowed to open.
constexpr bool isPlayable(const int index) { return index >= kCampaignFirst && index < kPuzzleCount; }

// Bits set for solved puzzles, packed into two words. The save carries this and
// the menu reads it; kPuzzleCount is 65, which is why it is not one.
struct Progress {
  uint64_t low = 0;
  uint64_t high = 0;

  bool isSolved(int index) const;
  // Refuses the tutorial. It is not a level, so it can never be finished, and a
  // guard here means no caller has to remember that.
  void markSolved(int index);
  // Campaign dungeons finished, out of kCampaignCount. The tutorial is not one
  // of them and is never counted.
  int solvedCount() const;
  // The first unsolved campaign dungeon, or the last when every one is done.
  // This is what PLAY opens, so the player never has to pick before playing --
  // and it never opens the tutorial.
  int nextUnsolved() const;
};

}  // namespace dungeon
