#pragma once

// FOREHEAD: the rules, with nothing else in them.
//
// Freestanding C++17 -- no renderer, no storage, no clock, no Arduino -- so
// host-tests/forehead/ can deal a hundred thousand rounds on a laptop. See
// docs/shelf.md for the three-way split.
//
// The game, in one paragraph. One player holds the device against their
// forehead, screen out, and cannot see it. Everybody else can, and shouts
// clues. The guesser presses the BOTTOM key when they get it and the TOP key
// to give up on one, and keeps going until the clock runs out. The score is
// how many they got.
//
// There is NO clock in here on purpose. Time is the activity's business: it
// owns millis(), and it tells this layer only that a round ended. That is what
// lets a test play a whole round in no time at all, and it is why every
// function below is about cards rather than seconds.

#include <cstdint>

#include "ForeheadWords.h"

namespace forehead {

// The round lengths the front door offers. Sixty is the one everybody knows;
// thirty is for a table that is passing the device round quickly and a hundred
// and twenty is for a category nobody is any good at.
inline constexpr int kRoundLengths[] = {30, 60, 90, 120};
inline constexpr int kRoundLengthCount = static_cast<int>(sizeof(kRoundLengths) / sizeof(kRoundLengths[0]));
inline constexpr int kDefaultRoundSeconds = 60;

// The most cards one round records. A card costs a partial refresh, so ~0.3s is
// the hardware floor per card and 128 of them would be 0.94s each sustained for
// two minutes: not reachable by a person, which is the point of the number.
// Reaching it ends the round rather than silently dropping the tail, because a
// results screen missing its last cards is a scoreboard that lies.
inline constexpr int kMaxCards = 128;

// How a card ended. Unanswered is the one in hand when the clock ran out: it is
// neither got nor given up on, and calling it either would be a lie the table
// will argue about. It scores nothing and it says so on the results screen.
enum class Mark : uint8_t { Got, Missed, Unanswered };

// xorshift32. Small, freestanding, and above all seedable, which is what lets a
// test replay an exact deal. The device seeds it from millis().
class Rng {
 public:
  explicit Rng(const uint32_t seed) : state_(seed ? seed : 0x9E3779B9u) {}

  uint32_t next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }

  // Unbiased below `bound` by rejection. The modulo shortcut skews the low
  // values, and across a 189-entry category that is one end of the alphabet
  // coming up more often than the other -- invisible in one round and obvious
  // over an evening.
  uint32_t below(uint32_t bound);

  uint32_t state() const { return state_; }

 private:
  uint32_t state_;
};

// The card deck: one bit per entry across every category, marking what this
// device has already dealt.
//
// A party plays a handful of rounds of one category in a row, and hearing
// PENGUIN twice in ten minutes reads as the device being broken. The mask is
// over the flat entry array and a category is a contiguous slice of it, so
// "unseen in this category" is a scan of that slice and nothing needs a
// per-category structure.
//
// A spent category laps silently rather than asking anybody to reset it: the
// save file is what the web versions of this game never had, so the deck can
// simply deal its 190th animal and start again.
class Deck {
 public:
  static constexpr int kMaskBytes = (kEntryCount + 7) / 8;

  void reset();

  // An unseen entry from `category`, marked as seen, or -1 when the category is
  // SPENT. It does not lap on its own, and that is the whole point: the deck
  // cannot know which cards are on the results screen of the round in progress,
  // and a lap that re-deals one of those is the "this device is broken" reading
  // the deck exists to prevent. Round::dealNext owns the lap because Round is
  // the thing that knows.
  int draw(int category, Rng& rng);

  // Clears the category's marks EXCEPT the entries listed, which stay seen.
  // `keep` is the round's own dealt cards.
  void lapExcept(int category, const int16_t* keep, int keepCount);

  int remainingIn(int category) const;
  bool seen(int entry) const;
  void markSeen(int entry);

  const uint8_t* mask() const { return seen_; }
  // Clears the bits above the last real entry, so a save written by a build
  // with a longer word list cannot leave this one permanently short of a lap.
  void setMask(const uint8_t* bytes);

 private:
  uint8_t seen_[kMaskBytes] = {};
};

// One round in progress, and afterwards the thing the results screen reads.
class Round {
 public:
  // Deals the first card. `lengthSeconds` is remembered but never counted here.
  void begin(int category, int lengthSeconds, Deck& deck, Rng& rng);

  // A card is face out and waiting for an answer.
  bool live() const { return live_; }
  int category() const { return category_; }
  int lengthSeconds() const { return lengthSeconds_; }

  // The card in hand. Empty string when the round is over, never nullptr, so a
  // screen builder cannot draw a null.
  const char* cardText() const;
  int cardEntry() const;

  // The two keys. Both record the card in hand and deal the next one.
  void got(Deck& deck, Rng& rng) { answer(Mark::Got, deck, rng); }
  void missed(Deck& deck, Rng& rng) { answer(Mark::Missed, deck, rng); }

  // The clock ran out. The card in hand is recorded Unanswered and the round
  // stops. Idempotent: a second call after the round is over does nothing, so
  // the activity does not have to guard its own timer against a last-instant
  // key press that already ended things.
  void expire();

  int score() const { return got_; }
  // Every card recorded, including the Unanswered one. This is the results
  // list, and it is NOT the score.
  int cards() const { return count_; }
  int entryAt(int index) const;
  const char* textAt(int index) const;
  Mark markAt(int index) const;

 private:
  void answer(Mark mark, Deck& deck, Rng& rng);
  void dealNext(Deck& deck, Rng& rng);

  int16_t entries_[kMaxCards] = {};
  Mark marks_[kMaxCards] = {};
  int16_t count_ = 0;
  int16_t got_ = 0;
  int16_t current_ = -1;
  uint8_t category_ = 0;
  uint16_t lengthSeconds_ = kDefaultRoundSeconds;
  bool live_ = false;
};

// What this device has done. There is one device and any number of people
// holding it, so a per-player record would be a fiction: everything here is
// "this device, lately", which is the only honest unit.
struct Record {
  uint16_t rounds = 0;
  uint16_t words = 0;  // every card ever got, across every category
  uint8_t best = 0;    // best single round, any category

  // The last sixteen round scores, oldest first. Sixteen because that is what
  // the front door's ornament has room for, and it is the ornament's data:
  // different every evening and identical on nobody else's device.
  static constexpr int kRecentCount = 16;
  uint8_t recent[kRecentCount] = {};
  uint8_t recentCount = 0;

  // Per category, so the picker can say what a list is worth to you rather than
  // only what it is called. `played` is separate because scoring zero is a real
  // result and must not read as never having tried.
  uint8_t bestIn[kCategoryCount] = {};
  uint32_t played = 0;

  void push(int category, int score);
  bool everPlayed(int category) const;
  // Index 0 is the oldest of the sixteen, kRecentCount - 1 the round just
  // played, so the ornament reads left to right in the order they happened.
  int recentAt(int index) const;
  // The tallest bar the ornament has to draw, so the chart scales to the player
  // rather than to a number picked here. Never zero, so nothing divides by it.
  int recentPeak() const;
};

// The clock, expressed as a repaint schedule rather than as a time.
//
// This is the whole reason the round screen is not a blinking mess, and it is
// the constraint that shaped it. A per-second countdown is sixty partial
// refreshes a round: eighteen seconds of the minute spent mid-update, on the
// one screen in this fork that somebody across the room is trying to read. So
// the activity repaints on nothing but "this moved", and this is what decides
// how often that is.
//
// Eight segments over the round, which is five changes across sixty seconds
// against sixty for a ticking numeral. Coarse on purpose: a bar with sixty
// positions is a per-second countdown wearing a different hat.
//
// Monotonically non-increasing as time runs out, so a repaint can never be
// missed by the count going back up, and it ROUNDS UP so the bar is full the
// instant the round starts.
inline constexpr int kBarSegments = 8;
int barSegments(int secondsLeft, int lengthSeconds);

}  // namespace forehead
