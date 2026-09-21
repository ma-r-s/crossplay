#pragma once

// The Hearts opponents. Freestanding: no renderer, no Activity, no storage.
//
// IT TAKES AN OBSERVATION, NEVER A GAME, and that is the whole architecture.
// An Observation has no field for anybody else's hand, so a brain cannot cheat
// by construction rather than by discipline -- the same rule SeaSaltBrain.h
// sets out and for the same reason. It matters more here than there: three of
// the four seats at this table are brains running in one process, and the
// tempting shortcut (one brain that can see all four hands) would turn a game
// of incomplete information into a solved one while every test still passed.
//
// Each seat therefore gets its OWN observation, rebuilt per decision from what
// that seat is entitled to know: its cards, the table, the cards already
// played, and who has shown void in what. All of that is public at a real
// table; none of it is a peek.
//
// No search. Hearts rewards bookkeeping rather than depth: the whole of decent
// play is duck under the trick, remember who is void, do not hand anyone the
// queen, and notice a moon before it lands. Those are rules over a value
// function, which is what this is, and a rule-based Hearts bot plays a genuinely
// good casual game -- unlike, say, poker, where the same approach produces an
// opponent that cannot bluff.

#include <cstdint>

#include "HeartsCore.h"

namespace hearts {

// How hard the opponent tries. Two settings rather than a slider, like Jaipur's
// and Sea Salt's: the honest difference is a set of behaviours, not a number.
//
// A ROOKIE follows suit, ducks under a trick when it can, and sheds its highest
// card when it is void. It does not track who is void, does not hunt the queen,
// and never notices a moon.
//
// A SHARP does all of that plus the three things that make Hearts a game:
// it counts what has gone, it reads a seat void in a suit as a seat that will
// dump the queen on it, and it will deliberately eat a point trick to stop
// somebody shooting.
enum class Skill : uint8_t {
  Rookie = 0,
  Sharp,
};

// What one seat legally knows. Rebuilt per decision; nothing here is private to
// another player.
struct Observation {
  Seat me = Seat::South;
  Hand hand;    // my own cards
  Trick trick;  // what is on the table, and who led
  Phase phase = Phase::Playing;

  // Every card dealt this hand that has already been played, indexed
  // suit * kRanks + rank. Public: it is what everyone watched happen.
  bool gone[cards::kDeck] = {};

  // Who has shown void in what, from failing to follow a led suit. Also public,
  // and the single most valuable thing a Hearts player carries in their head.
  bool showsVoid[kSeats][cards::kSuits] = {};

  int total[kSeats] = {};  // running game scores
  int taken[kSeats] = {};  // points taken so far this hand
  bool heartsBroken = false;
  uint8_t trickNumber = 0;
  Pass passDirection = Pass::Left;

  bool firstTrick() const { return trickNumber == 0; }
  bool wasPlayed(Suit suit, int rank) const { return gone[static_cast<int>(suit) * cards::kRanks + rank]; }
  // HAS THE QUEEN FALLEN? Not the same question as "is she in `gone`".
  //
  // observe() deliberately unmarks the cards lying face up in the current
  // trick, because for counting the trick in front of you they are visible
  // rather than spent. For "has she come out yet" that is wrong: a Sharp
  // fourth to play, with the queen already thrown onto this very trick, still
  // ducked to avoid drawing her and still paid to shed high spades before her
  // arrival. Two different questions were sharing one array.
  bool queenGone() const {
    if (wasPlayed(Suit::Spades, cards::kQueen)) return true;
    for (int s = 0; s < kSeats; ++s) {
      if (trick.played[s] != kNoCard && isQueenOfSpades(trick.played[s])) return true;
    }
    return false;
  }
};

// Fill `out` with what `seat` is entitled to know about `game`. The only
// function that ever reads another seat's hand is this one, and it reads it to
// work out what is GONE, never what is held -- the cards still in other hands
// are exactly the ones it leaves unmarked.
void observe(const Game& game, Seat seat, Observation& out);

// The three cards `seat` passes. Writes kPassCount cards into `out`.
void decidePass(const Observation& obs, Skill skill, uint32_t& rng, uint8_t* out);

// The card to play. Always one the rules will accept: the soak drives whole
// games through this and asserts exactly that.
uint8_t decidePlay(const Observation& obs, Skill skill, uint32_t& rng);

// How much this seat wants rid of `card` before a hand starts, in arbitrary
// units where bigger means pass it. Exposed because the tests calibrate against
// it: a value function nobody can inspect is a value function nobody can fix.
int passUrgency(const Observation& obs, uint8_t card);

// AM I SHOOTING? True when this seat holds every point taken so far and still
// holds the cards to finish the job.
//
// The brains had moon DEFENCE and no moon OFFENCE at all, so the SHOT THE MOON
// banner could only ever fire for the human and a quarter of what makes Hearts
// tense was missing from three of the four seats. Deliberately hard to enter: a
// shoot that fails hands you every point you collected on the way, so the bar
// is "already committed and still holding the top cards", never a hopeful start.
bool shootingTheMoon(const Observation& obs);

// Is somebody about to shoot the moon? Returns that seat, or -1. A seat is a
// threat when it holds every point taken so far and there are enough taken to
// mean something. Exposed for the same reason.
int moonThreat(const Observation& obs);

}  // namespace hearts
