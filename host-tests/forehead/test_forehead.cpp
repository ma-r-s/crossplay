// FOREHEAD's rules and its word lists, checked without a panel. ForeheadCore is
// freestanding C++17 and the generated word table is plain data, so the whole
// thing is testable on a laptop and only the drawing needs hardware.
//
// Three kinds of test here.
//
// The first pins each rule against a hand-built state. The second soaks the
// deck over tens of thousands of draws, because the interesting deck bugs live
// in the LAST card of a lap, which a spot check reaches roughly never.
//
// The third is the one worth copying elsewhere: an exhaustive pass over every
// entry and every category slice. The generator already refuses bad
// content, but the generator is not what ships -- the committed header is, and
// a hand-edit to it would sail past a script nobody re-ran. This checks the
// artefact.

#include <cstdio>
#include <cstring>

#include "ForeheadCore.h"

using namespace forehead;

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

// ---------------------------------------------------------------------------
// The content, exhaustively.

void testEveryEntryIsDrawable() {
  for (int entry = 0; entry < kEntryCount; ++entry) {
    const char* text = kEntries[entry];
    CHECK(text != nullptr);
    if (text == nullptr) continue;
    const int length = static_cast<int>(std::strlen(text));
    CHECK(length > 0);
    // The card ladder is sized against kMaxEntryLen, and the drawing code
    // copies a line into a buffer of exactly that size.
    CHECK(length <= kMaxEntryLen);
    for (int i = 0; i < length; ++i) {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      // Toybox's face is subset to ASCII and a glyph the font does not have
      // draws as NOTHING: no box, no fallback, no log line. This is the check
      // that stands between a pasted curly apostrophe and a card with a hole
      // in the middle of it.
      CHECK(c >= 0x20 && c <= 0x7E);
      // Upper case, because every card is drawn in a face whose lower case was
      // never the point, and because a stray lower-case entry is a sign the
      // generator was bypassed.
      CHECK(!(c >= 'a' && c <= 'z'));
    }
    // No leading, trailing or doubled spaces: the wrap walks spaces looking for
    // break points and an empty run would produce an empty line.
    CHECK(text[0] != ' ');
    CHECK(text[length - 1] != ' ');
    for (int i = 1; i < length; ++i) CHECK(!(text[i] == ' ' && text[i - 1] == ' '));
  }
}

void testCategoriesTileTheEntryTable() {
  int cursor = 0;
  for (int category = 0; category < kCategoryCount; ++category) {
    const CategoryInfo& info = kCategories[category];
    CHECK(info.title != nullptr && std::strlen(info.title) > 0);
    CHECK(info.hint != nullptr && std::strlen(info.hint) > 0);
    // Contiguous and in order, with no gap and no overlap. The deck's mask is a
    // single bitmap over the flat array and "unseen in this category" is a scan
    // of a slice, so a gap would be cards nothing can ever deal and an overlap
    // would be one card marked seen by two categories at once.
    CHECK(info.first == cursor);
    CHECK(info.count > 0);
    cursor += info.count;
  }
  CHECK(cursor == kEntryCount);
}

void testNoCategoryRepeatsAnEntry() {
  for (int category = 0; category < kCategoryCount; ++category) {
    const CategoryInfo& info = kCategories[category];
    for (int i = info.first; i < info.first + info.count; ++i) {
      for (int j = i + 1; j < info.first + info.count; ++j) {
        // A duplicate inside one list defeats the deck silently: the mask marks
        // one index and the other is still in the bag, so the word comes round
        // twice in an evening and looks like the no-repeat rule is broken.
        CHECK(std::strcmp(kEntries[i], kEntries[j]) != 0);
      }
    }
  }
}

void testEveryCategoryIsBigEnoughForAnEvening() {
  for (int category = 0; category < kCategoryCount; ++category) {
    // A round can record up to kMaxCards, and a list shorter than that would
    // lap inside a single round -- which is exactly the "the device is broken"
    // reading the deck exists to prevent.
    CHECK(kCategories[category].count > kMaxCards);
  }
}

// ---------------------------------------------------------------------------
// The deck

void testADeckDealsEveryCardOnceBeforeLapping() {
  for (int category = 0; category < kCategoryCount; ++category) {
    const CategoryInfo& info = kCategories[category];
    Deck deck;
    deck.reset();
    Rng rng(0xC0FFEEu + static_cast<uint32_t>(category));

    bool drawn[kEntryCount] = {};
    for (int i = 0; i < info.count; ++i) {
      const int entry = deck.draw(category, rng);
      CHECK(entry >= info.first && entry < info.first + info.count);
      if (entry < 0 || entry >= kEntryCount) continue;
      CHECK(!drawn[entry]);
      drawn[entry] = true;
    }
    CHECK(deck.remainingIn(category) == 0);

    // A spent category returns -1 rather than lapping itself. The deck cannot
    // know which cards are on the results screen of the round in progress, so
    // the lap is Round's to decide; see testARoundNeverRepeatsACard.
    CHECK(deck.draw(category, rng) == -1);
  }
}

void testALapKeepsWhatTheCallerIsHolding() {
  const int category = 6;
  const CategoryInfo& info = kCategories[category];
  Deck deck;
  deck.reset();
  Rng rng(31u);
  for (int i = 0; i < info.count; ++i) deck.draw(category, rng);
  CHECK(deck.remainingIn(category) == 0);

  const int16_t held[3] = {static_cast<int16_t>(info.first), static_cast<int16_t>(info.first + 5),
                           static_cast<int16_t>(info.first + info.count - 1)};
  deck.lapExcept(category, held, 3);
  CHECK(deck.remainingIn(category) == info.count - 3);
  for (const int16_t entry : held) CHECK(deck.seen(entry));

  // An entry from another category in the keep list is ignored rather than
  // marked, so a caller cannot poison a neighbour by passing the wrong slice.
  Deck other;
  other.reset();
  const int16_t alien[1] = {0};
  other.lapExcept(category, alien, 1);
  CHECK(!other.seen(0));
}

void testALapLeavesEveryOtherCategoryAlone() {
  // The lap's slice discipline. Wiping all seventeen categories on every lap
  // used to leave this suite green, because the one test that lapped only ever
  // looked at the category it had lapped.
  const int mine = 2;
  Deck deck;
  deck.reset();
  Rng rng(41u);
  for (int category = 0; category < kCategoryCount; ++category) {
    for (int i = 0; i < 7; ++i) deck.draw(category, rng);
  }
  const CategoryInfo& info = kCategories[mine];
  for (int i = 7; i < info.count; ++i) deck.draw(mine, rng);
  CHECK(deck.remainingIn(mine) == 0);

  deck.lapExcept(mine, nullptr, 0);
  CHECK(deck.remainingIn(mine) == info.count);
  for (int category = 0; category < kCategoryCount; ++category) {
    if (category == mine) continue;
    CHECK(deck.remainingIn(category) == kCategories[category].count - 7);
  }
}

void testTheDeckDealsEvenlyAcrossItsSlice() {
  // The deck deals a permutation, which the test above proves, and that says
  // nothing about WHERE in the slice the draws land. Front-loading the first
  // quarter fourfold used to leave this suite green: an evening would have kept
  // returning the same end of the alphabet and every test would have agreed it
  // was fine.
  const int category = 0;
  const CategoryInfo& info = kCategories[category];
  constexpr int kLaps = 400;
  int firstHalf = 0;
  Deck deck;
  deck.reset();
  Rng rng(0x5EED1234u);
  for (int lap = 0; lap < kLaps; ++lap) {
    // Only the first draw of each lap is measured. Later draws in a lap are
    // constrained by what is already gone, so the whole lap is a permutation
    // and only its opening is a free choice over the whole slice.
    const int entry = deck.draw(category, rng);
    CHECK(entry >= info.first && entry < info.first + info.count);
    if (entry - info.first < info.count / 2) ++firstHalf;
    for (int i = 1; i < info.count; ++i) deck.draw(category, rng);
    deck.lapExcept(category, nullptr, 0);
  }
  CHECK(firstHalf > kLaps * 2 / 5);
  CHECK(firstHalf < kLaps * 3 / 5);
}

void testDrawingOneCategoryLeavesTheOthersAlone() {
  Deck deck;
  deck.reset();
  Rng rng(7u);
  const int mine = 3;
  for (int i = 0; i < 40; ++i) deck.draw(mine, rng);
  for (int category = 0; category < kCategoryCount; ++category) {
    if (category == mine) continue;
    CHECK(deck.remainingIn(category) == kCategories[category].count);
  }
}

void testTheLastCardOfALapIsReachable() {
  // Rejection sampling on positions would expect ~count draws to find the one
  // card left, so this is the case a naive deck fails on and a spot check
  // never reaches. Every seed must find it in exactly one draw.
  const int category = 0;
  const CategoryInfo& info = kCategories[category];
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    Deck deck;
    deck.reset();
    for (int entry = info.first; entry < info.first + info.count - 1; ++entry) deck.markSeen(entry);
    CHECK(deck.remainingIn(category) == 1);
    Rng rng(seed);
    CHECK(deck.draw(category, rng) == info.first + info.count - 1);
  }
}

void testSetMaskIgnoresBitsPastTheLastEntry() {
  uint8_t mask[Deck::kMaskBytes];
  std::memset(mask, 0xFF, sizeof(mask));
  Deck deck;
  deck.setMask(mask);
  // Every real entry is seen...
  for (int category = 0; category < kCategoryCount; ++category) CHECK(deck.remainingIn(category) == 0);
  // ...and the padding bits above the table are cleared, so a save written by a
  // build with a longer word list cannot leave this one permanently short.
  //
  // Read out of the mask rather than through seen(), which answers "yes" for
  // any index outside the table by design -- asking it here would test the
  // guard instead of the clearing.
  const uint8_t* bits = deck.mask();
  for (int entry = kEntryCount; entry < Deck::kMaskBytes * 8; ++entry) {
    CHECK((bits[entry / 8] & (1u << (entry % 8))) == 0);
  }
}

// ---------------------------------------------------------------------------
// A round

void testARoundScoresOnlyWhatWasGot() {
  Deck deck;
  deck.reset();
  Rng rng(11u);
  Round round;
  round.begin(2, 60, deck, rng);
  CHECK(round.live());
  CHECK(round.category() == 2);
  CHECK(round.lengthSeconds() == 60);

  round.got(deck, rng);
  round.got(deck, rng);
  round.missed(deck, rng);
  round.got(deck, rng);
  CHECK(round.score() == 3);
  CHECK(round.cards() == 4);
  CHECK(round.markAt(0) == Mark::Got);
  CHECK(round.markAt(2) == Mark::Missed);
  CHECK(round.markAt(3) == Mark::Got);

  round.expire();
  CHECK(!round.live());
  // The card in hand when the clock ran out is recorded, and it is neither.
  CHECK(round.cards() == 5);
  CHECK(round.markAt(4) == Mark::Unanswered);
  CHECK(round.score() == 3);
}

void testExpiringTwiceRecordsOneCard() {
  Deck deck;
  deck.reset();
  Rng rng(12u);
  Round round;
  round.begin(0, 60, deck, rng);
  round.got(deck, rng);
  round.expire();
  const int after = round.cards();
  // The activity can reach expire() from its timer and from a key press landing
  // in the same frame; a second call must not invent a card.
  round.expire();
  round.expire();
  CHECK(round.cards() == after);
}

void testAnsweringAfterTheEndDoesNothing() {
  Deck deck;
  deck.reset();
  Rng rng(13u);
  Round round;
  round.begin(0, 60, deck, rng);
  round.expire();
  const int cards = round.cards();
  const int score = round.score();
  round.got(deck, rng);
  round.missed(deck, rng);
  CHECK(round.cards() == cards);
  CHECK(round.score() == score);
  CHECK(std::strcmp(round.cardText(), "") == 0);
}

void testARoundNeverRepeatsACard() {
  // Every card in one round is distinct, which is the property a player would
  // actually notice being broken.
  //
  // NOT from a fresh deck. The deck is persistent -- it lives in the save file
  // -- so after a few evenings a category has fewer unseen cards than a round
  // will answer, and the round crosses a lap. That is the case this test used
  // to reset away: with deck.reset() inside the loop the lap branch is
  // unreachable and the property is free. It was 63% of evenings.
  for (uint32_t seed = 1; seed <= 300; ++seed) {
    Deck deck;
    deck.reset();
    Rng rng(seed);
    const int category = static_cast<int>(seed % kCategoryCount);
    // Wear the category down to a handful of unseen cards first, so almost
    // every round below has to lap partway through.
    {
      Rng warm(seed * 7919u + 13u);
      const int wear = kCategories[category].count - static_cast<int>(seed % 5) - 2;
      for (int i = 0; i < wear; ++i) deck.draw(category, warm);
    }
    Round round;
    round.begin(category, 60, deck, rng);
    bool used[kEntryCount] = {};
    while (round.live() && round.cards() < kMaxCards - 1) {
      const int entry = round.cardEntry();
      CHECK(entry >= 0 && entry < kEntryCount);
      if (entry < 0) break;
      CHECK(!used[entry]);
      used[entry] = true;
      if ((rng.next() & 3u) == 0) {
        round.missed(deck, rng);
      } else {
        round.got(deck, rng);
      }
    }
  }
}

void testARoundStopsAtItsCardCap() {
  Deck deck;
  deck.reset();
  Rng rng(17u);
  Round round;
  round.begin(0, 120, deck, rng);
  for (int i = 0; i < kMaxCards + 20; ++i) round.got(deck, rng);
  CHECK(round.cards() == kMaxCards);
  CHECK(!round.live());
  // The score and the list still tell the same story, which is the whole reason
  // the round ends here rather than dropping the tail.
  CHECK(round.score() == kMaxCards);
}

void testTextAndMarkAreBoundedByTheCardCount() {
  Deck deck;
  deck.reset();
  Rng rng(19u);
  Round round;
  round.begin(0, 60, deck, rng);
  round.got(deck, rng);
  CHECK(std::strcmp(round.textAt(-1), "") == 0);
  CHECK(std::strcmp(round.textAt(round.cards()), "") == 0);
  CHECK(round.entryAt(999) == -1);
  CHECK(round.markAt(999) == Mark::Unanswered);
}

// ---------------------------------------------------------------------------
// The record

void testTheRecordKeepsTheLastSixteenRounds() {
  Record record;
  for (int i = 1; i <= 20; ++i) record.push(0, i);
  CHECK(record.rounds == 20);
  CHECK(record.best == 20);
  // The newest is always the last cell, so the ornament grows out of one corner
  // rather than sliding under the reader.
  CHECK(record.recentAt(Record::kRecentCount - 1) == 20);
  CHECK(record.recentAt(0) == 5);
  CHECK(record.recentPeak() == 20);
}

void testAShortRecordIsRightAligned() {
  Record record;
  record.push(0, 4);
  record.push(0, 9);
  // Two rounds played: the empty cells are on the LEFT and report -1, so the
  // ornament can tell "not played yet" from "scored nothing".
  CHECK(record.recentAt(Record::kRecentCount - 1) == 9);
  CHECK(record.recentAt(Record::kRecentCount - 2) == 4);
  CHECK(record.recentAt(Record::kRecentCount - 3) == -1);
  CHECK(record.recentAt(0) == -1);
}

void testAZeroRoundIsPlayedRatherThanAbsent() {
  Record record;
  CHECK(!record.everPlayed(5));
  record.push(5, 0);
  // Scoring nothing is a real result and must not read as never having tried.
  CHECK(record.everPlayed(5));
  CHECK(record.bestIn[5] == 0);
  CHECK(record.recentAt(Record::kRecentCount - 1) == 0);
  CHECK(record.recentPeak() >= 1);
}

void testPerCategoryBestsDoNotLeak() {
  Record record;
  record.push(1, 12);
  record.push(2, 3);
  CHECK(record.bestIn[1] == 12);
  CHECK(record.bestIn[2] == 3);
  CHECK(record.bestIn[0] == 0);
  CHECK(!record.everPlayed(0));
  record.push(1, 7);
  CHECK(record.bestIn[1] == 12);  // a worse round does not lower a best
  CHECK(record.best == 12);
  CHECK(record.words == 22);
}

void testAnOutOfRangeCategoryCannotCorruptTheRecord() {
  Record record;
  record.push(-1, 5);
  record.push(kCategoryCount, 5);
  // The round still counts for the device; only the per-category column is
  // skipped, and nothing is written outside the array.
  CHECK(record.rounds == 2);
  for (int i = 0; i < kCategoryCount; ++i) CHECK(record.bestIn[i] == 0);
  CHECK(record.played == 0);
}

// ---------------------------------------------------------------------------
// The clock, which is a repaint schedule rather than a time

void testTheClockRepaintsAreAffordable() {
  // The whole reason the clock is a bar. A per-second countdown is sixty
  // partial refreshes across a sixty-second round, which is eighteen seconds
  // of the minute spent mid-update on the one screen somebody is reading from
  // a sofa. This is the ceiling that keeps that from creeping back.
  int changes = 0;
  for (int left = 60; left > 0; --left) {
    if (barSegments(left, 60) != barSegments(left + 1, 60)) ++changes;
  }
  CHECK(changes <= 8);
  // And it has to actually move, or the bar on the panel is a lie.
  CHECK(changes >= 5);
}

void testTheBarIsFullAtTheStartAndEmptyAtTheEnd() {
  for (const int length : kRoundLengths) {
    CHECK(barSegments(length, length) == kBarSegments);
    CHECK(barSegments(0, length) == 0);
    // Rounding up, so the first card never shows a bar that has already lost a
    // segment -- which reads as the clock having stolen a second.
    CHECK(barSegments(length - 1, length) == kBarSegments);
    int previous = kBarSegments + 1;
    for (int left = length; left >= 0; --left) {
      const int segments = barSegments(left, length);
      CHECK(segments <= previous);
      CHECK(segments >= 0 && segments <= kBarSegments);
      previous = segments;
    }
  }
  CHECK(barSegments(30, 0) == 0);
}

// ---------------------------------------------------------------------------
// The generator

void testTheRngStaysInRange() {
  Rng rng(1u);
  for (int i = 0; i < 20000; ++i) {
    const uint32_t value = rng.below(189);
    CHECK(value < 189);
  }
  CHECK(rng.below(0) == 0);
}

void testTheRngIsNotVisiblyBiased() {
  // Not a statistics suite -- just enough to catch the modulo shortcut, which
  // would make one end of a category's alphabet likelier than the other.
  //
  // 189, which is ANIMALS' size, and NOT a power of two. The first version used
  // 16 buckets and could not fail: 2^32 divides exactly by 16, so `next() %
  // bound` is perfectly uniform there and the shortcut this test exists to
  // catch survived it with 0 failures. A rejection sampler is only
  // distinguishable from a modulo at a bound that does not divide 2^32.
  constexpr int kBuckets = 189;
  constexpr int kDraws = 189 * 4000;
  int counts[kBuckets] = {};
  Rng rng(0xABCDEF01u);
  for (int i = 0; i < kDraws; ++i) ++counts[rng.below(kBuckets)];
  const int expected = kDraws / kBuckets;
  for (const int count : counts) {
    CHECK(count > expected * 8 / 10);
    CHECK(count < expected * 12 / 10);
  }
}

void testARoundObjectIsCleanWhenItBeginsAgain() {
  // The device keeps ONE Round as an activity member and calls begin() for
  // every round of a session. Every other test here builds a fresh one, so
  // deleting any of the three resets in begin() left the suite green while the
  // second round of an evening would have carried the first one's score into
  // the record.
  Deck deck;
  deck.reset();
  Rng rng(77u);
  Round round;
  round.begin(3, 90, deck, rng);
  round.got(deck, rng);
  round.got(deck, rng);
  round.missed(deck, rng);
  round.expire();
  CHECK(round.score() == 2);
  CHECK(round.cards() == 4);

  round.begin(5, 30, deck, rng);
  CHECK(round.score() == 0);
  CHECK(round.cards() == 0);
  CHECK(round.live());
  // The new round's own settings, not the previous round's, and not the
  // defaults either -- 30 and 5 are both different from 60 and 0.
  CHECK(round.category() == 5);
  CHECK(round.lengthSeconds() == 30);
  CHECK(round.cardEntry() >= kCategories[5].first);
  CHECK(round.cardEntry() < kCategories[5].first + kCategories[5].count);
}

void testTheRecordSaturatesRatherThanWrapping() {
  Record record;
  for (int i = 0; i < 400; ++i) record.push(0, 255);
  // Both counters saturate. `words` wrapping would show a device that has
  // played all evening as having played nothing.
  CHECK(record.words == 65535);
  CHECK(record.rounds == 400);
  CHECK(record.best == 255);
}

void testTwoSeedsDealDifferentRounds() {
  Deck a;
  Deck b;
  a.reset();
  b.reset();
  Rng one(1u);
  Rng two(2u);
  Round first;
  Round second;
  first.begin(0, 60, a, one);
  second.begin(0, 60, b, two);
  int same = 0;
  for (int i = 0; i < 20; ++i) {
    if (first.cardEntry() == second.cardEntry()) ++same;
    first.got(a, one);
    second.got(b, two);
  }
  CHECK(same < 5);
}

void testTheSameSeedReplaysExactly() {
  // The property the tests above lean on: a seed is a replay, which is what
  // makes a failure reproducible instead of a story about one evening.
  Deck a;
  Deck b;
  a.reset();
  b.reset();
  Rng one(99u);
  Rng two(99u);
  Round first;
  Round second;
  first.begin(4, 60, a, one);
  second.begin(4, 60, b, two);
  for (int i = 0; i < 30; ++i) {
    CHECK(first.cardEntry() == second.cardEntry());
    first.got(a, one);
    second.got(b, two);
  }
}

}  // namespace

int main() {
  testEveryEntryIsDrawable();
  testCategoriesTileTheEntryTable();
  testNoCategoryRepeatsAnEntry();
  testEveryCategoryIsBigEnoughForAnEvening();

  testADeckDealsEveryCardOnceBeforeLapping();
  testALapKeepsWhatTheCallerIsHolding();
  testALapLeavesEveryOtherCategoryAlone();
  testTheDeckDealsEvenlyAcrossItsSlice();
  testDrawingOneCategoryLeavesTheOthersAlone();
  testTheLastCardOfALapIsReachable();
  testSetMaskIgnoresBitsPastTheLastEntry();

  testARoundScoresOnlyWhatWasGot();
  testExpiringTwiceRecordsOneCard();
  testAnsweringAfterTheEndDoesNothing();
  testARoundNeverRepeatsACard();
  testARoundStopsAtItsCardCap();
  testARoundObjectIsCleanWhenItBeginsAgain();
  testTextAndMarkAreBoundedByTheCardCount();

  testTheRecordKeepsTheLastSixteenRounds();
  testAShortRecordIsRightAligned();
  testAZeroRoundIsPlayedRatherThanAbsent();
  testPerCategoryBestsDoNotLeak();
  testAnOutOfRangeCategoryCannotCorruptTheRecord();
  testTheRecordSaturatesRatherThanWrapping();

  testTheClockRepaintsAreAffordable();
  testTheBarIsFullAtTheStartAndEmptyAtTheEnd();

  testTheRngStaysInRange();
  testTheRngIsNotVisiblyBiased();
  testTwoSeedsDealDifferentRounds();
  testTheSameSeedReplaysExactly();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
