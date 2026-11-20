// Train Tracks, the generator, checked without a panel.
//
// A thousand puzzles per level, each held to everything a level promises: a
// real path between the gates, clues that match it, givens that are part of
// it, exactly one track set, the exact rating the level asks for, and the same
// puzzle every time the seed is replayed.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "TrainTracksCore.h"
#include "TrainTracksFallback.h"
#include "TrainTracksGenerator.h"
#include "TrainTracksSolver.h"

using namespace traintracks;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

GenWork work;
Solver checker;  // separate from the generator's, so a check cannot reuse its state

constexpr int kPerLevel = 1000;
constexpr double kMaxAvgHole[kLevelCount] = {2.6, 2.8, 3.3};
constexpr int kMaxAttempts = 2000;

// Walks the solution masks from the start gate. True if it is one path that
// leaves through the end gate and covers every solution square.
bool solutionIsOnePath(const Puzzle& p) {
  int track = 0;
  for (int i = 0; i < kMaxCells; ++i) track += p.solution[i] != 0;
  int r = p.start.row;
  int c = p.start.col;
  int came = p.start.side;
  if (!(p.solution[cellIndex(r, c)] & dirBit(came))) return false;
  for (int steps = 1; steps <= track; ++steps) {
    const uint8_t m = p.solution[cellIndex(r, c)];
    if (popcount4(m) != 2 || !(m & dirBit(came))) return false;
    const int out = __builtin_ctz(m & ~dirBit(came));
    const int r2 = r + kDRow[out];
    const int c2 = c + kDCol[out];
    if (!inside(p, r2, c2)) {
      return r == p.end.row && c == p.end.col && out == p.end.side && steps == track;
    }
    if (!(p.solution[cellIndex(r2, c2)] & dirBit(opposite(out)))) return false;
    r = r2;
    c = c2;
    came = opposite(out);
  }
  return false;
}

// Side of the largest all-empty square in the solution: a big blank region
// means the path left part of the board unused.
int biggestHole(const Puzzle& p) {
  int best = 0;
  for (int k = 1; k <= p.size; ++k) {
    for (int r = 0; r + k <= p.size; ++r) {
      for (int c = 0; c + k <= p.size; ++c) {
        bool empty = true;
        for (int a = r; a < r + k && empty; ++a) {
          for (int b = c; b < c + k; ++b) {
            if (p.solution[cellIndex(a, b)]) {
              empty = false;
              break;
            }
          }
        }
        if (empty && k > best) best = k;
      }
    }
  }
  return best;
}

uint32_t fnv(const void* data, const size_t size) {
  uint32_t h = 2166136261u;
  const auto* b = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) h = (h ^ b[i]) * 16777619u;
  return h;
}

void checkPuzzle(const Level level, const Generated& g) {
  const LevelSpec& s = spec(level);
  const Puzzle& p = g.puzzle;
  CHECK(p.size == s.size);
  CHECK(validPuzzle(p));
  CHECK(solutionIsOnePath(p));
  CHECK(!(p.start.row == p.end.row && p.start.col == p.end.col));

  // Clues are the solution's counts; path length is in range.
  int track = 0;
  for (int k = 0; k < p.size; ++k) {
    int row = 0;
    int col = 0;
    for (int j = 0; j < p.size; ++j) {
      row += p.solution[cellIndex(k, j)] != 0;
      col += p.solution[cellIndex(j, k)] != 0;
    }
    CHECK(row == p.rowClue[k] && col == p.colClue[k]);
    track += row;
  }
  const int cells = s.size * s.size;
  CHECK(track * 100 >= cells * s.minTrackPct && track * 100 <= cells * s.maxTrackPct);

  // Givens are solution squares with the solution's shape, within the limits.
  int givens = 0;
  for (int i = 0; i < kMaxCells; ++i) {
    if (!p.given[i]) continue;
    ++givens;
    CHECK(p.given[i] == p.solution[i]);
  }
  CHECK(givens == g.givens);
  CHECK(givens <= s.maxGivens);
  if (s.givenPct > 0) CHECK(givens <= track * s.givenPct / 100);

  // Shape: at most one line without track (the path builder's own cap).
  int emptyLines = 0;
  for (int k = 0; k < p.size; ++k) emptyLines += (p.rowClue[k] == 0) + (p.colClue[k] == 0);
  CHECK(emptyLines <= 1);

  // Rated exactly at the level: solved within its tier, and not below it.
  CHECK(checker.load(p));
  const Rating rating = checker.deduce(3);
  CHECK(rating.solved && rating.tier == s.tier);
  if (s.tier > 1) {
    CHECK(checker.load(p));
    CHECK(!checker.deduce(s.tier - 1).solved);
  }
  CHECK(checker.load(p));
  CHECK(checker.countSolutions(2) == 1);

  // The generated answer wins.
  Board b;
  resetBoard(b, p);
  for (int i = 0; i < kMaxCells; ++i) {
    if (p.solution[i]) b.cell[i] = kTrack;
  }
  CHECK(isWin(p, b, checker));
}

void testEveryLevel() {
  for (int lv = 0; lv < kLevelCount; ++lv) {
    const Level level = static_cast<Level>(lv);
    const auto t0 = std::chrono::steady_clock::now();
    long attempts = 0;
    int maxAttempt = 0;
    long givens = 0;
    long holes = 0;
    long lowClues = 0;
    for (int i = 0; i < kPerLevel; ++i) {
      const uint32_t seed = 0x5EED0000u + static_cast<uint32_t>(i);
      Generated g;
      const bool ok = generate(level, seed, kMaxAttempts, g, work);
      CHECK(ok);
      if (!ok) continue;
      attempts += g.attempt + 1;
      if (g.attempt > maxAttempt) maxAttempt = g.attempt;
      givens += g.givens;
      holes += biggestHole(g.puzzle);
      for (int k = 0; k < g.puzzle.size; ++k) lowClues += (g.puzzle.rowClue[k] <= 1) + (g.puzzle.colClue[k] <= 1);
      CHECK(g.seed == seed);
      checkPuzzle(level, g);

      // The successful attempt alone rebuilds the same puzzle: this is what a
      // save relies on.
      Generated again;
      CHECK(generateAttempt(level, seed, g.attempt, again, work));
      CHECK(std::memcmp(&again.puzzle, &g.puzzle, sizeof(Puzzle)) == 0);
    }
    // Path shape, on average. Measured when the builder was tuned (Hard 3.0
    // and 1.1): a regression back towards the depth-first builder's 4.0 and
    // 2.0 trips these.
    const double avgHole = static_cast<double>(holes) / kPerLevel;
    const double avgLow = static_cast<double>(lowClues) / kPerLevel;
    std::printf("  level %d shape: biggest empty square avg %.2f, clues <= 1 avg %.2f\n", lv, avgHole, avgLow);
    CHECK(avgHole <= kMaxAvgHole[lv]);
    CHECK(avgLow <= 1.6);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  level %d: %d puzzles, attempts avg %.1f max %d, givens avg %.1f, %.1f ms/puzzle incl. checks\n", lv,
                kPerLevel, static_cast<double>(attempts) / kPerLevel, maxAttempt + 1,
                static_cast<double>(givens) / kPerLevel, ms / kPerLevel);
  }
}

// Full replay, not just the one attempt: same seed, same puzzle, same attempt.
void testDeterministic() {
  for (int lv = 0; lv < kLevelCount; ++lv) {
    for (uint32_t seed = 1; seed <= 20; ++seed) {
      Generated a;
      Generated b;
      CHECK(generate(static_cast<Level>(lv), seed, kMaxAttempts, a, work));
      CHECK(generate(static_cast<Level>(lv), seed, kMaxAttempts, b, work));
      CHECK(a.attempt == b.attempt);
      CHECK(std::memcmp(&a.puzzle, &b.puzzle, sizeof(Puzzle)) == 0);
    }
  }
}

// One puzzle per level, pinned by hash. A save stores (seed, attempt), so any
// change that moves these regenerates a different puzzle from an old save.
// When that change is intended, update the hashes AND bump the save format
// version.
void testPinnedPuzzles() {
  constexpr uint32_t kPinned[kLevelCount] = {0xFDEBC243u, 0x7761C1C3u, 0xAA70B754u};
  for (int lv = 0; lv < kLevelCount; ++lv) {
    Generated g;
    CHECK(generate(static_cast<Level>(lv), 42u, kMaxAttempts, g, work));
    const uint32_t h = fnv(&g.puzzle, sizeof(Puzzle));
    if (h != kPinned[lv]) std::printf("  level %d seed 42: hash 0x%08Xu attempt %u\n", lv, h, g.attempt);
    CHECK(h == kPinned[lv]);
  }
}

void testFallbackBank() {
  for (int lv = 0; lv < kLevelCount; ++lv) {
    for (int i = 0; i < kFallbackPerLevel; ++i) {
      Generated g;
      CHECK(loadFallback(static_cast<Level>(lv), i, g, work));
      CHECK(g.seed >= 0xF0000000u);
      checkPuzzle(static_cast<Level>(lv), g);
    }
  }
}

}  // namespace

int main() {
  // TT_QUICK=1 is for mutation runs: it stops at the first failing test and
  // skips the sweep, where a broken generator would spend every attempt on
  // every seed.
  const bool quick = std::getenv("TT_QUICK") != nullptr;
  testPinnedPuzzles();
  if (quick && failures) return 1;
  testFallbackBank();
  if (quick && failures) return 1;
  testDeterministic();
  if (!quick) testEveryLevel();
  std::printf("traintracks generator: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
