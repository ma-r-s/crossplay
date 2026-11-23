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

constexpr int kHistoryDepth = 64;

// One change to one square: its state and its chosen piece, before and after.
struct Move {
  uint8_t cell;
  uint8_t from;
  uint8_t to;
  uint8_t fromPin;
  uint8_t toPin;
};

// A ring of the last kHistoryDepth moves. Entries [0, cursor) are applied;
// [cursor, count) can be redone. Logical entry i lives at (first + i) % depth.
struct History {
  Move moves[kHistoryDepth];
  uint8_t first;
  uint8_t count;
  uint8_t cursor;
};

// A pin for track whose piece is left undecided: drawn as a question mark,
// inferred like an automatic piece so neighbours still join it, and judged by
// CHECK as a wrong piece. All four connections, which no piece has.
constexpr uint8_t kPinUnknown = 0x0F;

struct Board {
  // What each square is: empty, track or crossed. Everything that judges the
  // board (clues, the win, CHECK, HINT, the solver) reads only this.
  uint8_t cell[kMaxCells];
  // The piece the player chose for a track square (a connection mask), 0
  // while it is still automatic and follows its neighbours, or kPinUnknown
  // for track whose piece the player has not decided. Drawing only.
  uint8_t pin[kMaxCells];
  // The shapes last drawn (settle). Inference keeps the joins in it, so track
  // laid later joins a line's open ends instead of rerouting the line.
  uint8_t shape[kMaxCells];
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

// Sets a square, and for track the piece chosen for it (0: automatic), and
// records it. Givens and squares outside the grid are refused; setting what
// is already there is not a move. Anything but track carries no piece.
inline bool setCell(Board& b, const Puzzle& p, const int row, const int col, const uint8_t state, uint8_t pin = 0) {
  if (!inside(p, row, col) || state > kCross) return false;
  if (state != kTrack) pin = 0;
  const int i = cellIndex(row, col);
  if (p.given[i] || (b.cell[i] == state && b.pin[i] == pin)) return false;
  pushMove(b.history, Move{static_cast<uint8_t>(i), b.cell[i], state, b.pin[i], pin});
  b.cell[i] = state;
  b.pin[i] = pin;
  return true;
}

// Undo and redo return the square they changed, or -1 when there is nothing.
inline int undo(Board& b) {
  History& h = b.history;
  if (h.cursor == 0) return -1;
  --h.cursor;
  const Move& m = h.moves[(h.first + h.cursor) % kHistoryDepth];
  b.cell[m.cell] = m.from;
  b.pin[m.cell] = m.fromPin;
  return m.cell;
}

inline int redo(Board& b) {
  History& h = b.history;
  if (h.cursor >= h.count) return -1;
  const Move& m = h.moves[(h.first + h.cursor) % kHistoryDepth];
  b.cell[m.cell] = m.to;
  b.pin[m.cell] = m.toPin;
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
// Shapes for drawing, and what a tap and a hold do.
//
// Every track square draws one whole piece, two connections. A given's piece
// is its own, and so is a piece the player chose (Board::pin), whatever it
// points at: CHECK is what says a piece is wrong. The rest are automatic and
// follow these rules, earlier rules first, never closing a loop (a route is a
// path, so a loop is never legal):
//
//   1. a gate square connects through its gate;
//   2. a join drawn last time (Board::shape) between two squares that are
//      both still automatic track stays;
//   3. an end drawn last time toward a square that has since become track
//      joins it, so new track extends a line rather than breaking it;
//   4. a fixed piece pointing at an automatic neighbour with room pulls it in;
//   5. automatic neighbours join where a square has no other way to reach
//      two connections, then by the most constrained square first;
//   6. a square still short of two points its open ends at empty squares (or
//      its gate), first into a row or column that still has room for track,
//      then running straight on, so it reads as track waiting for more.
//      Boxed in, it takes the piece that leaves the board least, and never
//      one that closes a loop with a piece already pointing at it.
//
// An automatic join never leaves the board except through a gate, and never
// runs into a cross or into a fixed piece that does not point back.

// The six pieces, in the order an isolated square tries them and a tap steps
// through them: across, up and down, then the corners clockwise from
// north-east.
constexpr uint8_t kPieces[6] = {
    (1u << kEast) | (1u << kWest),  (1u << kNorth) | (1u << kSouth), (1u << kNorth) | (1u << kEast),
    (1u << kEast) | (1u << kSouth), (1u << kSouth) | (1u << kWest),  (1u << kWest) | (1u << kNorth),
};

inline bool isTrack(const Board& b, const Puzzle& p, const int row, const int col) {
  return inside(p, row, col) && b.cell[cellIndex(row, col)] == kTrack;
}

// The piece a track square is fixed to, or 0 if it is automatic (an
// undecided piece counts as automatic).
inline uint8_t fixedPiece(const Board& b, const Puzzle& p, const int i) {
  if (b.cell[i] != kTrack) return 0;
  if (p.given[i]) return p.given[i];
  return b.pin[i] == kPinUnknown ? 0 : b.pin[i];
}

namespace detail {

struct Links {
  uint8_t parent[kMaxCells];
  int find(int i) {
    while (parent[i] != i) i = parent[i] = parent[parent[i]];
    return i;
  }
  // Joins two squares; false if they were already on one chain (a loop).
  bool join(const int a, const int b) {
    const int ra = find(a);
    const int rb = find(b);
    if (ra == rb) return false;
    parent[ra] = static_cast<uint8_t>(rb);
    return true;
  }
};

// Whether square i may connect in direction d at all, whatever is there now.
inline bool mayConnect(const Board& b, const Puzzle& p, const int i, const int d) {
  const int r = rowOf(i);
  const int c = colOf(i);
  const int r2 = r + kDRow[d];
  const int c2 = c + kDCol[d];
  if (!inside(p, r2, c2)) return gateSide(p, r, c) == d;
  const int j = cellIndex(r2, c2);
  if (b.cell[j] == kCross) return false;
  const uint8_t other = fixedPiece(b, p, j);
  return other == 0 || (other & dirBit(opposite(d)));
}

}  // namespace detail

inline void inferShapes(const Board& b, const Puzzle& p, uint8_t* mask /* kMaxCells */) {
  const int n = p.size;
  bool automatic[kMaxCells] = {};
  detail::Links links;
  for (int i = 0; i < kMaxCells; ++i) {
    mask[i] = 0;
    links.parent[i] = static_cast<uint8_t>(i);
  }
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      if (b.cell[i] != kTrack) continue;
      mask[i] = fixedPiece(b, p, i);
      automatic[i] = mask[i] == 0;
      const int side = gateSide(p, r, c);
      if (automatic[i] && side >= 0) mask[i] = dirBit(side);  // 1
    }
  }
  // Fixed pieces that point at each other are already one chain.
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      if (b.cell[i] != kTrack || automatic[i]) continue;
      for (const int d : {kEast, kSouth}) {
        if (!(mask[i] & dirBit(d)) || !isTrack(b, p, r + kDRow[d], c + kDCol[d])) continue;
        const int j = cellIndex(r + kDRow[d], c + kDCol[d]);
        if (!automatic[j] && (mask[j] & dirBit(opposite(d)))) links.join(i, j);
      }
    }
  }
  auto link = [&](const int i, const int d) {
    const int j = cellIndex(rowOf(i) + kDRow[d], colOf(i) + kDCol[d]);
    if (!links.join(i, j)) return false;
    mask[i] = static_cast<uint8_t>(mask[i] | dirBit(d));
    mask[j] = static_cast<uint8_t>(mask[j] | dirBit(opposite(d)));
    return true;
  };
  // 2 and 3: what was drawn last time. Both sides' ends first, then one side's.
  // Before a chosen piece's pull, so a piece being turned beside a line never
  // drags the line toward it, and the line is still whole when the piece goes.
  // A join kept here may also be to a fixed piece that still points back: a
  // line running into a given is as much the player's line as any other.
  auto room = [&](const int i) { return automatic[i] && popcount4(mask[i]) < 2; };
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      for (int d = 0; d < 4; ++d) {
        if (!room(i) || !(b.shape[i] & dirBit(d)) || !isTrack(b, p, r + kDRow[d], c + kDCol[d])) continue;
        const int j = cellIndex(r + kDRow[d], c + kDCol[d]);
        const bool kept =
            automatic[j] ? room(j) && (b.shape[j] & dirBit(opposite(d))) : (mask[j] & dirBit(opposite(d))) != 0;
        if (kept) link(i, d);
      }
    }
  }
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      for (int d = 0; d < 4; ++d) {
        if (!room(i) || !(b.shape[i] & dirBit(d)) || !isTrack(b, p, r + kDRow[d], c + kDCol[d])) continue;
        const int j = cellIndex(r + kDRow[d], c + kDCol[d]);
        if (room(j)) link(i, d);
      }
    }
  }
  // 4: a fixed piece pulls in the automatic neighbour it points at, if that
  // neighbour still has room.
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      if (b.cell[i] != kTrack || automatic[i]) continue;
      for (int d = 0; d < 4; ++d) {
        if (!(mask[i] & dirBit(d)) || !isTrack(b, p, r + kDRow[d], c + kDCol[d])) continue;
        const int j = cellIndex(r + kDRow[d], c + kDCol[d]);
        if (!automatic[j] || popcount4(mask[j]) >= 2) continue;
        if (links.join(i, j)) mask[j] = static_cast<uint8_t>(mask[j] | dirBit(opposite(d)));
      }
    }
  }
  // 5: automatic neighbours. `open` lists the joins still possible from i.
  auto openJoins = [&](const int i, uint8_t& dirs) {
    dirs = 0;
    if (!automatic[i] || popcount4(mask[i]) >= 2) return 0;
    int count = 0;
    for (int d = 0; d < 4; ++d) {
      if (mask[i] & dirBit(d)) continue;
      const int r2 = rowOf(i) + kDRow[d];
      const int c2 = colOf(i) + kDCol[d];
      if (!isTrack(b, p, r2, c2)) continue;
      const int j = cellIndex(r2, c2);
      if (!automatic[j] || popcount4(mask[j]) >= 2) continue;
      if (links.find(i) == links.find(j)) continue;
      dirs = static_cast<uint8_t>(dirs | dirBit(d));
      ++count;
    }
    return count;
  };
  for (bool progress = true; progress;) {
    progress = false;
    // Forced: a square with no more open joins than it needs takes them all.
    for (int i = 0; i < kMaxCells; ++i) {
      uint8_t dirs = 0;
      const int open = openJoins(i, dirs);
      const int need = 2 - popcount4(mask[i]);
      if (open == 0 || open > need) continue;
      for (int d = 0; d < 4; ++d) {
        if ((dirs & dirBit(d)) && popcount4(mask[i]) < 2) progress |= link(i, d);
      }
    }
    if (progress) continue;
    // Otherwise one join, at the most constrained square, toward its most
    // constrained neighbour; then look for forced joins again.
    int best = -1;
    int bestOpen = 5;
    for (int i = 0; i < kMaxCells; ++i) {
      uint8_t dirs = 0;
      const int open = openJoins(i, dirs);
      if (open > 0 && open < bestOpen) {
        best = i;
        bestOpen = open;
      }
    }
    if (best < 0) break;
    uint8_t dirs = 0;
    openJoins(best, dirs);
    int pick = -1;
    int pickOpen = 5;
    for (int d = 0; d < 4; ++d) {
      if (!(dirs & dirBit(d))) continue;
      const int j = cellIndex(rowOf(best) + kDRow[d], colOf(best) + kDCol[d]);
      uint8_t unused = 0;
      const int open = openJoins(j, unused);
      if (open < pickOpen) {
        pick = d;
        pickOpen = open;
      }
    }
    if (pick >= 0) progress = link(best, pick);
  }
  // 6: open ends. An end toward an empty square whose row and column both
  // still have room for track (or toward the gate) scores 3, toward any other
  // empty square 2, toward anything else on the board 1, off the board 0; the
  // best whole piece that keeps the square's joins wins, and with one end set,
  // running straight on beats turning. An end that meets a piece already
  // pointing back is a join: worth most, unless it would close a loop, which
  // is worth least.
  auto endScore = [&](const int i, const int d) {
    const int r2 = rowOf(i) + kDRow[d];
    const int c2 = colOf(i) + kDCol[d];
    if (!inside(p, r2, c2)) return gateSide(p, rowOf(i), colOf(i)) == d ? 3 : 0;
    const int j = cellIndex(r2, c2);
    if (b.cell[j] == kTrack && (mask[j] & dirBit(opposite(d)))) return links.find(i) == links.find(j) ? 0 : 4;
    if (!detail::mayConnect(b, p, i, d)) return 1;
    if (b.cell[j] != kEmpty) return 1;
    const bool room = rowCount(b, p, r2) < p.rowClue[r2] && colCount(b, p, c2) < p.colClue[c2];
    return room ? 3 : 2;
  };
  for (int i = 0; i < kMaxCells; ++i) {
    if (!automatic[i] || popcount4(mask[i]) >= 2) continue;
    uint8_t chosen = 0;
    int best = -1;
    for (const uint8_t piece : kPieces) {
      if ((piece & mask[i]) != mask[i]) continue;
      int score = 0;
      for (int d = 0; d < 4; ++d) {
        if ((piece & dirBit(d)) && !(mask[i] & dirBit(d))) score += endScore(i, d);
      }
      const bool straight = piece == kPieces[0] || piece == kPieces[1];
      const int rank = 2 * score + (popcount4(mask[i]) == 1 && straight ? 1 : 0);
      if (rank > best) {
        best = rank;
        chosen = piece;
      }
    }
    for (int d = 0; d < 4; ++d) {
      if (!(chosen & dirBit(d)) || (mask[i] & dirBit(d)) || !isTrack(b, p, rowOf(i) + kDRow[d], colOf(i) + kDCol[d])) {
        continue;
      }
      const int j = cellIndex(rowOf(i) + kDRow[d], colOf(i) + kDCol[d]);
      if (mask[j] & dirBit(opposite(d))) links.join(i, j);
    }
    mask[i] = chosen;
  }
}

// Records what the board draws now, so the next inference keeps its joins.
// Called after every change the player sees.
inline void settle(Board& b, const Puzzle& p) {
  uint8_t next[kMaxCells];
  inferShapes(b, p, next);
  for (int i = 0; i < kMaxCells; ++i) b.shape[i] = next[i];
}

// A piece's place in kPieces, the order a tap steps through, or -1 for
// anything that is not a piece.
inline int pieceIndex(const uint8_t piece) {
  for (int k = 0; k < 6; ++k) {
    if (kPieces[k] == piece) return k;
  }
  return -1;
}

// A tap. An empty square becomes track with an automatic piece; the next tap
// leaves its piece undecided, and each tap after that steps through the six
// pieces in kPieces order, always the same whatever is around the square, so
// a piece is always the same number of taps from the question mark. After
// the last piece the square is undecided again. A tap never clears a square:
// that is the hold's job. A tap on a cross goes straight to undecided track,
// the quick way to say "track here" about a square once ruled out. Givens are
// refused.
inline bool tapCell(Board& b, const Puzzle& p, const int row, const int col) {
  if (!inside(p, row, col)) return false;
  const int i = cellIndex(row, col);
  if (p.given[i]) return false;
  if (b.cell[i] == kEmpty) return setCell(b, p, row, col, kTrack, 0);
  if (b.cell[i] == kCross) return setCell(b, p, row, col, kTrack, kPinUnknown);
  const uint8_t chosen = b.pin[i];
  if (chosen == 0) return setCell(b, p, row, col, kTrack, kPinUnknown);
  const int next = chosen == kPinUnknown ? 0 : pieceIndex(chosen) + 1;
  return setCell(b, p, row, col, kTrack, next >= 0 && next < 6 ? kPieces[next] : kPinUnknown);
}

// A hold takes a square back a step: track or a cross to empty, and an empty
// square to a cross. So a cross only ever goes on an empty square, and
// clearing track is one hold rather than a walk through every piece.
inline bool holdCell(Board& b, const Puzzle& p, const int row, const int col) {
  if (!inside(p, row, col)) return false;
  const int i = cellIndex(row, col);
  return setCell(b, p, row, col, b.cell[i] == kEmpty ? kCross : kEmpty);
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
