#pragma once

// The solo opponent. Freestanding, deterministic and pure: the same position
// always produces the same decision, and it decides from the card and the dice
// alone.
//
// It cannot cheat by construction, and that is a structural claim rather than a
// promise: `chooseHold` and `chooseBox` take the Game by const reference and
// never touch `game.rng`, so they cannot look at the dice they are about to
// roll. Knucklebones learned this the hard way -- a brain that copies the whole
// state to search with is one line away from seeing the future.
//
// The hold decision is an EXACT one-step lookahead, not a sample. For each of
// the 32 ways to keep dice it enumerates every outcome of re-rolling the rest
// and takes the expectation. That is only affordable because dice are
// unordered: enumerating multisets with multinomial weights turns 6^5 = 7776
// ordered outcomes into 252. Counted over a whole chooseHold call, across all
// 32 masks: 1,683 evaluations against 16,807 -- a ten-fold saving.
//
// It used to claim "3,200 instead of 250,000", which is 78-fold and wrong in
// both terms. 250,000 is 32 x 7776, which assumes every mask re-rolls all five
// dice; most re-roll fewer. Ten-fold is still the difference between instant
// and a visible stall on this device, and it is the number that is true.

#include <cstdint>

#include "YahtzeeCore.h"

namespace yahtzee {

// What each box is worth to a player who is aiming for it, near enough. Used as
// the opportunity cost of spending a box: taking a box is good when the dice
// beat what that box would usually give you later.
//
// Measured rather than asserted, because the justification written here first
// was wrong in both halves. Removing kBoxWorth entirely, over 200 games:
//
//   shipped          first-turn Chance   0/200,  Yahtzee crossed out 135/200
//   kBoxWorth gone   first-turn Chance  45/200,  Yahtzee crossed out 122/200
//
// So it does stop first-turn Chance, but that was 22% of games and not "every
// game"; and it makes the Yahtzee box WORSE, not better -- this brain crosses
// Yahtzee out in two games of every three either way. Solo strength is 219
// against 220 without it, and the record against a greedy mover is identical.
//
// It is kept because the ordering it encodes is right and the cost is nothing,
// not because it is doing what the first comment claimed.
constexpr int kBoxWorth[kCategories] = {
    2,   // Ones
    4,   // Twos
    6,   // Threes
    8,   // Fours
    10,  // Fives
    12,  // Sixes
    22,  // Three of a kind
    13,  // Four of a kind
    22,  // Full house
    29,  // Small straight
    24,  // Large straight
    17,  // Yahtzee
    22,  // Chance
};

// How much an upper-section point is worth beyond its face value, because 63
// buys 35. Spread over the 63 points it takes to get there, a point is worth
// about 0.55 extra -- but only while the bonus is still live.
constexpr int kUpperCreditNumerator = 5;
constexpr int kUpperCreditDenominator = 9;

// What taking `category` with these dice is worth to this card, all in.
//
// Score, plus credit for upper-section progress that is still chasing a live
// bonus, minus what the box would have been worth later.
inline int takeValue(const Card& card, const uint8_t* die, const Category category) {
  const int index = static_cast<int>(category);
  const int score = scoreFor(card, die, category);
  int value = score * 10;

  if (index < kUpperEnd) {
    int best = upperTotal(card);
    for (int i = 0; i < kUpperEnd; ++i) {
      if (card.box[i] == kUnscored && i != index) best += (i + 1) * kDice;
    }
    best += score;
    // Only pay the premium while 63 is still reachable. Chasing a bonus that
    // cannot arrive is how a bot talks itself into a 2 in Sixes.
    if (best >= kUpperBonusThreshold) {
      value += score * 10 * kUpperCreditNumerator / kUpperCreditDenominator;
    }
  }

  return value - kBoxWorth[index] * 10;
}

// The best box to take right now, or Count when there is none.
//
// Ties break toward the LOWER index, which is the printed order on the card,
// so the choice is reproducible and does not depend on iteration accidents.
inline Category chooseBox(const Card& card, const uint8_t* die) {
  Category best = Category::Count;
  int bestValue = 0;
  for (int i = 0; i < kCategories; ++i) {
    const Category category = static_cast<Category>(i);
    if (!canScore(card, die, category)) continue;
    const int value = takeValue(card, die, category);
    if (best == Category::Count || value > bestValue) {
      best = category;
      bestValue = value;
    }
  }
  return best;
}

// The value of a finished hand: the best box it could be put in.
inline int handValue(const Card& card, const uint8_t* die) {
  const Category best = chooseBox(card, die);
  return best == Category::Count ? 0 : takeValue(card, die, best);
}

// Every way to roll `count` dice, as multisets, with their weights.
//
// Recursion over non-decreasing faces, so 3 4 4 is generated once rather than
// three times, and the weight carries the multinomial that lost. Depth is at
// most five.
template <typename Visit>
void forEachOutcome(uint8_t* out, const int count, const int index, const int lowest, Visit&& visit) {
  if (index == count) {
    visit(out);
    return;
  }
  for (int face = lowest; face <= kFaces; ++face) {
    out[index] = static_cast<uint8_t>(face);
    forEachOutcome(out, count, index + 1, face, visit);
  }
}

// The number of orderings a particular multiset has, which is its probability
// weight before dividing by 6^count. Computed from the sorted faces the
// enumeration produces.
inline int64_t orderings(const uint8_t* die, const int count) {
  int64_t result = 1;
  int64_t chosen = 0;
  int run = 1;
  for (int i = 1; i <= count; ++i) {
    if (i < count && die[i] == die[i - 1]) {
      ++run;
      continue;
    }
    // Multiply in C(remaining, run).
    for (int k = 1; k <= run; ++k) {
      result = result * (count - chosen - run + k) / k;
    }
    chosen += run;
    run = 1;
  }
  return result;
}

// Which dice to keep, as a bitmask, given a card and the dice on the table.
//
// Exact expectation over one re-roll. It does not model the SECOND re-roll,
// which would square the work; one step is what fits, and it already beats
// every heuristic that does not look at the card at all.
//
// **Where this is known to differ from conventional play.** Holding 2 3 4 5 5,
// this keeps the PAIR rather than the four to an outside straight. That falls
// out of the two modelling choices above and is not a bug in either:
// `handValue` has to assume the hand is scored NOW, and under opportunity-cost
// valuation filling Small Straight at exactly its par value is worth about
// nothing -- so the guaranteed fallback that makes the run attractive to a
// human scores as almost zero here. Deeper lookahead, or a valuation that
// credits keeping a box open, would change it. Measured strength does not
// suffer (average 230 solo, 26-4 against a real greedy strategy), so it is
// recorded rather than tuned away: bending a constant until one hand agrees
// with folklore is how an evaluation stops meaning anything.
inline uint8_t chooseHold(const Card& card, const uint8_t* die) {
  uint8_t bestMask = 0;
  int64_t bestValue = 0;
  // A flag, not a sentinel. takeValue is a score minus an opportunity cost and
  // is routinely negative, so "-1 means nothing seen yet" would make the LAST
  // mask win every hand where no keep is worth its box.
  bool seen = false;

  for (uint8_t mask = 0; mask < (1u << kDice); ++mask) {
    uint8_t hand[kDice];
    int kept = 0;
    for (int i = 0; i < kDice; ++i) {
      if ((mask & (1u << i)) != 0) hand[kept++] = die[i];
    }
    const int free = kDice - kept;

    int64_t total = 0;
    int64_t weight = 0;
    uint8_t rolled[kDice];
    forEachOutcome(rolled, free, 0, 1, [&](const uint8_t* faces) {
      for (int i = 0; i < free; ++i) hand[kept + i] = faces[i];
      const int64_t ways = orderings(faces, free);
      total += static_cast<int64_t>(handValue(card, hand)) * ways;
      weight += ways;
    });
    if (weight == 0) continue;
    // Compare expectations on a common denominator rather than dividing, so
    // integer truncation cannot decide a close call.
    const int64_t value = total * 7776 / weight;

    // Ties break toward keeping FEWER dice, then toward the lower mask.
    //
    // Both halves are deliberate. Fewer is the better default because it is the
    // choice that has not committed, and every tie in this game is between a
    // keep and a strictly larger keep that adds nothing. The mask order alone
    // would NOT give that -- 0b00011 keeps two dice and sorts before 0b00100,
    // which keeps one -- so the popcount has to be compared explicitly. Without
    // it the comment would have been describing something the loop does not do.
    if (!seen || value > bestValue ||
        (value == bestValue && __builtin_popcount(mask) < __builtin_popcount(bestMask))) {
      seen = true;
      bestValue = value;
      bestMask = mask;
    }
  }
  return bestMask;
}

}  // namespace yahtzee
