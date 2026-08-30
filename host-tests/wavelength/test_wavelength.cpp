// WAVELENGTH rules tests.
//
// These encode the decisions that were argued out before any code existed, so
// that reversing one is a red suite rather than a quiet change of game. The
// two that matter most are exhaustive rather than sampled: no slot is
// dominated, and a perfect round can never be beaten by a worse one.

#include <cstdio>
#include <cstdlib>

#include "WavelengthCore.h"

namespace {

int checks = 0;
int failures = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL: %s\n", what);
  }
}

void checkEq(const int got, const int want, const char* what) {
  ++checks;
  if (got != want) {
    ++failures;
    std::printf("  FAIL: %s (got %d, want %d)\n", what, got, want);
  }
}

using namespace wavelength;

void testScoreCurve() {
  checkEq(scoreForGuess(10, 10), kPointsExact, "exact hit");
  checkEq(scoreForGuess(10, 11), kPointsOffByOne, "off by one, above");
  checkEq(scoreForGuess(10, 9), kPointsOffByOne, "off by one, below");
  checkEq(scoreForGuess(10, 12), kPointsOffByTwo, "off by two, above");
  checkEq(scoreForGuess(10, 8), kPointsOffByTwo, "off by two, below");
  checkEq(scoreForGuess(10, 13), 0, "off by three scores nothing");
  checkEq(scoreForGuess(1, 20), 0, "opposite ends score nothing");

  // The curve must be steep, not flat: off-by-two reads as "right part of the
  // board", so it cannot be worth half of telepathy.
  check(kPointsExact > kPointsOffByOne, "exact beats off by one");
  check(kPointsOffByOne > kPointsOffByTwo, "off by one beats off by two");
  check(kPointsExact >= kPointsOffByOne * 2 - 1, "the curve is steep, not flat");
}

void testBandWidth() {
  // Five slots score around a central target, and the band clips rather than
  // wraps at the ends.
  for (int target = 1; target <= kSlots; ++target) {
    int scoring = 0;
    for (int guess = 1; guess <= kSlots; ++guess)
      if (scoreForGuess(guess, target) > 0) ++scoring;
    const int lo = target - kBandRadius < 1 ? 1 : target - kBandRadius;
    const int hi = target + kBandRadius > kSlots ? kSlots : target + kBandRadius;
    checkEq(scoring, hi - lo + 1, "band width for this target");
  }
}

void testEndCall() {
  check(endCallCorrect(10, 15, Call::TowardTop), "target above, called top");
  check(!endCallCorrect(10, 15, Call::TowardBottom), "target above, called bottom");
  check(endCallCorrect(10, 5, Call::TowardBottom), "target below, called bottom");
  check(!endCallCorrect(10, 5, Call::TowardTop), "target below, called top");

  // Locked exactly: the call was made before the reveal and cannot have been
  // right either way, so it counts. This is the rule that keeps scoring
  // monotone; see testPerfectRoundIsUnbeatable.
  check(endCallCorrect(10, 10, Call::TowardTop), "exact lock, called top, counts");
  check(endCallCorrect(10, 10, Call::TowardBottom), "exact lock, called bottom, counts");
}

void testPerfectRoundIsUnbeatable() {
  // Exhaustive. A round that landed exactly must score strictly more than any
  // round that did not, whichever way either called. Without the exact-lock
  // rule above, an off-by-one with a correct call would beat a perfect round
  // whose call could not be right, and nobody could explain why.
  const Call calls[] = {Call::TowardTop, Call::TowardBottom};
  for (int target = 1; target <= kSlots; ++target) {
    for (const Call perfectCall : calls) {
      const int perfect = scoreRound(target, target, perfectCall);
      for (int guess = 1; guess <= kSlots; ++guess) {
        if (guess == target) continue;
        for (const Call call : calls)
          check(perfect > scoreRound(guess, target, call), "a perfect round is unbeatable");
      }
    }
  }
}

void testNoSlotIsDominated() {
  // The reason the target is not kept away from the ends. Every slot must be
  // the uniquely best guess for at least one target, or a player who noticed
  // would never guess it and the strip would be shorter than it looks.
  for (int guess = 1; guess <= kSlots; ++guess) {
    bool uniquelyBestSomewhere = false;
    for (int target = 1; target <= kSlots && !uniquelyBestSomewhere; ++target) {
      const int mine = scoreForGuess(guess, target);
      if (mine == 0) continue;
      bool strictlyBest = true;
      for (int other = 1; other <= kSlots && strictlyBest; ++other)
        if (other != guess && scoreForGuess(other, target) >= mine) strictlyBest = false;
      if (strictlyBest) uniquelyBestSomewhere = true;
    }
    check(uniquelyBestSomewhere, "this slot is worth guessing");
  }
}

void testTargetRangeAndSpread() {
  Rng rng(12345);
  int hits[kSlots + 1] = {};
  const int draws = 200000;
  for (int i = 0; i < draws; ++i) {
    const int t = drawTarget(rng);
    check(t >= 1 && t <= kSlots, "target is on the strip");
    if (t >= 1 && t <= kSlots) ++hits[t];
  }
  // Every slot including both ends must be reachable, and roughly evenly.
  const int expected = draws / kSlots;
  for (int slot = 1; slot <= kSlots; ++slot) {
    check(hits[slot] > expected / 2, "this slot comes up");
    check(hits[slot] < expected * 2, "this slot does not come up too often");
  }
}

void testPracticeRound() {
  Session s;
  check(s.isPractice(), "round one is practice");
  const int gained = s.record(10, 10, Call::TowardTop);
  checkEq(gained, 0, "a perfect practice round still scores nothing");
  checkEq(s.total, 0, "practice adds nothing to the total");
  checkEq(s.scoredRounds, 0, "practice is not a scored round");
  check(!s.isPractice(), "round two is real");

  const int real = s.record(10, 10, Call::TowardTop);
  checkEq(real, kPointsExact + kPointsEndCall, "a perfect real round pays out");
  checkEq(s.total, kPointsExact + kPointsEndCall, "the total moved");
  checkEq(s.scoredRounds, 1, "one scored round");
}

void testAverage() {
  Session s;
  checkEq(s.averageTenths(), 0, "no average before any scored round");
  s.record(1, 20, Call::TowardTop);  // practice, ignored
  s.record(10, 10, Call::TowardTop);
  checkEq(s.averageTenths(), 60, "one perfect round averages 6.0");
  s.record(1, 20, Call::TowardBottom);  // wide of the mark and called wrong: 0
  checkEq(s.averageTenths(), 30, "a scoreless round halves it to 3.0");
}

void testMissedRoundStillEarnsTheCall() {
  // Being nowhere near and still knowing which way you were wrong is worth the
  // point, and that is the whole reason the end-call exists: it gives the
  // person who lost the argument something to be right about.
  checkEq(scoreForGuess(1, 20), 0, "the guess itself scores nothing");
  checkEq(scoreRound(1, 20, Call::TowardTop), kPointsEndCall, "the call still pays");
  checkEq(scoreRound(1, 20, Call::TowardBottom), 0, "calling it the wrong way pays nothing");
}

void testDeckNeverRepeats() {
  const int pairs = 60;
  Deck deck(pairs);
  Rng rng(99);
  bool dealt[pairs] = {};
  int rounds = 0;
  for (;;) {
    int choice[2];
    const int n = deck.dealChoice(rng, choice);
    if (n == 0) break;
    check(choice[0] >= 0 && choice[0] < pairs, "first choice is a real pair");
    check(!deck.isSeen(choice[0]), "first choice is unseen");
    if (n == 2) {
      check(choice[1] >= 0 && choice[1] < pairs, "second choice is a real pair");
      check(!deck.isSeen(choice[1]), "second choice is unseen");
      check(choice[0] != choice[1], "the two choices differ");
    }
    // The clue-giver picks one; the other goes back in the pool.
    const int picked = choice[0];
    check(!dealt[picked], "a spectrum never comes back in one session");
    dealt[picked] = true;
    deck.markSeen(picked);
    ++rounds;
    if (rounds > pairs + 5) break;  // guard against a deck that never empties
  }
  checkEq(rounds, pairs, "the whole deck gets used exactly once");
  checkEq(deck.unseenCount(), 0, "and then it is empty");

  deck.forgetSeen();
  checkEq(deck.unseenCount(), pairs, "forgetting brings the whole deck back");
}

void testDeckEndgame() {
  Deck deck(2);
  Rng rng(7);
  int choice[2];
  checkEq(deck.dealChoice(rng, choice), 2, "two pairs left deals a choice of two");
  deck.markSeen(choice[0]);
  checkEq(deck.dealChoice(rng, choice), 1, "one pair left deals one, not a fake choice");
  check(choice[1] == -1, "and says so with an empty second slot");
  deck.markSeen(choice[0]);
  checkEq(deck.dealChoice(rng, choice), 0, "an empty deck deals nothing");
}

void testDeckSpread() {
  // dealChoice must not favour the front of the deck, or the same few spectra
  // open every session.
  const int pairs = 20;
  Rng rng(4242);
  int firstSeen[pairs] = {};
  for (int trial = 0; trial < 20000; ++trial) {
    Deck deck(pairs);
    int choice[2];
    deck.dealChoice(rng, choice);
    ++firstSeen[choice[0]];
  }
  const int expected = 20000 / pairs;
  for (int i = 0; i < pairs; ++i) {
    check(firstSeen[i] > expected / 2, "this pair opens sometimes");
    check(firstSeen[i] < expected * 2, "this pair does not open too often");
  }
}

}  // namespace

int main() {
  std::printf("WAVELENGTH rules\n");
  testScoreCurve();
  testBandWidth();
  testEndCall();
  testPerfectRoundIsUnbeatable();
  testNoSlotIsDominated();
  testTargetRangeAndSpread();
  testPracticeRound();
  testAverage();
  testMissedRoundStillEarnsTheCall();
  testDeckNeverRepeats();
  testDeckEndgame();
  testDeckSpread();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
