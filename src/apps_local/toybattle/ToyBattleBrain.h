#pragma once

// The Toy Battle opponent. Knows the rules and nothing else: no renderer, no
// storage, no radio.
//
// It cannot cheat, and that is a property of its input rather than a rule
// anyone follows. `chooseMove` takes an `Observation`, and an Observation is
// built only from what a player sitting opposite can see: the board, the
// discards, both rack *sizes*, and the multiset the enemy rack must have been
// drawn from. No secret is present, so no decision can depend on one.
//
// Two things a real player also cannot know are therefore modelled rather than
// simulated:
//
//   * The enemy rack's composition. `observe` rebuilds a plausible one from the
//     public multiset so that positions can be played forward at all. It is a
//     public function of public data, so it leaks nothing -- but it is a guess,
//     and no decision reads it directly. The threats that matter are computed
//     from the board instead, where they are exact.
//   * The order of either reserve. `observe` clears the seed, so a draw is
//     worth tempo rather than a known troop. Neither player knows what is
//     coming, and neither does this.

#include <cstdint>

#include "ToyBattleCore.h"

namespace toybattle {

// Both skills keep the same two reflexes -- take the win in front of you, never
// leave your own H.Q. open -- because in a game with sudden death those are
// table stakes rather than a difficulty setting. Recruit has nothing else and
// plays freely among the safe moves; General has an evaluation and a search.
//
// **What makes it strong is depth, and nothing else has ever measured.** Three
// separate attempts to strengthen this brain by adding evaluation terms -- a
// region race, pressure on the enemy H.Q., a Cap'n threat, then a region race
// and a medal-threat penalty again in the tournament -- were each worth nothing
// or slightly negative, and the last pair made the searching brain *worse* than
// the same brain without them. Meanwhile looking one move further ahead is
// worth 71 points against the brain that shipped before it.
//
// The reason is structural rather than a matter of tuning. A greedy brain
// already scores a region the instant it closes one, because the engine banks
// the medals inside `apply` -- so a term saying "I am close to a region" tells
// it something the medal count is about to tell it anyway. What it cannot see
// is *your* reply: nothing stopped it handing you a three-medal region, because
// the only threat it ever computed was its own H.Q. Adding a term for one more
// threat patches one hole; searching the reply closes the class.
//
// So: do not add an evaluation term to this file without a tournament run
// showing it earns its place. Four have been tried and four have been deleted.
enum class Skill : uint8_t { Recruit = 0, General };

// How hard it plays, as knobs rather than as a name. Every previous attempt to
// make this brain stronger added a term to the evaluation and measured nothing,
// which is the note above; the knob that was never tried is looking at the
// reply. A `Skill` is now just whichever policy won the tournament in
// `host-tests/toybattle/tournament.cpp`, and the tournament is the only thing
// allowed to decide that.
struct Policy {
  // How many of the best moves get a reply computed for them. 0 plays greedily,
  // which is what every version before this one did: it evaluates the position
  // its own move leaves and never asks what happens next. The one threat it
  // sees is its H.Q., because that one is exact.
  uint8_t beam = 0;
  // Plies searched along the beam. 2 is my move and their answer; 3 adds my
  // answer to that, which is the first depth at which a sacrifice can pay.
  uint8_t depth = 2;
  // Medals banked, territory, reach, tempo. The evaluation as it stands.
  bool material = true;
};

Policy policyFor(Skill skill);

struct Observation {
  // The position, playable forward. The enemy rack here is a reconstruction
  // (see the file comment) and the seed is cleared.
  Game view;
  uint8_t seat = 0;
  uint8_t opponentRackSize = 0;
  // What the enemy rack could hold: 3 of each kind, less what is already on the
  // board or in their discard. Their reserve and the 4 they set aside unseen
  // are in here too, which is exactly the ambiguity a player faces.
  uint8_t unseen[kTroopKinds] = {};
};

Observation observe(const Game& game, int seat);

// The move this skill would play. Always legal in the real game the
// observation came from; never a move that leaves an H.Q. the opponent can
// take on the spot, unless every legal move does.
Move chooseMove(const Observation& obs, Skill skill);
Move chooseMove(const Observation& obs, const Policy& policy);

// Exposed for the tests, which need to ask the questions the brain asks
// without reaching inside it.
namespace detail {

// Can the seat that is *not* `seat` place a troop on one of `seat`'s H.Q. right
// now? Exact, and computed from the board: any troop captures an H.Q., so the
// only questions are whether the H.Q. is connected for them and whether a gate
// admits anything they could be holding.
bool hqIsExposed(const Game& v, int seat, const uint8_t* unseen, int opponentRackSize);

int evaluate(const Game& v, int seat, const uint8_t* unseen, int opponentRackSize, const Policy& policy);

// Positions applied since the last reset. The only honest way to compare a
// policy's cost, and the number that decides whether one is affordable on the
// device: two counters, incremented in the search, read by the tournament.
struct Cost {
  uint32_t positions = 0;
  uint32_t worstPerMove = 0;
};
extern Cost cost;
void resetCost();

// Every move worth considering from here, effect and base-effect choices
// included. Bounded: see the .cpp for what is dropped and why.
int candidates(const Observation& obs, Move* out, int max);

}  // namespace detail

}  // namespace toybattle
