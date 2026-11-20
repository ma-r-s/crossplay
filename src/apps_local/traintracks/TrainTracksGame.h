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

// A track square on the route whose shape contradicts the answer, given the
// shapes the board draws (`shapes`, from inferShapes). The route is unique, so
// each of its squares has one right piece. Wrong is a piece the player chose
// that is not that piece, or a join to a neighbouring track square that the
// answer does not make, chosen or guessed. An open end toward an empty square
// is not a claim, only a guess waiting for track, and is never wrong.
inline bool isWrongShape(const Puzzle& p, const Board& b, const uint8_t* shapes, const int i) {
  if (b.cell[i] != kTrack || !p.solution[i] || p.given[i]) return false;
  if (b.pin[i] && b.pin[i] != p.solution[i]) return true;
  const int r = rowOf(i);
  const int c = colOf(i);
  for (int d = 0; d < 4; ++d) {
    if (!(shapes[i] & dirBit(d)) || (p.solution[i] & dirBit(d))) continue;
    const int r2 = r + kDRow[d];
    const int c2 = c + kDCol[d];
    if (isTrack(b, p, r2, c2) && (shapes[cellIndex(r2, c2)] & dirBit(opposite(d)))) return true;
  }
  return false;
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

// What CHECK flagged, kept until the flagged square changes: its state, or the
// piece it draws (a guessed piece re-bends when a neighbour changes, and that
// counts). Taking flags away on every move made a player CHECK again, and pay
// for it, just to see the flags they had not got to yet.
struct CheckFlags {
  uint8_t wrong[kMaxCells];
  uint8_t state[kMaxCells];  // each flagged square as CHECK saw it
  uint8_t shape[kMaxCells];
};

// CHECK, keeping what it saw. Returns how many squares are flagged.
inline int checkAndStamp(const Puzzle& p, const Board& b, CheckFlags& flags) {
  const int count = checkBoard(p, b, flags.wrong);
  inferShapes(b, p, flags.shape);
  for (int i = 0; i < kMaxCells; ++i) flags.state[i] = b.cell[i];
  return count;
}

// After a move: drops each flag whose square has changed since the CHECK, and
// returns how many are left.
inline int keepFlags(const Puzzle& p, const Board& b, CheckFlags& flags) {
  uint8_t shapes[kMaxCells];
  inferShapes(b, p, shapes);
  int left = 0;
  for (int i = 0; i < kMaxCells; ++i) {
    if (!flags.wrong[i]) continue;
    if (b.cell[i] != flags.state[i] || shapes[i] != flags.shape[i]) {
      flags.wrong[i] = 0;
    } else {
      ++left;
    }
  }
  return left;
}

enum class HintKind : uint8_t {
  None,
  Fixed,  // a square the player had wrong, or its shape, put right
  Track,  // track belongs here
  Cross,  // track does not belong here
};

struct Hint {
  int cell = -1;
  uint8_t state = kEmpty;  // what the square becomes
  uint8_t pin = 0;         // and, for track put right, its piece
  HintKind kind = HintKind::None;
  uint8_t tier = 0;  // the solver tier that found it, 0 for a fix
};

// HINT: one square, always correct.
//
// A wrong mark comes first, because any deduction made past it would be built
// on something false, then a wrong shape, set to the answer's piece. Otherwise
// the solver reasons forward from the player's
// own marks and stops at the lowest tier that decides a square the player has
// left empty, preferring track, which tells the player more than a cross does.
inline Hint findHint(const Puzzle& p, const Board& b, Solver& solver) {
  Hint hint;
  const int n = p.size;
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      if (!isWrong(p, b, i)) continue;
      hint.cell = i;
      hint.state = p.solution[i] ? kTrack : kCross;
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

  if (solver.load(p) && solver.applyMarks(b)) {
    for (int tier = 1; tier <= 3; ++tier) {
      solver.deduce(tier);
      int cross = -1;
      for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
          const int i = cellIndex(r, c);
          if (b.cell[i] != kEmpty) continue;
          const uint8_t known = solver.square(r, c);
          if (known == kYes) {
            hint.cell = i;
            hint.state = kTrack;
            hint.kind = HintKind::Track;
            hint.tier = static_cast<uint8_t>(tier);
            return hint;
          }
          if (known == kNo && cross < 0) cross = i;
        }
      }
      if (cross >= 0) {
        hint.cell = cross;
        hint.state = kCross;
        hint.kind = HintKind::Cross;
        hint.tier = static_cast<uint8_t>(tier);
        return hint;
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
inline bool applyHint(Game& game, const Hint& hint) {
  if (hint.cell < 0) return false;
  return setCell(game.board, game.puzzle, rowOf(hint.cell), colOf(hint.cell), hint.state, hint.pin);
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
  const bool solves = isWin(game.puzzle, game.board, scratch);
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
