// Insider rules tests. Freestanding: no device, no PlatformIO.
//
// This game has almost no rules, and the ones it has are statistical: the deal
// is only correct if every seat is equally likely to hold the Insider and if
// "nobody is the Insider" happens exactly one time in n. Neither can be checked
// by a spot test, and both are the kind of thing that stays plausible while
// being wrong -- a modulo shuffle looks fine and quietly favours the low seats.
// So the deal is the equivalent of perft here: a hundred thousand rounds per
// table size, counted against the exact expected frequency.
//
// The other property worth exhausting is the deck: it must deal all 560 words
// before repeating any, which is a claim about a whole lap rather than about
// any one draw.

#include <cstdio>
#include <cstring>

#include "../../src/apps_local/insider/InsiderCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_insider.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace insider;

// ---------------------------------------------------------------------------
// The deal

void testDealShape() {
  // Whatever the seed and whatever the table, the shape is fixed: exactly one
  // Master, at most one Insider, and everyone else a Citizen.
  for (int players = kMinPlayers; players <= kMaxPlayers; ++players) {
    for (uint32_t seed = 1; seed <= 500; ++seed) {
      Rng rng(seed * 2654435761u + players);
      Round round;
      round.deal(players, 0, rng);

      int masters = 0;
      int insiders = 0;
      int citizens = 0;
      for (int seat = 0; seat < players; ++seat) {
        switch (round.roleOf(seat)) {
          case Role::Master:
            ++masters;
            break;
          case Role::Insider:
            ++insiders;
            break;
          case Role::Citizen:
            ++citizens;
            break;
        }
      }
      if (masters != 1 || insiders > 1 || masters + insiders + citizens != players) {
        CHECK(masters == 1);
        CHECK(insiders <= 1);
        CHECK(masters + insiders + citizens == players);
        return;  // one report is enough; do not print 500 of them
      }
      // The accessors have to agree with the table they read.
      if (round.roleOf(round.masterSeat()) != Role::Master) {
        CHECK(round.roleOf(round.masterSeat()) == Role::Master);
        return;
      }
      if (insiders == 1 && round.roleOf(round.insiderSeat()) != Role::Insider) {
        CHECK(round.roleOf(round.insiderSeat()) == Role::Insider);
        return;
      }
      if (insiders == 0 && round.insiderSeat() != kNoInsider) {
        CHECK(round.insiderSeat() == kNoInsider);
        return;
      }
    }
  }
  CHECK(true);  // the loops above assert by exception; record one pass
}

void testNoInsiderFrequency() {
  // The variant's whole promise: with n at the table, the discarded role is the
  // Insider exactly one time in n. Off by a factor of two here and the game is
  // either never worth calling "nobody" or always worth it.
  constexpr int kRounds = 100000;
  for (int players = kMinPlayers; players <= kMaxPlayers; ++players) {
    Rng rng(0xC0FFEEu + static_cast<uint32_t>(players));
    int without = 0;
    for (int i = 0; i < kRounds; ++i) {
      Round round;
      round.deal(players, 0, rng);
      if (!round.hasInsider()) ++without;
    }
    const double expected = 1.0 / players;
    const double actual = static_cast<double>(without) / kRounds;
    // Three sigma on a binomial at this n is well under 0.005 for every table
    // size here, so this band is generous and still catches a wrong divisor.
    const bool close = actual > expected - 0.006 && actual < expected + 0.006;
    if (!close) std::printf("  players=%d expected %.4f got %.4f\n", players, expected, actual);
    CHECK(close);
  }
}

void testSeatsAreEquallyLikely() {
  // A biased shuffle is the classic silent bug: the game still works, and one
  // chair is quietly the Insider more often than the others all evening.
  constexpr int kRounds = 200000;
  for (int players = kMinPlayers; players <= kMaxPlayers; ++players) {
    Rng rng(0x5EEDu * static_cast<uint32_t>(players) + 7u);
    int insiderAt[kMaxPlayers] = {};
    int masterAt[kMaxPlayers] = {};
    for (int i = 0; i < kRounds; ++i) {
      Round round;
      round.deal(players, 0, rng);
      if (round.hasInsider()) ++insiderAt[round.insiderSeat()];
      ++masterAt[round.masterSeat()];
    }
    // The Master is in every deal, so each seat should hold it 1/n of the time.
    const double masterExpected = static_cast<double>(kRounds) / players;
    // The Insider appears in (n-1)/n of deals, spread over n seats.
    const double insiderExpected = static_cast<double>(kRounds) * (players - 1) / players / players;
    for (int seat = 0; seat < players; ++seat) {
      const double masterOff = (masterAt[seat] - masterExpected) / masterExpected;
      const double insiderOff = (insiderAt[seat] - insiderExpected) / insiderExpected;
      const bool flat = masterOff > -0.05 && masterOff < 0.05 && insiderOff > -0.05 && insiderOff < 0.05;
      if (!flat) {
        std::printf("  players=%d seat=%d master %+.3f insider %+.3f\n", players, seat, masterOff, insiderOff);
      }
      CHECK(flat);
    }
  }
}

// ---------------------------------------------------------------------------
// Judging

void testJudge() {
  Rng rng(12345);
  // Find a deal that has an Insider, and one that does not, so both branches
  // are judged against a real deal rather than a hand-built one.
  Round withInsider;
  Round without;
  bool haveWith = false;
  bool haveWithout = false;
  for (int i = 0; i < 1000 && !(haveWith && haveWithout); ++i) {
    Round round;
    round.deal(5, 3, rng);
    if (round.hasInsider() && !haveWith) {
      withInsider = round;
      haveWith = true;
    }
    if (!round.hasInsider() && !haveWithout) {
      without = round;
      haveWithout = true;
    }
  }
  CHECK(haveWith);
  CHECK(haveWithout);

  // Naming the Insider wins.
  CHECK(withInsider.judge(withInsider.insiderSeat()) == Outcome::Won);
  // Naming anybody else loses, including the Master.
  for (int seat = 0; seat < withInsider.players(); ++seat) {
    if (seat == withInsider.insiderSeat()) continue;
    CHECK(withInsider.judge(seat) == Outcome::Lost);
  }
  // Saying "nobody" when somebody was there loses.
  CHECK(withInsider.judge(kNoInsider) == Outcome::Lost);

  // And the mirror image, on the deal that really had no Insider.
  CHECK(without.judge(kNoInsider) == Outcome::Won);
  for (int seat = 0; seat < without.players(); ++seat) {
    CHECK(without.judge(seat) == Outcome::Lost);
  }
}

// ---------------------------------------------------------------------------
// The deck

void testDeckDealsAFullLap() {
  // The claim is about a whole lap, not about one draw: 560 draws must produce
  // 560 distinct words, and the 561st must start a fresh lap rather than fail.
  Deck deck;
  Rng rng(99);
  bool drawn[kWordCount] = {};
  for (int i = 0; i < kWordCount; ++i) {
    const int index = deck.draw(rng);
    if (index < 0 || index >= kWordCount || drawn[index]) {
      CHECK(index >= 0 && index < kWordCount);
      CHECK(!drawn[index]);
      return;
    }
    drawn[index] = true;
    if (deck.remaining() != kWordCount - i - 1) {
      CHECK(deck.remaining() == kWordCount - i - 1);
      return;
    }
  }
  CHECK(deck.remaining() == 0);

  // The lap wraps silently, which is the behaviour that replaced the
  // prototype's "Reset Word List" button.
  const int wrapped = deck.draw(rng);
  CHECK(wrapped >= 0 && wrapped < kWordCount);
  CHECK(deck.remaining() == kWordCount - 1);
}

void testDeckMaskRoundTrips() {
  Deck deck;
  Rng rng(4242);
  for (int i = 0; i < 37; ++i) deck.draw(rng);
  CHECK(deck.remaining() == kWordCount - 37);

  uint8_t copy[Deck::kMaskBytes];
  std::memcpy(copy, deck.mask(), sizeof(copy));

  Deck restored;
  restored.setMask(copy);
  CHECK(restored.remaining() == kWordCount - 37);
  bool same = true;
  for (int i = 0; i < kWordCount; ++i) same = same && restored.seen(i) == deck.seen(i);
  CHECK(same);
}

void testDeckIgnoresBitsPastTheEnd() {
  // A save written by a build with a longer word list -- or simply a corrupted
  // one -- must not leave the deck permanently short of a full lap.
  uint8_t all[Deck::kMaskBytes];
  std::memset(all, 0xFF, sizeof(all));
  Deck deck;
  deck.setMask(all);
  CHECK(deck.remaining() == 0);

  uint8_t none[Deck::kMaskBytes];
  std::memset(none, 0, sizeof(none));
  // Set only the padding bits above the last real word.
  for (int bit = kWordCount; bit < Deck::kMaskBytes * 8; ++bit) {
    none[bit >> 3] |= static_cast<uint8_t>(1u << (bit & 7));
  }
  Deck padded;
  padded.setMask(none);
  CHECK(padded.remaining() == kWordCount);
}

// ---------------------------------------------------------------------------
// The record

void testRecord() {
  Record record;
  CHECK(record.rounds == 0);
  for (int i = 0; i < 16; ++i) CHECK(record.at(i) == Outcome::Unplayed);

  record.push(Outcome::Won);
  record.push(Outcome::Lost);
  record.push(Outcome::OutOfTime);
  CHECK(record.rounds == 3);
  CHECK(record.won == 1);
  CHECK(record.lost == 1);
  CHECK(record.outOfTime == 1);
  // The newest sits at the far end, so the ornament reads in the order played.
  CHECK(record.at(15) == Outcome::OutOfTime);
  CHECK(record.at(14) == Outcome::Lost);
  CHECK(record.at(13) == Outcome::Won);
  CHECK(record.at(12) == Outcome::Unplayed);

  // Seventeen rounds: the first must have fallen off the end.
  Record rolling;
  for (int i = 0; i < 16; ++i) rolling.push(Outcome::Won);
  rolling.push(Outcome::Lost);
  CHECK(rolling.at(15) == Outcome::Lost);
  for (int i = 0; i < 15; ++i) CHECK(rolling.at(i) == Outcome::Won);
  CHECK(rolling.rounds == 17);
}

// ---------------------------------------------------------------------------
// The clock

void testClockTick() {
  // Non-decreasing as time runs out, every single second of the round. If this
  // ever went backwards the screen would repaint on a tick that had already
  // been drawn, or worse, stop repainting.
  int previous = clockTick(kQuestionSeconds);
  CHECK(previous == 0);
  bool monotonic = true;
  for (int seconds = kQuestionSeconds; seconds >= 0; --seconds) {
    const int tick = clockTick(seconds);
    monotonic = monotonic && tick >= previous;
    previous = tick;
  }
  CHECK(monotonic);

  // And the budget it exists to enforce: a whole round has to cost about two
  // dozen repaints, not three hundred. Counting distinct ticks IS the refresh
  // count, because the activity repaints exactly when this number moves.
  int repaints = 0;
  int last = clockTick(kQuestionSeconds);
  for (int seconds = kQuestionSeconds - 1; seconds >= 0; --seconds) {
    const int tick = clockTick(seconds);
    if (tick != last) ++repaints;
    last = tick;
  }
  if (repaints > 40) std::printf("  clock would repaint %d times\n", repaints);
  CHECK(repaints > 10);
  CHECK(repaints <= 40);

  // The last minute has to move faster than the rest, or the ending has no
  // shape. Five-second steps against fifteen-second ones.
  CHECK(clockTick(61) != clockTick(46));
  CHECK(clockTick(60) != clockTick(55));
  CHECK(clockTick(20) != clockTick(15));
  CHECK(clockTick(20) == clockTick(19));
}

// ---------------------------------------------------------------------------
// The words

void testWordsAreDrawable() {
  // The Toybox face is subset to ASCII and a glyph it does not have draws as
  // nothing at all -- no box, no log line. A non-ASCII word would simply be a
  // blank card that nobody could guess.
  for (int i = 0; i < kWordCount; ++i) {
    const char* word = kWords[i];
    bool ok = word != nullptr && word[0] != '\0';
    for (const char* c = word; ok && *c; ++c) {
      ok = *c >= 'A' && *c <= 'Z';
    }
    if (!ok) {
      std::printf("  word %d is not plain uppercase ASCII: %s\n", i, word ? word : "(null)");
      CHECK(ok);
      return;
    }
    if (static_cast<int>(std::strlen(word)) > kMaxWordLen) {
      std::printf("  word %d is longer than kMaxWordLen: %s\n", i, word);
      CHECK(false);
      return;
    }
  }
  CHECK(true);

  // No duplicates, or the no-repeat deck is lying.
  bool unique = true;
  for (int i = 0; i < kWordCount && unique; ++i) {
    for (int j = i + 1; j < kWordCount; ++j) {
      if (std::strcmp(kWords[i], kWords[j]) == 0) {
        std::printf("  duplicate word: %s\n", kWords[i]);
        unique = false;
        break;
      }
    }
  }
  CHECK(unique);
}

}  // namespace

int main() {
  testDealShape();
  testNoInsiderFrequency();
  testSeatsAreEquallyLikely();
  testJudge();
  testDeckDealsAFullLap();
  testDeckMaskRoundTrips();
  testDeckIgnoresBitsPastTheEnd();
  testRecord();
  testClockTick();
  testWordsAreDrawable();

  std::printf("insider: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
