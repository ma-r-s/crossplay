#pragma once

// FSRS-5, the scheduler Anki runs when "FSRS" is enabled.
//
// Freestanding C++17: no Arduino, no renderer, no heap. That is what lets
// host-tests/study replay 301 real review histories out of Mario's own
// collection and assert this file reproduces Anki's stored memory state.
//
// Semantics were not taken from a paper -- they were recovered by replaying
// that collection and keeping the variant that matched. Three of them are not
// obvious and are the difference between 89/301 and 287/301:
//
//   1. Filtered-deck ("cram") reviews do not update memory state at all.
//      Anki logs them as revlog type 3 and skips them when rebuilding state,
//      because rescheduling is off in those decks. Feeding them in costs 42
//      cards. The caller is responsible for not calling review() for them.
//   2. Same-day reviews use the short-term stability path, for every review
//      kind -- not only learning/relearning steps. Restricting it to steps
//      costs 6 cards.
//   3. A lapse may never raise stability: the forget path is clamped to the
//      stability it started from. Worth 2 cards.
//
// Known residual: 14 of 301 cards still differ, all of them heavily lapsed
// (34-51 reviews) with stability under three days. Difficulty matches to three
// decimals on every one of them, so the gap is somewhere in the same-day
// stability chain, not in the difficulty model. The practical effect is a card
// due tomorrow either way. host-tests/study pins the 287 so a change that makes
// this worse is caught.

#include <cstdint>

namespace study {

// Anki's four buttons. The numeric values are Anki's revlog `ease` column.
enum class Rating : uint8_t { Again = 1, Hard = 2, Good = 3, Easy = 4 };

// FSRS-5 takes 19 weights. Anki stores them per deck-preset; the converter
// lifts them out of the collection so the device runs Mario's own optimized
// parameters rather than the published defaults.
inline constexpr int kNumParams = 19;

// Anki's defaults, used only when a deck ships no parameters of its own.
extern const float kDefaultParams[kNumParams];

// What FSRS remembers about a card. Mirrors the `{"s":..,"d":..}` blob Anki
// keeps in `cards.data`, so a converted deck round-trips exactly.
struct Memory {
  float stability = 0.0f;   // days for retrievability to fall to 90%
  float difficulty = 0.0f;  // 1..10
  bool learned = false;     // false until the first review

  bool operator==(const Memory& o) const {
    return learned == o.learned && stability == o.stability && difficulty == o.difficulty;
  }
};

class Fsrs {
 public:
  // `params` must point at kNumParams floats that outlive this object; the
  // deck's parameter block is memory-mapped from the SD card and never copied.
  // Passing nullptr selects kDefaultParams.
  explicit Fsrs(const float* params = nullptr, float desiredRetention = 0.9f);

  // Probability the card is still recallable `elapsedDays` after its last
  // review. This is the forgetting curve; 1.0 for a card never studied.
  float retrievability(const Memory& m, float elapsedDays) const;

  // Apply a review and return the new memory state.
  //
  // `elapsedDays` is whole days between the previous review's day and this
  // one's, measured against the deck's rollover hour -- 0 for a same-day
  // review. It is ignored for a card's first review.
  //
  // Do not call this for a filtered-deck review; see the header note.
  Memory review(const Memory& m, Rating rating, int elapsedDays) const;

  // Days until the card next falls to the desired retention. This is the
  // number shown on a rating button.
  int intervalDays(const Memory& m) const;

  // Interval each button would produce, for the four labels under a card.
  // `out` receives Again, Hard, Good, Easy in that order.
  void previewIntervals(const Memory& m, int elapsedDays, int out[4]) const;

  float desiredRetention() const { return desiredRetention_; }
  void setDesiredRetention(float r) { desiredRetention_ = r; }

  // Longest interval the scheduler may hand out, in days. Anki's default is
  // 36500 (a century); a deck preset may lower it.
  void setMaximumInterval(int days) { maximumInterval_ = days; }

 private:
  float initialStability(Rating r) const;
  float initialDifficulty(Rating r) const;
  float nextDifficulty(float d, Rating r) const;
  float shortTermStability(float s, Rating r) const;
  float recallStability(float d, float s, float retr, Rating r) const;
  float forgetStability(float d, float s, float retr) const;

  const float* w_;
  float desiredRetention_;
  int maximumInterval_ = 36500;
};

}  // namespace study
