#include "PicrossCore.h"

namespace picross {

namespace {

int popcount(uint32_t value) {
  int count = 0;
  while (value != 0) {
    value &= value - 1;
    ++count;
  }
  return count;
}

}  // namespace

int lineRuns(const uint16_t bits, const int size, uint8_t* out) {
  int count = 0;
  int run = 0;
  for (int i = 0; i < size; ++i) {
    if ((bits & (uint16_t{1} << i)) != 0) {
      ++run;
    } else if (run != 0) {
      out[count++] = static_cast<uint8_t>(run);
      run = 0;
    }
  }
  if (run != 0) out[count++] = static_cast<uint8_t>(run);
  return count;
}

bool Board::inside(const int row, const int col) const {
  const int n = size();
  return row >= 0 && row < n && col >= 0 && col < n;
}

void Board::load(const int index) {
  index_ = isPlayable(index) ? index : 0;
  mistakes_ = 0;
  for (int r = 0; r < kMaxSize; ++r)
    for (int c = 0; c < kMaxSize; ++c) cell_[r][c] = static_cast<uint8_t>(Cell::Blank);
}

void Board::restore(const int index, const uint8_t* cells, const int mistakes) {
  load(index);
  const int n = size();
  int locked = 0;
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      Cell value = static_cast<Cell>(cells[r * kMaxSize + c]);
      // The save comes off the card, so it is treated as input rather than as
      // truth. A Filled cell must sit on a solid cell and a Mistake on an empty
      // one; anything else is a corrupt byte, and dropping it to Blank costs a
      // few marks rather than producing a board that cannot be reconciled with
      // its own mistake count.
      if (value == Cell::Filled && !solid(r, c)) value = Cell::Blank;
      if (value == Cell::Mistake && solid(r, c)) value = Cell::Blank;
      if (value > Cell::Mistake) value = Cell::Blank;
      if (value == Cell::Mistake) ++locked;
      cell_[r][c] = static_cast<uint8_t>(value);
    }
  }
  // The count cannot be fewer than the locked cells on the board, and a wildly
  // large stored count is not trusted either: the mistakes on the board are the
  // ground truth, and a saved number below them would let the counter disagree
  // with what the player can see.
  mistakes_ = mistakes < locked ? locked : mistakes;
}

Cell Board::cell(const int row, const int col) const {
  if (!inside(row, col)) return Cell::Blank;
  return static_cast<Cell>(cell_[row][col]);
}

bool Board::solid(const int row, const int col) const {
  if (!inside(row, col)) return false;
  return (kPuzzles[index_].rows[row] & (uint16_t{1} << col)) != 0;
}

int Board::rowClues(const int row, uint8_t* out) const {
  if (row < 0 || row >= size()) return 0;
  return lineRuns(kPuzzles[index_].rows[row], size(), out);
}

int Board::colClues(const int col, uint8_t* out) const {
  if (col < 0 || col >= size()) return 0;
  uint16_t bits = 0;
  const int n = size();
  for (int r = 0; r < n; ++r)
    if (solid(r, col)) bits |= uint16_t{1} << r;
  return lineRuns(bits, n, out);
}

int Board::filledInRow(const int row) const {
  int count = 0;
  for (int c = 0; c < size(); ++c)
    if (static_cast<Cell>(cell_[row][c]) == Cell::Filled) ++count;
  return count;
}

int Board::filledInCol(const int col) const {
  int count = 0;
  for (int r = 0; r < size(); ++r)
    if (static_cast<Cell>(cell_[r][col]) == Cell::Filled) ++count;
  return count;
}

int Board::solidInRow(const int row) const {
  int count = 0;
  for (int c = 0; c < size(); ++c)
    if (solid(row, c)) ++count;
  return count;
}

int Board::solidInCol(const int col) const {
  int count = 0;
  for (int r = 0; r < size(); ++r)
    if (solid(r, col)) ++count;
  return count;
}

bool Board::rowSatisfied(const int row) const {
  if (row < 0 || row >= size()) return false;
  return filledInRow(row) == solidInRow(row);
}

bool Board::colSatisfied(const int col) const {
  if (col < 0 || col >= size()) return false;
  return filledInCol(col) == solidInCol(col);
}

bool Board::fill(const int row, const int col) {
  if (!inside(row, col)) return false;
  const Cell cur = static_cast<Cell>(cell_[row][col]);
  if (cur == Cell::Mistake) return false;  // locked
  if (cur == Cell::Filled) {
    cell_[row][col] = static_cast<uint8_t>(Cell::Blank);  // correct fills are removable
    return true;
  }
  // Blank or Crossed: this is a commit.
  if (solid(row, col)) {
    cell_[row][col] = static_cast<uint8_t>(Cell::Filled);
    return true;
  }
  if (rules_ == Rules::Punish) {
    cell_[row][col] = static_cast<uint8_t>(Cell::Mistake);  // wrong, and it stays wrong
    ++mistakes_;
    return true;
  }
  // FreeErase: a wrong fill is just a note that the cell is empty. The single
  // branch that is the whole difference between the two rule sets.
  cell_[row][col] = static_cast<uint8_t>(Cell::Crossed);
  return true;
}

bool Board::mark(const int row, const int col) {
  if (!inside(row, col)) return false;
  const Cell cur = static_cast<Cell>(cell_[row][col]);
  if (cur == Cell::Filled || cur == Cell::Mistake) return false;
  cell_[row][col] = static_cast<uint8_t>(cur == Cell::Crossed ? Cell::Blank : Cell::Crossed);
  return true;
}

bool Board::solved() const {
  const int n = size();
  for (int r = 0; r < n; ++r)
    for (int c = 0; c < n; ++c)
      if (solid(r, c) && static_cast<Cell>(cell_[r][c]) != Cell::Filled) return false;
  return true;
}

bool Board::touched() const {
  const int n = size();
  for (int r = 0; r < n; ++r)
    for (int c = 0; c < n; ++c)
      if (static_cast<Cell>(cell_[r][c]) != Cell::Blank) return true;
  return false;
}

void Board::reset() {
  mistakes_ = 0;
  for (int r = 0; r < kMaxSize; ++r)
    for (int c = 0; c < kMaxSize; ++c) cell_[r][c] = static_cast<uint8_t>(Cell::Blank);
}

bool Progress::isSolved(const int index) const {
  if (!isPlayable(index)) return false;
  return (solved[index / 32] & (uint32_t{1} << (index % 32))) != 0;
}

void Progress::markSolved(const int index) {
  if (!isPlayable(index)) return;
  solved[index / 32] |= uint32_t{1} << (index % 32);
}

int Progress::solvedCount() const {
  int total = 0;
  for (int w = 0; w < kProgressWords; ++w) total += popcount(solved[w]);
  return total;
}

int Progress::nextUnsolved() const {
  for (int i = 0; i < kPuzzleCount; ++i)
    if (!isSolved(i)) return i;
  return kPuzzleCount - 1;
}

}  // namespace picross
