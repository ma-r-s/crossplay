#pragma once

// Yahtzee, the rules. Freestanding: no renderer, no Activity, no storage.
//
// Five dice, three rolls a turn, thirteen categories, thirteen turns. Every
// category is filled exactly once, so the game ends when the card is full and
// not before -- there is no "no legal move" state and no draw condition to
// invent.
//
// The rules are Hasbro's, including the upper-section bonus, the Yahtzee bonus
// and the Joker rules, which most implementations quietly drop. They are here
// because leaving them out changes the game a good player is playing: rolling a
// second Yahtzee is the single biggest swing available, and an app that scores
// it as nothing is not the game on the box. Where Hasbro's wording leaves a gap
// the choice is marked **[house]**.

#include <cstdint>

namespace yahtzee {

constexpr int kDice = 5;
constexpr int kFaces = 6;
constexpr int kRollsPerTurn = 3;

// The thirteen boxes, in the order they are printed on the card. The order is
// load-bearing: the screen draws them in it, `kUpperEnd` splits the sections,
// and the tests index by it.
enum class Category : uint8_t {
  Ones = 0,
  Twos,
  Threes,
  Fours,
  Fives,
  Sixes,
  ThreeOfAKind,
  FourOfAKind,
  FullHouse,
  SmallStraight,
  LargeStraight,
  Yahtzee,
  Chance,
  Count,
};

constexpr int kCategories = static_cast<int>(Category::Count);
// Ones through Sixes. Everything below is the lower section.
constexpr int kUpperEnd = static_cast<int>(Category::ThreeOfAKind);

constexpr int kUpperBonusThreshold = 63;
constexpr int kUpperBonus = 35;
constexpr int kFullHouseScore = 25;
constexpr int kSmallStraightScore = 30;
constexpr int kLargeStraightScore = 40;
constexpr int kYahtzeeScore = 50;
constexpr int kYahtzeeBonus = 100;

constexpr int8_t kUnscored = -1;

struct Card {
  // -1 until the box is filled. Zero is a real, common and painful score, so it
  // cannot double as "empty" -- crossing out a category you cannot make is a
  // legal and sometimes correct move.
  int8_t box[kCategories];
  // How many Yahtzees beyond the first have been rolled and scored. Each is
  // worth kYahtzeeBonus, and it is tracked rather than derived because the
  // bonus is earned at the moment of rolling and no box records it.
  uint8_t yahtzeeBonuses;
};

struct Game {
  Card card[2];
  // The five dice, faces 1..6. Meaningless before the first roll of a turn,
  // which `rollsUsed == 0` says.
  uint8_t die[kDice];
  // Which dice the player is keeping, as a bitmask over the five.
  uint8_t held;
  uint8_t rollsUsed;
  uint8_t turn;
  uint32_t rng;
};

inline uint32_t nextRandom(uint32_t& state) {
  if (state == 0) state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

inline void startCard(Card& card) {
  for (int i = 0; i < kCategories; ++i) card.box[i] = kUnscored;
  card.yahtzeeBonuses = 0;
}

inline void start(Game& game, const uint32_t seed) {
  startCard(game.card[0]);
  startCard(game.card[1]);
  for (int i = 0; i < kDice; ++i) game.die[i] = 1;
  game.held = 0;
  game.rollsUsed = 0;
  game.turn = 0;
  game.rng = seed;
}

inline bool scored(const Card& card, const Category category) {
  return card.box[static_cast<int>(category)] != kUnscored;
}

inline bool cardFull(const Card& card) {
  for (int i = 0; i < kCategories; ++i) {
    if (card.box[i] == kUnscored) return false;
  }
  return true;
}

// The game is over when BOTH cards are full. Turns alternate, so they fill
// together, but asking about both is the honest question and costs nothing.
inline bool over(const Game& game) { return cardFull(game.card[0]) && cardFull(game.card[1]); }

// How many of each face, indexed 1..6. Index 0 is unused and stays zero.
struct Counts {
  uint8_t of[kFaces + 1];
};

inline Counts countFaces(const uint8_t* die) {
  Counts counts{};
  for (int i = 0; i <= kFaces; ++i) counts.of[i] = 0;
  for (int i = 0; i < kDice; ++i) {
    if (die[i] >= 1 && die[i] <= kFaces) ++counts.of[die[i]];
  }
  return counts;
}

inline int pipTotal(const uint8_t* die) {
  int total = 0;
  for (int i = 0; i < kDice; ++i) total += die[i];
  return total;
}

inline bool isYahtzee(const uint8_t* die) {
  const Counts counts = countFaces(die);
  for (int face = 1; face <= kFaces; ++face) {
    if (counts.of[face] == kDice) return true;
  }
  return false;
}

// The longest run of consecutive faces present.
inline int longestRun(const Counts& counts) {
  int best = 0;
  int run = 0;
  for (int face = 1; face <= kFaces; ++face) {
    run = counts.of[face] > 0 ? run + 1 : 0;
    if (run > best) best = run;
  }
  return best;
}

// What `die` would score in `category`, ignoring whether the box is free and
// ignoring the Joker rules. `scoreFor` below is the one callers want.
inline int rawScore(const uint8_t* die, const Category category) {
  const Counts counts = countFaces(die);
  switch (category) {
    case Category::Ones:
    case Category::Twos:
    case Category::Threes:
    case Category::Fours:
    case Category::Fives:
    case Category::Sixes: {
      const int face = static_cast<int>(category) + 1;
      return counts.of[face] * face;
    }
    case Category::ThreeOfAKind:
      for (int face = 1; face <= kFaces; ++face) {
        if (counts.of[face] >= 3) return pipTotal(die);
      }
      return 0;
    case Category::FourOfAKind:
      for (int face = 1; face <= kFaces; ++face) {
        if (counts.of[face] >= 4) return pipTotal(die);
      }
      return 0;
    case Category::FullHouse: {
      bool three = false;
      bool two = false;
      for (int face = 1; face <= kFaces; ++face) {
        // Five of a kind IS a full house: three of them and two of them. The
        // rule that says otherwise does not exist, and dropping it would make a
        // Yahtzee score zero here, which is the opposite of the Joker rules.
        if (counts.of[face] >= 3 && !three) {
          three = true;
          if (counts.of[face] == kDice) two = true;
          continue;
        }
        if (counts.of[face] >= 2) two = true;
      }
      return three && two ? kFullHouseScore : 0;
    }
    case Category::SmallStraight:
      return longestRun(counts) >= 4 ? kSmallStraightScore : 0;
    case Category::LargeStraight:
      return longestRun(counts) >= 5 ? kLargeStraightScore : 0;
    case Category::Yahtzee:
      return isYahtzee(die) ? kYahtzeeScore : 0;
    case Category::Chance:
      return pipTotal(die);
    case Category::Count:
      return 0;
  }
  return 0;
}

// The most a box can ever hold. Used to validate a card off the wire, and it is
// per category rather than a blanket cap because a tampered card claiming 50 in
// the Ones box (real maximum: five) is exactly the kind of thing a blanket cap
// waves through.
inline int maxScore(const Category category) {
  switch (category) {
    case Category::Ones:
    case Category::Twos:
    case Category::Threes:
    case Category::Fours:
    case Category::Fives:
    case Category::Sixes:
      return (static_cast<int>(category) + 1) * kDice;
    case Category::ThreeOfAKind:
    case Category::FourOfAKind:
    case Category::Chance:
      return kFaces * kDice;
    case Category::FullHouse:
      return kFullHouseScore;
    case Category::SmallStraight:
      return kSmallStraightScore;
    case Category::LargeStraight:
      return kLargeStraightScore;
    case Category::Yahtzee:
      return kYahtzeeScore;
    case Category::Count:
      return 0;
  }
  return 0;
}

// The Joker rules, which is where implementations usually go wrong. THREE
// separate rules, and conflating any two of them is the bug.
//
// Hasbro, verbatim in structure: roll a Yahtzee when the Yahtzee box is already
// filled, and
//   (1) if it holds 50, you score a 100 bonus -- immediately, whatever box you
//       then take. A zero there earns nothing.
//   (2) you MUST use the matching upper box if it is free.
//   (3) otherwise you must use a box in the LOWER SECTION, and there the
//       straights and the full house pay in full whatever the dice show. Only
//       when every lower box is also filled may you put a zero in an upper one.
//
// Two things here were wrong and both came from guessing rather than reading.
//
// Rule (1) does not depend on (2) or (3). The first version made the bonus
// conditional on the matching upper box being gone, which silently robs a
// player of a hundred points for five threes with Threes still open.
//
// Rule (3) is a LOWER SECTION restriction, not "any free box". The second
// version offered free upper boxes while the lower section was still open --
// 30,240 (card, dice, category) triples that Hasbro forbids, reached in 1.25%
// of real games. The code was wider than its own comment, which already said
// "any lower box".
//
// **A Yahtzee box holding ZERO still triggers the Joker.** This file used to
// claim Hasbro was silent here and pick the strictest reading as [house]. That
// was simply wrong: the rulebook says the Joker applies when the box "has been
// previously filled with 50 or zero", and Hasbro's own support says a zero
// earns no bonus but still forces the Joker rules. A [house] marker on a rule
// that is written down is worse than the wrong rule, because it stops anyone
// looking it up.
inline bool yahtzeeBonusDue(const Card& card, const uint8_t* die) {
  if (!isYahtzee(die)) return false;
  if (!scored(card, Category::Yahtzee)) return false;
  // The bonus, and only the bonus, needs the 50.
  return card.box[static_cast<int>(Category::Yahtzee)] != 0;
}

// Whether the Joker is live at all: a Yahtzee, and the Yahtzee box already
// filled with anything. Rules (2) and (3) hang off this; rule (1) does not.
inline bool jokerLive(const Card& card, const uint8_t* die) {
  return isYahtzee(die) && scored(card, Category::Yahtzee);
}

inline bool anyLowerBoxFree(const Card& card) {
  for (int i = kUpperEnd; i < kCategories; ++i) {
    if (card.box[i] == kUnscored) return true;
  }
  return false;
}

// Whether rule (3) is in force: the Joker is live, the matching upper box is
// gone, so a lower box must take it and the shaped ones pay in full.
inline bool jokerApplies(const Card& card, const uint8_t* die) {
  if (!jokerLive(card, die)) return false;
  return scored(card, static_cast<Category>(die[0] - 1));
}

// Whether `category` may be taken with these dice.
inline bool canScore(const Card& card, const uint8_t* die, const Category category) {
  if (category == Category::Count) return false;
  if (scored(card, category)) return false;
  if (!jokerLive(card, die)) return true;

  // Rule (2): the matching upper box, if free, is compulsory.
  const Category matching = static_cast<Category>(die[0] - 1);
  if (!scored(card, matching)) return category == matching;

  // Rule (3): the lower section, while any of it is open.
  if (anyLowerBoxFree(card)) return static_cast<int>(category) >= kUpperEnd;
  // Everything below is full: a zero goes in an upper box of your choice.
  return true;
}

// What taking `category` with these dice would actually put in the box.
inline int scoreFor(const Card& card, const uint8_t* die, const Category category) {
  if (!jokerApplies(card, die)) return rawScore(die, category);
  switch (category) {
    case Category::FullHouse:
      return kFullHouseScore;
    case Category::SmallStraight:
      return kSmallStraightScore;
    case Category::LargeStraight:
      return kLargeStraightScore;
    default:
      return rawScore(die, category);
  }
}

inline int upperTotal(const Card& card) {
  int total = 0;
  for (int i = 0; i < kUpperEnd; ++i) {
    if (card.box[i] != kUnscored) total += card.box[i];
  }
  return total;
}

inline int bonusEarned(const Card& card) { return upperTotal(card) >= kUpperBonusThreshold ? kUpperBonus : 0; }

inline int total(const Card& card) {
  int sum = bonusEarned(card) + card.yahtzeeBonuses * kYahtzeeBonus;
  for (int i = 0; i < kCategories; ++i) {
    if (card.box[i] != kUnscored) sum += card.box[i];
  }
  return sum;
}

inline bool canRoll(const Game& game) { return !over(game) && game.rollsUsed < kRollsPerTurn; }

// Roll every die that is not held. Returns false and changes nothing when there
// is no roll left, so a caller can route a tap straight here.
//
// The first roll of a turn ignores `held`, because holding a die from the
// previous player's turn is not a thing the game has.
inline bool roll(Game& game) {
  if (!canRoll(game)) return false;
  const bool first = game.rollsUsed == 0;
  for (int i = 0; i < kDice; ++i) {
    if (!first && (game.held & (1 << i)) != 0) continue;
    game.die[i] = static_cast<uint8_t>(nextRandom(game.rng) % kFaces + 1);
  }
  if (first) game.held = 0;
  ++game.rollsUsed;
  return true;
}

// Keep or release a die. Only between rolls: before the first roll there is
// nothing to keep, and after the third there is nothing left to re-roll, so
// holding would be a control that does nothing.
inline bool canHold(const Game& game) { return game.rollsUsed > 0 && game.rollsUsed < kRollsPerTurn && !over(game); }

inline bool toggleHold(Game& game, const int index) {
  if (!canHold(game) || index < 0 || index >= kDice) return false;
  game.held ^= static_cast<uint8_t>(1 << index);
  return true;
}

// Take a box, and hand the turn over. Returns false and changes nothing when
// the box is not takeable or nothing has been rolled yet.
inline bool take(Game& game, const Category category) {
  if (over(game) || game.rollsUsed == 0) return false;
  Card& card = game.card[game.turn];
  if (!canScore(card, game.die, category)) return false;

  // The bonus is earned by ROLLING a Yahtzee, before the box is chosen and
  // whatever box is chosen, so it is counted here rather than inside any
  // score. yahtzeeBonusDue, not jokerApplies: the hundred is owed even when the
  // matching upper box is still free and rule (2) is forcing your hand.
  if (yahtzeeBonusDue(card, game.die)) ++card.yahtzeeBonuses;

  card.box[static_cast<int>(category)] = static_cast<int8_t>(scoreFor(card, game.die, category));
  game.held = 0;
  game.rollsUsed = 0;
  game.turn = static_cast<uint8_t>(1 - game.turn);
  return true;
}

// How many boxes are still open on a card. Thirteen at the start, and it is
// also how many turns that player has left.
inline int boxesLeft(const Card& card) {
  int left = 0;
  for (int i = 0; i < kCategories; ++i) {
    if (card.box[i] == kUnscored) ++left;
  }
  return left;
}

// Whether a state could have come from real play. Checked on anything arriving
// over the wire: a corrupt packet must not be able to put the game in a
// position the rules cannot produce.
inline bool plausible(const Game& game) {
  if (game.turn > 1) return false;
  if (game.rollsUsed > kRollsPerTurn) return false;
  if (game.held >= (1 << kDice)) return false;
  // Nothing is held before the first roll of a turn: roll() clears it, and
  // toggleHold refuses. A wire state with held dice at rollsUsed 0 reaches
  // roll()'s `if (first) game.held = 0` from a direction play cannot.
  if (game.rollsUsed == 0 && game.held != 0) return false;
  for (int i = 0; i < kDice; ++i) {
    if (game.die[i] < 1 || game.die[i] > kFaces) return false;
  }
  for (int player = 0; player < 2; ++player) {
    const Card& card = game.card[player];
    for (int i = 0; i < kCategories; ++i) {
      const int8_t value = card.box[i];
      if (value == kUnscored) continue;
      if (value < 0) return false;
      const Category category = static_cast<Category>(i);
      if (value > maxScore(category)) return false;
      // The FIXED-value boxes hold their value or a zero, nothing between. A
      // magnitude cap gets the size right and the shape wrong: 17 in Small
      // Straight and 25 in Yahtzee both passed it, and neither is a score the
      // rules can produce.
      switch (category) {
        case Category::FullHouse:
        case Category::SmallStraight:
        case Category::LargeStraight:
        case Category::Yahtzee:
          if (value != 0 && value != maxScore(category)) return false;
          break;
        case Category::Ones:
        case Category::Twos:
        case Category::Threes:
        case Category::Fours:
        case Category::Fives:
        case Category::Sixes:
          // An upper box is a multiple of its own face.
          if (value % (i + 1) != 0) return false;
          break;
        case Category::ThreeOfAKind:
        case Category::FourOfAKind:
          // Five dice, at least three of a kind, so the total cannot be under
          // 1+1+1+1+1 and cannot be 2 or 3.
          if (value != 0 && value < kDice) return false;
          break;
        case Category::Chance:
          if (value != 0 && value < kDice) return false;
          break;
        case Category::Count:
          return false;
      }
    }
    // TWELVE, not thirteen. The first Yahtzee costs a turn to score in the
    // Yahtzee box and earns no bonus, so twelve turns remain in which one can
    // be earned. The off-by-one let an impossible card through.
    if (card.yahtzeeBonuses > kCategories - 1) return false;
    // A bonus cannot exist without a Yahtzee having been scored above zero.
    if (card.yahtzeeBonuses > 0 &&
        (!scored(card, Category::Yahtzee) || card.box[static_cast<int>(Category::Yahtzee)] == 0)) {
      return false;
    }
  }
  // Turns alternate from player zero, so the two cards are level or player zero
  // is exactly one ahead.
  const int filled0 = kCategories - boxesLeft(game.card[0]);
  const int filled1 = kCategories - boxesLeft(game.card[1]);
  if (filled0 != filled1 && filled0 != filled1 + 1) return false;
  if (!over(game) && game.turn != (filled0 == filled1 ? 0 : 1)) return false;
  // A finished game has no turn in progress.
  if (over(game) && (game.rollsUsed != 0 || game.held != 0)) return false;
  return true;
}

}  // namespace yahtzee
