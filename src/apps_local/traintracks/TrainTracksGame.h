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

// CHECK. Fills `wrong` (one flag per square) and returns how many.
inline int checkBoard(const Puzzle& p, const Board& b, uint8_t* wrong) {
  int count = 0;
  for (int i = 0; i < kMaxCells; ++i) {
    const bool bad = inside(p, rowOf(i), colOf(i)) && isWrong(p, b, i);
    wrong[i] = bad ? 1 : 0;
    count += bad;
  }
  return count;
}

enum class HintKind : uint8_t {
  None,
  Fixed,  // a square the player had wrong, put right
  Track,  // track belongs here
  Cross,  // track does not belong here
};

struct Hint {
  int cell = -1;
  uint8_t state = kEmpty;  // what the square becomes
  HintKind kind = HintKind::None;
  uint8_t tier = 0;  // the solver tier that found it, 0 for a fix
};

// HINT: one square, always correct.
//
// A wrong mark comes first, because any deduction made past it would be built
// on something false. Otherwise the solver reasons forward from the player's
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
  return setCell(game.board, game.puzzle, rowOf(hint.cell), colOf(hint.cell), hint.state);
}

// Squares of track the player has laid, givens included.
inline int laidTrack(const Board& b) {
  int laid = 0;
  for (int i = 0; i < kMaxCells; ++i) laid += b.cell[i] == kTrack;
  return laid;
}

}  // namespace traintracks
