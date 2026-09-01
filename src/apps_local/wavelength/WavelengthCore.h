#pragma once

// WAVELENGTH: the rules, with nothing else in them.
//
// Freestanding C++17 -- no renderer, no storage, no clock, no Arduino -- so
// host-tests/wavelength/ can play a hundred thousand rounds on a laptop. See
// docs/shelf.md for the three-way split.
//
// The game, in one paragraph. A strip runs between two opposing words, slot 1
// at the bottom and slot 20 at the top. One player sees a hidden target slot
// and says a single clue out loud that, to them, sits exactly there. Everyone
// else argues, moves a marker and locks it in, then calls whether the target
// was nearer the top word or the bottom one. Closer is worth more, the whole
// table shares one score, and the clue-giver passes left.
//
// There is no deck data in here and no notion of a spectrum's words: a pair is
// an index. That is what lets the tests exercise every rule without a deck, and
// it is why adding a hundred pairs cannot change a single result below.

#include <cstdint>

namespace wavelength {

// Twenty slots. The panel cannot do a continuous dial (a dragged needle trails
// the finger by about a third of a second), and a discrete strip also gives the
// table a spoken vocabulary that carries across a loud room.
inline constexpr int kSlots = 20;

// The scoring band reaches this far either side of the target, so five slots
// score. Wider than the physical game's wedge on purpose: players are drunk,
// and a table that zeroes three rounds running stops playing.
inline constexpr int kBandRadius = 2;

// Steep rather than flat. A five-wide band on a twenty-slot strip means a
// scoring result happens fairly often by luck, so off-by-two has to read as
// "right part of the board" rather than as a near miss, and an exact hit has to
// stay worth shouting about.
inline constexpr int kPointsExact = 5;
inline constexpr int kPointsOffByOne = 3;
inline constexpr int kPointsOffByTwo = 1;
inline constexpr int kPointsEndCall = 1;

// The first round of a session is a practice round and does not score. Every
// table's first round is a zero because nobody has understood the game yet, and
// that is the worst possible place to land a discouraging number.
inline constexpr int kPracticeRounds = 1;

// Which way the table called the target, relative to the slot they locked.
// Named for the ends of the strip rather than for higher and lower: the device
// lies flat on a table and the player opposite reads it upside down, so "up"
// is the one word that means different things to different seats.
enum class Call : uint8_t { TowardTop, TowardBottom };

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
  // values, which across twenty slots is a target that favours one end of the
  // strip: invisible in one round and unfair over a night.
  uint32_t below(const uint32_t bound) {
    if (bound == 0) return 0;
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % bound) - 1;
    uint32_t v = next();
    while (v > limit) v = next();
    return v % bound;
  }

 private:
  uint32_t state_;
};

// A target may land anywhere, including slot 1 and slot 20.
//
// An earlier draft kept it away from the ends so the five-wide band always
// fitted. That was wrong: it made slots 1, 2, 19 and 20 dominated, so a player
// who noticed would never guess them and the strip was really sixteen slots
// with four of decoration. Unrestricted, no slot is dominated, which
// test_wavelength.cpp asserts exhaustively. The cost is that an extreme target
// has a clipped band, and it is paid back by extreme targets producing
// unusually clear clues.
inline int drawTarget(Rng& rng) { return static_cast<int>(rng.below(kSlots)) + 1; }

// Points for the guess alone, before the end-call.
int scoreForGuess(int guess, int target);

// Was the end-call right?
//
// An exact lock has no side, so it is neither right nor wrong and pays nothing.
// A perfect round is still unbeatable without the bonus, because the best a
// non-exact round can reach is kPointsOffByOne + kPointsEndCall = 4 against
// kPointsExact = 5; testPerfectRoundIsUnbeatable asserts that exhaustively.
//
// It used to count as correct, justified by a monotonicity argument that was
// arithmetically false, and it made an exact lock pay 6 while every screen in
// the app said 5. A player doing the arithmetic caught it.
//
// The call is still ASKED on an exact lock, and must be: the device knows the
// guess is exact and the table does not, so skipping the question would leak
// the result before the reveal.
bool endCallCorrect(int guess, int target, Call call);

// The whole round, guess and call together.
int scoreRound(int guess, int target, Call call);

// The most pairs a deck may hold. Raising it costs one word of RAM per 32.
inline constexpr int kMaxPairs = 640;
inline constexpr int kSeenWords = kMaxPairs / 32;

// The deck, as indices into whatever pair table the caller owns.
//
// A repeated spectrum is ruined outright rather than merely stale: somebody
// remembers roughly where the target was last time and says so. It is the same
// few friends every week, so the seen set is worth persisting between sessions.
class Deck {
 public:
  Deck(int pairCount);

  // Deals the two spectra the clue-giver chooses between, writing them to
  // out[0] and out[1] and returning how many it could deal. Two is the normal
  // answer. One means the deck has a single unseen pair left, and the screen
  // says so rather than pretending there is a choice. Zero means exhausted.
  //
  // Dealing does not consume: only the pair actually chosen is marked seen, so
  // the one passed over comes back another round.
  int dealChoice(Rng& rng, int out[2]) const;

  void markSeen(int index);
  bool isSeen(int index) const;
  int unseenCount() const;
  void forgetSeen();

  int pairCount() const { return pairCount_; }

 private:
  int nthUnseen(int n) const;

  int pairCount_;
  uint32_t seen_[kSeenWords];
};

// The running session. Rounds are 1-based and round 1 is practice.
struct Session {
  int round = 1;
  int total = 0;
  int scoredRounds = 0;

  bool isPractice() const { return round <= kPracticeRounds; }

  // Records a finished round and moves to the next. Returns the points it
  // actually added, which is zero on the practice round however well it went.
  int record(int guess, int target, Call call);

  // Points per scored round, in tenths so the caller needs no float. Zero
  // before any round has scored.
  int averageTenths() const;
};

// How far off a round was, bucketed for the front door's ornament. Distance
// rather than points, because the question the table asks itself between rounds
// is "are we getting closer", and points fold the end-call into that.
inline constexpr int kBucketCount = 5;  // exact, 1, 2, 3-5, wider

int bucketFor(int guess, int target);

// The record that survives between sessions. Small on purpose: it is what the
// front door draws, and a menu that is byte-identical on every device is
// wallpaper rather than ornament.
struct Record {
  uint16_t rounds = 0;
  uint16_t points = 0;
  uint16_t buckets[kBucketCount] = {};
  uint16_t bestRoundTenths = 0;  // best single-session average, in tenths

  void add(int guess, int target, Call call);
  int averageTenths() const;
  // The tallest bucket, so the ornament can scale to what is actually there
  // rather than to a guessed maximum.
  uint16_t peak() const;
};

// What a table that is genuinely communicating averages, in tenths, for the
// front door to sit next to its own average. A score with nothing beside it
// means nothing: nobody can tell whether 19 points is good.
inline constexpr int kGoodTableTenths = 25;

}  // namespace wavelength
