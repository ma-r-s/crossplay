#pragma once

// Train Tracks, one game in progress: the puzzle, the player's board, and what
// the help buttons do to them. Freestanding C++17, no heap.
//
// Navigation lives in TrainTracksFlow.h and the save format in
// TrainTracksSave.h; this is the part both of them carry.

#include <cstdint>

#include "TrainTracksCore.h"
#include "TrainTracksGenerator.h"
#include "TrainTracksSolver.h"

namespace traintracks {

struct Game {
  Level level = Level::Easy;
  // (seed, attempt) rebuilds the puzzle; nothing else about it is saved.
  uint32_t seed = 0;
  uint16_t attempt = 0;
  Puzzle puzzle{};
  Board board{};
  uint32_t elapsedMs = 0;
  // How much help the run took. Shown when it is solved, and kept for a future
  // record that can tell an unaided solve from an assisted one.
  uint16_t hints = 0;
  uint16_t checks = 0;
  bool solved = false;

  bool assisted() const { return hints != 0 || checks != 0; }
};

// FNV-1a over the whole puzzle, which is value-initialised before it is built,
// so padding and unused squares are zero. A save carries it so a puzzle that
// no longer regenerates the same is discarded rather than misread.
inline uint32_t puzzleHash(const Puzzle& p) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&p);
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < sizeof(Puzzle); ++i) {
    h ^= bytes[i];
    h *= 16777619u;
  }
  return h;
}

// A marked square that contradicts the answer: track where there is none, or a
// cross where there is track. An empty square is never wrong.
inline bool isWrong(const Puzzle& p, const Board& b, const int i) {
  if (b.cell[i] == kTrack) return p.solution[i] == 0;
  if (b.cell[i] == kCross) return p.solution[i] != 0;
  return false;
}

// A track square on the route drawn in a shape the answer does not have, given
// the shapes the board draws (`shapes`, from inferShapes). The route is unique,
// so each of its squares has one right piece, and CHECK judges what is on the
// screen: a piece the game guessed is judged exactly as one the player chose.
inline bool isWrongShape(const Puzzle& p, const Board& b, const uint8_t* shapes, const int i) {
  if (b.cell[i] != kTrack || !p.solution[i] || p.given[i]) return false;
  return shapes[i] != p.solution[i];
}

// CHECK. Fills `wrong` (one flag per square) and returns how many: squares
// marked wrong, and route squares drawn in a shape the answer does not have.
inline int checkBoard(const Puzzle& p, const Board& b, uint8_t* wrong) {
  uint8_t shapes[kMaxCells];
  inferShapes(b, p, shapes);
  int count = 0;
  for (int i = 0; i < kMaxCells; ++i) {
    const bool bad = inside(p, rowOf(i), colOf(i)) && (isWrong(p, b, i) || isWrongShape(p, b, shapes, i));
    wrong[i] = bad ? 1 : 0;
    count += bad;
  }
  return count;
}

enum class HintKind : uint8_t {
  None,
  Fixed,  // a square the player had wrong, or its shape, put right
  Track,  // track belongs here
};

struct Hint {
  int cell = -1;
  uint8_t state = kEmpty;  // what the square becomes
  uint8_t pin = 0;         // and, for track put right, its piece
  HintKind kind = HintKind::None;
  uint8_t tier = 0;  // the solver tier that found it, 0 for a fix
};

// HINT: one square, always correct, and always track: a hint lays track or
// turns track already laid to the answer's piece. It never lays a cross and
// never takes track away, since revealing track is what HINT is pressed for.
//
// A cross on the route comes first, becoming track, then a route square drawn
// in the wrong piece, set to the answer's. Otherwise the solver reasons
// forward from the player's marks that are right, leaving out any that are
// wrong so nothing is deduced from them, and stops at the lowest tier that
// decides a square the player has left empty is track. Track laid off the
// route is left for the player, and for CHECK, to find.
inline Hint findHint(const Puzzle& p, const Board& b, Solver& solver) {
  Hint hint;
  const int n = p.size;
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      if (b.cell[i] != kCross || !p.solution[i]) continue;
      hint.cell = i;
      hint.state = kTrack;
      hint.kind = HintKind::Fixed;
      return hint;
    }
  }
  // Then a wrong shape, put right by setting the answer's piece.
  {
    uint8_t shapes[kMaxCells];
    inferShapes(b, p, shapes);
    for (int i = 0; i < kMaxCells; ++i) {
      if (!inside(p, rowOf(i), colOf(i)) || !isWrongShape(p, b, shapes, i)) continue;
      hint.cell = i;
      hint.state = kTrack;
      hint.pin = p.solution[i];
      hint.kind = HintKind::Fixed;
      return hint;
    }
  }

  if (solver.load(p) && solver.applyRightMarks(b)) {
    for (int tier = 1; tier <= 3; ++tier) {
      solver.deduce(tier);
      for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
          const int i = cellIndex(r, c);
          if (b.cell[i] != kEmpty || solver.square(r, c) != kYes) continue;
          hint.cell = i;
          hint.state = kTrack;
          hint.kind = HintKind::Track;
          hint.tier = static_cast<uint8_t>(tier);
          return hint;
        }
      }
    }
  }

  // Every generated puzzle solves by tier 3, so this is only reached by a
  // board the deduction cannot extend. The answer is still the answer.
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      if (b.cell[i] != kEmpty || !p.solution[i]) continue;
      hint.cell = i;
      hint.state = kTrack;
      hint.kind = HintKind::Track;
      return hint;
    }
  }
  return hint;
}

// Applies a hint as an ordinary move, so it can be undone like any other.
// Track a hint lays is the answer's piece, set, so a bracketed square never
// shows a guess the answer does not have.
inline bool applyHint(Game& game, const Hint& hint) {
  if (hint.cell < 0) return false;
  const uint8_t pin = hint.state == kTrack ? game.puzzle.solution[hint.cell] : 0;
  return setCell(game.board, game.puzzle, rowOf(hint.cell), colOf(hint.cell), hint.state, pin);
}

// Whether the line drawn from the start gate, followed piece by piece, leaves
// through the end gate having run through every square of the route. The
// route is unique, so that line is the answer, and track laid elsewhere does
// not stop it counting.
inline bool lineSolves(const Puzzle& p, const Board& b) {
  uint8_t shapes[kMaxCells];
  inferShapes(b, p, shapes);
  int row = p.start.row;
  int col = p.start.col;
  int from = p.start.side;
  for (int steps = 1; steps <= kMaxCells; ++steps) {
    const int i = cellIndex(row, col);
    const uint8_t piece = shapes[i];
    if (b.cell[i] != kTrack || !p.solution[i] || !(piece & dirBit(from)) || popcount4(piece) != 2) return false;
    const int to = popcount4(piece & ~dirBit(from)) == 1 ? __builtin_ctz(piece & ~dirBit(from)) : -1;
    if (to < 0) return false;
    if (row == p.end.row && col == p.end.col && to == p.end.side) return steps == totalTrack(p);
    row += kDRow[to];
    col += kDCol[to];
    if (!inside(p, row, col)) return false;
    from = opposite(to);
  }
  return false;
}

// Solved: the marks alone satisfy every rule (isWin), or the drawn line is the
// route whatever else is on the board.
inline bool isSolved(const Puzzle& p, const Board& b, Solver& scratch) {
  return lineSolves(p, b) || isWin(p, b, scratch);
}

// A solved board drawn as the answer: track off the route is taken away, and
// every route square shows its own piece, so no chosen piece is left behind
// that the drawing contradicts. Outside the history, since a solved game takes
// no more moves.
inline void tidySolved(Board& b, const Puzzle& p) {
  for (int i = 0; i < kMaxCells; ++i) {
    if (b.cell[i] == kTrack && !p.solution[i]) b.cell[i] = kEmpty;
    b.pin[i] = 0;
    b.shape[i] = p.solution[i];
  }
}

// Whether a hint would finish the puzzle. The last move is the player's: a
// hint that solves it is refused. Tried on the board itself and taken back,
// redo entry and all, so the board and its history are left exactly as they
// were and no copy of either is made.
inline bool hintSolves(Game& game, const Hint& hint, Solver& scratch) {
  // A move writes one history slot and drops the redo branch, so that slot and
  // the counters are what to put back; a player who had just undone something
  // keeps their redo.
  History& h = game.board.history;
  const uint8_t first = h.first;
  const uint8_t count = h.count;
  const uint8_t cursor = h.cursor;
  const int slot = (first + cursor) % kHistoryDepth;
  const Move overwritten = h.moves[slot];
  if (!applyHint(game, hint)) return false;
  const bool solves = isSolved(game.puzzle, game.board, scratch);
  undo(game.board);
  h.moves[slot] = overwritten;
  h.first = first;
  h.count = count;
  h.cursor = cursor;
  return solves;
}

// "ST", "ND", "RD" or "TH" for a count: 1ST, 2ND, 3RD, 4TH, 11TH, 12TH, 13TH,
// 21ST, 112TH.
constexpr const char* ordinalSuffix(const unsigned n) {
  return (n % 100 >= 11 && n % 100 <= 13) ? "TH" : n % 10 == 1 ? "ST" : n % 10 == 2 ? "ND" : n % 10 == 3 ? "RD" : "TH";
}

// Squares of track the player has laid, givens included.
inline int laidTrack(const Board& b) {
  int laid = 0;
  for (int i = 0; i < kMaxCells; ++i) laid += b.cell[i] == kTrack;
  return laid;
}

}  // namespace traintracks
