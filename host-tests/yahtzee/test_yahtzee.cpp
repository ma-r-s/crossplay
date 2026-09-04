// Yahtzee's rules, its two state machines and its opponent.
//
// The Joker rules get most of the attention here because they are where this
// game is usually wrong, and because two of the three bugs found while writing
// it were in them.

#include <cstdio>
#include <cstring>

#include "YahtzeeBrain.h"
#include "YahtzeeCore.h"
#include "YahtzeeFlow.h"

using namespace yahtzee;

namespace {

int checks = 0;
int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

uint32_t rngState = 0x1BADB002u;
uint32_t roll32() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

void setDice(Game& game, const int a, const int b, const int c, const int d, const int e) {
  game.die[0] = static_cast<uint8_t>(a);
  game.die[1] = static_cast<uint8_t>(b);
  game.die[2] = static_cast<uint8_t>(c);
  game.die[3] = static_cast<uint8_t>(d);
  game.die[4] = static_cast<uint8_t>(e);
}

uint8_t dice(const int a, const int b, const int c, const int d, const int e, uint8_t* out) {
  out[0] = static_cast<uint8_t>(a);
  out[1] = static_cast<uint8_t>(b);
  out[2] = static_cast<uint8_t>(c);
  out[3] = static_cast<uint8_t>(d);
  out[4] = static_cast<uint8_t>(e);
  return 0;
}

void testAFreshCardIsEmptyAndScoresNothing() {
  Card card{};
  startCard(card);
  for (int i = 0; i < kCategories; ++i) CHECK(card.box[i] == kUnscored);
  CHECK(total(card) == 0);
  CHECK(boxesLeft(card) == kCategories);
  CHECK(!cardFull(card));
  CHECK(upperTotal(card) == 0);
  CHECK(bonusEarned(card) == 0);
  CHECK(upperShortfall(card) == kUpperBonusThreshold);
  CHECK(upperBonusStillPossible(card));
}

void testEveryCategoryScoresWhatTheRulesSay() {
  uint8_t d[kDice];

  dice(3, 3, 3, 5, 1, d);
  CHECK(rawScore(d, Category::Threes) == 9);
  CHECK(rawScore(d, Category::Ones) == 1);
  CHECK(rawScore(d, Category::Twos) == 0);
  CHECK(rawScore(d, Category::ThreeOfAKind) == 15);
  CHECK(rawScore(d, Category::FourOfAKind) == 0);
  CHECK(rawScore(d, Category::FullHouse) == 0);
  CHECK(rawScore(d, Category::Chance) == 15);

  dice(4, 4, 4, 2, 2, d);
  CHECK(rawScore(d, Category::FullHouse) == kFullHouseScore);
  CHECK(rawScore(d, Category::ThreeOfAKind) == 16);

  // Two pair is not a full house, and neither is three of a kind alone.
  dice(4, 4, 2, 2, 5, d);
  CHECK(rawScore(d, Category::FullHouse) == 0);
  dice(4, 4, 4, 2, 5, d);
  CHECK(rawScore(d, Category::FullHouse) == 0);

  // Five of a kind IS a full house: three of them and two of them.
  dice(6, 6, 6, 6, 6, d);
  CHECK(rawScore(d, Category::FullHouse) == kFullHouseScore);
  CHECK(rawScore(d, Category::Yahtzee) == kYahtzeeScore);
  CHECK(rawScore(d, Category::FourOfAKind) == 30);
  CHECK(rawScore(d, Category::LargeStraight) == 0);

  dice(1, 2, 3, 4, 6, d);
  CHECK(rawScore(d, Category::SmallStraight) == kSmallStraightScore);
  CHECK(rawScore(d, Category::LargeStraight) == 0);
  dice(2, 3, 4, 5, 6, d);
  CHECK(rawScore(d, Category::LargeStraight) == kLargeStraightScore);
  CHECK(rawScore(d, Category::SmallStraight) == kSmallStraightScore);
  // A run of four with a duplicate is still a small straight.
  dice(3, 3, 4, 5, 6, d);
  CHECK(rawScore(d, Category::SmallStraight) == kSmallStraightScore);
  CHECK(rawScore(d, Category::LargeStraight) == 0);
  // 1-2-3 plus 5-6 is not a straight of any kind.
  dice(1, 2, 3, 5, 6, d);
  CHECK(rawScore(d, Category::SmallStraight) == 0);
}

// Every printed constant, against its literal. The old bonus test asserted
// bonusEarned(card) == kUpperBonus, which is true whatever kUpperBonus is: a
// mutation run found the 35 in that test's own NAME was not pinned, and neither
// were 25, 30, 40 or 100.
void testThePrintedNumbersAreTheNumbersOnTheCard() {
  CHECK(kUpperBonusThreshold == 63);
  CHECK(kUpperBonus == 35);
  CHECK(kFullHouseScore == 25);
  CHECK(kSmallStraightScore == 30);
  CHECK(kLargeStraightScore == 40);
  CHECK(kYahtzeeScore == 50);
  CHECK(kYahtzeeBonus == 100);
  CHECK(kDice == 5);
  CHECK(kRollsPerTurn == 3);
  CHECK(kCategories == 13);

  // And they reach a real card, so renaming a constant cannot quietly detach
  // the literal from the score.
  Card card{};
  startCard(card);
  for (int i = 0; i < kUpperEnd; ++i) card.box[i] = static_cast<int8_t>((i + 1) * 3);
  CHECK(total(card) == 63 + 35);
  startCard(card);
  card.box[static_cast<int>(Category::LargeStraight)] = static_cast<int8_t>(kLargeStraightScore);
  CHECK(total(card) == 40);
}

void testTheUpperBonusIsSixtyThreeAndThirtyFive() {
  Card card{};
  startCard(card);
  // Exactly three of each: 3+6+9+12+15+18 = 63.
  for (int i = 0; i < kUpperEnd; ++i) card.box[i] = static_cast<int8_t>((i + 1) * 3);
  CHECK(upperTotal(card) == 63);
  CHECK(bonusEarned(card) == kUpperBonus);
  CHECK(upperShortfall(card) == 0);

  // One point short earns nothing. The threshold is a cliff and the test says so.
  card.box[0] = 2;
  CHECK(upperTotal(card) == 62);
  CHECK(bonusEarned(card) == 0);
  CHECK(upperShortfall(card) == 1);
}

void testTheBonusStopsBeingPossibleWhenItStops() {
  Card card{};
  startCard(card);
  CHECK(upperBonusStillPossible(card));
  // Zero the sixes and fives: the most the rest can give is 3+6+9+12 = 30.
  card.box[static_cast<int>(Category::Sixes)] = 0;
  card.box[static_cast<int>(Category::Fives)] = 0;
  CHECK(!upperBonusStillPossible(card));
  // But zeroing only the ones leaves 4+9+12+15+18 = 58... short. Check the real
  // arithmetic rather than the guess: 2*5 + 3*5 + 4*5 + 5*5 + 6*5 = 100.
  startCard(card);
  card.box[static_cast<int>(Category::Ones)] = 0;
  CHECK(upperBonusStillPossible(card));
}

// --- the Joker rules, which is where this game is usually wrong -------------

void testASecondYahtzeeEarnsTheBonusEvenWithTheUpperBoxFree() {
  // The bug the first version had. Five threes, Yahtzee box already holding 50,
  // Threes box still FREE. Rule (2) forces the Threes box; rule (1), the 100
  // bonus, is owed regardless and does not wait for rule (3).
  Game game{};
  start(game, 1);
  game.card[0].box[static_cast<int>(Category::Yahtzee)] = kYahtzeeScore;
  setDice(game, 3, 3, 3, 3, 3);
  game.rollsUsed = 1;

  CHECK(yahtzeeBonusDue(game.card[0], game.die));
  CHECK(!jokerApplies(game.card[0], game.die));  // matching upper still free
  // Only the matching upper box may be taken.
  CHECK(canScore(game.card[0], game.die, Category::Threes));
  CHECK(!canScore(game.card[0], game.die, Category::Chance));
  CHECK(!canScore(game.card[0], game.die, Category::FullHouse));

  CHECK(take(game, Category::Threes));
  CHECK(game.card[0].box[static_cast<int>(Category::Threes)] == 15);
  CHECK(game.card[0].yahtzeeBonuses == 1);
  CHECK(total(game.card[0]) == 50 + 15 + kYahtzeeBonus);
}

void testAJokerPaysStraightsAndFullHouseInFull() {
  Game game{};
  start(game, 1);
  game.card[0].box[static_cast<int>(Category::Yahtzee)] = kYahtzeeScore;
  game.card[0].box[static_cast<int>(Category::Threes)] = 9;  // matching upper gone
  setDice(game, 3, 3, 3, 3, 3);
  game.rollsUsed = 1;

  CHECK(yahtzeeBonusDue(game.card[0], game.die));
  CHECK(jokerApplies(game.card[0], game.die));
  // Any free box now, and the shaped ones pay in full despite the dice.
  CHECK(canScore(game.card[0], game.die, Category::LargeStraight));
  CHECK(scoreFor(game.card[0], game.die, Category::LargeStraight) == kLargeStraightScore);
  CHECK(scoreFor(game.card[0], game.die, Category::SmallStraight) == kSmallStraightScore);
  CHECK(scoreFor(game.card[0], game.die, Category::FullHouse) == kFullHouseScore);
  // But the unshaped ones still score their dice.
  CHECK(scoreFor(game.card[0], game.die, Category::Chance) == 15);
  CHECK(scoreFor(game.card[0], game.die, Category::FourOfAKind) == 15);

  CHECK(take(game, Category::LargeStraight));
  CHECK(game.card[0].box[static_cast<int>(Category::LargeStraight)] == kLargeStraightScore);
  CHECK(game.card[0].yahtzeeBonuses == 1);
}

void testACrossedOutYahtzeeStillTriggersTheJokerButEarnsNoBonus() {
  // Hasbro is NOT silent here, which this file used to claim. The rulebook says
  // the Joker applies when the Yahtzee box "has been previously filled with 50
  // OR ZERO", and their support says a zero earns no bonus but still forces the
  // Joker rules. Marking a written-down rule as [house] is worse than getting
  // it wrong, because it stops anyone looking it up.
  Game game{};
  start(game, 1);
  game.card[0].box[static_cast<int>(Category::Yahtzee)] = 0;
  game.card[0].box[static_cast<int>(Category::Threes)] = 9;
  setDice(game, 3, 3, 3, 3, 3);
  game.rollsUsed = 1;

  // No bonus. That half was right.
  CHECK(!yahtzeeBonusDue(game.card[0], game.die));
  // But the Joker is live, so the shaped boxes pay in full.
  CHECK(jokerLive(game.card[0], game.die));
  CHECK(jokerApplies(game.card[0], game.die));
  CHECK(scoreFor(game.card[0], game.die, Category::LargeStraight) == kLargeStraightScore);
  CHECK(scoreFor(game.card[0], game.die, Category::FullHouse) == kFullHouseScore);
  CHECK(take(game, Category::LargeStraight));
  CHECK(game.card[0].box[static_cast<int>(Category::LargeStraight)] == kLargeStraightScore);
  CHECK(game.card[0].yahtzeeBonuses == 0);

  // And the compulsory-upper rule applies to a zeroed Yahtzee box too.
  Game forced{};
  start(forced, 2);
  forced.card[0].box[static_cast<int>(Category::Yahtzee)] = 0;
  setDice(forced, 5, 5, 5, 5, 5);
  forced.rollsUsed = 1;
  CHECK(canScore(forced.card[0], forced.die, Category::Fives));
  CHECK(!canScore(forced.card[0], forced.die, Category::Chance));
}

// Rule (3) is a LOWER SECTION restriction, not "any free box". The first
// version offered free upper boxes while the lower section was open: 30,240
// (card, dice, category) triples Hasbro forbids, reached in 1.25% of games.
void testAJokerMustGoInTheLowerSectionWhileAnyOfItIsFree() {
  Game game{};
  start(game, 7);
  game.card[0].box[static_cast<int>(Category::Yahtzee)] = kYahtzeeScore;
  game.card[0].box[static_cast<int>(Category::Threes)] = 9;  // matching upper gone
  setDice(game, 3, 3, 3, 3, 3);
  game.rollsUsed = 1;

  CHECK(jokerApplies(game.card[0], game.die));
  // Every lower box is offered.
  for (int i = kUpperEnd; i < kCategories; ++i) {
    const Category c = static_cast<Category>(i);
    if (scored(game.card[0], c)) continue;
    CHECK(canScore(game.card[0], game.die, c));
  }
  // And no upper box is, however free.
  for (int i = 0; i < kUpperEnd; ++i) {
    const Category c = static_cast<Category>(i);
    if (scored(game.card[0], c)) continue;
    CHECK(!canScore(game.card[0], game.die, c));
    CHECK(!take(game, c));
  }

  // Fill every lower box: now, and only now, an upper box takes the zero.
  for (int i = kUpperEnd; i < kCategories; ++i) {
    if (game.card[0].box[i] == kUnscored) game.card[0].box[i] = 0;
  }
  CHECK(!anyLowerBoxFree(game.card[0]));
  CHECK(canScore(game.card[0], game.die, Category::Ones));
  CHECK(scoreFor(game.card[0], game.die, Category::Ones) == 0);
}

void testTheFirstYahtzeeIsNotABonus() {
  Game game{};
  start(game, 1);
  setDice(game, 5, 5, 5, 5, 5);
  game.rollsUsed = 1;
  CHECK(!yahtzeeBonusDue(game.card[0], game.die));
  CHECK(take(game, Category::Yahtzee));
  CHECK(game.card[0].box[static_cast<int>(Category::Yahtzee)] == kYahtzeeScore);
  CHECK(game.card[0].yahtzeeBonuses == 0);
  CHECK(total(game.card[0]) == kYahtzeeScore);
}

// --- rolling and holding ---------------------------------------------------

void testThreeRollsAndNoMore() {
  Game game{};
  start(game, 12345);
  CHECK(canRoll(game));
  CHECK(roll(game));
  CHECK(game.rollsUsed == 1);
  CHECK(roll(game));
  CHECK(roll(game));
  CHECK(game.rollsUsed == kRollsPerTurn);
  CHECK(!canRoll(game));
  const Game before = game;
  CHECK(!roll(game));
  CHECK(std::memcmp(&before, &game, sizeof(Game)) == 0);
}

void testHeldDiceSurviveARollAndFreeOnesChange() {
  Game game{};
  start(game, 0xABCDEF01u);
  CHECK(roll(game));
  const uint8_t kept[kDice] = {game.die[0], game.die[1], game.die[2], game.die[3], game.die[4]};
  game.held = 0b00101;
  CHECK(roll(game));
  CHECK(game.die[0] == kept[0]);
  CHECK(game.die[2] == kept[2]);

  // Over many turns the free dice really do change, or "held" is doing nothing.
  int changed = 0;
  for (int trial = 0; trial < 200; ++trial) {
    Game g{};
    start(g, 0x1000u + trial);
    roll(g);
    const uint8_t was = g.die[1];
    g.held = 0b00101;
    roll(g);
    if (g.die[1] != was) ++changed;
  }
  // A free die keeps its face 1 time in 6 by chance, so about 167 of 200.
  CHECK(changed > 140);
}

void testTheFirstRollOfATurnIgnoresHolds() {
  Game game{};
  start(game, 777);
  CHECK(roll(game));
  game.held = 0b11111;
  CHECK(roll(game));
  // All held, so nothing moved.
  const uint8_t frozen[kDice] = {game.die[0], game.die[1], game.die[2], game.die[3], game.die[4]};
  CHECK(take(game, Category::Chance));
  CHECK(game.held == 0);
  CHECK(game.rollsUsed == 0);
  // New turn: the first roll must move everything even though held was full.
  CHECK(roll(game));
  int same = 0;
  for (int i = 0; i < kDice; ++i) {
    if (game.die[i] == frozen[i]) ++same;
  }
  CHECK(same < kDice);
  CHECK(game.held == 0);
}

void testHoldingIsOnlyBetweenRolls() {
  Game game{};
  start(game, 4242);
  CHECK(!canHold(game));  // nothing rolled
  CHECK(!toggleHold(game, 0));
  CHECK(game.held == 0);
  roll(game);
  CHECK(canHold(game));
  CHECK(toggleHold(game, 2));
  CHECK(game.held == 0b00100);
  CHECK(toggleHold(game, 2));
  CHECK(game.held == 0);
  CHECK(!toggleHold(game, -1));
  CHECK(!toggleHold(game, kDice));
  roll(game);
  roll(game);
  CHECK(!canHold(game));  // no roll left to change anything
  CHECK(!toggleHold(game, 0));
}

void testABoxIsTakenOnceAndTheTurnPasses() {
  Game game{};
  start(game, 999);
  CHECK(game.turn == 0);
  CHECK(!take(game, Category::Chance));  // nothing rolled yet
  roll(game);
  CHECK(take(game, Category::Chance));
  CHECK(game.turn == 1);
  CHECK(game.rollsUsed == 0);
  CHECK(scored(game.card[0], Category::Chance));
  CHECK(!scored(game.card[1], Category::Chance));
  roll(game);
  // Player 1's Chance is still free; player 0's is not.
  CHECK(canScore(game.card[1], game.die, Category::Chance));
  CHECK(take(game, Category::Chance));
  CHECK(game.turn == 0);
  roll(game);
  CHECK(!canScore(game.card[0], game.die, Category::Chance));
  const Game before = game;
  CHECK(!take(game, Category::Chance));
  CHECK(std::memcmp(&before, &game, sizeof(Game)) == 0);
}

void testAZeroIsARealScoreAndNotAnEmptyBox() {
  Game game{};
  start(game, 31337);
  setDice(game, 1, 1, 2, 2, 3);
  game.rollsUsed = 1;
  CHECK(take(game, Category::Yahtzee));
  CHECK(game.card[0].box[static_cast<int>(Category::Yahtzee)] == 0);
  CHECK(scored(game.card[0], Category::Yahtzee));
  CHECK(boxesLeft(game.card[0]) == kCategories - 1);
  // And it cannot be taken again.
  setDice(game, 4, 4, 4, 4, 4);
  game.turn = 0;
  game.rollsUsed = 1;
  CHECK(!canScore(game.card[0], game.die, Category::Yahtzee));
}

void testAGameIsExactlyTwentySixTurnsAndThenOver() {
  Game game{};
  start(game, 0xFEEDu);
  int turns = 0;
  while (!over(game)) {
    CHECK(roll(game));
    Category taken = Category::Count;
    for (int i = 0; i < kCategories; ++i) {
      const Category c = static_cast<Category>(i);
      if (canScore(game.card[game.turn], game.die, c)) {
        taken = c;
        break;
      }
    }
    CHECK(taken != Category::Count);
    CHECK(take(game, taken));
    ++turns;
    CHECK(turns <= kCategories * 2);
  }
  CHECK(turns == kCategories * 2);
  CHECK(cardFull(game.card[0]));
  CHECK(cardFull(game.card[1]));
  CHECK(boxesLeft(game.card[0]) == 0);
  // A finished game accepts nothing.
  CHECK(!canRoll(game));
  CHECK(!roll(game));
  CHECK(!take(game, Category::Chance));
  CHECK(takeableBoxes(game, game.turn) == 0);
}

void testEveryRandomGameStaysPlausible() {
  for (int trial = 0; trial < 400; ++trial) {
    Game game{};
    start(game, 0x2000u + trial);
    CHECK(plausible(game));
    while (!over(game)) {
      const int rolls = 1 + static_cast<int>(roll32() % 3);
      for (int r = 0; r < rolls; ++r) {
        if (!roll(game)) break;
        if (canHold(game)) game.held = static_cast<uint8_t>(roll32() % 32);
      }
      int legal[kCategories];
      int count = 0;
      for (int i = 0; i < kCategories; ++i) {
        if (canScore(game.card[game.turn], game.die, static_cast<Category>(i))) legal[count++] = i;
      }
      CHECK(count > 0);
      CHECK(take(game, static_cast<Category>(legal[roll32() % static_cast<uint32_t>(count)])));
      CHECK(plausible(game));
    }
    // The total really is the sum of its parts.
    for (int player = 0; player < 2; ++player) {
      const Card& card = game.card[player];
      int sum = bonusEarned(card) + card.yahtzeeBonuses * kYahtzeeBonus;
      for (int i = 0; i < kCategories; ++i) sum += card.box[i];
      CHECK(total(card) == sum);
      CHECK(total(card) >= 0);
    }
  }
}

void testPlausibleRejectsWhatPlayCannotProduce() {
  Game good{};
  start(good, 5);
  CHECK(plausible(good));

  {  // a die off the faces
    Game bad = good;
    bad.die[2] = 7;
    CHECK(!plausible(bad));
  }
  {  // a score above the box's own maximum -- 50 in Ones, whose real max is 5
    Game bad = good;
    bad.card[0].box[static_cast<int>(Category::Ones)] = 50;
    CHECK(!plausible(bad));
  }
  {  // and one that is legal in another box, to prove the cap is per category
    Game bad = good;
    bad.card[0].box[static_cast<int>(Category::Chance)] = 30;
    bad.card[1].box[static_cast<int>(Category::Chance)] = 30;
    CHECK(plausible(bad));
  }
  {  // a value inside the cap but not a shape the box can hold
    //
    // Every one of these sets turn = 1, because filling one box on card 0 makes
    // it one ahead and the turn-parity clause would otherwise be the thing
    // doing the rejecting. That is exactly how the suite's own "floating disc"
    // board in Connect Four turned out to be testing something else.
    const int8_t kBadValues[][2] = {
        {static_cast<int8_t>(Category::SmallStraight), 17}, {static_cast<int8_t>(Category::Yahtzee), 25},
        {static_cast<int8_t>(Category::FullHouse), 13},     {static_cast<int8_t>(Category::LargeStraight), 7},
        {static_cast<int8_t>(Category::Twos), 3},           {static_cast<int8_t>(Category::Sixes), 7},
        {static_cast<int8_t>(Category::ThreeOfAKind), 2},   {static_cast<int8_t>(Category::Chance), 3},
    };
    for (const auto& entry : kBadValues) {
      Game bad = good;
      bad.turn = 1;
      bad.card[0].box[entry[0]] = entry[1];
      // The parity clause must be satisfied, so only the shape can reject.
      CHECK(kCategories - boxesLeft(bad.card[0]) == kCategories - boxesLeft(bad.card[1]) + 1);
      CHECK(!plausible(bad));
    }
    // And the legal values in those same boxes really are accepted, or the
    // check above is rejecting everything.
    const int8_t kGoodValues[][2] = {
        {static_cast<int8_t>(Category::SmallStraight), static_cast<int8_t>(kSmallStraightScore)},
        {static_cast<int8_t>(Category::SmallStraight), 0},
        {static_cast<int8_t>(Category::Yahtzee), static_cast<int8_t>(kYahtzeeScore)},
        {static_cast<int8_t>(Category::Twos), 4},
        {static_cast<int8_t>(Category::Sixes), 12},
        {static_cast<int8_t>(Category::ThreeOfAKind), 18},
    };
    for (const auto& entry : kGoodValues) {
      Game ok = good;
      ok.turn = 1;
      ok.card[0].box[entry[0]] = entry[1];
      CHECK(plausible(ok));
    }
  }
  {  // thirteen bonuses is one too many: the first Yahtzee costs a turn
    Game bad = good;
    bad.card[0].box[static_cast<int>(Category::Yahtzee)] = kYahtzeeScore;
    bad.card[1].box[static_cast<int>(Category::Yahtzee)] = kYahtzeeScore;
    bad.card[0].yahtzeeBonuses = kCategories;
    CHECK(!plausible(bad));
    bad.card[0].yahtzeeBonuses = kCategories - 1;
    CHECK(plausible(bad));
  }
  {  // a finished game with a turn still in progress
    Game bad{};
    start(bad, 6);
    for (int i = 0; i < kCategories; ++i) {
      bad.card[0].box[i] = 0;
      bad.card[1].box[i] = 0;
    }
    bad.turn = 0;
    CHECK(over(bad));
    CHECK(plausible(bad));
    bad.rollsUsed = 2;
    CHECK(!plausible(bad));
    bad.rollsUsed = 0;
    bad.held = 0b00011;
    CHECK(!plausible(bad));
  }
  {  // dice held before anything was rolled
    Game bad = good;
    bad.held = 0b00101;
    CHECK(bad.rollsUsed == 0);
    CHECK(!plausible(bad));
  }
  {  // a Yahtzee bonus with no Yahtzee behind it
    Game bad = good;
    bad.card[0].yahtzeeBonuses = 2;
    CHECK(!plausible(bad));
  }
  {  // a bonus behind a crossed-out Yahtzee
    Game bad = good;
    bad.card[0].box[static_cast<int>(Category::Yahtzee)] = 0;
    bad.card[1].box[static_cast<int>(Category::Yahtzee)] = 0;
    bad.card[0].yahtzeeBonuses = 1;
    CHECK(!plausible(bad));
  }
  {  // one card two boxes ahead of the other
    Game bad = good;
    bad.card[0].box[0] = 3;
    bad.card[0].box[1] = 4;
    CHECK(!plausible(bad));
  }
  {  // the wrong player to move for the boxes filled
    Game bad = good;
    bad.card[0].box[0] = 3;
    bad.turn = 0;
    CHECK(!plausible(bad));
  }
  {  // rolls used past the limit
    Game bad = good;
    bad.rollsUsed = kRollsPerTurn + 1;
    CHECK(!plausible(bad));
  }
}

// --- the two state machines ------------------------------------------------

void testBackIsTotalAndAlwaysReachesTheTop() {
  const Screen all[] = {Screen::Menu, Screen::HowTo, Screen::Card, Screen::Result};
  for (const Screen screen : all) CHECK(back(screen) == Screen::Menu);
  CHECK(leavesApp(Screen::Menu));
  CHECK(!leavesApp(Screen::HowTo));
  CHECK(!leavesApp(Screen::Card));
  CHECK(!leavesApp(Screen::Result));
}

void testTheStageIsDerivedFromTheRollsUsed() {
  CHECK(stageFor(0) == Stage::Fresh);
  CHECK(stageFor(1) == Stage::Rolling);
  CHECK(stageFor(2) == Stage::Rolling);
  CHECK(stageFor(3) == Stage::Spent);

  Game game{};
  start(game, 8);
  CHECK(stageOf(game) == Stage::Fresh);
  CHECK(screenFor(game) == Screen::Card);
  roll(game);
  CHECK(stageOf(game) == Stage::Rolling);
  roll(game);
  roll(game);
  CHECK(stageOf(game) == Stage::Spent);
  // Every stage agrees with what the rules will accept.
  CHECK(!canHold(game));
  CHECK(!canRoll(game));
}

void testTakeableBoxesAgreesWithWhatTheRulesAccept() {
  for (int trial = 0; trial < 300; ++trial) {
    Game game{};
    start(game, 0x9000u + trial);
    // Nothing rolled: nothing is takeable, however empty the card is.
    CHECK(takeableBoxes(game, game.turn) == 0);
    while (!over(game)) {
      roll(game);
      const uint16_t mask = takeableBoxes(game, game.turn);
      int offered = 0;
      for (int i = 0; i < kCategories; ++i) {
        const bool marked = (mask & (1u << i)) != 0;
        // A row drawn as takeable that refuses a tap is the failure Checkers
        // paid for. Here the Joker rule makes it a live risk rather than a
        // theoretical one.
        CHECK(marked == canScore(game.card[game.turn], game.die, static_cast<Category>(i)));
        if (marked) ++offered;
      }
      CHECK(offered > 0);
      int first = -1;
      for (int i = 0; i < kCategories; ++i) {
        if ((mask & (1u << i)) != 0) {
          first = i;
          break;
        }
      }
      CHECK(take(game, static_cast<Category>(first)));
    }
    CHECK(screenFor(game) == Screen::Result);
  }
}

// The screen bug this signature exists to make impossible. During the
// opponent's turn the card being LOOKED at is not the card being played, and
// the first version read card[game.turn] -- so your column filled with previews
// computed from their dice, about three repaints a turn, every game.
void testNothingIsTakeableOnACardWhoseTurnItIsNot() {
  Game game{};
  start(game, 4711);
  roll(game);
  CHECK(game.turn == 0);
  CHECK(takeableBoxes(game, 0) != 0);
  CHECK(takeableBoxes(game, 1) == 0);

  take(game, chooseBox(game.card[0], game.die));
  roll(game);
  CHECK(game.turn == 1);
  CHECK(takeableBoxes(game, 1) != 0);
  CHECK(takeableBoxes(game, 0) == 0);

  // And the mask really is about the seat's own card, not the mover's. Fill a
  // box on ONE card only and the two masks must differ in that bit.
  Game skewed{};
  start(skewed, 99);
  skewed.card[0].box[static_cast<int>(Category::Chance)] = 20;
  skewed.card[1].box[static_cast<int>(Category::Ones)] = 3;
  skewed.turn = 0;
  skewed.rollsUsed = 1;
  setDice(skewed, 1, 2, 3, 4, 5);
  const uint16_t mine = takeableBoxes(skewed, 0);
  CHECK((mine & (1u << static_cast<int>(Category::Chance))) == 0);
  CHECK((mine & (1u << static_cast<int>(Category::Ones))) != 0);
}

void testTheJokerHintFiresExactlyWhenABoxIsForced() {
  Game game{};
  start(game, 3);
  game.card[0].box[static_cast<int>(Category::Yahtzee)] = kYahtzeeScore;
  setDice(game, 4, 4, 4, 4, 4);
  game.rollsUsed = 2;
  // Fours still free: one box is legal and the card has to say why.
  CHECK(jokerForcing(game, 0));
  CHECK(takeableBoxes(game, 0) == (1u << static_cast<int>(Category::Fours)));

  // Fours gone: a bonus is still due but nothing is forced, so no hint.
  game.card[0].box[static_cast<int>(Category::Fours)] = 16;
  CHECK(!jokerForcing(game, 0));
  CHECK(yahtzeeBonusDue(game.card[0], game.die));

  // And never for the seat that is not playing.
  CHECK(!jokerForcing(game, 1));
}

void testUnderAJokerExactlyOneBoxIsOffered() {
  Game game{};
  start(game, 11);
  game.card[0].box[static_cast<int>(Category::Yahtzee)] = kYahtzeeScore;
  setDice(game, 2, 2, 2, 2, 2);
  game.rollsUsed = 2;
  const uint16_t mask = takeableBoxes(game, 0);
  CHECK(mask == (1u << static_cast<int>(Category::Twos)));
  // This is the ONE case where a free row cannot be tapped, which is why the
  // card has to draw the difference.
  CHECK(!scored(game.card[0], Category::Chance));
  CHECK((mask & (1u << static_cast<int>(Category::Chance))) == 0);
}

// --- the opponent ----------------------------------------------------------

void testTheBrainOnlyEverTakesALegalBox() {
  for (int trial = 0; trial < 60; ++trial) {
    Game game{};
    start(game, 0x3000u + trial);
    while (!over(game)) {
      roll(game);
      while (canHold(game)) {
        game.held = chooseHold(game.card[game.turn], game.die);
        roll(game);
      }
      const Category box = chooseBox(game.card[game.turn], game.die);
      CHECK(box != Category::Count);
      CHECK(canScore(game.card[game.turn], game.die, box));
      CHECK(take(game, box));
    }
  }
}

void testTheBrainIsDeterministicAndPure() {
  for (int trial = 0; trial < 40; ++trial) {
    Game game{};
    start(game, 0x4000u + trial);
    roll(game);
    for (int step = 0; step < 6 && !over(game); ++step) {
      const Card before = game.card[game.turn];
      const uint8_t frozen[kDice] = {game.die[0], game.die[1], game.die[2], game.die[3], game.die[4]};

      const uint8_t first = chooseHold(before, frozen);
      CHECK(chooseHold(before, frozen) == first);
      const Category box = chooseBox(before, frozen);
      CHECK(chooseBox(before, frozen) == box);
      // It did not touch what it was given.
      CHECK(std::memcmp(&before, &game.card[game.turn], sizeof(Card)) == 0);
      for (int i = 0; i < kDice; ++i) CHECK(game.die[i] == frozen[i]);

      CHECK(take(game, box));
      if (!over(game)) roll(game);
    }
  }
}

void testTheBrainKeepsTheObviousThings() {
  Card card{};
  startCard(card);
  uint8_t d[kDice];

  // Four of a kind: keep the four, re-roll the odd one.
  dice(5, 5, 5, 5, 2, d);
  const uint8_t keepFour = chooseHold(card, d);
  CHECK((keepFour & 0b01111) == 0b01111);
  CHECK((keepFour & 0b10000) == 0);

  // A made large straight is kept whole. Nothing beats 40 by re-rolling.
  dice(1, 2, 3, 4, 5, d);
  CHECK(chooseHold(card, d) == 0b11111);

  // Three of a kind on a fresh card: keep the three.
  dice(6, 6, 6, 2, 3, d);
  const uint8_t keepThree = chooseHold(card, d);
  CHECK((keepThree & 0b00111) == 0b00111);
  CHECK((keepThree & 0b11000) == 0);

  // NOT asserted, and the reason is worth recording: with 2 3 4 5 5 this brain
  // keeps the PAIR, where conventional play keeps the four to the outside
  // straight. That is a real consequence of one-step lookahead plus
  // opportunity-cost valuation, not an accident -- filling Small Straight at
  // exactly its par value is worth about nothing, so the guaranteed fallback
  // the run gives you scores as nearly zero. See YahtzeeBrain.h. Its measured
  // strength does not suffer, so this stays a documented limitation rather
  // than a number tuned until the test agreed with folklore.
}

void testTheBrainDoesNotThrowAwayAYahtzee() {
  Card card{};
  startCard(card);
  uint8_t d[kDice];
  dice(4, 4, 4, 4, 4, d);
  CHECK(chooseHold(card, d) == 0b11111);
  CHECK(chooseBox(card, d) == Category::Yahtzee);
}

void testTheBrainBeatsANaiveMoverConvincingly() {
  // The test that matters. The naive mover keeps nothing but its most common
  // face and always takes whatever scores most right now -- a real strategy,
  // not a random one, so beating it means something.
  //
  // Legal and deterministic were both true of a Checkers brain whose evaluation
  // had been negated. Only a strength test notices that.
  int brainWins = 0;
  int naiveWins = 0;
  int ties = 0;
  constexpr int kGames = 30;

  for (int trial = 0; trial < kGames; ++trial) {
    Game game{};
    start(game, 0x5000u + trial * 7);
    // The brain alternates seats, because player 0 moves first and a bot that
    // always went first would be measuring the rules rather than itself.
    const uint8_t brainSeat = static_cast<uint8_t>(trial % 2);
    while (!over(game)) {
      roll(game);
      const bool mine = game.turn == brainSeat;
      while (canHold(game)) {
        if (mine) {
          game.held = chooseHold(game.card[game.turn], game.die);
        } else {
          const Counts counts = countFaces(game.die);
          int commonest = 1;
          for (int face = 2; face <= kFaces; ++face) {
            if (counts.of[face] > counts.of[commonest]) commonest = face;
          }
          uint8_t mask = 0;
          for (int i = 0; i < kDice; ++i) {
            if (game.die[i] == commonest) mask |= static_cast<uint8_t>(1 << i);
          }
          game.held = mask;
        }
        roll(game);
      }
      Category box = Category::Count;
      if (mine) {
        box = chooseBox(game.card[game.turn], game.die);
      } else {
        int best = -1;
        for (int i = 0; i < kCategories; ++i) {
          const Category c = static_cast<Category>(i);
          if (!canScore(game.card[game.turn], game.die, c)) continue;
          const int score = scoreFor(game.card[game.turn], game.die, c);
          if (score > best) {
            best = score;
            box = c;
          }
        }
      }
      CHECK(box != Category::Count);
      CHECK(take(game, box));
    }
    const int brainScore = total(game.card[brainSeat]);
    const int naiveScore = total(game.card[1 - brainSeat]);
    if (brainScore > naiveScore)
      ++brainWins;
    else if (naiveScore > brainScore)
      ++naiveWins;
    else
      ++ties;
  }
  std::printf("  brain vs naive: %d - %d (%d ties) of %d\n", brainWins, naiveWins, ties, kGames);
  // Two thirds, not "more than half". A bound a coin could pass is not a bound.
  CHECK(brainWins >= kGames * 2 / 3);
  CHECK(brainWins > naiveWins * 2);
}

void testTheBrainScoresLikeAPlayerNotABot() {
  // An absolute floor as well as a relative one: a bot that beats a weak
  // opponent could still be playing badly. Average human scores land around
  // 200-250; this asks only that it clears 180 on average over 40 games, which
  // no strategy that spends its boxes carelessly manages.
  int totalScore = 0;
  int worst = 10000;
  constexpr int kGames = 40;
  for (int trial = 0; trial < kGames; ++trial) {
    Game game{};
    start(game, 0x6000u + trial * 13);
    while (!over(game)) {
      roll(game);
      while (canHold(game)) {
        game.held = chooseHold(game.card[game.turn], game.die);
        roll(game);
      }
      CHECK(take(game, chooseBox(game.card[game.turn], game.die)));
    }
    const int score = total(game.card[0]);
    totalScore += score;
    if (score < worst) worst = score;
  }
  const int average = totalScore / kGames;
  std::printf("  brain solo: average %d, worst %d over %d games\n", average, worst, kGames);
  CHECK(average >= 180);
  // NOT a floor on the worst game. Re-running this harness across 61 seed bases
  // fails a "worst >= 100" bound on three of them, lowest 89 -- so that
  // assertion was measuring the seed, not the brain. A bad hand really can cost
  // a game, and a test that pretends otherwise fails for whoever changes an
  // unrelated constant.
  CHECK(worst >= 60);
}

}  // namespace

int main() {
  testAFreshCardIsEmptyAndScoresNothing();
  testEveryCategoryScoresWhatTheRulesSay();
  testThePrintedNumbersAreTheNumbersOnTheCard();
  testTheUpperBonusIsSixtyThreeAndThirtyFive();
  testTheBonusStopsBeingPossibleWhenItStops();

  testASecondYahtzeeEarnsTheBonusEvenWithTheUpperBoxFree();
  testAJokerPaysStraightsAndFullHouseInFull();
  testACrossedOutYahtzeeStillTriggersTheJokerButEarnsNoBonus();
  testAJokerMustGoInTheLowerSectionWhileAnyOfItIsFree();
  testTheFirstYahtzeeIsNotABonus();

  testThreeRollsAndNoMore();
  testHeldDiceSurviveARollAndFreeOnesChange();
  testTheFirstRollOfATurnIgnoresHolds();
  testHoldingIsOnlyBetweenRolls();
  testABoxIsTakenOnceAndTheTurnPasses();
  testAZeroIsARealScoreAndNotAnEmptyBox();
  testAGameIsExactlyTwentySixTurnsAndThenOver();
  testEveryRandomGameStaysPlausible();
  testPlausibleRejectsWhatPlayCannotProduce();

  testBackIsTotalAndAlwaysReachesTheTop();
  testTheStageIsDerivedFromTheRollsUsed();
  testTakeableBoxesAgreesWithWhatTheRulesAccept();
  testNothingIsTakeableOnACardWhoseTurnItIsNot();
  testTheJokerHintFiresExactlyWhenABoxIsForced();
  testUnderAJokerExactlyOneBoxIsOffered();

  testTheBrainOnlyEverTakesALegalBox();
  testTheBrainIsDeterministicAndPure();
  testTheBrainKeepsTheObviousThings();
  testTheBrainDoesNotThrowAwayAYahtzee();
  testTheBrainBeatsANaiveMoverConvincingly();
  testTheBrainScoresLikeAPlayerNotABot();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
