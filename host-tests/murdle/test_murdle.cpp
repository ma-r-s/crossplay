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
#include <cstdlib>
#include <cstring>
#include <utility>

#include "../../src/apps_local/murdle/MurdleCast.h"
#include "../../src/apps_local/murdle/MurdleCore.h"
#include "../../src/apps_local/murdle/MurdleText.h"

namespace {

int runTests();

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

// One whole case, the way the app makes one: draw a cast, read the attribute
// masks off that draw, then generate against them. The tests use the real cast
// rather than a fixture, because a fixture more convenient than the real caller
// stops testing the real caller.
bool makeCase(const Tier tier, const uint32_t seed, Scratch& scratch, Puzzle& out) {
  const Shape shape = shapeOf(tier);
  uint8_t cast[kMaxCats][kMaxItems];
  if (!drawCast(seed, shape, cast)) return false;
  return generate(tier, seed, cast, attrMasksFor(cast, shape), scratch, out);
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

// Random tapping, and the invariant that has to survive all of it.
//
// Four defects lived here across three rewrites, every one of them a case
// somebody had to think of first. This does not think: it taps at random,
// thousands of times, and after EVERY tap asserts what the board promises.
//
//   1. At most one tick a row and a column. Two would be one suspect holding
//      two weapons.
//   2. Every cell a tick rules out is shown crossed. That is the service being
//      offered and it must never be half-delivered.
//   3. Nothing is shown crossed without a reason -- either the player entered
//      it, or a tick implies it. This is the one all four bugs violated, and
//      the one that turns a stale mark into a board that contradicts itself.
//
// Under the current design 3 cannot fail by construction, because a derived
// cross is never stored anywhere to go stale. The test stays anyway: it is the
// statement of what the abstraction is FOR, and the next person to reach for a
// cache will find out here rather than from Mario.
void testRandomTappingKeepsTheGridHonest() {
  Rng rng(0xC0FFEEu);
  for (int trial = 0; trial < 400; ++trial) {
    const int items = 3 + static_cast<int>(rng.below(2));
    const int cats = 3 + static_cast<int>(rng.below(2));
    Marks marks;
    marks.reset(Shape{static_cast<uint8_t>(cats), static_cast<uint8_t>(items)});

    for (int tap = 0; tap < 60; ++tap) {
      const int a = static_cast<int>(rng.below(static_cast<uint32_t>(cats)));
      int b = static_cast<int>(rng.below(static_cast<uint32_t>(cats - 1)));
      if (b >= a) ++b;
      const int ta = static_cast<int>(rng.below(static_cast<uint32_t>(items)));
      const int tb = static_cast<int>(rng.below(static_cast<uint32_t>(items)));

      // 4. A tap writes its own cell and no other. Everything the board shows
      //    beyond that is derived, so the only way for a tap to reach another
      //    cell is to have deleted something the player entered -- which is how
      //    one tap on a finished grid took two answers away and blanked a third.
      Marks was = marks;
      marks.tap(a, ta, b, tb);
      for (int x = 0; x < cats; ++x) {
        for (int y = x + 1; y < cats; ++y) {
          for (int p = 0; p < items; ++p) {
            for (int q = 0; q < items; ++q) {
              const bool tapped = (x == a && y == b && p == ta && q == tb) || (x == b && y == a && p == tb && q == ta);
              if (tapped) continue;
              CHECK(marks.entered(x, p, y, q) == was.entered(x, p, y, q));
            }
          }
        }
      }

      for (int x = 0; x < cats; ++x) {
        for (int y = x + 1; y < cats; ++y) {
          for (int p = 0; p < items; ++p) {
            int inRow = 0;
            int inCol = 0;
            for (int q = 0; q < items; ++q) {
              if (marks.shown(x, p, y, q) == Mark::Yes) ++inRow;
              if (marks.shown(x, q, y, p) == Mark::Yes) ++inCol;
            }
            CHECK(inRow <= 1);
            CHECK(inCol <= 1);
          }
          for (int p = 0; p < items; ++p) {
            for (int q = 0; q < items; ++q) {
              bool implied = false;
              for (int i = 0; i < items; ++i) {
                if (i != q && marks.shown(x, p, y, i) == Mark::Yes) implied = true;
                if (i != p && marks.shown(x, i, y, q) == Mark::Yes) implied = true;
              }
              const Mark here = marks.shown(x, p, y, q);
              if (implied) CHECK(here == Mark::No);
              if (here == Mark::No) CHECK(implied || marks.entered(x, p, y, q) == Mark::No);
              // Reading a cell either way round must agree, or the two views of
              // the board disagree and a tap lands somewhere else than it looks.
              CHECK(marks.shown(y, q, x, p) == here);
            }
          }
        }
      }
    }
  }
}

// THE ONE THING A TAP MAY NOT DO: TAKE AWAY A MARK THE PLAYER MADE.
//
// A cold tester finished a case -- 27 of 27, every square right -- tapped one
// false square, and watched three squares they had never touched change: two
// locked-in answers became crosses and a third went blank. Nothing was flagged
// and cycling the tapped square back did not bring any of them back.
//
// The board derives crosses from ticks, so unseating the two ticks that crossed
// the tapped square out also un-derived the cross a third square was getting
// from them. One tap, three cells, no undo. This rebuilds that exact board.
void testATapNeverTakesAwayAMarkThePlayerMade() {
  const Shape shape = shapeOf(Tier::Elementary);
  Marks marks;
  marks.reset(shape);

  // A finished 3x3 block: weapon i is in place i. Every other square in it is
  // crossed by those three ticks and by nothing else.
  const int wCat = static_cast<int>(Cat::Weapon);
  const int lCat = static_cast<int>(Cat::Location);
  for (int i = 0; i < shape.items; ++i) marks.enter(wCat, i, lCat, i, Mark::Yes);

  // Weapon 0 with place 1: shown crossed, and the cross is the board's own.
  CHECK(marks.shown(wCat, 0, lCat, 1) == Mark::No);
  CHECK(marks.derived(wCat, 0, lCat, 1));

  const TapResult result = marks.tap(wCat, 0, lCat, 1);
  CHECK(!result.changed);
  CHECK(result.blocked());
  CHECK(result.sameRow == 0);  // the tick at weapon 0 x place 0
  CHECK(result.sameCol == 1);  // the tick at weapon 1 x place 1

  // Every tick still stands, and so does every cross that followed from one.
  for (int i = 0; i < shape.items; ++i) CHECK(marks.entered(wCat, i, lCat, i) == Mark::Yes);
  CHECK(marks.entered(wCat, 0, lCat, 1) == Mark::Unknown);
  CHECK(marks.shown(wCat, 1, lCat, 0) == Mark::No);
  CHECK(marks.decided() == marks.cells() / (shape.cats * (shape.cats - 1) / 2));

  // And the message names both of them rather than restating the rule.
  Scratch scratch;
  Puzzle puzzle;
  CHECK(makeCase(Tier::Elementary, 12345u, scratch, puzzle));
  char line[96];
  murdletext::blockedLine(puzzle, wCat, 0, lCat, 1, result, line, sizeof(line));
  CHECK(std::strstr(line, murdletext::label(puzzle, wCat, 0)) != nullptr);
  CHECK(std::strstr(line, murdletext::label(puzzle, lCat, 0)) != nullptr);
  CHECK(std::strstr(line, murdletext::label(puzzle, wCat, 1)) != nullptr);
  CHECK(std::strstr(line, murdletext::label(puzzle, lCat, 1)) != nullptr);
  CHECK(std::strstr(line, " AND ") != nullptr);

  // One blocker on its own is named on its own.
  Marks single;
  single.reset(shape);
  single.enter(wCat, 0, lCat, 0, Mark::Yes);
  const TapResult one = single.tap(wCat, 0, lCat, 1);
  CHECK(!one.changed);
  CHECK(one.sameRow == 0);
  CHECK(one.sameCol == kNoBlocker);
  murdletext::blockedLine(puzzle, wCat, 0, lCat, 1, one, line, sizeof(line));
  CHECK(std::strstr(line, " AND ") == nullptr);
  CHECK(std::strlen(line) > 0);
}

// The other half of that guard, and the half a guard gets wrong: refusing taps
// that were fine. A board this game cannot mark is worse than one that loses a
// mark, and on a panel that takes a second to answer it presents identically --
// as a screen that has stopped responding.
void testOrdinaryMarkingStillWorks() {
  const Shape shape = shapeOf(Tier::HardBoiled);
  Marks marks;
  marks.reset(shape);
  const int sCat = static_cast<int>(Cat::Suspect);
  const int wCat = static_cast<int>(Cat::Weapon);

  // The whole cycle on an empty board, twice round.
  for (int lap = 0; lap < 2; ++lap) {
    CHECK(marks.tap(sCat, 1, wCat, 2).changed);
    CHECK(marks.shown(sCat, 1, wCat, 2) == Mark::No);
    CHECK(!marks.derived(sCat, 1, wCat, 2));
    CHECK(marks.tap(sCat, 1, wCat, 2).changed);
    CHECK(marks.shown(sCat, 1, wCat, 2) == Mark::Yes);
    CHECK(marks.tap(sCat, 1, wCat, 2).changed);
    CHECK(marks.shown(sCat, 1, wCat, 2) == Mark::Unknown);
  }

  // Moving a tick: clear the old one, then mark the new one. Three taps, all of
  // which have to land.
  CHECK(marks.tap(sCat, 0, wCat, 0).changed);
  CHECK(marks.tap(sCat, 0, wCat, 0).changed);
  CHECK(marks.shown(sCat, 0, wCat, 0) == Mark::Yes);
  CHECK(marks.tap(sCat, 0, wCat, 0).changed);  // back to blank
  CHECK(marks.tap(sCat, 0, wCat, 1).changed);
  CHECK(marks.tap(sCat, 0, wCat, 1).changed);
  CHECK(marks.shown(sCat, 0, wCat, 1) == Mark::Yes);
  CHECK(marks.shown(sCat, 0, wCat, 0) == Mark::No);

  // A cross the player entered by hand still ticks in one tap: the refusal is
  // about ticks in the way, not about the square looking crossed.
  Marks byHand;
  byHand.reset(shape);
  CHECK(byHand.tap(sCat, 3, wCat, 3).changed);
  CHECK(byHand.entered(sCat, 3, wCat, 3) == Mark::No);
  CHECK(byHand.tap(sCat, 3, wCat, 3).changed);
  CHECK(byHand.shown(sCat, 3, wCat, 3) == Mark::Yes);

  // A tick in ANOTHER block never blocks: the rule is per block, and reading it
  // any wider would refuse most of the board once a few answers were in.
  const int lCat = static_cast<int>(Cat::Location);
  Marks wide;
  wide.reset(shape);
  wide.enter(sCat, 0, wCat, 0, Mark::Yes);
  CHECK(wide.tap(sCat, 0, lCat, 1).changed);
  CHECK(wide.tap(sCat, 0, lCat, 1).changed);
  CHECK(wide.shown(sCat, 0, lCat, 1) == Mark::Yes);
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

// No clue may be droppable, where "droppable" means the case can still be
// SOLVED without it -- not merely that its answer stays unique.
//
// Those are two different properties and the difference is the whole point of
// the generator. A clue can be redundant for pinning the answer down and still
// be the only thing that lets a person reach it with a pencil; dropping it
// would leave a puzzle that is technically well-posed and practically a wall.
// The old version of this asserted the uniqueness flavour, which was the right
// check for a generator that built its cases against an enumerator and is the
// wrong one now that they are built through the solver.
//
// The murder clue and the witness statements are structural rather than
// deduced, so they are exempt: the first is the only thing that can name the
// murderer and the second is one per suspect by definition.
bool isMinimal(const Puzzle& puzzle) {
  for (int i = 0; i < puzzle.clueCount; ++i) {
    if (puzzle.clues[i].anchor == Anchor::Murderer) continue;
    if (puzzle.clues[i].speaker != kNobodySpeaks) continue;
    Puzzle probe = puzzle;
    for (int j = i; j + 1 < probe.clueCount; ++j) probe.clues[j] = probe.clues[j + 1];
    --probe.clueCount;
    Grid grid;
    if (deduce(probe, grid) >= 0) return false;
  }
  return true;
}

// Two cases are the same case when everything a player could see is the same.
// Deliberately not memcmp: Puzzle has padding between its members and the
// clue array keeps whatever the pruner left past clueCount, so a byte compare
// would be asserting things about stack rubbish rather than about the case.
bool sameCase(const Puzzle& a, const Puzzle& b) {
  if (a.seed != b.seed || a.tier != b.tier || a.rounds != b.rounds) return false;
  if (a.shape.cats != b.shape.cats || a.shape.items != b.shape.items) return false;
  if (a.murderRow != b.murderRow || a.clueCount != b.clueCount) return false;
  for (int c = 0; c < a.shape.cats; ++c) {
    for (int i = 0; i < a.shape.items; ++i) {
      if (a.assign[c][i] != b.assign[c][i] || a.cast[c][i] != b.cast[c][i]) return false;
    }
  }
  for (int i = 0; i < a.clueCount; ++i) {
    const Clue& x = a.clues[i];
    const Clue& y = b.clues[i];
    if (x.anchor != y.anchor || x.anchorCat != y.anchorCat || x.anchorItem != y.anchorItem) return false;
    if (x.targetCat != y.targetCat || x.targetMask != y.targetMask) return false;
    if (x.speaker != y.speaker || x.voice != y.voice || x.attr != y.attr) return false;
  }
  return true;
}

struct Stats {
  int cases = 0;
  int clueTotal = 0;
  int clueMin = 999;
  int clueMax = 0;
  int roundMax = 0;
  // Dossier clues, and height clues in particular. Height used to be printed on
  // every case file and reachable by no clue above Elementary, because the only
  // masks it produced named one suspect and single-suspect attributes are
  // banned wherever bare positives are. Nothing went red: the case was still
  // unique, still minimal, still fair. Counting is the only way that shows up.
  int attrClues = 0;
  int heightClues = 0;
};

void sweepTier(const Tier tier, const int seeds, Stats& stats) {
  static Scratch scratch;
  const Shape shape = shapeOf(tier);

  for (int s = 1; s <= seeds; ++s) {
    const uint32_t seed = static_cast<uint32_t>(s) * 2654435761u + 101u;

    Puzzle puzzle;
    if (!makeCase(tier, seed, scratch, puzzle)) {
      check(false, "generate() gave up", __LINE__);
      continue;
    }

    CHECK(puzzle.shape.cats == shape.cats);
    CHECK(puzzle.shape.items == shape.items);
    CHECK(puzzle.clueCount > 0 && puzzle.clueCount <= kMaxClues);

    // 1. exactly one solution.
    //
    // This is now the suite's most valuable single assertion, because the
    // generator no longer computes it. Cases are built by watching the pencil
    // solver finish them, and uniqueness is inferred: propagation only writes
    // marks that are logical consequences, so a grid it fills completely is one
    // in which every cell was forced. That inference is sound, but it rests on
    // propagate() having no bugs -- and exhaustive enumeration is the only
    // check here that shares no code with the thing it is checking. If a
    // propagation rule ever writes a mark it has not earned, this line is what
    // catches it.
    CHECK(countSolutions(puzzle, 2, scratch) == 1);
    // 2. and it is the one the generator says it is
    CHECK(everyClueHolds(puzzle));
    // 3. minimal: no clue can be dropped and still leave a solvable case
    CHECK(isMinimal(puzzle));
    // 4. fair: pencil rules alone reach the answer
    Grid grid;
    const int rounds = deduce(puzzle, grid);
    CHECK(rounds >= 0);
    CHECK(grid.complete());
    CHECK(gridMatchesSolution(puzzle, grid));

    // 5. deterministic, which is what lets a save be a seed
    Puzzle again;
    CHECK(makeCase(tier, seed, scratch, again));
    CHECK(fingerprint(again) == fingerprint(puzzle));
    CHECK(sameCase(again, puzzle));

    stats.cases++;
    stats.clueTotal += puzzle.clueCount;
    if (puzzle.clueCount < stats.clueMin) stats.clueMin = puzzle.clueCount;
    if (puzzle.clueCount > stats.clueMax) stats.clueMax = puzzle.clueCount;
    if (rounds > stats.roundMax) stats.roundMax = rounds;
    for (int i = 0; i < puzzle.clueCount; ++i) {
      const uint8_t tag = puzzle.clues[i].attr;
      if (tag == kNoAttr) continue;
      ++stats.attrClues;
      const bool height = tag == kAttrTallest || tag == kAttrShortest ||
                          (tag >= kAttrTallerThan && tag < kAttrTallerThan + kMaxItems) ||
                          (tag >= kAttrShorterThan && tag < kAttrShorterThan + kMaxItems);
      if (height) ++stats.heightClues;
    }
  }
}

// ---------------------------------------------------------------------------
// Round five: the dossier, and the bug that came with it

// "Taller than X" has to be exactly the suspects taller than X, read off the
// same inches the case file prints. The mask is what the solver checks and the
// tag is what the sentence says, so a mismatch is a clue that is true as logic
// and a lie as English -- the failure mode no amount of solving will surface.
void testComparativeHeightMasks() {
  for (uint32_t seed = 1; seed <= 400; ++seed) {
    for (int t = 0; t < kTierCount; ++t) {
      const Shape shape = shapeOf(kTiers[t]);
      uint8_t cast[kMaxCats][kMaxItems];
      CHECK(drawCast(seed * 2654435761u + 31u, shape, cast));
      const AttrMasks attrs = attrMasksFor(cast, shape);

      for (int i = 0; i < attrs.count; ++i) {
        const uint8_t tag = attrs.tag[i];
        const bool taller = tag >= kAttrTallerThan && tag < kAttrTallerThan + kMaxItems;
        const bool shorter = tag >= kAttrShorterThan && tag < kAttrShorterThan + kMaxItems;
        if (!taller && !shorter) continue;

        const int who = tag - (taller ? kAttrTallerThan : kAttrShorterThan);
        CHECK(who < shape.items);
        const uint8_t ref = kSuspects[cast[0][who]].inches;

        uint8_t want = 0;
        for (int j = 0; j < shape.items; ++j) {
          if (j == who) continue;
          const uint8_t h = kSuspects[cast[0][j]].inches;
          if (taller ? h > ref : h < ref) want = static_cast<uint8_t>(want | (1u << j));
        }
        CHECK(attrs.mask[i] == want);
        // The reference suspect is never in their own comparison, which is what
        // makes ties harmless: "taller than ANNA" simply excludes anybody at
        // ANNA's exact height, including ANNA.
        CHECK((attrs.mask[i] & static_cast<uint8_t>(1u << who)) == 0);
      }
    }
  }
}

// The bias toward dossier clues must be able to give up.
//
// This seed is the one that found it. Its statements fail on 54 of 64 attempts,
// so only ten ever reach clue selection, and its cast's attribute masks are too
// weak to close a case -- with the bias applied unconditionally, all ten ran
// into kMaxClues and the tier produced no case at all. Backing off by attempt
// number does not help, because the cast is drawn from the seed and so is the
// same on every attempt; the fallback has to live inside the attempt. A silent
// generation failure is worse than any puzzle this bias was meant to improve.
void testDossierBiasNeverStarvesGeneration() {
  static Scratch scratch;
  Puzzle p;
  CHECK(makeCase(Tier::Impossible, 1352920181u, scratch, p));
  CHECK(countSolutions(p, 2, scratch) == 1);
}

// ---------------------------------------------------------------------------
// The Impossible tier's own claim

void testStatementsAreOnePerSuspectAndExactlyOneLies() {
  static Scratch scratch;
  const Shape shape = shapeOf(Tier::Impossible);

  for (int s = 1; s <= 200; ++s) {
    const uint32_t seed = static_cast<uint32_t>(s) * 40503u + 7u;
    Puzzle puzzle;
    CHECK(makeCase(Tier::Impossible, seed, scratch, puzzle));

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
// The sentences
//
// The logic is checked above; this checks that what the player actually reads
// says the same thing. Two failure modes are worth the trouble, and both are
// silent: a template that produces something ungrammatical, and a template that
// produces something grammatical which describes a *different* clue from the
// one the solver verified.

bool isAscii(const char* text) {
  for (const char* c = text; *c; ++c) {
    if (static_cast<unsigned char>(*c) > 126 || static_cast<unsigned char>(*c) < 32) return false;
  }
  return true;
}

bool contains(const char* haystack, const char* needle) { return std::strstr(haystack, needle) != nullptr; }

// Do two fixture details share a word worth confusing?
//
// Content words compared on their first three letters, so "crack" catches
// "cracked" and "mud" catches "muddy" -- both were real reported collisions and
// a four-letter rule missed the second of them.
//
// WHAT THIS CANNOT CATCH, and it is the more dangerous half: "a bent handle"
// beside "a broken step" share no word at all. That rhyme is thematic, and no
// string comparison sees it. It is held off by writing the two tables in
// different registers -- a weapon carries a substance or a mark, a place has a
// state -- which is a discipline recorded above kWeapons, not a check. Anybody
// adding a fixture has to apply it themselves.
bool shareASignificantWord(const char* a, const char* b) {
  static const char* const kStructure[] = {"the", "and", "one", "with", "its", "was"};
  const auto forEachWord = [](const char* s, auto&& fn) {
    while (*s != '\0') {
      while (*s == ' ') ++s;
      const char* start = s;
      while (*s != '\0' && *s != ' ') ++s;
      if (s - start < 3) continue;  // "a", "an", "on", "it", "in", "of"
      bool structural = false;
      for (const char* stop : kStructure) {
        if (static_cast<int>(std::strlen(stop)) == s - start && std::strncmp(start, stop, s - start) == 0) {
          structural = true;
        }
      }
      if (!structural) fn(start);
    }
  };
  bool shared = false;
  forEachWord(a, [&](const char* wa) {
    forEachWord(b, [&](const char* wb) {
      if (std::strncmp(wa, wb, 3) == 0) shared = true;
    });
  });
  return shared;
}

// What an item looks like inside a sentence, which is not what it looks like on
// the grid: the grid says LIGHTHOUSE and the sentence says "in the lighthouse".
// Two forms on purpose -- a label is read as a label and a clue is read as
// prose -- and the tests below have to ask for the right one.
const char* sentencePhrase(const Puzzle& p, const int cat, const int item) {
  const int entry = p.cast[cat][item];
  switch (static_cast<Cat>(cat)) {
    case Cat::Suspect:
      return kSuspects[entry].name;
    case Cat::Weapon:
      return kWeapons[entry].phrase;
    case Cat::Location:
      return kPlaces[entry].phrase;
    case Cat::Motive:
      return kMotives[entry].phrase;
  }
  return "";
}

void testCastTableIsDrawable() {
  // The Toybox face is subset to ASCII and a glyph it does not have draws as
  // *nothing* -- no box, no fallback, no log line. So a stray curly quote in
  // this table would silently delete a word from a clue.
  for (int i = 0; i < kSuspectCount; ++i) CHECK(isAscii(kSuspects[i].name));
  for (int i = 0; i < kWeaponCount; ++i) {
    CHECK(isAscii(kWeapons[i].name));
    CHECK(isAscii(kWeapons[i].phrase));
    CHECK(isAscii(kWeapons[i].trait));
  }
  for (int i = 0; i < kPlaceCount; ++i) {
    CHECK(isAscii(kPlaces[i].name));
    CHECK(isAscii(kPlaces[i].phrase));
    CHECK(isAscii(kPlaces[i].trait));
  }
  for (int i = 0; i < kMotiveCount; ++i) {
    CHECK(isAscii(kMotives[i].name));
    CHECK(isAscii(kMotives[i].phrase));
  }

  // Every weapon and place has one. `anchorPhrase` reaches for a trait in one
  // wording out of three with no empty-string fallback, so a blank here would
  // ship "the suspect in the place with ." -- a clue that is still logically
  // true, still minimal, still solvable, and unreadable.
  for (int i = 0; i < kWeaponCount; ++i) CHECK(kWeapons[i].trait[0] != '\0');
  for (int i = 0; i < kPlaceCount; ++i) CHECK(kPlaces[i].trait[0] != '\0');

  // NO TWO DETAILS ANYWHERE SHARE A SIGNIFICANT WORD, across both tables.
  //
  // Distinctness of the whole string is not enough and was not enough: "a bent
  // handle" and "a broken step" are different strings that a reader scanning for
  // "the broken one" cannot tell apart. The body clue names the crime scene by
  // its detail, so a rhyme between a weapon and a place is a wrong accusation
  // reached by careful reasoning -- six play-testers hit it, and no logic test
  // could ever see it.
  //
  // Words of four or more characters only; "a", "an", "on", "it", "in", "the"
  // are structure rather than content.
  {
    const char* details[kWeaponCount + kPlaceCount];
    int n = 0;
    for (int i = 0; i < kWeaponCount; ++i) details[n++] = kWeapons[i].trait;
    for (int i = 0; i < kPlaceCount; ++i) details[n++] = kPlaces[i].trait;
    for (int a = 0; a < n; ++a) {
      for (int b = a + 1; b < n; ++b) {
        const bool clash = shareASignificantWord(details[a], details[b]);
        if (clash) std::printf("  detail clash: \"%s\" / \"%s\"\n", details[a], details[b]);
        CHECK(!clash);
      }
    }
  }

  // A trait names exactly one thing, or it is not a clue. Two weapons with "a
  // bent tip" would make the murder clue ambiguous and nothing would complain.
  for (int i = 0; i < kWeaponCount; ++i) {
    for (int j = i + 1; j < kWeaponCount; ++j) CHECK(std::strcmp(kWeapons[i].trait, kWeapons[j].trait) != 0);
  }
  for (int i = 0; i < kPlaceCount; ++i) {
    for (int j = i + 1; j < kPlaceCount; ++j) CHECK(std::strcmp(kPlaces[i].trait, kPlaces[j].trait) != 0);
  }
  // Same for names, which the accusation sheet lists side by side.
  for (int i = 0; i < kSuspectCount; ++i) {
    for (int j = i + 1; j < kSuspectCount; ++j) CHECK(std::strcmp(kSuspects[i].name, kSuspects[j].name) != 0);
  }
}

// THE RULE THE WHOLE CAST TABLE EXISTS TO SATISFY: inside one case, no two
// items anywhere share an initial. Not within a category and not across them,
// so a letter on the grid means exactly one thing whichever axis it is on.
//
// Swept rather than spot-checked, because a collision needs a particular
// unlucky draw and the whole point is that there is no unlucky draw.
void testEveryCaseHasSixteenDistinctInitials() {
  for (int t = 0; t < kTierCount; ++t) {
    const Shape shape = shapeOf(kTiers[t]);
    for (uint32_t seed = 1; seed <= 25000; ++seed) {
      uint8_t cast[kMaxCats][kMaxItems];
      // Never runs out. The tables are sized so it cannot: motives is the
      // tightest at twelve letters and needs four, and it draws first.
      if (!drawCast(seed * 2654435761u + 11u, shape, cast)) {
        check(false, "drawCast ran out of letters", __LINE__);
        continue;
      }
      uint32_t seen = 0;
      for (int c = 0; c < shape.cats; ++c) {
        for (int i = 0; i < shape.items; ++i) {
          const char* name = castName(c, cast[c][i]);
          const uint32_t bit = 1u << (name[0] - 'A');
          CHECK((seen & bit) == 0);
          // And never a letter the axes are using. S on this grid is the
          // SUSPECTS axis, so nothing drawn may be SHOVEL or STABLE: a letter
          // that means both an axis and an item is the same defect as a letter
          // that means two items.
          CHECK((bit & kReservedLetters) == 0);
          seen |= bit;
        }
      }
      // 16 items at four categories of four, 9 at three of three.
      int count = 0;
      for (int b = 0; b < 26; ++b) {
        if (seen & (1u << b)) ++count;
      }
      CHECK(count == shape.cats * shape.items);
    }
  }
}

// One word each, and that is the other half of the same rule: a label is a
// letter and a name, and a name that needs two words does not fit a grid legend
// or an accusation button.
void testEveryNameIsOneWord() {
  for (int cat = 0; cat < kCatCount; ++cat) {
    for (int i = 0; i < castSize(cat); ++i) {
      const char* name = castName(cat, i);
      CHECK(name[0] >= 'A' && name[0] <= 'Z');
      for (const char* c = name; *c; ++c) CHECK(*c != ' ');
      // Seven, not twelve. The grid's key lays entries in four columns of about
      // a hundred pixels and "X=NAME" has to fit one: JEALOUSY was eight and
      // collided with the entry beside it. See host-tests/ui for the measured
      // version of this claim; this is the cheap one that fails first.
      CHECK(std::strlen(name) <= 7);
    }
  }
  // And within a table the initials are already distinct, which is what lets
  // the draw treat "a free letter" and "a usable item" as the same question.
  for (int cat = 0; cat < kCatCount; ++cat) {
    uint32_t seen = 0;
    for (int i = 0; i < castSize(cat); ++i) {
      const uint32_t bit = 1u << (castName(cat, i)[0] - 'A');
      CHECK((seen & bit) == 0);
      seen |= bit;
    }
  }
  // The draw takes categories scarcest-first and that order is hard-coded, so
  // assert the tables still have the shape that makes it the right order.
  CHECK(kMotiveCount <= kPlaceCount);
  CHECK(kPlaceCount <= kSuspectCount);
  CHECK(kWeaponCount <= kSuspectCount);
  // The draw can only fail by running a category out of free letters, so the
  // real claim is about usable letters after the axes take theirs, not about
  // table sizes. Each category must still have four left once every earlier
  // category in the draw order has taken four.
  int usable[kCatCount] = {};
  for (int cat = 0; cat < kCatCount; ++cat) {
    for (int i = 0; i < castSize(cat); ++i) {
      const uint32_t bit = 1u << (castName(cat, i)[0] - 'A');
      if ((bit & kReservedLetters) == 0) usable[cat]++;
    }
  }
  // Draw order is motives, places, weapons, suspects: 0, 4, 8 and 12 letters
  // already gone by the time each one picks.
  CHECK(usable[static_cast<int>(Cat::Motive)] >= 4);
  CHECK(usable[static_cast<int>(Cat::Location)] >= 8);
  CHECK(usable[static_cast<int>(Cat::Weapon)] >= 12);
  CHECK(usable[static_cast<int>(Cat::Suspect)] >= 16);
}

void testDrawIsDistinctAndInRange() {
  for (int t = 0; t < kTierCount; ++t) {
    const Shape shape = shapeOf(kTiers[t]);
    for (uint32_t seed = 1; seed <= 500; ++seed) {
      uint8_t cast[kMaxCats][kMaxItems];
      CHECK(drawCast(seed * 2246822519u + 3u, shape, cast));
      for (int c = 0; c < shape.cats; ++c) {
        for (int i = 0; i < shape.items; ++i) {
          CHECK(cast[c][i] < castSize(c));
          // Nobody appears twice in one case. A repeated suspect would give the
          // grid two identical rows and no clue could tell them apart.
          for (int j = i + 1; j < shape.items; ++j) CHECK(cast[c][i] != cast[c][j]);
        }
      }
    }
  }
}

// The grid axes are letters, and two rows carrying the same letter is a grid
// that cannot be read. Swept over every draw rather than spot-checked, because
// a collision needs two drawn items whose names start alike and that is exactly
// the case a hand-written example would not contain.
void testAxisLettersAreDistinct() {
  for (int t = 0; t < kTierCount; ++t) {
    const Shape shape = shapeOf(kTiers[t]);
    for (uint32_t seed = 1; seed <= 800; ++seed) {
      Puzzle puzzle;
      puzzle.shape = shape;
      CHECK(drawCast(seed * 2654435761u + 5u, shape, puzzle.cast));
      for (int c = 0; c < shape.cats; ++c) {
        char letters[kMaxItems + 1];
        murdletext::axisLetters(puzzle, c, letters);
        CHECK(static_cast<int>(std::strlen(letters)) == shape.items);
        for (int i = 0; i < shape.items; ++i) {
          CHECK(letters[i] > 32 && letters[i] < 127);
          for (int j = i + 1; j < shape.items; ++j) CHECK(letters[i] != letters[j]);
        }
      }
    }
  }
}

void testEverySentenceReads() {
  static Scratch scratch;
  int lines = 0;
  for (int t = 0; t < kTierCount; ++t) {
    for (uint32_t seed = 1; seed <= 300; ++seed) {
      Puzzle puzzle;
      if (!makeCase(kTiers[t], seed * 22695477u + 13u, scratch, puzzle)) continue;
      for (int i = 0; i < puzzle.clueCount; ++i) {
        char line[murdletext::kLineMax];
        murdletext::clueLine(puzzle, i, line, sizeof(line));
        ++lines;

        const int len = static_cast<int>(std::strlen(line));
        CHECK(len > 10);
        CHECK(len < murdletext::kLineMax - 1);
        CHECK(isAscii(line));
        // A capital at the front and a full stop at the back, and the closing
        // quote of a statement counts as the back.
        CHECK(line[0] >= 'A' && line[0] <= 'Z');
        CHECK(line[len - 1] == '.' || line[len - 1] == '"');
        // The tells of a template that did not get filled.
        CHECK(!contains(line, "%s"));
        CHECK(!contains(line, "  "));
        CHECK(!contains(line, " ."));
        CHECK(!contains(line, "(null)"));
        // A motive is a thing you are driven by, never a thing you want:
        // "whoever wanted an inheritance" reads and "whoever wanted jealousy"
        // does not, and the broken third of that table shipped once already.
        CHECK(!contains(line, "wanted "));
        // A clue whose target is a suspect is rendered from the suspect's side
        // and must therefore START with a name (or "Either"). The bug this
        // catches is routing that whole sentence through the
        // anchor-plus-predicate path as well, which produced "Whoever carried
        // the pan FELIX did not carry the pan" -- a sentence with two subjects,
        // a capital letter, a full stop and no double space, which passed every
        // other check here.
        {
          const Clue& c = puzzle.clues[i];
          if (c.anchor == Anchor::Item && c.speaker == kNobodySpeaks && c.attr == kNoAttr &&
              static_cast<Cat>(c.targetCat) == Cat::Suspect) {
            bool startsWithName = std::strncmp(line, "Either ", 7) == 0;
            for (int b = 0; b < puzzle.shape.items && !startsWithName; ++b) {
              const char* name = murdletext::label(puzzle, 0, b);
              startsWithName = std::strncmp(line, name, std::strlen(name)) == 0;
            }
            check(startsWithName, line, __LINE__);
          }
        }

        // And the one that matters: the sentence has to be about the same items
        // the solver checked. A positive names its item; a denial names the item
        // it denies and says "not" or "did not".
        const Clue& clue = puzzle.clues[i];
        if (clue.anchor == Anchor::Murderer || clue.speaker != kNobodySpeaks || clue.attr != kNoAttr) continue;
        int set = 0;
        int only = 0;
        int missing = 0;
        for (int b = 0; b < puzzle.shape.items; ++b) {
          if (clue.targetMask & static_cast<uint8_t>(1u << b)) {
            ++set;
            only = b;
          } else {
            missing = b;
          }
        }
        if (set == 1) {
          CHECK(contains(line, sentencePhrase(puzzle, clue.targetCat, only)));
          CHECK(!contains(line, " not "));
        } else if (set == puzzle.shape.items - 1) {
          // A denial has to name the item it rules OUT. Naming one of the ones
          // still open produces a perfectly grammatical sentence describing the
          // complement of the real clue, and no logic test can see it.
          CHECK(contains(line, " not "));
          CHECK(contains(line, sentencePhrase(puzzle, clue.targetCat, missing)));
        } else {
          CHECK(contains(line, "either") || contains(line, " or "));
          for (int b = 0; b < puzzle.shape.items; ++b) {
            if (clue.targetMask & static_cast<uint8_t>(1u << b)) {
              CHECK(contains(line, sentencePhrase(puzzle, clue.targetCat, b)));
            }
          }
        }
        (void)only;
      }
    }
  }
  CHECK(lines > 3000);
}

// The sentence for a denial has to name the item that is ruled OUT, not one of
// the ones left in. Getting that backwards produces a perfectly grammatical
// sentence describing the complement of the real clue, which no logic test can
// see. Checked against a case built by hand so the expected words are known.
void testDenialNamesTheRuledOutItem() {
  static Scratch scratch;
  Puzzle puzzle;
  CHECK(makeCase(Tier::HardBoiled, 4242u, scratch, puzzle));

  Clue clue{};
  clue.anchor = Anchor::Item;
  clue.anchorCat = static_cast<uint8_t>(Cat::Suspect);
  clue.anchorItem = 0;
  clue.targetCat = static_cast<uint8_t>(Cat::Location);
  clue.speaker = kNobodySpeaks;
  clue.attr = kNoAttr;
  clue.voice = 0;
  // Everything but place 2.
  clue.targetMask = static_cast<uint8_t>(0x0F & ~(1u << 2));
  puzzle.clues[0] = clue;
  puzzle.clueCount = 1;

  const int place = static_cast<int>(Cat::Location);
  char line[murdletext::kLineMax];
  murdletext::clueLine(puzzle, 0, line, sizeof(line));
  CHECK(contains(line, murdletext::label(puzzle, static_cast<int>(Cat::Suspect), 0)));
  CHECK(contains(line, "was not "));
  // The ruled-out place, and none of the ones still open.
  CHECK(contains(line, sentencePhrase(puzzle, place, 2)));
  CHECK(!contains(line, sentencePhrase(puzzle, place, 0)));
  CHECK(!contains(line, sentencePhrase(puzzle, place, 1)));

  // And a bare positive names the one item it allows, and nothing else.
  clue.targetMask = static_cast<uint8_t>(1u << 1);
  puzzle.clues[0] = clue;
  murdletext::clueLine(puzzle, 0, line, sizeof(line));
  CHECK(contains(line, sentencePhrase(puzzle, place, 1)));
  CHECK(!contains(line, sentencePhrase(puzzle, place, 2)));
  CHECK(!contains(line, " not "));
}

// ---------------------------------------------------------------------------
// Mutation check
//
// A suite that passes more easily than expected is a suite that cannot fail.
// These deliberately break a generated case and assert the checks notice.

// The murderer's own statement must be about somebody else. A lie about the
// liar's own whereabouts is inert: the crime-scene clue already fixes where the
// murderer was, so the false claim contradicts what the player has and never
// touches another row. Two critics solved a case in seconds off exactly that.
void testTheMurdererLiesAboutSomebodyElse() {
  static Scratch scratch;
  for (uint32_t seed = 1; seed <= 400; ++seed) {
    Puzzle p;
    if (!makeCase(Tier::Impossible, seed * 22699u + 5u, scratch, p)) continue;
    for (int i = 0; i < p.clueCount; ++i) {
      const Clue& c = p.clues[i];
      if (c.speaker == kNobodySpeaks) continue;
      if (c.speaker != p.murderRow) continue;
      CHECK(c.anchorItem != c.speaker);
    }
  }
}

// The crime-scene clue is the reveal, so it is the last thing read. Asserted
// because it is a property of the *order*, which nothing else in this file
// looks at and which a future edit to the generator could silently undo.
void testTheSceneClueComesLast() {
  static Scratch scratch;
  for (int t = 0; t < kTierCount; ++t) {
    for (uint32_t seed = 1; seed <= 200; ++seed) {
      Puzzle p;
      if (!makeCase(kTiers[t], seed * 2654435761u + 19u, scratch, p)) continue;
      int scene = -1;
      for (int i = 0; i < p.clueCount; ++i) {
        if (p.clues[i].anchor == Anchor::Murderer) scene = i;
      }
      CHECK(scene == p.clueCount - 1);
    }
  }
}

// No two witnesses may assert the same proposition, and no witness may place
// anybody at the crime scene. Both make the case collapse in two lines: the
// first because identical statements cannot straddle the one-lie rule, the
// second because it is "X says: Y did it" wearing a placement's clothes.
void testStatementsDoNotGiveTheCaseAway() {
  static Scratch scratch;
  for (uint32_t seed = 1; seed <= 400; ++seed) {
    Puzzle p;
    if (!makeCase(Tier::Impossible, seed * 40503u + 23u, scratch, p)) continue;
    // The rule is about the MURDERER, not about the scene in general. A
    // truthful witness placing the murderer at the crime scene hands the case
    // over; a murderer *lying* that some innocent was there is the mechanic
    // working, and has to be untangled rather than banned.
    for (int i = 0; i < p.clueCount; ++i) {
      const Clue& a = p.clues[i];
      if (a.speaker == kNobodySpeaks) continue;
      if (a.anchorItem == p.murderRow) {
        const int sceneItem = p.assign[a.targetCat][p.murderRow];
        CHECK((a.targetMask & static_cast<uint8_t>(1u << sceneItem)) == 0);
      }
      for (int j = i + 1; j < p.clueCount; ++j) {
        const Clue& b = p.clues[j];
        if (b.speaker == kNobodySpeaks) continue;
        const bool same = a.anchorItem == b.anchorItem && a.targetCat == b.targetCat && a.targetMask == b.targetMask;
        CHECK(!same);
      }
    }
  }
}

void testTheChecksCanFail() {
  static Scratch scratch;
  const uint32_t seed = 991u;

  Puzzle puzzle;
  CHECK(makeCase(Tier::HardBoiled, seed, scratch, puzzle));
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
    // The crime-scene clue is now printed last rather than first, so find it
    // rather than assuming where it sits.
    Puzzle probe = puzzle;
    int scene = -1;
    for (int i = 0; i < probe.clueCount; ++i) {
      if (probe.clues[i].anchor == Anchor::Murderer) scene = i;
    }
    CHECK(scene >= 0);
    probe.clues[0] = probe.clues[scene];
    probe.clueCount = 1;
    CHECK(countSolutions(probe, 2, scratch) > 1);
    Grid grid;
    CHECK(deduce(probe, grid) == kUnfair);
  }
}

// Printing a case out. No assertions here: whether a clue reads well is not
// something a test can answer, and every wording defect in this game was found
// by looking at one of these rather than by reading the templates.
//
//   host-tests/murdle/run.sh --show
void showCase(const Tier tier, const uint32_t seed) {
  static Scratch scratch;
  Puzzle puzzle;
  if (!makeCase(tier, seed, scratch, puzzle)) {
    std::printf("  (no case)\n");
    return;
  }
  std::printf("\n=== %s  seed %u  %d categories of %d  %d clues  %d rounds ===\n", tierName(tier), seed,
              puzzle.shape.cats, puzzle.shape.items, puzzle.clueCount, puzzle.rounds);
  char buf[murdletext::kLineMax];
  for (int c = 0; c < puzzle.shape.cats; ++c) {
    std::printf("%-9s", murdletext::categoryName(c));
    for (int i = 0; i < puzzle.shape.items; ++i) std::printf(" | %-16s", murdletext::label(puzzle, c, i));
    std::printf("\n");
  }
  std::printf("\n");
  for (int i = 0; i < puzzle.shape.items; ++i) {
    murdletext::suspectAttributes(puzzle, i, buf, sizeof(buf));
    std::printf("  %-17s %s\n", murdletext::label(puzzle, 0, i), buf);
  }
  std::printf("\n");
  for (int i = 0; i < puzzle.clueCount; ++i) {
    murdletext::clueLine(puzzle, i, buf, sizeof(buf));
    std::printf("  %2d. %s\n", i + 1, buf);
  }
  uint8_t picks[kMaxCats] = {};
  for (int c = 0; c < puzzle.shape.cats; ++c) picks[c] = puzzle.assign[c][puzzle.murderRow];
  murdletext::accusationLine(puzzle, picks, buf, sizeof(buf));
  std::printf("\n  ANSWER: %s\n", buf);
}

}  // namespace

// ---------------------------------------------------------------------------
// Solving one, out loud.
//
// Not a test. This exists because a case can pass every assertion in this file
// -- one solution, minimal, reachable by pencil rules -- and still be a bad
// puzzle to sit in front of. The things that make it bad are not properties of
// the answer, they are properties of the *path*: how long before the first
// square is settled, how many clues you have to hold at once, how much of the
// work is one long chain with no branch to rest on.
//
//   host-tests/murdle/run.sh --solve [tier] [seed]
//
// It replays deduce()'s own rules one at a time and says which rule fired and
// why, so the path can be read rather than guessed at.

struct Step {
  const char* rule;
  int clue;  // -1 when the step is not a clue
  int settled;
};

// How many squares each rule settles, round by round, using exactly the rules
// deduce() is allowed. Returns the number of rounds; fills `firstYes` with the
// round in which the first square was locked in, which is the number that says
// whether a case gives you a foothold or a wall.
// The round at which the murderer becomes known, against the round the grid
// finishes. If the first is much smaller than the second, the case hands you
// its answer and then asks you to keep filling in a form -- which is a
// structural defect no correctness check can see.
int traceSolve(const Puzzle& p, int& firstYes, int& cluesBeforeFirstYes, int& maxChain, int* murdererKnownRound) {
  Grid grid;
  grid.reset(p.shape);
  firstYes = -1;
  cluesBeforeFirstYes = 0;
  maxChain = 0;

  // Only the unconditional clues, which is what a player has before they start
  // supposing anything.
  const int items = p.shape.items;
  const uint8_t full = static_cast<uint8_t>((1u << items) - 1u);

  // The place the murder clue names; the murderer is known the moment that
  // place has an owner.
  int murderPlace = -1;
  for (int i = 0; i < p.clueCount; ++i) {
    if (p.clues[i].anchor != Anchor::Murderer) continue;
    for (int b = 0; b < items; ++b) {
      if (p.clues[i].targetMask & static_cast<uint8_t>(1u << b)) murderPlace = b;
    }
  }
  if (murdererKnownRound) *murdererKnownRound = -1;

  int round = 0;
  bool changed = true;
  while (changed && round < 64) {
    changed = false;
    ++round;
    int settledThisRound = 0;

    for (int i = 0; i < p.clueCount; ++i) {
      const Clue& clue = p.clues[i];
      if (clue.anchor == Anchor::Murderer || clue.speaker != kNobodySpeaks) continue;
      for (int t = 0; t < items; ++t) {
        if (clue.targetMask & static_cast<uint8_t>(1u << t)) continue;
        if (grid.get(clue.anchorCat, clue.anchorItem, clue.targetCat, t) != Mark::Unknown) continue;
        grid.set(clue.anchorCat, clue.anchorItem, clue.targetCat, t, Mark::No);
        changed = true;
        ++settledThisRound;
        if (firstYes < 0) ++cluesBeforeFirstYes;
      }
    }
    (void)full;

    // Set a Yes and cross its row and column. Grid used to offer this, back
    // when one class served both the solver and the player; it is the player's
    // half and it went with them. Here it is plain bookkeeping.
    const auto settle = [&](const int a, const int ia, const int b, const int ib) {
      grid.set(a, ia, b, ib, Mark::Yes);
      for (int i = 0; i < items; ++i) {
        if (i != ib) grid.set(a, ia, b, i, Mark::No);
        if (i != ia) grid.set(a, i, b, ib, Mark::No);
      }
    };

    // Lone survivors, then transitivity: the same two rules deduce() uses.
    for (int a = 0; a < p.shape.cats; ++a) {
      for (int b = a + 1; b < p.shape.cats; ++b) {
        for (int ia = 0; ia < items; ++ia) {
          int open = 0, last = -1;
          for (int ib = 0; ib < items; ++ib) {
            if (grid.get(a, ia, b, ib) != Mark::No) {
              ++open;
              last = ib;
            }
          }
          if (open == 1 && grid.get(a, ia, b, last) != Mark::Yes) {
            settle(a, ia, b, last);
            if (firstYes < 0) firstYes = round;
            changed = true;
            ++settledThisRound;
          }
        }
        for (int ib = 0; ib < items; ++ib) {
          int open = 0, last = -1;
          for (int ia = 0; ia < items; ++ia) {
            if (grid.get(a, ia, b, ib) != Mark::No) {
              ++open;
              last = ia;
            }
          }
          if (open == 1 && grid.get(a, last, b, ib) != Mark::Yes) {
            settle(a, last, b, ib);
            if (firstYes < 0) firstYes = round;
            changed = true;
            ++settledThisRound;
          }
        }
      }
    }

    for (int a = 0; a < p.shape.cats; ++a) {
      for (int b = 0; b < p.shape.cats; ++b) {
        if (a == b) continue;
        for (int ia = 0; ia < items; ++ia) {
          for (int ib = 0; ib < items; ++ib) {
            if (grid.get(a, ia, b, ib) != Mark::Yes) continue;
            for (int c = 0; c < p.shape.cats; ++c) {
              if (c == a || c == b) continue;
              for (int ic = 0; ic < items; ++ic) {
                const Mark ma = grid.get(a, ia, c, ic);
                const Mark mb = grid.get(b, ib, c, ic);
                if (ma == mb) continue;
                if (ma == Mark::Unknown) {
                  grid.set(a, ia, c, ic, mb);
                } else if (mb == Mark::Unknown) {
                  grid.set(b, ib, c, ic, ma);
                }
                changed = true;
                ++settledThisRound;
              }
            }
          }
        }
      }
    }
    if (settledThisRound > maxChain) maxChain = settledThisRound;
    if (murdererKnownRound && *murdererKnownRound < 0 && murderPlace >= 0) {
      const int place = static_cast<int>(Cat::Location);
      for (int su = 0; su < items; ++su) {
        if (grid.get(place, murderPlace, 0, su) == Mark::Yes) *murdererKnownRound = round;
      }
    }
  }
  return grid.complete() ? round : -round;
}

// Exactly what a player sees on the device, and nothing else. No solution, no
// murderer, no hint of which clue matters. This is the harness a critic plays
// against: the assertions in this file can prove a case is solvable, and cannot
// tell you whether solving it was any good, so that judgement has to come from
// something that actually sits down and tries.
//
//   host-tests/murdle/run.sh --play <tier 0-3> <seed>
void playView(const Tier tier, const uint32_t seed) {
  static Scratch scratch;
  Puzzle p;
  if (!makeCase(tier, seed, scratch, p)) {
    std::printf("no case\n");
    return;
  }
  char buf[murdletext::kLineMax];
  std::printf("TIER: %s   (%d categories of %d)\n", tierName(tier), p.shape.cats, p.shape.items);
  std::printf("\nTHE SUSPECTS\n");
  for (int i = 0; i < p.shape.items; ++i) {
    murdletext::suspectAttributes(p, i, buf, sizeof(buf));
    std::printf("  %s -- %s\n", murdletext::label(p, 0, i), buf);
  }
  for (int cat = 1; cat < p.shape.cats; ++cat) {
    std::printf("\n%s\n", murdletext::categoryName(cat));
    for (int i = 0; i < p.shape.items; ++i) {
      const char* mark = murdletext::trait(p, cat, i);
      if (mark[0] != '\0') {
        std::printf("  %s (with %s)\n", murdletext::label(p, cat, i), mark);
      } else {
        std::printf("  %s\n", murdletext::label(p, cat, i));
      }
    }
  }
  std::printf("\nCLUES\n");
  for (int i = 0; i < p.clueCount; ++i) {
    murdletext::clueLine(p, i, buf, sizeof(buf));
    std::printf("  %2d. %s\n", i + 1, buf);
  }
  std::printf(
      "\nEvery suspect carried one weapon and was in one place%s. Nobody shares.\n"
      "One of them is the murderer. Name the suspect, weapon, place%s.\n",
      p.shape.cats > 3 ? ", and had one motive" : "", p.shape.cats > 3 ? " and motive" : "");
}

void solveOut(const Tier tier, const uint32_t seed) {
  static Scratch scratch;
  Puzzle p;
  if (!makeCase(tier, seed, scratch, p)) {
    std::printf("no case\n");
    return;
  }
  char buf[murdletext::kLineMax];
  std::printf("\n=== %s  seed %u  %d clues ===\n", tierName(tier), seed, p.clueCount);
  for (int i = 0; i < p.clueCount; ++i) {
    murdletext::clueLine(p, i, buf, sizeof(buf));
    std::printf("  %2d. %s\n", i + 1, buf);
  }
  int firstYes = 0, before = 0, chain = 0;
  int known = 0;
  const int rounds = traceSolve(p, firstYes, before, chain, &known);
  std::printf("\n  grid completes: %s\n", rounds > 0 ? "yes" : "NO -- needs a supposition");
  std::printf("  rounds: %d\n", rounds < 0 ? -rounds : rounds);
  std::printf("  first square locked in: round %d, after %d eliminations\n", firstYes, before);
  std::printf("  murderer known at round %d of %d\n", known, rounds < 0 ? -rounds : rounds);
  uint8_t picks[kMaxCats] = {};
  for (int c = 0; c < p.shape.cats; ++c) picks[c] = p.assign[c][p.murderRow];
  murdletext::accusationLine(p, picks, buf, sizeof(buf));
  std::printf("  answer: %s\n", buf);
}

// How many of the case's sixteen items are never named by any clue. They are
// still deducible -- by elimination -- but to a player they read as information
// that was never given, which is a different feeling from a hard deduction and
// is worth knowing the size of.
int itemsNeverNamed(const Puzzle& p) {
  bool named[kMaxCats][kMaxItems] = {};
  for (int i = 0; i < p.clueCount; ++i) {
    const Clue& clue = p.clues[i];
    if (clue.anchor == Anchor::Item) named[clue.anchorCat][clue.anchorItem] = true;
    if (clue.speaker != kNobodySpeaks) named[0][clue.speaker] = true;
    // A mask names the items it *singles out*: one bit set names that item, one
    // bit clear names the excluded one, two bits name both. An attribute mask
    // names nobody -- that is the point of it.
    if (clue.attr != kNoAttr) continue;
    int set = 0;
    for (int b = 0; b < p.shape.items; ++b) {
      if (clue.targetMask & static_cast<uint8_t>(1u << b)) ++set;
    }
    for (int b = 0; b < p.shape.items; ++b) {
      const bool on = (clue.targetMask & static_cast<uint8_t>(1u << b)) != 0;
      if ((set <= 2 && on) || (set == p.shape.items - 1 && !on)) named[clue.targetCat][b] = true;
    }
  }
  int missing = 0;
  for (int c = 0; c < p.shape.cats; ++c) {
    for (int i = 0; i < p.shape.items; ++i) {
      if (!named[c][i]) ++missing;
    }
  }
  return missing;
}

// The distribution that says whether these are pleasant puzzles, which is a
// different question from whether they are correct ones.
void auditDifficulty() {
  static Scratch scratch;
  std::printf("\n%-12s  %6s  %8s  %10s  %14s  %s\n", "TIER", "CASES", "ROUNDS", "FIRST YES", "NEEDS SUPPOSE",
              "NEVER NAMED");
  for (int t = 0; t < kTierCount; ++t) {
    int cases = 0, roundSum = 0, roundMax = 0, firstSum = 0, firstMax = 0, needSuppose = 0, beforeMax = 0;
    int unnamedSum = 0, unnamedMax = 0, attrClues = 0, suspectTargeted = 0, clueTotal = 0;
    int knownSum = 0, knownOfSum = 0, earlyReveal = 0;
    for (uint32_t seed = 1; seed <= 300; ++seed) {
      Puzzle p;
      if (!makeCase(kTiers[t], seed * 2654435761u + 7u, scratch, p)) continue;
      int firstYes = 0, before = 0, chain = 0;
      int known = -1;
      const int r = traceSolve(p, firstYes, before, chain, &known);
      ++cases;
      if (r < 0) ++needSuppose;
      const int rr = r < 0 ? -r : r;
      roundSum += rr;
      if (rr > roundMax) roundMax = rr;
      if (firstYes > 0) {
        firstSum += firstYes;
        if (firstYes > firstMax) firstMax = firstYes;
      }
      if (before > beforeMax) beforeMax = before;
      for (int i = 0; i < p.clueCount; ++i) {
        if (p.clues[i].attr != kNoAttr) ++attrClues;
        if (p.clues[i].anchor == Anchor::Item && p.clues[i].targetCat == 0) ++suspectTargeted;
      }
      clueTotal += p.clueCount;
      const int rr2 = r < 0 ? -r : r;
      if (known > 0 && rr2 > 0) {
        knownSum += known;
        knownOfSum += rr2;
        if (known * 100 / rr2 <= 50) ++earlyReveal;
      }
      const int unnamed = itemsNeverNamed(p);
      unnamedSum += unnamed;
      if (unnamed > unnamedMax) unnamedMax = unnamed;
    }
    std::printf("%-12s  %6d  %3.1f/%-3d  %5.1f/%-3d  %6d (%3d%%)  %4.1f/%-2d\n", tierName(kTiers[t]), cases,
                cases ? static_cast<double>(roundSum) / cases : 0.0, roundMax,
                cases ? static_cast<double>(firstSum) / cases : 0.0, firstMax, needSuppose,
                cases ? needSuppose * 100 / cases : 0, cases ? static_cast<double>(unnamedSum) / cases : 0.0,
                unnamedMax);
    std::printf("%-12s  murderer known at round %.1f of %.1f   revealed in first half: %d%%\n", "",
                cases ? static_cast<double>(knownSum) / cases : 0.0,
                cases ? static_cast<double>(knownOfSum) / cases : 0.0, cases ? earlyReveal * 100 / cases : 0);
    std::printf("%-12s  attribute clues: %d of %d (%d%%)   suspect-targeted: %d (%d%%)\n", "", attrClues, clueTotal,
                clueTotal ? attrClues * 100 / clueTotal : 0, suspectTargeted,
                clueTotal ? suspectTargeted * 100 / clueTotal : 0);
  }
}

int main(const int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--audit") == 0) {
    auditDifficulty();
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "--play") == 0) {
    const int t = argc > 2 ? std::atoi(argv[2]) : 2;
    const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::atol(argv[3])) : 12345u;
    playView(kTiers[t < 0 || t >= kTierCount ? 2 : t], seed);
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "--solve") == 0) {
    const int t = argc > 2 ? std::atoi(argv[2]) : 2;
    const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::atol(argv[3])) : 12345u;
    solveOut(kTiers[t < 0 || t >= kTierCount ? 2 : t], seed);
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "--show") == 0) {
    for (int t = 0; t < kTierCount; ++t) showCase(kTiers[t], 20260805u + static_cast<uint32_t>(t));
    return 0;
  }
  return runTests();
}

namespace {

int runTests() {
  testShapes();
  testRngIsUnbiased();
  testGridIsOrderIndependent();
  testRandomTappingKeepsTheGridHonest();
  testATapNeverTakesAwayAMarkThePlayerMade();
  testOrdinaryMarkingStillWorks();
  testCastTableIsDrawable();
  testEveryNameIsOneWord();
  testEveryCaseHasSixteenDistinctInitials();
  testDrawIsDistinctAndInRange();
  testStatementsAreOnePerSuspectAndExactlyOneLies();
  testAxisLettersAreDistinct();
  testEverySentenceReads();
  testDenialNamesTheRuledOutItem();
  testTheSceneClueComesLast();
  testTheMurdererLiesAboutSomebodyElse();
  testStatementsDoNotGiveTheCaseAway();
  testComparativeHeightMasks();
  testDossierBiasNeverStarvesGeneration();
  testTheChecksCanFail();

  const int seedsPerTier = 400;
  for (int t = 0; t < kTierCount; ++t) {
    Stats stats;
    sweepTier(kTiers[t], seedsPerTier, stats);
    std::printf("  %-12s %4d cases   clues %d-%d (avg %.1f)   rounds <= %d   dossier %d (height %d)\n",
                tierName(kTiers[t]), stats.cases, stats.clueMin, stats.clueMax,
                stats.cases ? static_cast<double>(stats.clueTotal) / stats.cases : 0.0, stats.roundMax, stats.attrClues,
                stats.heightClues);

    // A floor, not a target. The dossier is printed on every case file, so a
    // tier that hardly references it is asking the player to maintain a table it
    // will not use -- which is what Hard Boiled (5%) and Impossible (2%) were
    // doing. The measured figures are now 38/28/18/11 percent, so 8 is a
    // regression guard with room in it rather than a restatement of today's
    // numbers. Height gets its own floor because it is the axis that was
    // silently unreachable, and a floor on the total would not have noticed.
    CHECK(stats.attrClues * 12 >= stats.clueTotal);
    CHECK(stats.heightClues > 0);
  }

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}

}  // namespace
