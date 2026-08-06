// Murdle puzzle tests. Freestanding: no device, no PlatformIO.
//
// A generator is the wrong shape for spot tests. Almost any bug in one produces
// puzzles that look completely normal and are quietly unsolvable, or quietly
// have two answers, or quietly need a guess -- and you find out when somebody
// spends twenty minutes on one. So the suite here is the equivalent of perft:
// every tier, thousands of sequential seeds, and five properties asserted on
// every single case.
//
//   1. it has exactly one solution
//   2. every clue in it is true of that solution
//   3. no clue can be removed and leave it unique  (it is minimal)
//   4. the reference solver reaches the answer with pencil rules alone
//      (it is fair: no case ever needs a guess)
//   5. the same seed builds the same case twice   (saves are six bytes)
//
// Properties 3 and 4 are the ones worth the runtime. Uniqueness is easy and
// almost meaningless on its own: a unique puzzle can still be unsolvable
// without trial and error, and a puzzle carrying four redundant clues is
// unique, solvable, and boring.

#include <cstdio>
#include <cstring>
#include <utility>

#include "../../src/apps_local/murdle/MurdleCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  if (checksFailed < 20) std::printf("FAIL test_murdle.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace murdle;

const char* tierName(const Tier tier) {
  switch (tier) {
    case Tier::Elementary:
      return "ELEMENTARY";
    case Tier::Nosy:
      return "NOSY";
    case Tier::HardBoiled:
      return "HARD BOILED";
    case Tier::Impossible:
      return "IMPOSSIBLE";
  }
  return "?";
}

const Tier kTiers[kTierCount] = {Tier::Elementary, Tier::Nosy, Tier::HardBoiled, Tier::Impossible};

// Stands in for MurdleCast until the cast tables exist. The shape is what
// matters to the generator: a handful of masks over the drawn suspects, some
// naming one of them and some naming two, which is exactly what a real
// attribute column produces once four suspects have been drawn.
AttrMasks fakeAttrs(const Shape shape, const uint32_t seed) {
  Rng rng(seed * 2654435761u + 17u);
  AttrMasks attrs;
  const uint8_t full = static_cast<uint8_t>((1u << shape.items) - 1u);
  for (int i = 0; i < 8; ++i) {
    uint8_t mask = 0;
    for (int s = 0; s < shape.items; ++s) {
      if (rng.below(2) != 0) mask = static_cast<uint8_t>(mask | (1u << s));
    }
    if (mask == 0 || mask == full) continue;
    attrs.mask[attrs.count] = mask;
    attrs.tag[attrs.count] = static_cast<uint8_t>(i);
    ++attrs.count;
  }
  return attrs;
}

void identityCast(uint8_t cast[kMaxCats][kMaxItems]) {
  for (int c = 0; c < kMaxCats; ++c) {
    for (int i = 0; i < kMaxItems; ++i) cast[c][i] = static_cast<uint8_t>(i);
  }
}

// ---------------------------------------------------------------------------
// Shape and arithmetic

void testShapes() {
  CHECK(shapeOf(Tier::Elementary).cats == 3);
  CHECK(shapeOf(Tier::Elementary).items == 3);
  CHECK(shapeOf(Tier::Nosy).cats == 3);
  CHECK(shapeOf(Tier::Nosy).items == 4);
  CHECK(shapeOf(Tier::HardBoiled).cats == 4);
  CHECK(shapeOf(Tier::Impossible).items == 4);

  // The ceiling the solver is built against. If a tier ever exceeds this the
  // enumerator stops being affordable on the device, and it should fail here
  // rather than on somebody's desk.
  for (int t = 0; t < kTierCount; ++t) {
    CHECK(candidateCount(shapeOf(kTiers[t])) <= kMaxCandidates);
    CHECK(shapeOf(kTiers[t]).items <= kMaxItems);
    CHECK(shapeOf(kTiers[t]).cats <= kMaxCats);
  }
  CHECK(candidateCount(Shape{3, 3}) == 6 * 6 * 3);
  CHECK(candidateCount(Shape{4, 4}) == 24 * 24 * 24 * 4);
}

// ---------------------------------------------------------------------------
// The random source

void testRngIsUnbiased() {
  // A skewed shuffle would make one suspect likelier than another to be the
  // murderer, which is invisible in any single case and is exactly the sort of
  // thing that stays plausible while being wrong.
  for (uint32_t bound = 2; bound <= 5; ++bound) {
    int counts[8] = {};
    Rng rng(bound * 7919u + 1u);
    const int draws = 200000;
    for (int i = 0; i < draws; ++i) counts[rng.below(bound)]++;
    const int expected = draws / static_cast<int>(bound);
    for (uint32_t v = 0; v < bound; ++v) {
      const int delta = counts[v] - expected;
      CHECK(delta > -expected / 20 && delta < expected / 20);
    }
  }
}

// ---------------------------------------------------------------------------
// The grid

void testGridIsOrderIndependent() {
  Grid grid;
  grid.reset(Shape{4, 4});
  CHECK(grid.get(1, 2, 0, 3) == Mark::Unknown);
  CHECK(grid.set(1, 2, 0, 3, Mark::Yes));
  // The screens ask in whichever order the axes happen to be drawn, and a cell
  // that answered differently depending on the order would be a bug nobody
  // could see until the two views disagreed.
  CHECK(grid.get(0, 3, 1, 2) == Mark::Yes);
  CHECK(grid.get(1, 2, 0, 3) == Mark::Yes);

  // Writing the same value again is not a contradiction; writing over it is.
  CHECK(grid.set(0, 3, 1, 2, Mark::Yes));
  CHECK(!grid.set(0, 3, 1, 2, Mark::No));
}

void testSetYesCrossesItsOwnBlockOnly() {
  Grid grid;
  grid.reset(Shape{4, 4});
  CHECK(grid.setYes(0, 1, 1, 1));
  for (int i = 0; i < 4; ++i) {
    if (i == 1) continue;
    CHECK(grid.get(0, 1, 1, i) == Mark::No);
    CHECK(grid.get(0, i, 1, 1) == Mark::No);
  }
  // Crossing out is bookkeeping. Reaching into another block would be doing the
  // deduction for the player, which is the game.
  for (int i = 0; i < 4; ++i) CHECK(grid.get(0, 1, 2, i) == Mark::Unknown);
  CHECK(!grid.complete());
}

// ---------------------------------------------------------------------------
// The generator, swept

// Every clue must be true of the solution the generator claims. A false clue
// makes a puzzle with no answer at all, which countSolutions would catch, but
// this says which clue rather than just "zero solutions".
bool everyClueHolds(const Puzzle& puzzle) {
  for (int i = 0; i < puzzle.clueCount; ++i) {
    if (!clueHolds(puzzle.clues[i], puzzle)) return false;
  }
  return true;
}

// The deduced grid has to agree with the solution cell for cell, not merely be
// complete. A solver that filled the grid with a *different* consistent answer
// would pass a completeness check and be wrong.
bool gridMatchesSolution(const Puzzle& puzzle, const Grid& grid) {
  for (int a = 0; a < puzzle.shape.cats; ++a) {
    for (int b = a + 1; b < puzzle.shape.cats; ++b) {
      for (int r = 0; r < puzzle.shape.items; ++r) {
        if (grid.get(a, puzzle.assign[a][r], b, puzzle.assign[b][r]) != Mark::Yes) return false;
      }
    }
  }
  return true;
}

// No clue may be droppable. The murder clue and the witness statements are
// structural rather than deduced, so they are exempt: the first is the only
// thing that can name the murderer and the second is one per suspect by
// definition.
bool isMinimal(const Puzzle& puzzle, Scratch& scratch) {
  for (int i = 0; i < puzzle.clueCount; ++i) {
    if (puzzle.clues[i].anchor == Anchor::Murderer) continue;
    if (puzzle.clues[i].speaker != kNobodySpeaks) continue;
    Puzzle probe = puzzle;
    for (int j = i; j + 1 < probe.clueCount; ++j) probe.clues[j] = probe.clues[j + 1];
    --probe.clueCount;
    if (countSolutions(probe, 2, scratch) == 1) return false;
  }
  return true;
}

struct Stats {
  int cases = 0;
  int clueTotal = 0;
  int clueMin = 999;
  int clueMax = 0;
  int roundMax = 0;
};

void sweepTier(const Tier tier, const int seeds, Stats& stats) {
  static Scratch scratch;
  const Shape shape = shapeOf(tier);
  uint8_t cast[kMaxCats][kMaxItems];
  identityCast(cast);

  for (int s = 1; s <= seeds; ++s) {
    const uint32_t seed = static_cast<uint32_t>(s) * 2654435761u + 101u;
    const AttrMasks attrs = fakeAttrs(shape, seed);

    Puzzle puzzle;
    if (!generate(tier, seed, cast, attrs, scratch, puzzle)) {
      check(false, "generate() gave up", __LINE__);
      continue;
    }

    CHECK(puzzle.shape.cats == shape.cats);
    CHECK(puzzle.shape.items == shape.items);
    CHECK(puzzle.clueCount > 0 && puzzle.clueCount <= kMaxClues);

    // 1. exactly one solution
    CHECK(countSolutions(puzzle, 2, scratch) == 1);
    // 2. and it is the one the generator says it is
    CHECK(everyClueHolds(puzzle));
    // 3. minimal
    CHECK(isMinimal(puzzle, scratch));
    // 4. fair: pencil rules alone reach the answer
    Grid grid;
    const int rounds = deduce(puzzle, grid);
    CHECK(rounds >= 0);
    CHECK(grid.complete());
    CHECK(gridMatchesSolution(puzzle, grid));

    // 5. deterministic, which is what lets a save be a seed
    Puzzle again;
    CHECK(generate(tier, seed, cast, attrs, scratch, again));
    CHECK(fingerprint(again) == fingerprint(puzzle));
    CHECK(std::memcmp(&again, &puzzle, sizeof(Puzzle)) == 0);

    stats.cases++;
    stats.clueTotal += puzzle.clueCount;
    if (puzzle.clueCount < stats.clueMin) stats.clueMin = puzzle.clueCount;
    if (puzzle.clueCount > stats.clueMax) stats.clueMax = puzzle.clueCount;
    if (rounds > stats.roundMax) stats.roundMax = rounds;
  }
}

// ---------------------------------------------------------------------------
// The Impossible tier's own claim

void testStatementsAreOnePerSuspectAndExactlyOneLies() {
  static Scratch scratch;
  const Shape shape = shapeOf(Tier::Impossible);
  uint8_t cast[kMaxCats][kMaxItems];
  identityCast(cast);

  for (int s = 1; s <= 200; ++s) {
    const uint32_t seed = static_cast<uint32_t>(s) * 40503u + 7u;
    Puzzle puzzle;
    CHECK(generate(Tier::Impossible, seed, cast, fakeAttrs(shape, seed), scratch, puzzle));

    int spoken = 0;
    int liars = 0;
    bool spoke[kMaxItems] = {};
    for (int i = 0; i < puzzle.clueCount; ++i) {
      const Clue& clue = puzzle.clues[i];
      if (clue.speaker == kNobodySpeaks) continue;
      ++spoken;
      CHECK(clue.speaker < shape.items);
      CHECK(!spoke[clue.speaker]);
      spoke[clue.speaker] = true;

      // The claim itself, with the honesty stripped off: true for everybody
      // except the murderer, false for the murderer, and never both.
      Clue bare = clue;
      bare.speaker = kNobodySpeaks;
      const bool claimTrue = clueHolds(bare, puzzle);
      const bool isMurderer = puzzle.rowOf(0, clue.speaker) == puzzle.murderRow;
      CHECK(claimTrue != isMurderer);
      if (!claimTrue) ++liars;
    }
    CHECK(spoken == shape.items);
    CHECK(liars == 1);
  }
}

// ---------------------------------------------------------------------------
// Mutation check
//
// A suite that passes more easily than expected is a suite that cannot fail.
// These deliberately break a generated case and assert the checks notice.

void testTheChecksCanFail() {
  static Scratch scratch;
  const Shape shape = shapeOf(Tier::HardBoiled);
  uint8_t cast[kMaxCats][kMaxItems];
  identityCast(cast);
  const uint32_t seed = 991u;

  Puzzle puzzle;
  CHECK(generate(Tier::HardBoiled, seed, cast, fakeAttrs(shape, seed), scratch, puzzle));
  CHECK(countSolutions(puzzle, 2, scratch) == 1);

  // Drop a deduced clue: the case must stop being unique. If this passes, the
  // pruner is leaving redundant clues in and "minimal" means nothing.
  {
    Puzzle probe = puzzle;
    int victim = -1;
    for (int i = 0; i < probe.clueCount; ++i) {
      if (probe.clues[i].anchor != Anchor::Murderer && probe.clues[i].speaker == kNobodySpeaks) victim = i;
    }
    CHECK(victim >= 0);
    for (int j = victim; j + 1 < probe.clueCount; ++j) probe.clues[j] = probe.clues[j + 1];
    --probe.clueCount;
    CHECK(countSolutions(probe, 2, scratch) > 1);
  }

  // Falsify a clue by inverting its mask. Whatever the case now says, it no
  // longer says the thing the generator claims is the answer -- which is what
  // everyClueHolds() exists to notice. (It does not have to become unsolvable:
  // inverting one clue usually just describes some *other* arrangement, and
  // asserting zero solutions here would be asserting something untrue.)
  {
    Puzzle probe = puzzle;
    bool inverted = false;
    for (int i = 0; i < probe.clueCount && !inverted; ++i) {
      if (probe.clues[i].anchor == Anchor::Murderer) continue;
      const uint8_t full = static_cast<uint8_t>((1u << probe.shape.items) - 1u);
      probe.clues[i].targetMask = static_cast<uint8_t>(full & ~probe.clues[i].targetMask);
      inverted = true;
    }
    CHECK(inverted);
    CHECK(!everyClueHolds(probe));
    CHECK(countSolutions(probe, 2, scratch) != 1 || fingerprint(probe) != fingerprint(puzzle));
  }

  // Move the solution out from under the clues: they no longer all hold.
  {
    Puzzle probe = puzzle;
    std::swap(probe.assign[1][0], probe.assign[1][1]);
    CHECK(!everyClueHolds(probe));
  }

  // A case with only its murder clue is wide open, and the fairness gate has to
  // say so rather than reporting a tidy zero rounds.
  {
    Puzzle probe = puzzle;
    probe.clueCount = 1;
    CHECK(probe.clues[0].anchor == Anchor::Murderer);
    CHECK(countSolutions(probe, 2, scratch) > 1);
    Grid grid;
    CHECK(deduce(probe, grid) == kUnfair);
  }
}

}  // namespace

int main() {
  testShapes();
  testRngIsUnbiased();
  testGridIsOrderIndependent();
  testSetYesCrossesItsOwnBlockOnly();
  testStatementsAreOnePerSuspectAndExactlyOneLies();
  testTheChecksCanFail();

  const int seedsPerTier = 400;
  for (int t = 0; t < kTierCount; ++t) {
    Stats stats;
    sweepTier(kTiers[t], seedsPerTier, stats);
    std::printf("  %-12s %4d cases   clues %d-%d (avg %.1f)   rounds <= %d\n", tierName(kTiers[t]), stats.cases,
                stats.clueMin, stats.clueMax, stats.cases ? static_cast<double>(stats.clueTotal) / stats.cases : 0.0,
                stats.roundMax);
  }

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
