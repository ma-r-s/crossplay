#pragma once

// Insider: the rules, with nothing else in them.
//
// Freestanding C++17 -- no renderer, no storage, no clock, no Arduino -- so
// host-tests/insider/ can deal a hundred thousand rounds on a laptop and assert
// on the distribution. See docs/shelf.md for the three-way split.
//
// The game, in one paragraph. One player is the MASTER and knows the secret
// word. One of the others is the INSIDER and knows it too. Everybody questions
// the Master until somebody says the word out loud; then the table votes on who
// the Insider was. The Insider is trying to steer the room to the answer
// without being the one who looks like they knew.
//
// The twist this port keeps from the prototype: before the roles are dealt, one
// is thrown away. So with n at the table there is a 1-in-n chance that nobody
// is the Insider, and the table can vote for exactly that. It is the variant
// that makes the game worth a second round, because "you were too quiet" stops
// being evidence.

#include <cstdint>

#include "InsiderWords.h"

namespace insider {

// Four is the floor because below it the Master plus one accusation is the
// whole game; eight is the ceiling because the reveal has to be passed hand to
// hand and a ninth pass is where a table starts talking over it.
constexpr int kMinPlayers = 4;
constexpr int kMaxPlayers = 8;

// Five minutes, as the boxed game has it.
constexpr int kQuestionSeconds = 300;

// Inside the last minute the clock starts moving in fives instead of fifteens.
// This is a refresh budget as much as a design choice: see clockTick().
constexpr int kUrgentSeconds = 60;
constexpr int kCoarseStep = 15;
constexpr int kFineStep = 5;

enum class Role : uint8_t {
  Citizen,
  Insider,
  Master,
};

// From the table's point of view, which is the only point of view the record
// keeps -- the seats are different people every night, so per-player stats are
// not a thing this device can honestly hold.
//
// Two bits each, because sixteen of them pack into the record's one uint32_t.
enum class Outcome : uint8_t {
  Unplayed = 0,
  Won = 1,        // named the Insider, or correctly said there was none
  Lost = 2,       // named the wrong person
  OutOfTime = 3,  // the clock ran out with the word still unsaid
};

// The vote meaning "there is no Insider at this table". A real seat is 0..n-1.
constexpr int kNoInsider = -1;

// xorshift32. Small, freestanding, and above all *seedable*, which is what lets
// a test replay an exact deal. The device seeds it from millis(), the only
// entropy here that differs between two boots.
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
  // values, which for a 560-word deck is invisible and for an 8-seat shuffle is
  // not: it would make one chair likelier to be the Insider than another, which
  // is precisely the thing this game cannot have.
  uint32_t below(const uint32_t bound) {
    if (bound == 0) return 0;
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % bound) - (bound - 1);
    uint32_t value = next();
    while (value > limit) value = next();
    return value % bound;
  }

  uint32_t state() const { return state_; }

 private:
  uint32_t state_;
};

// The word deck, which deals without repeating until it has been all the way
// through. A table plays a handful of rounds a night and hearing "APPLE" twice
// in an evening reads as the device being broken.
//
// Exhaustion wraps silently rather than asking the player to reset it. The
// prototype grew a "Reset Word List" button because the web version had no
// place to keep the mark; here there is a save file, so the deck can just deal
// its 561st word and start the next lap.
class Deck {
 public:
  void reset();

  // Draws an unseen word and marks it. Wraps when the deck is spent.
  int draw(Rng& rng);

  int remaining() const;
  bool seen(int index) const;
  void markSeen(int index);

  // The save carries the mark, so a deck survives the device sleeping.
  static constexpr int kMaskBytes = (kWordCount + 7) / 8;
  const uint8_t* mask() const { return seen_; }
  void setMask(const uint8_t* bytes);

 private:
  uint8_t seen_[kMaskBytes] = {};
};

// One dealt round. Holds who is what and which word, and can say who won.
class Round {
 public:
  // Deals `players` seats: one Master, and the rest drawn from a bag of
  // (n-1) Citizens and one Insider with a single role thrown away first.
  void deal(int players, int wordIndex, Rng& rng);

  bool dealt() const { return players_ >= kMinPlayers; }
  int players() const { return players_; }
  Role roleOf(int seat) const;
  int masterSeat() const;

  // The seat holding the Insider, or kNoInsider when the discarded role was the
  // Insider and there is genuinely nobody to catch.
  int insiderSeat() const;
  bool hasInsider() const { return insiderSeat() != kNoInsider; }

  int wordIndex() const { return wordIndex_; }
  // NOT word(). Arduino.h defines `word(...)` as a function-like macro that
  // rewrites it to makeWord(), so `round.word()` compiles everywhere this
  // header is used freestanding -- the host tests, the screens, the simulator --
  // and fails to link only in the one translation unit that pulls in Arduino.
  // See docs/building-apps.md.
  const char* secretWord() const;

  // `accused` is a seat, or kNoInsider for "there is no Insider among us".
  Outcome judge(int accused) const;

 private:
  uint8_t players_ = 0;
  int16_t wordIndex_ = 0;
  Role roles_[kMaxPlayers] = {};
};

// What this table has done, which is the only record the game can keep. Sixteen
// rounds of history, two bits apiece, because sixteen marks is what the menu's
// ornament has room for.
struct Record {
  uint16_t rounds = 0;
  uint16_t won = 0;
  uint16_t lost = 0;
  uint16_t outOfTime = 0;
  uint32_t recent = 0;

  void push(Outcome outcome);

  // Index 0 is the oldest of the sixteen, 15 the round just played, so the
  // ornament reads left to right in the order they happened.
  Outcome at(int index) const;
};

// How many times the clock face has changed since the round began.
//
// This is the whole repaint schedule, and it is a rule rather than a detail: a
// per-second countdown is three hundred refreshes, which is both a fifth of the
// game's battery cost and a screen that is never still. Fifteen-second steps
// until the last minute and five-second steps inside it come to twenty-eight
// repaints, and the change of pace is what makes the end feel like the end.
//
// Strictly non-decreasing as time runs out, so the activity can repaint on
// nothing but "this number moved".
int clockTick(int secondsLeft);

}  // namespace insider
