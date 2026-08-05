#pragma once

// Learning steps: the part of Anki that is not FSRS.
//
// FSRS answers "how many days until this should come back". It does not answer
// "what happens in the next ten minutes", and that is a separate machine Anki
// runs on top: a new card walks a list of short steps (Mario's deck uses 1
// minute then 10 minutes) before it graduates to day-scale intervals, and a
// lapsed card walks a shorter list (10 minutes) before it goes back.
//
// Without this the Again button on a mature card offered *fifteen days*, which
// is the interval the card gets after it has been relearned -- not when you
// will actually see it again. A user pressing Again is saying "I forgot this",
// and being told the answer comes back in a fortnight is both wrong and
// discouraging.
//
// Freestanding C++17, like StudyFsrs, so host-tests/study can drive the whole
// state machine without a device.

#include <cstdint>

#include "StudyDeck.h"
#include "StudyFsrs.h"

namespace study {

// Anki allows an arbitrary number of steps; six is well past what anyone uses
// and keeps Steps a fixed-size struct that lives in the deck's meta record.
inline constexpr int kMaxSteps = 6;

// The four card states, matching Anki's own numbering and cards.dat's `state`.
enum class State : uint8_t {
  New = 0,
  Learning = 1,
  Review = 2,
  Relearning = 3,
  // Ours, with no Anki equivalent: a card Anki has suspended or buried. Kept
  // in the deck rather than dropped so note indices stay stable across
  // reconversions, and never shown. Meeting a card here that Anki has
  // suspended is the kind of divergence that makes the sync untrustworthy.
  Suspended = 4,
};

struct Steps {
  float learn[kMaxSteps] = {};    // minutes
  float relearn[kMaxSteps] = {};  // minutes
  uint8_t learnCount = 0;
  uint8_t relearnCount = 0;

  // Anki's defaults, for a deck that ships none.
  static Steps defaults();
};

// What one answer produces.
struct Outcome {
  CardState card;            // the card's new state, ready to store
  int32_t delayMinutes = 0;  // >0: comes back this session, in this many minutes
  int32_t intervalDays = 0;  // >0: leaves for a future day
  bool graduated = false;    // crossed from a step list into day-scale review
};

class Scheduler {
 public:
  Scheduler(const Fsrs& fsrs, const Steps& steps) : fsrs_(&fsrs), steps_(steps) {}

  // Apply a rating. `today` is the deck's day number and `nowMinute` is minutes
  // since that day began, which together give a learning card a due instant
  // finer than a day without needing a second clock.
  Outcome answer(const CardState& card, Rating rating, int today, int nowMinute) const;

  // What each of the four buttons would do, for the labels under them.
  void preview(const CardState& card, int today, int nowMinute, Outcome out[4]) const;

  // Is this card due at this instant?
  static bool isDue(const CardState& card, int today, int nowMinute);

 private:
  const Fsrs* fsrs_;
  Steps steps_;
};

// Render a delay as Anki renders it on a button: minutes and hours below a day,
// then days, months, years. `out` must have room for at least 12 bytes.
void formatDelay(int minutes, int days, char* out, size_t size);

}  // namespace study
