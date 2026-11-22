#pragma once

// Train Tracks, the generator. Freestanding C++17, no heap.
//
// A seed and a level fully define a puzzle. Generation runs in attempts; each
// attempt draws from its own RNG stream (seed, attempt), so the attempt that
// succeeded can be regenerated directly without replaying the failures before
// it. A save stores (seed, attempt) and rebuilds in one attempt's time.
//
// One attempt:
//   1. Gates on two edge squares, far enough apart.
//   2. A random path between them, grown from a shortest path by sideways
//      bumps until its length falls in the level's range (see layPath).
//   3. Clues from the path.
//   4. Every track square given, then givens removed in random order, each
//      removal kept only if the solver still solves the puzzle using tiers up
//      to the level's. Stop once the givens are down to the level's target and
//      the puzzle rates exactly at the level's tier.
//   5. A complete deduction implies one track set; the host tests confirm it
//      with the exhaustive search on every puzzle they generate.
//
// Attempts are capped by count, never by time, so the result for a seed does
// not depend on how fast the device is. The caller adds its own wall-clock
// budget and falls back to the bundled puzzles if it runs out.

#include <cstdint>

#include "TrainTracksCore.h"
#include "TrainTracksSolver.h"

namespace traintracks {

enum class Level : uint8_t {
  Easy = 0,
  Medium = 1,
  Hard = 2,
};
constexpr int kLevelCount = 3;

struct LevelSpec {
  uint8_t size;
  uint8_t tier;         // the rating a puzzle must have, exactly
  uint8_t minTrackPct;  // path length as a share of the grid
  uint8_t maxTrackPct;
  uint8_t givenPct;   // stop removing givens at this share of track squares
  uint8_t maxGivens;  // and never ship more than this many
};

// First guesses, to tune in playtesting (see docs/apps/traintracks.md).
constexpr LevelSpec kLevels[kLevelCount] = {
    {6, 1, 40, 60, 25, 9},
    {8, 2, 40, 60, 10, 5},
    {10, 3, 40, 60, 0, 2},
};

constexpr const LevelSpec& spec(const Level level) { return kLevels[static_cast<int>(level)]; }

struct Generated {
  Puzzle puzzle;
  uint32_t seed;
  uint16_t attempt;
  uint8_t givens;
  Rating rating;
};

// Statistics for the host bench; ignored on the device.
struct GenStats {
  uint32_t pathFailures;
  uint32_t pruneFailures;
  uint32_t rateFailures;
  uint32_t routeFailures;
  uint32_t solverCalls;
};

// Everything one attempt needs besides its output: the solver and the path
// builder's arrays. About 9.6KB, held by the caller (the activity allocates it
// once), so generation puts almost nothing on the stack.
struct GenWork {
  Solver solver;
  bool used[kMaxCells];
  uint8_t path[kMaxCells];
  uint8_t order[kMaxCells];
};

namespace detail {

inline uint32_t attemptSeed(const uint32_t seed, const uint32_t attempt) {
  Rng mix{seed ^ (attempt * 0x9E3779B9u + 0x7F4A7C15u)};
  mix.next();
  return mix.next();
}

inline bool onEdge(const int size, const int r, const int c) {
  return r == 0 || c == 0 || r == size - 1 || c == size - 1;
}

// A random edge square and an outward side for it.
inline Gate randomGate(const int size, Rng& rng) {
  // Walk the perimeter: 4 * (size - 1) squares, each corner once.
  const int perimeter = 4 * (size - 1);
  const int k = rng.below(perimeter);
  const int side = k / (size - 1);
  const int offset = k % (size - 1);
  Gate g{};
  switch (side) {
    case 0:  // top, left to right
      g = Gate{0, static_cast<uint8_t>(offset), kNorth};
      break;
    case 1:  // right, top to bottom
      g = Gate{static_cast<uint8_t>(offset), static_cast<uint8_t>(size - 1), kEast};
      break;
    case 2:  // bottom, right to left
      g = Gate{static_cast<uint8_t>(size - 1), static_cast<uint8_t>(size - 1 - offset), kSouth};
      break;
    default:  // left, bottom to top
      g = Gate{static_cast<uint8_t>(size - 1 - offset), 0, kWest};
      break;
  }
  return g;
}

inline int distance(const Gate& a, const Gate& b) {
  const int dr = a.row > b.row ? a.row - b.row : b.row - a.row;
  const int dc = a.col > b.col ? a.col - b.col : b.col - a.col;
  return dr + dc;
}

// Grows a path from `start` to `end` with a length in [minLen, maxLen].
//
// It starts from a random shortest path, then repeatedly pushes a random
// segment sideways: a step a->b becomes a->c->d->b, where c and d are the two
// free squares beside it. Every bump adds two squares, and a random segment
// is as likely to be anywhere along the path as anywhere else, so the track
// spreads across the grid instead of sweeping along whichever wall a
// depth-first search reached first. Returns the length, or 0.
inline int layPath(const int size, const Gate& start, const Gate& end, const int minLen, const int maxLen,
                   const int maxTries, Rng& rng, GenWork& work) {
  bool* used = work.used;
  uint8_t* path = work.path;
  for (int i = 0; i < kMaxCells; ++i) used[i] = false;

  // Track squares per row and per column, to steer growth into empty lines.
  uint8_t rowUse[kMaxSize] = {};
  uint8_t colUse[kMaxSize] = {};
  auto take = [&](const int cell) {
    used[cell] = true;
    ++rowUse[rowOf(cell)];
    ++colUse[colOf(cell)];
  };

  int r = start.row;
  int c = start.col;
  int length = 0;
  path[length++] = static_cast<uint8_t>(cellIndex(r, c));
  take(cellIndex(r, c));
  const int stepR = end.row > r ? 1 : -1;
  const int stepC = end.col > c ? 1 : -1;
  int vertical = end.row > r ? end.row - r : r - end.row;
  int horizontal = end.col > c ? end.col - c : c - end.col;
  while (vertical + horizontal > 0) {
    if (rng.below(vertical + horizontal) < vertical) {
      r += stepR;
      --vertical;
    } else {
      c += stepC;
      --horizontal;
    }
    path[length++] = static_cast<uint8_t>(cellIndex(r, c));
    take(cellIndex(r, c));
  }

  // Bumps add two squares, so the length keeps the shortest path's parity.
  int target = minLen + rng.below(maxLen - minLen + 1);
  if ((target - length) % 2 != 0) target += target < maxLen ? 1 : -1;
  if (target < length || target < minLen) return 0;

  // Each bump is the best of a few random candidates, scored by how many free
  // squares surround the two it would take, plus a bonus for each row or
  // column it would be the first to enter. That steers growth into open
  // space, so the path does not leave empty lines and blank corners behind.
  // How open the 5x5 window around a square is: free squares as a percentage
  // of the window's squares that are on the grid. A wide window tells a large
  // empty region from a pocket beside the track, so growth heads for the big
  // gaps; a percentage keeps the border neutral, neither favoured nor shunned.
  auto freeAround = [&](const int r0, const int c0) {
    int free = 0;
    int onGrid = 0;
    for (int rr = r0 - 2; rr <= r0 + 2; ++rr) {
      for (int cc = c0 - 2; cc <= c0 + 2; ++cc) {
        if (rr < 0 || cc < 0 || rr >= size || cc >= size) continue;
        ++onGrid;
        if (!used[cellIndex(rr, cc)]) ++free;
      }
    }
    return free * 100 / onGrid;
  };
  constexpr int kCandidates = 40;
  for (int tries = 0; tries < maxTries && length < target;) {
    int bestScore = -1;
    int bestAt = 0;
    int bestN1 = 0;
    int bestN2 = 0;
    for (int k = 0; k < kCandidates && tries < maxTries; ++k, ++tries) {
      const int i = rng.below(length - 1);
      const int a = path[i];
      const int b = path[i + 1];
      int dir = 0;
      while (rowOf(a) + kDRow[dir] != rowOf(b) || colOf(a) + kDCol[dir] != colOf(b)) ++dir;
      const int side = (dir + (rng.below(2) ? 1 : 3)) & 3;
      const int r1 = rowOf(a) + kDRow[side];
      const int c1 = colOf(a) + kDCol[side];
      const int r2 = rowOf(b) + kDRow[side];
      const int c2 = colOf(b) + kDCol[side];
      if (r1 < 0 || c1 < 0 || r1 >= size || c1 >= size || r2 < 0 || c2 < 0 || r2 >= size || c2 >= size) continue;
      const int n1 = cellIndex(r1, c1);
      const int n2 = cellIndex(r2, c2);
      if (used[n1] || used[n2]) continue;
      int score = freeAround(r1, c1) + freeAround(r2, c2);
      constexpr int kEmptyLineBonus = 40;  // in the same percentage units
      if (rowUse[r1] == 0) score += kEmptyLineBonus;
      if (colUse[c1] == 0) score += kEmptyLineBonus;
      if (r2 != r1 && rowUse[r2] == 0) score += kEmptyLineBonus;
      if (c2 != c1 && colUse[c2] == 0) score += kEmptyLineBonus;
      if (score > bestScore) {
        bestScore = score;
        bestAt = i;
        bestN1 = n1;
        bestN2 = n2;
      }
    }
    if (bestScore < 0) continue;
    for (int k = length - 1; k > bestAt; --k) path[k + 2] = path[k];
    path[bestAt + 1] = static_cast<uint8_t>(bestN1);
    path[bestAt + 2] = static_cast<uint8_t>(bestN2);
    take(bestN1);
    take(bestN2);
    length += 2;
  }
  // A line with no track is a free gift of a whole row of crosses; allow one.
  int emptyLines = 0;
  for (int k = 0; k < size; ++k) emptyLines += (rowUse[k] == 0) + (colUse[k] == 0);
  if (emptyLines > 1) return 0;
  return length >= minLen ? length : 0;
}

}  // namespace detail

// One attempt. True when it produced a puzzle that meets the level; `out` is
// then complete. `work` is scratch (~9.6KB), owned by the caller.
inline bool generateAttempt(const Level level, const uint32_t seed, const uint16_t attempt, Generated& out,
                            GenWork& work, GenStats* stats = nullptr) {
  Solver& solver = work.solver;
  const LevelSpec& s = spec(level);
  const int size = s.size;
  const int cells = size * size;
  Rng rng{detail::attemptSeed(seed, attempt)};
  const int minLen = (cells * s.minTrackPct + 99) / 100;
  const int maxLen = (cells * s.maxTrackPct) / 100;

  Gate start{};
  Gate end{};
  for (int tries = 0;; ++tries) {
    if (tries > 64) return false;
    start = detail::randomGate(size, rng);
    end = detail::randomGate(size, rng);
    if (detail::distance(start, end) >= size) break;
  }

  const uint8_t* path = work.path;
  const int length = detail::layPath(size, start, end, minLen, maxLen, 20 * cells, rng, work);
  if (length == 0) {
    if (stats) ++stats->pathFailures;
    return false;
  }
  Puzzle& p = out.puzzle;
  if (!buildFromPath(p, size, path, length, start.side, end.side)) return false;

  auto rate = [&](const int maxTier) {
    if (stats) ++stats->solverCalls;
    if (!solver.load(p)) return Rating{false, 0};
    return solver.deduce(maxTier);
  };
  int givens = 0;
  uint8_t* order = work.order;
  Rating rating{false, 0};
  const int target = (length * s.givenPct) / 100;

  if (s.givenPct == 0) {
    // Few givens: build up from none instead of stripping down from all. Each
    // given goes on a track square the solver could not decide, so it is one
    // the puzzle actually needs.
    for (;;) {
      rating = rate(s.tier);
      if (rating.solved) break;
      if (givens == s.maxGivens) {
        if (stats) ++stats->pruneFailures;
        return false;
      }
      int open = 0;
      for (int i = 0; i < length; ++i) {
        if (solver.square(rowOf(path[i]), colOf(path[i])) == kUnknown) order[open++] = path[i];
      }
      if (open == 0) return false;
      const int cell = order[rng.below(open)];
      p.given[cell] = p.solution[cell];
      ++givens;
    }
  }

  // Strip: try removing each given in random order, keeping the removal only
  // if the puzzle still solves within the level's tiers.
  if (s.givenPct > 0) {
    for (int i = 0; i < length; ++i) p.given[path[i]] = p.solution[path[i]];
    givens = length;
    rating = rate(s.tier);
  }
  int stripCount = 0;
  for (int i = 0; i < kMaxCells; ++i) {
    if (p.given[i]) order[stripCount++] = static_cast<uint8_t>(i);
  }
  for (int i = stripCount - 1; i > 0; --i) {
    const int j = rng.below(i + 1);
    const uint8_t t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  for (int k = 0; k < stripCount; ++k) {
    if (givens <= target && givens <= s.maxGivens && rating.tier == s.tier) break;
    const int cell = order[k];
    const uint8_t keep = p.given[cell];
    p.given[cell] = 0;
    const Rating trial = rate(s.tier);
    if (trial.solved) {
      --givens;
      rating = trial;
    } else {
      p.given[cell] = keep;
    }
  }
  if (givens > s.maxGivens || (s.givenPct > 0 && givens > target)) {
    if (stats) ++stats->pruneFailures;
    return false;
  }
  // The rating is the hardest tier a full deduction needs. At tier 3 that is
  // the rating the last solve above already made of this exact puzzle (a
  // rejected strip restores what it removed), so it is not solved again.
  if (s.tier != 3) rating = rate(3);
  if (!rating.solved || rating.tier != s.tier) {
    if (stats) ++stats->rateFailures;
    return false;
  }
  // One route, not just one set of squares: CHECK and HINT judge each
  // square's piece against the answer's, which is only fair when no other
  // route runs through the same squares. Searched with every square fixed,
  // so only the joins are open, which is quick.
  if (!solver.load(p) || !solver.applySolution() || solver.countRoutes(2) != 1) {
    if (stats) ++stats->routeFailures;
    return false;
  }
  // A complete deduction implies one track set: every tier is sound, so the
  // squares it fixed are the only ones possible. The exhaustive search that
  // used to confirm it here cost a further full solve on the device; the host
  // tests run it on every generated puzzle instead (test_generator).

  out.seed = seed;
  out.attempt = attempt;
  out.givens = static_cast<uint8_t>(givens);
  out.rating = rating;
  return true;
}

// Tries attempts 0.. maxAttempts-1 in order. Deterministic for a seed.
inline bool generate(const Level level, const uint32_t seed, const int maxAttempts, Generated& out, GenWork& work,
                     GenStats* stats = nullptr) {
  for (int a = 0; a < maxAttempts; ++a) {
    if (generateAttempt(level, seed, static_cast<uint16_t>(a), out, work, stats)) return true;
  }
  return false;
}

}  // namespace traintracks
