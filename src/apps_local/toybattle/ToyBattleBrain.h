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
// table stakes rather than a difficulty setting. What separates them is whether
// there is any opinion behind that: Recruit has none and plays freely among the
// safe moves, General weighs medals, territory, reach and tempo.
//
// The split is where it is because that is where it measured. An earlier
// version gave General extra evaluation terms -- region racing, pressure on the
// enemy H.Q., a Cap'n threat looked one turn ahead -- and over 600 games each
// of them was worth nothing: 48%, 48%, 50% against the same opponent without
// them. They were deleted. The gap that does exist is between having an
// evaluation and not having one, and that one is 99%.
enum class Skill : uint8_t { Recruit = 0, General };

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

// Exposed for the tests, which need to ask the questions the brain asks
// without reaching inside it.
namespace detail {

// Can the seat that is *not* `seat` place a troop on one of `seat`'s H.Q. right
// now? Exact, and computed from the board: any troop captures an H.Q., so the
// only questions are whether the H.Q. is connected for them and whether a gate
// admits anything they could be holding.
bool hqIsExposed(const Game& v, int seat, const uint8_t* unseen, int opponentRackSize);

int evaluate(const Game& v, int seat, const uint8_t* unseen, int opponentRackSize, Skill skill);

// Every move worth considering from here, effect and base-effect choices
// included. Bounded: see the .cpp for what is dropped and why.
int candidates(const Observation& obs, Move* out, int max);

}  // namespace detail

}  // namespace toybattle
