// WAVELENGTH rules tests.
//
// These encode the decisions that were argued out before any code existed, so
// that reversing one is a red suite rather than a quiet change of game. The
// two that matter most are exhaustive rather than sampled: no slot is
// dominated, and a perfect round can never be beaten by a worse one.

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

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
  check(scoreRound(10, 10, Call::TowardTop, Mode::Teams) == kPointsExact, "an exact lock pays the table's EXACT");
  check(scoreRound(10, 10, Call::TowardBottom, Mode::Teams) == kPointsExact, "and pays it whichever way it was called");
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
      const int perfect = scoreRound(target, target, perfectCall, Mode::Teams);
      for (int guess = 1; guess <= kSlots; ++guess) {
        if (guess == target) continue;
        for (const Call call : calls)
          check(perfect > scoreRound(guess, target, call, Mode::Teams), "a perfect round is unbeatable");
      }
    }
  }
}

void testCoOpNeverConsultsTheCall() {
  // Exhaustive, and the point is the METHOD: co-op is proved not to read the
  // call by scoring every position both ways and requiring the same answer,
  // rather than by reading the code and believing it. A path that is bypassed
  // rather than absent is the failure this guards -- it looks handled and stays
  // live, which is how a bounded input path went on catching taps after the
  // other one was fixed.
  for (int target = 1; target <= kSlots; ++target) {
    for (int guess = 1; guess <= kSlots; ++guess) {
      const int top = scoreRound(guess, target, Call::TowardTop, Mode::CoOp);
      const int bottom = scoreRound(guess, target, Call::TowardBottom, Mode::CoOp);
      check(top == bottom, "co-op scores the same whichever way the call went");
      check(top == scoreForGuess(guess, target), "co-op scores on distance alone");
      check(top <= kPointsExact, "co-op cannot exceed the ceiling its screens promise");
    }
  }
}

void testBothModesKeepTheirProperties() {
  // The two properties the game is built on hold in BOTH modes. Teams keeps a
  // live configuration rather than dead code, so it has to keep being true.
  for (const Mode mode : {Mode::CoOp, Mode::Teams}) {
    const Call calls[] = {Call::TowardTop, Call::TowardBottom};
    for (int target = 1; target <= kSlots; ++target) {
      for (const Call perfectCall : calls) {
        const int perfect = scoreRound(target, target, perfectCall, mode);
        for (int guess = 1; guess <= kSlots; ++guess) {
          if (guess == target) continue;
          for (const Call call : calls)
            check(perfect > scoreRound(guess, target, call, mode), "a perfect round is unbeatable in both modes");
        }
      }
    }
    for (int slot = 1; slot <= kSlots; ++slot) {
      bool uniquelyBest = false;
      for (int target = 1; target <= kSlots && !uniquelyBest; ++target) {
        const int mine = scoreForGuess(slot, target);
        bool strictlyBest = true;
        for (int other = 1; other <= kSlots; ++other)
          if (other != slot && scoreForGuess(other, target) >= mine) strictlyBest = false;
        if (strictlyBest) uniquelyBest = true;
      }
      check(uniquelyBest, "no slot is dominated in either mode");
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
  const int gained = s.record(10, 10, Call::TowardTop, Mode::Teams);
  checkEq(gained, 0, "a perfect practice round still scores nothing");
  checkEq(s.total, 0, "practice adds nothing to the total");
  checkEq(s.scoredRounds, 0, "practice is not a scored round");
  check(!s.isPractice(), "round two is real");

  const int real = s.record(10, 10, Call::TowardTop, Mode::Teams);
  checkEq(real, kPointsExact, "a perfect real round pays the table's EXACT, with no call bonus");
  checkEq(s.total, kPointsExact, "the total moved");
  checkEq(s.scoredRounds, 1, "one scored round");
}

void testAverage() {
  Session s;
  checkEq(s.averageTenths(), 0, "no average before any scored round");
  s.record(1, 20, Call::TowardTop, Mode::Teams);  // practice, ignored
  s.record(10, 10, Call::TowardTop, Mode::Teams);
  checkEq(s.averageTenths(), 50, "one perfect round averages 5.0");
  s.record(1, 20, Call::TowardBottom, Mode::Teams);  // wide of the mark and called wrong: 0
  checkEq(s.averageTenths(), 25, "a scoreless round halves it to 2.5");
}

void testMissedRoundStillEarnsTheCall() {
  // Being nowhere near and still knowing which way you were wrong is worth the
  // point, and that is the whole reason the end-call exists: it gives the
  // person who lost the argument something to be right about.
  checkEq(scoreForGuess(1, 20), 0, "the guess itself scores nothing");
  checkEq(scoreRound(1, 20, Call::TowardTop, Mode::Teams), kPointsEndCall, "the call still pays");
  checkEq(scoreRound(1, 20, Call::TowardBottom, Mode::Teams), 0, "calling it the wrong way pays nothing");
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

// ---------------------------------------------------------------------------
// THE SAVE FILE.
//
// This file's header has always claimed these tests existed. They did not: the
// run script compiled WavelengthCore.cpp and nothing else, so the layer that
// carries a game in progress across a chip reset was the one layer here never
// executed by anything but a device. It is now compiled and run below.

// A save with something in every field, so a round trip that drops one is a
// failure rather than a coincidence of zeroes.
Saved populated() {
  Saved s;
  s.record.rounds = 41;
  s.record.points = 97;
  for (int i = 0; i < kBucketCount; ++i) s.record.buckets[i] = static_cast<uint16_t>(3 + i);
  s.record.bestRoundTenths = 34;
  for (int i = 0; i < kSeenWords; ++i) s.seen[i] = 0xA5A50000u + static_cast<uint32_t>(i);
  s.session.round = 7;
  s.session.total = 23;
  s.session.scoredRounds = 6;
  s.abandoned = 2;
  s.sessionStarted = true;
  s.bootId = 0xC0FFEEu;
  s.savedAt = kClockFloor + 86400;
  s.screen = 6;
  s.resumeScreen = 5;
  s.spectrum = 88;
  s.choice[0] = 12;
  s.choice[1] = 99;
  s.dealt = 2;
  s.target = 14;
  s.guess = 11;
  s.lastPoints = 3;
  s.hasPeeked = true;
  s.practiceRound = false;
  s.callWasRight = true;
  s.abandonedRound = false;
  return s;
}

void testSaveRoundTrip() {
  const Saved in = populated();
  uint8_t bytes[kSaveBytes] = {};
  checkEq(static_cast<int>(pack(in, bytes, sizeof(bytes))), static_cast<int>(kSaveBytes), "pack writes the whole file");
  checkEq(static_cast<int>(pack(in, bytes, kSaveBytes - 1)), 0, "pack refuses a buffer it would overrun");

  Saved out;
  check(unpack(bytes, kSaveBytes, out), "a file this build wrote is a file this build reads");
  checkEq(out.record.rounds, in.record.rounds, "the all-time round count survives");
  checkEq(out.record.points, in.record.points, "the all-time points survive");
  checkEq(out.record.bestRoundTenths, in.record.bestRoundTenths, "the best session survives");
  for (int i = 0; i < kBucketCount; ++i) checkEq(out.record.buckets[i], in.record.buckets[i], "a bucket survives");
  for (int i = 0; i < kSeenWords; ++i)
    checkEq(static_cast<int>(out.seen[i] == in.seen[i]), 1, "a word of the seen deck survives");
  checkEq(out.session.round, in.session.round, "the round number survives");
  checkEq(out.session.total, in.session.total, "the session score survives");
  checkEq(out.session.scoredRounds, in.session.scoredRounds, "the scored round count survives");
  checkEq(out.abandoned, in.abandoned, "the abandon count survives");
  check(out.sessionStarted == in.sessionStarted, "sessionStarted survives");
  checkEq(static_cast<int>(out.bootId), static_cast<int>(in.bootId), "the boot id survives");
  checkEq(static_cast<int>(out.savedAt - kClockFloor), 86400, "the clock stamp survives");
  checkEq(out.screen, in.screen, "the screen survives");
  checkEq(out.resumeScreen, in.resumeScreen, "the paused-from screen survives");
  checkEq(out.spectrum, in.spectrum, "the spectrum survives");
  checkEq(out.choice[0], in.choice[0], "the first offered pair survives");
  checkEq(out.choice[1], in.choice[1], "the second offered pair survives");
  checkEq(out.dealt, in.dealt, "the dealt count survives");
  checkEq(out.target, in.target, "the hidden number survives");
  checkEq(out.guess, in.guess, "the marker survives");
  checkEq(out.lastPoints, in.lastPoints, "the last round's points survive");
  check(out.hasPeeked == in.hasPeeked, "hasPeeked survives");
  check(out.practiceRound == in.practiceRound, "practiceRound survives");
  check(out.callWasRight == in.callWasRight, "callWasRight survives");
  check(out.abandonedRound == in.abandonedRound, "abandonedRound survives");
}

// Bytes that are not a save this build reads must be REFUSED rather than
// half-believed. The shipped Hacker News bug emptied a real user's reading list
// by trusting fields it never validated.
void testSaveRefusesWhatItCannotRead() {
  const Saved in = populated();
  uint8_t bytes[kSaveBytes] = {};
  pack(in, bytes, sizeof(bytes));

  Saved out;
  check(!unpack(nullptr, kSaveBytes, out), "a null file is refused");
  check(!unpack(bytes, 0, out), "an empty file is refused");
  check(!unpack(bytes, kLegacyBytes - 1, out), "a file too short for even the record is refused");

  uint8_t future[kSaveBytes] = {};
  for (size_t i = 0; i < kSaveBytes; ++i) future[i] = bytes[i];
  future[0] = kSaveVersion + 1;
  check(!unpack(future, kSaveBytes, out), "a version from a later build is refused, not guessed at");

  // A round whose numbers are outside the rules is dropped and the record kept.
  uint8_t bad[kSaveBytes] = {};
  for (size_t i = 0; i < kSaveBytes; ++i) bad[i] = bytes[i];
  bad[kSessionBytes - 3] = kSlots + 1;  // the target byte
  Saved dropped;
  check(unpack(bad, kSaveBytes, dropped), "an impossible round still loads the record");
  checkEq(dropped.record.rounds, in.record.rounds, "the record survives an impossible round");
  checkEq(dropped.screen, 0, "an impossible round lands on the front door");
  checkEq(dropped.spectrum, -1, "an impossible round keeps no spectrum");
}

// A card written by an older build still loads, and the fixtures are DERIVED
// from this build's own packer rather than typed out, so they cannot drift into
// describing a format nobody ever wrote.
void testOlderCardsStillLoad() {
  const Saved in = populated();
  uint8_t bytes[kSaveBytes] = {};
  pack(in, bytes, sizeof(bytes));

  // Version 2: everything except the boot and the clock stamp.
  uint8_t v2[kSessionBytes] = {};
  for (size_t i = 0; i < kSessionBytes; ++i) v2[i] = bytes[i];
  v2[0] = kSaveVersionSession;
  Saved out;
  check(unpack(v2, kSessionBytes, out), "a version 2 card loads");
  checkEq(out.session.round, in.session.round, "a version 2 card keeps its round number");
  checkEq(out.session.total, in.session.total, "a version 2 card keeps its score");
  checkEq(out.target, in.target, "a version 2 card keeps its hidden number");
  checkEq(static_cast<int>(out.bootId), 0, "a version 2 card carries no boot id");
  checkEq(static_cast<int>(out.savedAt), 0, "a version 2 card carries no clock stamp");

  // Version 1: the record and the deck, and no evening at all.
  uint8_t v1[kLegacyBytes] = {};
  for (size_t i = 0; i < kLegacyBytes; ++i) v1[i] = bytes[i];
  v1[0] = kSaveVersionLegacy;
  Saved old;
  check(unpack(v1, kLegacyBytes, old), "a version 1 card loads");
  checkEq(old.record.rounds, in.record.rounds, "a version 1 card keeps its record");
  checkEq(old.session.round, 1, "a version 1 card starts the evening fresh");
  checkEq(old.screen, 0, "a version 1 card has no round in flight");

  // And a v3 file cut off inside its tail is a v2 file, not a broken one.
  Saved cut;
  check(unpack(bytes, kSessionBytes, cut), "a v3 card truncated after the session block still loads");
  checkEq(cut.session.round, in.session.round, "and keeps the session it did carry");
  checkEq(static_cast<int>(cut.bootId), 0, "and claims no boot id it did not carry");
}

// THE BUG THIS WAS WRITTEN FOR. A save had no notion of going stale, so a
// completely different group opening the app days later was dropped into the
// middle of the previous group's session: round 2, someone else's score, a clue
// they never heard, and nothing on the panel saying so.
//
// The axis is the BOOT, because this device cannot measure elapsed time it can
// rely on: wake is a chip reset so millis() restarts, and the wall clock is a
// fitted part on some boards, absent on others, and only ever set by an NTP
// sync. Within one run of the chip the device has not been away, so it is the
// same room; across a reset the answer is unknown and only the table knows it.
void testAStaleSessionIsOfferedRatherThanTaken() {
  const uint32_t thisBoot = 0xC0FFEEu;
  Saved live = populated();  // written by this boot, mid-round
  check(resumeFor(live, thisBoot) == Resume::Carry, "the same boot resumes silently: Home and back costs nothing");

  Saved old = populated();
  old.bootId = 0x1234u;
  check(resumeFor(old, thisBoot) == Resume::Ask, "a session from before this boot is offered, not taken");

  Saved older = populated();
  older.bootId = 0;  // every v1 and v2 card
  check(resumeFor(older, thisBoot) == Resume::Ask, "a card that cannot say which boot wrote it is offered");
  check(resumeFor(live, 0) == Resume::Ask, "and a caller with no boot id of its own matches nothing");

  // Nothing to carry means no question. The ask must never appear over an
  // empty evening, or every launch on a fresh card opens with it.
  Saved fresh;
  fresh.bootId = 0x1234u;
  check(resumeFor(fresh, thisBoot) == Resume::Nothing, "a card with no evening on it asks nothing");
  fresh.record.rounds = 400;  // months of history, still no session
  check(resumeFor(fresh, thisBoot) == Resume::Nothing, "and the all-time record is not an evening");

  // THE REPORTED SHAPE, ONE SCREEN FURTHER OUT. A session sitting on the front
  // door has no round in flight, and the menu's own button offers to play its
  // round 7 into its score. Guarding only the round in flight would leave that
  // untouched, which is the same bug with a tap in front of it.
  Saved onTheMenu;
  onTheMenu.bootId = 0x1234u;
  onTheMenu.sessionStarted = true;
  onTheMenu.session.round = 7;
  onTheMenu.session.total = 23;
  onTheMenu.screen = 0;  // the front door
  check(resumeFor(onTheMenu, thisBoot) == Resume::Ask, "a stale session with no round in flight is still asked about");

  // Each of the four things that count as an evening, on its own.
  Saved started;
  started.bootId = 0x1234u;
  started.sessionStarted = true;
  check(resumeFor(started, thisBoot) == Resume::Ask, "a started session counts on its own");
  Saved mid;
  mid.bootId = 0x1234u;
  mid.screen = 6;
  check(resumeFor(mid, thisBoot) == Resume::Ask, "a round in flight counts on its own");
  Saved scored;
  scored.bootId = 0x1234u;
  scored.session.total = 5;
  check(resumeFor(scored, thisBoot) == Resume::Ask, "points on the board count on their own");
}

// The clock INFORMS the question and never decides it. It is a fitted part on
// some boards and absent on others, it is only ever set by an NTP sync over
// Wi-Fi, and a flat coin cell puts it back to 1970 -- so a rule that depended on
// it would behave differently on two devices sitting on the same table.
void testTheClockOnlyInforms() {
  checkEq(minutesSince(0, kClockFloor + 100000), -1, "a save with no stamp cannot be dated");
  checkEq(minutesSince(kClockFloor + 100000, 0), -1, "a device with no clock cannot date one");
  checkEq(minutesSince(kClockFloor + 100000, kClockFloor + 99999), -1, "a clock that moved backwards says nothing");
  checkEq(minutesSince(kClockFloor + 600, kClockFloor + 600), 0, "no time at all is zero minutes, not unknown");
  checkEq(minutesSince(kClockFloor, kClockFloor + 3600), 60, "an hour is sixty minutes");
  checkEq(minutesSince(kClockFloor, kClockFloor + 6 * 86400), 6 * 24 * 60, "six days is six days of minutes");

  // And the decision does not read it, whatever it says.
  Saved sameBootLongAgo = populated();
  sameBootLongAgo.savedAt = kClockFloor;
  check(resumeFor(sameBootLongAgo, sameBootLongAgo.bootId) == Resume::Carry,
        "an old stamp does not break a same-boot resume");
  Saved otherBootJustNow = populated();
  otherBootJustNow.bootId = 0x999u;
  otherBootJustNow.savedAt = kClockFloor + 100000;
  check(resumeFor(otherBootJustNow, 0xC0FFEEu) == Resume::Ask, "a fresh stamp does not buy a cross-boot resume");
}

int main() {
  std::printf("WAVELENGTH rules\n");
  testScoreCurve();
  testBandWidth();
  testEndCall();
  testPerfectRoundIsUnbeatable();
  testCoOpNeverConsultsTheCall();
  testBothModesKeepTheirProperties();
  testNoSlotIsDominated();
  testTargetRangeAndSpread();
  testPracticeRound();
  testAverage();
  testMissedRoundStillEarnsTheCall();
  testDeckNeverRepeats();
  testDeckEndgame();
  testDeckSpread();
  testSaveRoundTrip();
  testSaveRefusesWhatItCannotRead();
  testOlderCardsStillLoad();
  testAStaleSessionIsOfferedRatherThanTaken();
  testTheClockOnlyInforms();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
