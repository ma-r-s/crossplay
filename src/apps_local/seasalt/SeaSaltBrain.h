#pragma once

// The Sea Salt & Paper opponent. Freestanding: no renderer, no Activity, no
// storage.
//
// It takes an `Observation`, never a `Game`. An Observation has no field for
// the opponent's unrevealed hand -- the deck and that hand collapse into the
// same fog -- so this cannot cheat by construction rather than by discipline.
//
// No search. The hard decisions in this game are not deep: they are "is this
// card worth more to me than an average draw", asked at every step, and "am I
// far enough ahead to bet", asked once. Both are questions about the value of
// cards, so the whole brain is one marginal-value function and a handful of
// comparisons against it.

#include "SeaSaltCore.h"

namespace seasalt {

// How hard the opponent tries. Two settings rather than a slider, like
// Jaipur's: the honest difference is one behaviour, not a number. A
// Beachcomber never bets -- it stops as soon as stopping is safe. A Navigator
// estimates what your hidden hand is worth, races the deck, and calls LAST
// CHANCE when the margin says it will hold.
enum class Skill : uint8_t {
  Beachcomber = 0,
  Navigator,
};

// One outstanding decision, answered. The step in the observation says which
// decision that is; the driver (Activity or test) translates the answer into
// the corresponding Game call. LayDuo names kinds, not card ids, because which
// physical crab leaves your hand is not a decision at all.
struct Decision {
  enum class Act : uint8_t {
    TakeDeck = 0,  // Step::Take
    TakePile,      // Step::Take           pile in `a`
    Keep,          // Step::ChooseKeep     drawn index in `a`
    DiscardTo,     // Step::ChoosePile     pile in `a`
    LayDuo,        // Step::Play           `kind` (Swimmer means the pair)
    EndTurn,       // Step::Play
    Stop,          // Step::Play
    LastChance,    // Step::Play
    DigPile,       // Step::CrabPile       pile in `a`
    DigCard,       // Step::CrabPick       card id in `a` -- the pile is public
  };
  Act act = Act::EndTurn;
  uint8_t a = 0;
  Kind kind = Kind::Crab;
};

// The decision this opponent makes in the state the observation describes.
// Always one the core will accept; the soak drives whole matches through it
// and asserts exactly that.
Decision decide(const Observation& obs, Skill skill, uint32_t& rng);

// What one more card of `kind` would add to my score, in quarter-points.
// Quarter-points so a colour tiebreak fits underneath the real values instead
// of beside them. Exposed because the tests calibrate against it: a value
// function nobody can inspect is a value function nobody can fix.
int marginalValue(const Observation& obs, uint8_t card);

// The Navigator's estimate of the opponent's card points: their table, which
// is public, plus fog cards at the average marginal value of the unseen
// census. Exposed for the same reason.
int estimateTheirPoints(const Observation& obs);

}  // namespace seasalt
