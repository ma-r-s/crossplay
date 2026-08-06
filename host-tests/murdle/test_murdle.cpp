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
  drawCast(seed, shape, cast);
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
    CHECK(makeCase(tier, seed, scratch, again));
    CHECK(fingerprint(again) == fingerprint(puzzle));
    CHECK(sameCase(again, puzzle));

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

void testDrawIsDistinctAndInRange() {
  for (int t = 0; t < kTierCount; ++t) {
    const Shape shape = shapeOf(kTiers[t]);
    for (uint32_t seed = 1; seed <= 500; ++seed) {
      uint8_t cast[kMaxCats][kMaxItems];
      drawCast(seed * 2246822519u + 3u, shape, cast);
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
    Puzzle probe = puzzle;
    probe.clueCount = 1;
    CHECK(probe.clues[0].anchor == Anchor::Murderer);
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

int main(const int argc, char** argv) {
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
  testSetYesCrossesItsOwnBlockOnly();
  testCastTableIsDrawable();
  testDrawIsDistinctAndInRange();
  testStatementsAreOnePerSuspectAndExactlyOneLies();
  testEverySentenceReads();
  testDenialNamesTheRuledOutItem();
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

}  // namespace
