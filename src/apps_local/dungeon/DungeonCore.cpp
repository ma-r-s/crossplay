#include "DungeonCore.h"

namespace dungeon {

namespace {

int popcount(uint64_t value) {
  int count = 0;
  while (value != 0) {
    value &= value - 1;
    ++count;
  }
  return count;
}

// Bit r*8+c for a whole row, masked to the puzzle's width.
uint64_t rowMask(const int row, const int size) { return (uint64_t{(1u << size) - 1u}) << (row * 8); }

uint64_t colMask(const int col, const int size) {
  uint64_t mask = 0;
  for (int row = 0; row < size; ++row) mask |= uint64_t{1} << (row * 8 + col);
  return mask;
}

}  // namespace

bool Board::inside(const int row, const int col) const {
  const int n = size();
  return row >= 0 && row < n && col >= 0 && col < n;
}

void Board::load(const int index) {
  index_ = (index < 0 || index >= kPuzzleCount) ? 0 : index;
  walls_ = 0;
  floors_ = 0;
}

void Board::restore(const int index, const uint64_t walls, const uint64_t floors) {
  load(index);
  // A cell cannot be both, and neither can sit on a monster or a chest. The
  // save comes off the card, so it is treated as input rather than as truth:
  // a corrupted byte should cost the position, not put a wall on a monster and
  // make the puzzle unsolvable with no way to tell.
  const Puzzle& p = kPuzzles[index_];
  const uint64_t fixed = p.monsters | p.chests;
  walls_ = walls & ~fixed;
  floors_ = floors & ~fixed & ~walls_;
}

Mark Board::mark(const int row, const int col) const {
  if (!inside(row, col)) return Mark::Unknown;
  const uint64_t b = cellBit(row, col);
  if ((walls_ & b) != 0) return Mark::Wall;
  if ((floors_ & b) != 0) return Mark::Floor;
  return Mark::Unknown;
}

void Board::tap(const int row, const int col) {
  if (!inside(row, col)) return;
  if (isMonster(row, col) || isChest(row, col)) return;
  const uint64_t b = cellBit(row, col);
  if ((walls_ & b) != 0) {
    walls_ &= ~b;
    floors_ |= b;
  } else if ((floors_ & b) != 0) {
    floors_ &= ~b;
  } else {
    walls_ |= b;
  }
}

void Board::reset() {
  walls_ = 0;
  floors_ = 0;
}

bool Board::isMonster(const int row, const int col) const {
  return inside(row, col) && (kPuzzles[index_].monsters & cellBit(row, col)) != 0;
}

bool Board::isChest(const int row, const int col) const {
  return inside(row, col) && (kPuzzles[index_].chests & cellBit(row, col)) != 0;
}

int Board::rowWalls(const int row) const {
  if (row < 0 || row >= size()) return 0;
  return popcount(walls_ & rowMask(row, size()));
}

int Board::colWalls(const int col) const {
  if (col < 0 || col >= size()) return 0;
  return popcount(walls_ & colMask(col, size()));
}

int tierStart(const int tier) {
  for (int i = 0; i < kPuzzleCount; ++i) {
    if (kPuzzles[i].tier == tier) return i;
  }
  return 0;
}

int tierCount(const int tier) {
  int count = 0;
  for (int i = 0; i < kPuzzleCount; ++i) {
    if (kPuzzles[i].tier == tier) ++count;
  }
  return count;
}

bool Progress::isSolved(const int index) const {
  if (index < 0 || index >= kPuzzleCount) return false;
  return index < 64 ? (low & (uint64_t{1} << index)) != 0 : (high & (uint64_t{1} << (index - 64))) != 0;
}

void Progress::markSolved(const int index) {
  if (index < 0 || index >= kPuzzleCount) return;
  if (index < 64) {
    low |= uint64_t{1} << index;
  } else {
    high |= uint64_t{1} << (index - 64);
  }
}

int Progress::solvedCount() const { return popcount(low) + popcount(high); }

int Progress::nextUnsolved() const {
  for (int i = 0; i < kPuzzleCount; ++i) {
    if (!isSolved(i)) return i;
  }
  return kPuzzleCount - 1;
}

}  // namespace dungeon
