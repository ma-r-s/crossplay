#pragma once

// Train Tracks, the model. Freestanding C++17: no renderer, no Activity, no
// storage, no heap. Host-tested in host-tests/traintracks.
//
// The grid is N x N (N <= kMaxSize). The track runs from the start gate to the
// end gate, both on the outer edge, through every track square exactly once.
// Each row and column clue counts the track squares in that line.
//
// The player marks squares, not pieces: a square is empty, track, or crossed
// (a note meaning "no track here"). Shapes are inferred for drawing. Given
// squares are locked track whose shape is shown, so a given also fixes its two
// connections; the solver treats them that way too.
//
// Cells are indexed row * kMaxSize + col whatever the puzzle's size, so every
// array is sized once for the largest grid.

#include <cstdint>
#include <initializer_list>

namespace traintracks {

constexpr int kMaxSize = 12;
constexpr int kMinSize = 3;
constexpr int kMaxCells = kMaxSize * kMaxSize;

// Directions as indices 0..3 and as bits in a connection mask.
constexpr int kNorth = 0;
constexpr int kEast = 1;
constexpr int kSouth = 2;
constexpr int kWest = 3;
constexpr int kDRow[4] = {-1, 0, 1, 0};
constexpr int kDCol[4] = {0, 1, 0, -1};

constexpr uint8_t dirBit(const int dir) { return static_cast<uint8_t>(1u << dir); }
constexpr int opposite(const int dir) { return (dir + 2) & 3; }
constexpr int cellIndex(const int row, const int col) { return row * kMaxSize + col; }
constexpr int rowOf(const int cell) { return cell / kMaxSize; }
constexpr int colOf(const int cell) { return cell % kMaxSize; }

inline int popcount4(const uint8_t mask) {
  return (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1);
}

// A gate sits outside one edge square; `side` is the direction from that square
// out through the grid's edge.
struct Gate {
  uint8_t row;
  uint8_t col;
  uint8_t side;
};

struct Puzzle {
  uint8_t size;
  uint8_t rowClue[kMaxSize];
  uint8_t colClue[kMaxSize];
  Gate start;
  Gate end;
  // Connection mask of each given square (including its gate side, if any);
  // 0 means not given.
  uint8_t given[kMaxCells];
  // Connection mask of each square in the generated solution; 0 = not track.
  // Used for CHECK and HINT only. Winning is decided by the rules.
  uint8_t solution[kMaxCells];
};

inline bool inside(const Puzzle& p, const int row, const int col) {
  return row >= 0 && col >= 0 && row < p.size && col < p.size;
}

// The gate side of a square, or -1 when the square has no gate. Start and end
// may share a corner square only in a degenerate puzzle, which validPuzzle
// rejects.
inline int gateSide(const Puzzle& p, const int row, const int col) {
  if (p.start.row == row && p.start.col == col) return p.start.side;
  if (p.end.row == row && p.end.col == col) return p.end.side;
  return -1;
}

inline bool gateOnEdge(const Puzzle& p, const Gate& g) {
  if (!inside(p, g.row, g.col) || g.side > 3) return false;
  return !inside(p, g.row + kDRow[g.side], g.col + kDCol[g.side]);
}

inline int totalTrack(const Puzzle& p) {
  int total = 0;
  for (int i = 0; i < p.size; ++i) total += p.rowClue[i];
  return total;
}

// Structural sanity only: sizes, gates and clue sums. Says nothing about
// whether the puzzle has a solution; that is the solver's job.
inline bool validPuzzle(const Puzzle& p) {
  if (p.size < kMinSize || p.size > kMaxSize) return false;
  if (!gateOnEdge(p, p.start) || !gateOnEdge(p, p.end)) return false;
  if (p.start.row == p.end.row && p.start.col == p.end.col) return false;
  int rows = 0;
  int cols = 0;
  for (int i = 0; i < p.size; ++i) {
    if (p.rowClue[i] > p.size || p.colClue[i] > p.size) return false;
    rows += p.rowClue[i];
    cols += p.colClue[i];
  }
  if (rows != cols || rows < 2) return false;
  for (int r = 0; r < kMaxSize; ++r) {
    for (int c = 0; c < kMaxSize; ++c) {
      const uint8_t g = p.given[cellIndex(r, c)];
      if (g == 0) continue;
      if (!inside(p, r, c) || popcount4(g) != 2 || (g & ~0x0F) != 0) return false;
      const int side = gateSide(p, r, c);
      for (int d = 0; d < 4; ++d) {
        if (!(g & dirBit(d))) continue;
        if (!inside(p, r + kDRow[d], c + kDCol[d]) && d != side) return false;
      }
      if (side >= 0 && !(g & dirBit(side))) return false;
    }
  }
  return true;
}

// Fills clues, gates and the solution masks from an ordered path of squares.
// `path` must be a simple 4-connected walk; startSide / endSide must point out
// of the grid from the first and last squares. Givens are cleared.
inline bool buildFromPath(Puzzle& p, const int size, const uint8_t* path, const int length, const int startSide,
                          const int endSide) {
  if (size < kMinSize || size > kMaxSize || length < 2) return false;
  p = Puzzle{};
  p.size = static_cast<uint8_t>(size);
  p.start =
      Gate{static_cast<uint8_t>(rowOf(path[0])), static_cast<uint8_t>(colOf(path[0])), static_cast<uint8_t>(startSide)};
  p.end = Gate{static_cast<uint8_t>(rowOf(path[length - 1])), static_cast<uint8_t>(colOf(path[length - 1])),
               static_cast<uint8_t>(endSide)};
  for (int i = 0; i < length; ++i) {
    const int r = rowOf(path[i]);
    const int c = colOf(path[i]);
    if (r >= size || c >= size || p.solution[path[i]] != 0) return false;
    uint8_t mask = 0;
    if (i == 0) mask |= dirBit(startSide);
    if (i == length - 1) mask |= dirBit(endSide);
    for (const int j : {i - 1, i + 1}) {
      if (j < 0 || j >= length) continue;
      const int dr = rowOf(path[j]) - r;
      const int dc = colOf(path[j]) - c;
      int dir = -1;
      for (int d = 0; d < 4; ++d) {
        if (kDRow[d] == dr && kDCol[d] == dc) dir = d;
      }
      if (dir < 0) return false;
      mask |= dirBit(dir);
    }
    p.solution[path[i]] = mask;
    ++p.rowClue[r];
    ++p.colClue[c];
  }
  return validPuzzle(p);
}

// ---------------------------------------------------------------------------
// The player's board.

enum CellState : uint8_t {
  kEmpty = 0,
  kTrack = 1,
  kCross = 2,
};

// What a tap moves a square to.
constexpr uint8_t nextState(const uint8_t state) {
  return state == kEmpty ? kTrack : state == kTrack ? kCross : kEmpty;
}

constexpr int kHistoryDepth = 64;

struct Move {
  uint8_t cell;
  uint8_t from;
  uint8_t to;
};

// A ring of the last kHistoryDepth moves. Entries [0, cursor) are applied;
// [cursor, count) can be redone. Logical entry i lives at (first + i) % depth.
struct History {
  Move moves[kHistoryDepth];
  uint8_t first;
  uint8_t count;
  uint8_t cursor;
};

struct Board {
  uint8_t cell[kMaxCells];
  History history;
};

inline void resetBoard(Board& b, const Puzzle& p) {
  b = Board{};
  for (int i = 0; i < kMaxCells; ++i) b.cell[i] = p.given[i] ? kTrack : kEmpty;
}

inline void pushMove(History& h, const Move m) {
  h.count = h.cursor;  // a new move drops whatever could have been redone
  if (h.count == kHistoryDepth) {
    h.first = static_cast<uint8_t>((h.first + 1) % kHistoryDepth);
    --h.count;
  }
  h.moves[(h.first + h.count) % kHistoryDepth] = m;
  ++h.count;
  h.cursor = h.count;
}

inline bool canUndo(const Board& b) { return b.history.cursor > 0; }
inline bool canRedo(const Board& b) { return b.history.cursor < b.history.count; }

// Sets a square and records it. Givens and squares outside the grid are
// refused; a set to the current state is not a move.
inline bool setCell(Board& b, const Puzzle& p, const int row, const int col, const uint8_t state) {
  if (!inside(p, row, col) || state > kCross) return false;
  const int i = cellIndex(row, col);
  if (p.given[i] || b.cell[i] == state) return false;
  pushMove(b.history, Move{static_cast<uint8_t>(i), b.cell[i], state});
  b.cell[i] = state;
  return true;
}

inline bool tapCell(Board& b, const Puzzle& p, const int row, const int col) {
  if (!inside(p, row, col)) return false;
  return setCell(b, p, row, col, nextState(b.cell[cellIndex(row, col)]));
}

// Undo and redo return the square they changed, or -1 when there is nothing.
inline int undo(Board& b) {
  History& h = b.history;
  if (h.cursor == 0) return -1;
  --h.cursor;
  const Move& m = h.moves[(h.first + h.cursor) % kHistoryDepth];
  b.cell[m.cell] = m.from;
  return m.cell;
}

inline int redo(Board& b) {
  History& h = b.history;
  if (h.cursor >= h.count) return -1;
  const Move& m = h.moves[(h.first + h.cursor) % kHistoryDepth];
  b.cell[m.cell] = m.to;
  ++h.cursor;
  return m.cell;
}

// ---------------------------------------------------------------------------
// Clues as drawn.

enum class ClueState : uint8_t {
  Under,  // fewer track squares than the clue
  Met,    // exactly the clue: drawn boxed
  Over,   // more than the clue: drawn inverted
};

inline int rowCount(const Board& b, const Puzzle& p, const int row) {
  int n = 0;
  for (int c = 0; c < p.size; ++c) n += b.cell[cellIndex(row, c)] == kTrack;
  return n;
}

inline int colCount(const Board& b, const Puzzle& p, const int col) {
  int n = 0;
  for (int r = 0; r < p.size; ++r) n += b.cell[cellIndex(r, col)] == kTrack;
  return n;
}

constexpr ClueState clueState(const int count, const int clue) {
  return count < clue ? ClueState::Under : count == clue ? ClueState::Met : ClueState::Over;
}

inline bool allCluesMet(const Board& b, const Puzzle& p) {
  for (int i = 0; i < p.size; ++i) {
    if (rowCount(b, p, i) != p.rowClue[i] || colCount(b, p, i) != p.colClue[i]) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Shapes for drawing.
//
// Each track square starts with a candidate connection toward every track
// neighbour (and its gate); givens keep their own shape. A connection is
// forced from a square's side when that square is a given, has at most two
// candidates, or it is the gate. Two rules run until nothing changes:
//   - symmetry: a candidate the neighbour does not return is dropped;
//   - saturation: a square with exactly two forced connections drops the rest.
// A square left with more than two candidates is ambiguous and drawn as a
// plain block until the player's marks resolve it; one with fewer than two
// draws the stubs it has.

inline bool isTrack(const Board& b, const Puzzle& p, const int row, const int col) {
  return inside(p, row, col) && b.cell[cellIndex(row, col)] == kTrack;
}

inline void inferShapes(const Board& b, const Puzzle& p, uint8_t* mask /* kMaxCells */) {
  for (int i = 0; i < kMaxCells; ++i) mask[i] = 0;
  for (int r = 0; r < p.size; ++r) {
    for (int c = 0; c < p.size; ++c) {
      const int i = cellIndex(r, c);
      if (b.cell[i] != kTrack) continue;
      if (p.given[i]) {
        mask[i] = p.given[i];
        continue;
      }
      const int side = gateSide(p, r, c);
      if (side >= 0) mask[i] |= dirBit(side);
      for (int d = 0; d < 4; ++d) {
        if (isTrack(b, p, r + kDRow[d], c + kDCol[d])) mask[i] |= dirBit(d);
      }
    }
  }
  for (bool changed = true; changed;) {
    changed = false;
    for (int r = 0; r < p.size; ++r) {
      for (int c = 0; c < p.size; ++c) {
        const int i = cellIndex(r, c);
        if (b.cell[i] != kTrack || p.given[i]) continue;
        uint8_t forced = 0;
        const int side = gateSide(p, r, c);
        if (side >= 0) forced |= dirBit(side);
        for (int d = 0; d < 4; ++d) {
          if (!(mask[i] & dirBit(d)) || !inside(p, r + kDRow[d], c + kDCol[d])) continue;
          const int j = cellIndex(r + kDRow[d], c + kDCol[d]);
          if (!(mask[j] & dirBit(opposite(d)))) {
            mask[i] = static_cast<uint8_t>(mask[i] & ~dirBit(d));
            changed = true;
          } else if (p.given[j] || popcount4(mask[j]) <= 2) {
            forced |= dirBit(d);
          }
        }
        if (popcount4(mask[i]) > 2 && popcount4(forced) == 2) {
          mask[i] = forced;
          changed = true;
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// A small deterministic RNG. The seed fully defines a puzzle, so this must
// never change behaviour without bumping the save format version.

struct Rng {
  uint32_t state;

  uint32_t next() {
    // splitmix32
    uint32_t z = (state += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
  }

  // Uniform in [0, n). n must be > 0.
  int below(const int n) { return static_cast<int>((static_cast<uint64_t>(next()) * static_cast<uint32_t>(n)) >> 32); }
};

}  // namespace traintracks
