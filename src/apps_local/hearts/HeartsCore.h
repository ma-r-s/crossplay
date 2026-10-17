#pragma once

// Hearts, as pure rules.
//
// Freestanding by the same rule as ChessCore and SolitaireCore: no renderer, no
// storage, no Arduino. Everything here is decided by looking at the state and
// nothing here can draw, which is what lets host-tests/hearts/ play thousands
// of hands and check every invariant without a device.
//
// Four seats, clockwise, with the human always South:
//
//        NORTH
//   WEST       EAST      South -> West -> North -> East -> South
//        SOUTH
//
// On a compass face that IS clockwise (6 -> 9 -> 12 -> 3), and it makes the
// player to your left the next player, which is what "pass left" means.

#include <cstdint>

#include "../cards/Cards.h"

namespace hearts {

using cards::kNoCard;
using cards::Suit;

constexpr int kSeats = 4;
constexpr int kHandSize = 13;
constexpr int kTricks = 13;
constexpr int kPassCount = 3;

// A game runs to 100 and the LOWEST score wins. Standard, and the number is
// load-bearing for pacing: at ~6 points a hand for a competent player it is
// about four hands, which is one sitting on a device you put down.
constexpr int kTargetScore = 100;

// All 26 points in one hand: thirteen hearts plus the queen of spades.
constexpr int kMoonPoints = 26;

enum class Seat : uint8_t { South = 0, West = 1, North = 2, East = 3 };

inline Seat nextSeat(const Seat seat) { return static_cast<Seat>((static_cast<int>(seat) + 1) % kSeats); }
inline Seat prevSeat(const Seat seat) { return static_cast<Seat>((static_cast<int>(seat) + 3) % kSeats); }
inline Seat acrossSeat(const Seat seat) { return static_cast<Seat>((static_cast<int>(seat) + 2) % kSeats); }
inline int seatIndex(const Seat seat) { return static_cast<int>(seat); }

// Where the three passed cards go. The cycle repeats every four hands and the
// fourth is a hold, which is the standard rotation and the reason hand number
// has to survive a save.
enum class Pass : uint8_t { Left = 0, Right = 1, Across = 2, Hold = 3 };

inline Pass passForHand(const int handNumber) { return static_cast<Pass>(handNumber % 4); }

// Who `seat` hands its three cards to. Hold is the caller's problem: there is
// no valid answer, and returning the seat itself would quietly deal a player
// its own cards back through the normal path instead of skipping the phase.
inline Seat passTarget(const Seat seat, const Pass pass) {
  switch (pass) {
    case Pass::Left:
      return nextSeat(seat);
    case Pass::Right:
      return prevSeat(seat);
    case Pass::Across:
      return acrossSeat(seat);
    case Pass::Hold:
      break;
  }
  return seat;
}

// THE TRICK ORDER, AND WHY IT IS NOT rankOf().
//
// cards::Cards.h numbers the deck ace-low (0 = ace .. 12 = king) because
// Klondike builds foundations upward from the ace. In Hearts the ace is the
// HIGHEST card, so comparing two cards on their raw rank makes the ace lose
// every trick it enters -- silently, and in the one operation the entire game
// is made of. Everything in this file that asks "which card is higher" goes
// through here, and host-tests/hearts asserts the ace beats the king in all
// four suits so a regression cannot pass.
//
// Two is 1 in the deck's numbering and 0 here; ace is 0 there and 12 here.
inline int trickRank(const uint8_t card) {
  const int rank = cards::rankOf(card);
  return rank == cards::kAce ? (cards::kRanks - 1) : (rank - 1);
}

inline bool isQueenOfSpades(const uint8_t card) {
  return cards::suitOf(card) == Suit::Spades && cards::rankOf(card) == cards::kQueen;
}
inline bool isHeart(const uint8_t card) { return cards::suitOf(card) == Suit::Hearts; }

// What a card costs whoever takes the trick it is in.
inline int penaltyOf(const uint8_t card) {
  if (isQueenOfSpades(card)) return 13;
  return isHeart(card) ? 1 : 0;
}

// The two of clubs leads the first trick of every hand, always.
inline uint8_t twoOfClubs() { return cards::makeCard(Suit::Clubs, cards::kTwo); }

// What the game is waiting for. The board draws from this and nothing else, so
// a phase that draws wrong is a phase that is wrong.
enum class Phase : uint8_t {
  Passing,     // everyone is choosing three cards
  Playing,     // a trick is in progress and `turn` owes a card
  TrickTaken,  // four cards are down; the panel is showing them before the sweep
  HandOver,    // the hand scored; the panel is showing what it cost
  GameOver,    // somebody reached kTargetScore
};

// One seat's cards, kept sorted so the hand never reorders under a finger.
//
// Sorted by suit then trick rank, which is how a person holds a hand and what
// makes "I have no more clubs" readable at a glance rather than a search.
struct Hand {
  uint8_t cards[kHandSize] = {};
  uint8_t count = 0;

  uint8_t at(const int index) const { return (index < 0 || index >= count) ? kNoCard : cards[index]; }
  bool empty() const { return count == 0; }
  void clear() { count = 0; }
  void add(uint8_t card);           // keeps the sort
  bool remove(uint8_t card);        // false if it was not there
  int indexOf(uint8_t card) const;  // -1 if absent
  bool has(uint8_t card) const { return indexOf(card) >= 0; }
  bool hasSuit(Suit suit) const;
  int countSuit(Suit suit) const;
  bool onlyHearts() const;
};

// The cards on the table right now.
struct Trick {
  // Indexed by seat, so a card's position is its player and the board needs no
  // parallel array of who played what. kNoCard means that seat has not played.
  uint8_t played[kSeats] = {kNoCard, kNoCard, kNoCard, kNoCard};
  Seat leader = Seat::South;
  uint8_t count = 0;

  bool empty() const { return count == 0; }
  bool complete() const { return count == kSeats; }
  Suit ledSuit() const;  // only meaningful when !empty()
  int points() const;    // what taking it costs
  Seat winner() const;   // only meaningful when complete()
  void clear();
};

// One finished hand, kept so the score screen can say what happened rather than
// only what it totalled.
struct HandResult {
  int taken[kSeats] = {};   // points each seat took, before the moon
  int scored[kSeats] = {};  // what actually went on the scoreboard
  int total[kSeats] = {};   // the running total after this hand
  bool moon = false;
  Seat shooter = Seat::South;  // only meaningful when moon
};

struct Game {
  Hand hands[kSeats];
  Trick trick;
  int total[kSeats] = {};          // running game score
  int taken[kSeats] = {};          // points taken so far this hand
  uint8_t tricksWon[kSeats] = {};  // how many tricks, for the moon check and the board

  // Who has FAILED TO FOLLOW a led suit, and is therefore known to hold none of
  // it. Public information -- everyone at the table watched it happen -- and
  // maintained here rather than in the brain so the board can show it and the
  // brain cannot be the only thing that knows it.
  bool voidShown[kSeats][cards::kSuits] = {};

  // Every card that has left a hand this deal, indexed suit * kRanks + rank,
  // including the ones still face up on the table.
  //
  // Written down rather than worked out. It can be derived -- a card in nobody's
  // hand and not on the table has obviously been played -- but that derivation
  // is only true while hands plus table plus tricks account for all 52, so it
  // silently answers "everything has gone" for any position built with fewer.
  // That is not a hypothetical: it read the queen of spades as already fallen
  // in a three-card test position and changed what the brain did.
  bool played[cards::kDeck] = {};

  // The three cards each seat is passing. Filled during Phase::Passing and
  // consumed by commitPass().
  uint8_t passing[kSeats][kPassCount] = {};
  uint8_t passingCount[kSeats] = {};
  // What arrived, so the board can point at the three new cards for one beat.
  uint8_t received[kSeats][kPassCount] = {};
  uint8_t receivedCount[kSeats] = {};

  Seat turn = Seat::South;
  Phase phase = Phase::Passing;
  bool heartsBroken = false;
  uint8_t trickNumber = 0;  // 0..12 within the hand
  uint16_t handNumber = 0;  // grows for the whole game; picks the pass direction
  HandResult lastHand;

  Pass passDirection() const { return passForHand(handNumber); }
  bool firstTrick() const { return trickNumber == 0; }
};

// Deal a fresh hand into `game`. Does NOT touch handNumber -- nextHand() does,
// and the pass direction is read from it, so dealing twice without it deals the
// same pass direction again.
// `seed` is advanced, so a caller holding one rng gets a different deal each
// time and a test holding a fixed one gets the same deal every time.
void deal(Game& game, uint32_t& seed);

// Start a whole new game: scores to zero, hand number to zero, then deal.
void newGame(Game& game, uint32_t& seed);

// LEGALITY, over the public facts alone.
//
// Split out from isLegalPlay() so the brain can ask it from an Observation,
// which by design holds no Game. Everything a legality decision depends on is
// public -- your own hand, the table, whether hearts are broken, whether this
// is the first trick -- so this signature is complete and the brain needs no
// opinion of its own. A second implementation of "what may I play" is how a
// game ends up offering a move the rules then reject.
bool isLegalCard(const Hand& hand, const Trick& trick, bool heartsBroken, bool firstTrick, uint8_t card);

// LEGALITY. One function, asked per card, and every caller goes through it --
// the human's tap, the brain's search and the tests. A second opinion about
// what is legal is how a game ends up with a move the rules reject.
//
// The rules it enforces, in the order they bite:
//   * the two of clubs leads the very first trick, and nothing else may;
//   * follow the led suit if you hold it;
//   * on the first trick, no penalty card (heart or queen of spades) unless
//     your hand is nothing but penalty cards;
//   * do not LEAD a heart until hearts are broken, unless hearts are all you
//     hold.
bool isLegalPlay(const Game& game, Seat seat, uint8_t card);

// Every legal card in `seat`'s hand, as indices into that hand. Returns the
// count. `out` must hold kHandSize.
int legalPlays(const Game& game, Seat seat, uint8_t* out);

// Play `card` from `game.turn`. False (and no change) if it is not legal, which
// is the only failure mode: the caller never has to check first.
//
// Completing a trick moves to Phase::TrickTaken WITHOUT sweeping it, so the
// panel can show four cards. sweepTrick() does the sweep.
bool playCard(Game& game, uint8_t card);

// Take the completed trick off the table, award it, and either start the next
// trick or score the hand. Only valid in Phase::TrickTaken.
void sweepTrick(Game& game);

// Choose `seat`'s three cards to pass. False if the count is wrong or a card is
// not in the hand. Idempotent per seat: calling again replaces the selection.
bool setPass(Game& game, Seat seat, const uint8_t* cards, int count);

// True once all four seats have chosen. On Pass::Hold this is true immediately.
bool passReady(const Game& game);

// Move every seat's three cards to their target and start the hand. Only valid
// once passReady(). On Pass::Hold it deals no cards and just starts the hand.
void commitPass(Game& game);

// Score the finished hand into totals, applying the moon. Called by
// sweepTrick() on the thirteenth trick; exposed for the tests.
void scoreHand(Game& game);

// Deal the next hand of the same game. Only valid in Phase::HandOver.
void nextHand(Game& game, uint32_t& seed);

// Who is winning, for the game-over screen. Ties go to the lower seat index,
// which is the human, and the screen says "tied" rather than pretending.
Seat leader(const Game& game);
bool isTied(const Game& game);

// IS THIS GAME INTERNALLY POSSIBLE?
//
// The activity restores a game by reading the whole struct back as bytes, and a
// torn write leaves a file of exactly the right length holding a mix of two
// states. A version byte and a length check both pass on that. What does not
// pass is arithmetic: a seat index out of range, a hand longer than thirteen, a
// phase that is not one of the five, or -- the one that catches almost
// everything -- a deck that does not add up to exactly 52 distinct cards across
// the hands, the table and what has been played.
//
// Lives here rather than in the activity so it is testable without a device,
// and so the rules own the definition of a possible position.
bool isConsistent(const Game& game);

// A tiny deterministic generator, so a deal is reproducible from a seed and a
// test can pin one. xorshift32: no state beyond the seed, no library.
uint32_t nextRandom(uint32_t& seed);

}  // namespace hearts
