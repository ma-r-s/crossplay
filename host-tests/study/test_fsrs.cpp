// Replays 301 real review histories from Mario's Anki collection through
// StudyFsrs and checks we land on the memory state Anki itself stored.
//
// This is the whole reason the scheduler is a freestanding module. It is not a
// test of "does the code run" but of "does it agree with Anki", which is the
// only property that matters: a card scheduled differently here than on the
// phone would silently desynchronise the two.

#include <cmath>
#include <cstdio>

#include "../../src/apps_local/study/StudyFsrs.h"
#include "FsrsVectors.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    if (failures <= 20) std::printf("  FAIL: %s\n", what);
  }
}

// Mario's "Mandarin: Vocabulary::Current" preset, lifted from the collection.
// The vectors were produced by Anki running these, so anything else is a
// different scheduler and the comparison would be meaningless.
constexpr float kCurrentDeckParams[study::kNumParams] = {0.3219f, 1.2066f, 3.0221f, 6.6069f, 7.4556f, 0.5159f, 1.7549f,
                                                         0.001f,  1.2577f, 0.3674f, 0.7788f, 1.9467f, 0.1041f, 0.2738f,
                                                         2.1961f, 0.3586f, 3.0137f, 0.3027f, 0.9353f};

// Tolerances. Anki stores stability rounded to three decimals, so an exact
// float comparison would fail on rounding alone; 2% is well inside the
// resolution at which a difference could change a displayed interval.
constexpr float kStabilityTolerance = 0.02f;   // relative
constexpr float kDifficultyTolerance = 0.02f;  // absolute

// 287 of 301 reproduce exactly. The 14 that do not are all heavily lapsed
// cards (34-51 reviews) whose stability is under three days, and every one of
// them matches on difficulty -- see the note in StudyFsrs.h. This number is
// pinned so a change that makes agreement *worse* fails the suite, and one
// that makes it better fails too, loudly, and asks to be recorded.
constexpr int kExpectedMatches = 287;

void testAgainstCollection() {
  const study::Fsrs fsrs(kCurrentDeckParams, 0.9f);
  int matched = 0;

  for (int i = 0; i < study::vectors::kCardCount; ++i) {
    const study::vectors::Card& card = study::vectors::kCards[i];
    study::Memory m;
    for (int s = 0; s < card.stepCount; ++s) {
      const study::vectors::Step& step = card.steps[s];
      m = fsrs.review(m, static_cast<study::Rating>(step.rating), step.elapsedDays);
    }
    const float ds = std::fabs(m.stability - card.expectedStability) / card.expectedStability;
    const float dd = std::fabs(m.difficulty - card.expectedDifficulty);
    if (ds < kStabilityTolerance && dd < kDifficultyTolerance) ++matched;
  }

  std::printf("  agreement with Anki: %d/%d cards\n", matched, study::vectors::kCardCount);
  if (matched != kExpectedMatches) {
    std::printf("  FAIL: expected %d matches, got %d.\n", kExpectedMatches, matched);
    std::printf("        If this went UP, the model improved -- update kExpectedMatches\n");
    std::printf("        and say what changed. If it went DOWN, something regressed.\n");
    ++failures;
  }
  ++checks;
}

void testForgettingCurve() {
  const study::Fsrs fsrs(kCurrentDeckParams, 0.9f);
  study::Memory m;
  m.learned = true;
  m.stability = 10.0f;
  m.difficulty = 5.0f;

  // Stability is defined as the point where recall has decayed to 90%. If this
  // drifts, every interval the app displays is wrong by the same factor.
  check(std::fabs(fsrs.retrievability(m, 10.0f) - 0.9f) < 0.001f, "R(S) == 0.9");
  check(std::fabs(fsrs.retrievability(m, 0.0f) - 1.0f) < 1e-6f, "R(0) == 1.0");
  check(fsrs.retrievability(m, 100.0f) < fsrs.retrievability(m, 10.0f), "R decreases with time");

  // ... and the interval is that definition run backwards.
  check(fsrs.intervalDays(m) == 10, "interval at 90% retention == stability");

  study::Fsrs strict(kCurrentDeckParams, 0.97f);
  check(strict.intervalDays(m) < fsrs.intervalDays(m), "higher retention target shortens interval");
}

void testMonotonicity() {
  const study::Fsrs fsrs(kCurrentDeckParams, 0.9f);
  study::Memory m;
  m.learned = true;
  m.stability = 20.0f;
  m.difficulty = 5.0f;

  int iv[4];
  fsrs.previewIntervals(m, 20, iv);
  // The four buttons must be ordered, or the card UI is lying to the user.
  check(iv[0] <= iv[1], "Again <= Hard");
  check(iv[1] <= iv[2], "Hard <= Good");
  check(iv[2] <= iv[3], "Good <= Easy");
  check(iv[0] >= 1, "no zero-day interval");

  // A lapse must never be rewarded with more stability than it started with.
  const study::Memory lapsed = fsrs.review(m, study::Rating::Again, 20);
  check(lapsed.stability <= m.stability, "Again never raises stability");
  check(lapsed.difficulty > m.difficulty, "Again raises difficulty");

  const study::Memory easy = fsrs.review(m, study::Rating::Easy, 20);
  check(easy.stability > m.stability, "Easy raises stability");
  check(easy.difficulty < m.difficulty, "Easy lowers difficulty");
}

void testBounds() {
  const study::Fsrs fsrs(kCurrentDeckParams, 0.9f);
  study::Memory m;

  // Difficulty is a 1..10 scale and nothing may leave it, however long the
  // streak. A saturated card is the common case in a 5000-card deck.
  for (int i = 0; i < 200; ++i) m = fsrs.review(m, study::Rating::Again, 1);
  check(m.difficulty <= 10.0f && m.difficulty >= 1.0f, "difficulty stays in 1..10 under Again");
  check(m.stability >= 0.01f, "stability stays positive under Again");

  study::Memory e;
  for (int i = 0; i < 200; ++i) e = fsrs.review(e, study::Rating::Easy, 365);
  check(e.difficulty <= 10.0f && e.difficulty >= 1.0f, "difficulty stays in 1..10 under Easy");

  // Without a cap, a long Easy streak runs past any sane review horizon.
  study::Fsrs capped(kCurrentDeckParams, 0.9f);
  capped.setMaximumInterval(365);
  check(capped.intervalDays(e) <= 365, "maximum interval is respected");
}

void testFirstReview() {
  const study::Fsrs fsrs(kCurrentDeckParams, 0.9f);
  const study::Memory fresh;
  check(!fresh.learned, "a new card is not learned");
  check(std::fabs(fsrs.retrievability(fresh, 30.0f) - 1.0f) < 1e-6f, "unseen card has R == 1");

  // The first review takes its stability straight from w[0..3], so the four
  // buttons must produce four increasing starting points.
  float prev = 0.0f;
  for (int r = 1; r <= 4; ++r) {
    const study::Memory m = fsrs.review(fresh, static_cast<study::Rating>(r), 0);
    check(m.learned, "review marks the card learned");
    check(m.stability > prev, "initial stability increases with rating");
    prev = m.stability;
  }
}

}  // namespace

int main() {
  std::printf("StudyFsrs\n");
  testAgainstCollection();
  testForgettingCurve();
  testMonotonicity();
  testBounds();
  testFirstReview();
  std::printf("%s %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}
