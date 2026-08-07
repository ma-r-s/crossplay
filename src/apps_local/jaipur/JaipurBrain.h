#pragma once

// The Jaipur opponent. Freestanding: no renderer, no Activity, no storage.
//
// It takes an `Observation`, never a `Game`. That is the whole point: an
// Observation has no field for the opponent's hand, so this cannot cheat by
// construction rather than by discipline. What it knows is what a person
// sitting opposite would know -- the market, its own hand, both herds, every
// token pile, and how many cards have gone where.
//
// Jaipur hides much less than it looks like it does. Every take, trade and sale
// is public, camels are face up, and the token piles record what was sold. The
// only genuine unknown is which cards came off the deck into the other hand,
// drawn from a deck whose composition is known exactly. So the belief state is
// small and exact, and a heuristic over it plays a real game without any
// search: the hard part of Jaipur is knowing what a position is worth, not
// looking further ahead.

#include "JaipurCore.h"

namespace jaipur {

// How hard the opponent tries. Two settings rather than a slider: the honest
// difference is whether it plans a sale or takes the obvious card, and three
// arbitrary points on a scale would be three things to tune and no better.
enum class Skill : uint8_t {
  Merchant = 0,  // plays the board in front of it
  Maharaja,      // also counts what is left and races the round's end
};

// The move this opponent would play. Always legal against the game the
// observation came from; falls back to the least bad legal move rather than
// ever returning something illegal.
Move chooseMove(const Observation& obs, Skill skill, uint32_t& rng);

// Every legal move from an observation, which is also what the UI would allow.
// Exposed because the tests enumerate it and because a soak needs it.
// Writes at most `capacity` moves and returns how many it wrote.
int legalMoves(const Observation& obs, Move* out, int capacity);

// What a position is worth to `obs.seat`, in rupees-ish units. Exposed for the
// tests: a scoring function nobody can inspect is a scoring function nobody can
// fix.
int evaluate(const Observation& obs);

}  // namespace jaipur
