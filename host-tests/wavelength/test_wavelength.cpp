// WAVELENGTH rules tests.
//
// These encode the decisions that were argued out before any code existed, so
// that reversing one is a red suite rather than a quiet change of game. The
// two that matter most are exhaustive rather than sampled: no slot is
// dominated, and a perfect round can never be beaten by a worse one.

#include <cstdio>
#include <cstdlib>

#include "WavelengthCore.h"
#include "WavelengthSave.h"

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
  check(!endCallCorrect(10, 10, Call::TowardTop), "exact lock has no side, called top pays nothing");
  check(!endCallCorrect(10, 10, Call::TowardBottom), "exact lock has no side, called bottom pays nothing");
  // The number the screens promise. An exact lock is worth exactly this.
  check(scoreRound(10, 10, Call::TowardTop) == kPointsExact, "an exact lock pays the table's EXACT");
  check(scoreRound(10, 10, Call::TowardBottom) == kPointsExact, "and pays it whichever way it was called");
}

void testPerfectRoundIsUnbeatable() {
  // Exhaustive. A round that landed exactly must score strictly more than any
  // round that did not, whichever way either called. This holds WITHOUT paying
  // the call bonus on an exact lock: the best a non-exact round reaches is
  // kPointsOffByOne + kPointsEndCall = 4, against kPointsExact = 5. The old
  // code paid the bonus anyway and justified it with this property, which it
  // never needed -- and that made an exact worth 6 while every screen said 5.
  const Call calls[] = {Call::TowardTop, Call::TowardBottom};
  for (int target = 1; target <= kSlots; ++target) {
    for (const Call perfectCall : calls) {
      const int perfect = scoreRound(target, target, perfectCall);
      for (int guess = 1; guess <= kSlots; ++guess) {
        if (guess == target) continue;
        for (const Call call : calls) check(perfect > scoreRound(guess, target, call), "a perfect round is unbeatable");
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
  checkEq(real, kPointsExact, "a perfect real round pays the table's EXACT, with no call bonus");
  checkEq(s.total, kPointsExact, "the total moved");
  checkEq(s.scoredRounds, 1, "one scored round");
}

void testAverage() {
  Session s;
  checkEq(s.averageTenths(), 0, "no average before any scored round");
  s.record(1, 20, Call::TowardTop);  // practice, ignored
  s.record(10, 10, Call::TowardTop);
  checkEq(s.averageTenths(), 50, "one perfect round averages 5.0");
  s.record(1, 20, Call::TowardBottom);  // wide of the mark and called wrong: 0
  checkEq(s.averageTenths(), 25, "a scoreless round halves it to 2.5");
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

// The save file, which is where a round in progress now lives.
//
// This is the layer the Home key and deep sleep both go through, and neither
// of them asks first: Home destroys the activity and deep sleep resets the
// chip. Before these, a live round, its hidden number and the session score
// were all lost to one keypress beside Back, and the only instrument that
// could see it was a person playing.

Saved liveRound() {
  Saved s;
  s.record.rounds = 41;
  s.record.points = 118;
  s.record.buckets[0] = 9;
  s.record.buckets[4] = 5;
  s.record.bestRoundTenths = 34;
  s.seen[0] = 0xDEADBEEFu;
  s.seen[kSeenWords - 1] = 0x0000CAFEu;
  s.session.round = 6;
  s.session.total = 17;
  s.session.scoredRounds = 4;
  s.sessionStarted = true;
  s.abandoned = 2;
  s.screen = 6;  // the dial
  s.resumeScreen = 6;
  s.spectrum = 137;
  s.choice[0] = 137;
  s.choice[1] = 91;
  s.dealt = 2;
  s.target = 14;
  s.guess = 17;
  s.lastPoints = 3;
  s.hasPeeked = true;
  s.practiceRound = false;
  s.callWasRight = true;
  s.abandonedRound = false;
  return s;
}

void testSaveRoundTrip() {
  const Saved in = liveRound();
  uint8_t bytes[kSaveBytes] = {};
  checkEq(static_cast<int>(pack(in, bytes, sizeof(bytes))), static_cast<int>(kSaveBytes), "pack fills the file");

  Saved out;
  check(unpack(bytes, kSaveBytes, out), "the file it just wrote reads back");

  // The all-time record and the deck, which version 1 already carried.
  checkEq(out.record.rounds, in.record.rounds, "rounds survive");
  checkEq(out.record.points, in.record.points, "points survive");
  checkEq(out.record.buckets[0], in.record.buckets[0], "an exact bucket survives");
  checkEq(out.record.buckets[4], in.record.buckets[4], "the wide bucket survives");
  checkEq(out.record.bestRoundTenths, in.record.bestRoundTenths, "the best average survives");
  check(out.seen[0] == in.seen[0], "the first deck word survives");
  check(out.seen[kSeenWords - 1] == in.seen[kSeenWords - 1], "the last deck word survives");

  // The evening.
  checkEq(out.session.round, in.session.round, "the round number survives");
  checkEq(out.session.total, in.session.total, "the session score survives");
  checkEq(out.session.scoredRounds, in.session.scoredRounds, "the scored count survives");
  check(out.sessionStarted, "the session is still running");
  checkEq(out.abandoned, in.abandoned, "the abandon count survives");

  // The round itself. Every one of these was thrown away by the Home key.
  checkEq(out.screen, in.screen, "the screen survives");
  checkEq(out.resumeScreen, in.resumeScreen, "what the pause resumes to survives");
  checkEq(out.spectrum, in.spectrum, "the spectrum survives");
  checkEq(out.choice[0], in.choice[0], "the first offered pair survives");
  checkEq(out.choice[1], in.choice[1], "the second offered pair survives");
  checkEq(out.dealt, in.dealt, "how many were dealt survives");
  checkEq(out.target, in.target, "THE HIDDEN NUMBER survives");
  checkEq(out.guess, in.guess, "the marker survives");
  checkEq(out.lastPoints, in.lastPoints, "the last round's points survive");
  check(out.hasPeeked, "having seen the number survives");
  check(!out.practiceRound, "not being the practice round survives");
  check(out.callWasRight, "the side call survives");
}

void testVersionOneStillLoads() {
  // A card written by any build up to v1.12.4: the record and the deck, and
  // nothing after them. It must keep its year of rounds rather than being
  // rejected wholesale, which is what a bare version check would have done.
  const Saved in = liveRound();
  uint8_t bytes[kSaveBytes] = {};
  pack(in, bytes, sizeof(bytes));
  bytes[0] = kSaveVersionLegacy;

  Saved out;
  check(unpack(bytes, kLegacyBytes, out), "a version 1 file loads");
  checkEq(out.record.rounds, in.record.rounds, "version 1 keeps its rounds");
  check(out.seen[0] == in.seen[0], "version 1 keeps its deck");
  checkEq(out.session.round, 1, "version 1 has no session to resume");
  checkEq(out.screen, 0, "version 1 opens the front door");
}

void testTruncatedFileKeepsTheRecord() {
  // A write interrupted by the battery, or an older tail. Same rule: the
  // history is still good, the round is not.
  const Saved in = liveRound();
  uint8_t bytes[kSaveBytes] = {};
  pack(in, bytes, sizeof(bytes));

  Saved out;
  check(unpack(bytes, kSaveBytes - 4, out), "a short version 2 file still loads");
  checkEq(out.record.points, in.record.points, "the record survives a short read");
  checkEq(out.screen, 0, "a short read resumes nothing");
}

void testGarbageIsRefused() {
  uint8_t bytes[kSaveBytes] = {};
  Saved out;
  bytes[0] = 99;
  check(!unpack(bytes, kSaveBytes, out), "an unknown version is refused");
  check(!unpack(bytes, 3, out), "a file too short to hold a record is refused");
  check(!unpack(nullptr, kSaveBytes, out), "no file at all is refused");
}

void testImpossibleSlotDropsTheRound() {
  // A half-written tail must not put the game on a screen describing a slot
  // the strip does not have.
  Saved in = liveRound();
  in.target = kSlots + 7;
  uint8_t bytes[kSaveBytes] = {};
  pack(in, bytes, sizeof(bytes));

  Saved out;
  check(unpack(bytes, kSaveBytes, out), "the file still loads");
  checkEq(out.screen, 0, "an impossible number drops the round");
  checkEq(out.target, 0, "and the number with it");
  checkEq(out.record.rounds, in.record.rounds, "but not the record");
}

void testEndedSessionResumesNothing() {
  // Persistence that outlives a finished session is its own bug: the round
  // would come back onto a board whose score had been cleared. START OVER
  // clears the round, and this is the shape of what it writes.
  Saved in = liveRound();
  in.session = Session{};
  in.sessionStarted = false;
  in.abandoned = 0;
  in.clearRound();
  uint8_t bytes[kSaveBytes] = {};
  pack(in, bytes, sizeof(bytes));

  Saved out;
  check(unpack(bytes, kSaveBytes, out), "the cleared file loads");
  checkEq(out.screen, 0, "nothing to resume");
  checkEq(out.session.round, 1, "back to round one");
  checkEq(out.session.total, 0, "with no score");
  check(!out.sessionStarted, "and no session running");
  checkEq(out.record.rounds, in.record.rounds, "the all-time record is NOT cleared");
  check(out.seen[0] == in.seen[0], "and neither is the seen deck");
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
  testSaveRoundTrip();
  testVersionOneStillLoads();
  testTruncatedFileKeepsTheRecord();
  testGarbageIsRefused();
  testImpossibleSlotDropsTheRound();
  testEndedSessionResumesNothing();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
