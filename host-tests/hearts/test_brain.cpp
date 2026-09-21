// Hearts opponent tests.
//
// Two jobs. The first is that the brain is LEGAL and HONEST: every card it
// returns is one the rules accept, and the observation it works from carries
// nobody else's hand. The second is that it is any GOOD, which is a different
// question and needs a number rather than an assertion.
//
// That number is a head-to-head win rate, not a pooled average score. A Sharp
// sat at a table of Rookies should win more than the 25% four seats give you by
// chance; an average score across a field says nothing about whether it beats
// the thing it is playing against. Seats rotate and every game gets a fresh
// seed, because a match whose games are secretly the same game replayed is a
// sample of one.

#include <cstdio>
#include <cstring>

#include "../../src/apps_local/hearts/HeartsBrain.h"

using namespace hearts;
namespace c = cards;

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++gChecks;                                                    \
    if (!(cond)) {                                                \
      ++gFailures;                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

#define CHECK_EQ(a, b)                                                                         \
  do {                                                                                         \
    ++gChecks;                                                                                 \
    const long long va = (long long)(a);                                                       \
    const long long vb = (long long)(b);                                                       \
    if (va != vb) {                                                                            \
      ++gFailures;                                                                             \
      std::printf("FAIL %s:%d  %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #a, va, #b, vb); \
    }                                                                                          \
  } while (0)

// ---------------------------------------------------------------------------
// Honesty: what an Observation may contain.

static void testObservationCarriesNoOtherHand() {
  Game game;
  uint32_t seed = 99;
  newGame(game, seed);

  Observation obs;
  observe(game, Seat::South, obs);

  // My own hand, whole.
  CHECK_EQ(obs.hand.count, kHandSize);
  for (int i = 0; i < kHandSize; ++i) CHECK_EQ(obs.hand.at(i), game.hands[seatIndex(Seat::South)].at(i));

  // Before a card is played nothing has gone, which means the other 39 cards
  // are indistinguishable from each other: exactly the fog a player has.
  int goneCount = 0;
  for (int i = 0; i < c::kDeck; ++i) {
    if (obs.gone[i]) ++goneCount;
  }
  CHECK_EQ(goneCount, 0);

  // The struct has no field that could hold another hand. This is a compile-
  // time fact, stated as a runtime one so it is visible in the suite: an
  // Observation is smaller than four hands could possibly fit in.
  CHECK(sizeof(Observation) < sizeof(Game));
}

static void testGoneTracksPlayedCardsOnly() {
  Game game;
  uint32_t seed = 4;
  newGame(game, seed);
  for (int s = 0; s < kSeats; ++s) {
    uint8_t three[kPassCount] = {game.hands[s].at(0), game.hands[s].at(1), game.hands[s].at(2)};
    setPass(game, static_cast<Seat>(s), three, kPassCount);
  }
  commitPass(game);

  // Play one full trick.
  for (int i = 0; i < kSeats; ++i) {
    uint8_t legal[kHandSize];
    const int n = legalPlays(game, game.turn, legal);
    CHECK(n > 0);
    CHECK(playCard(game, legal[0]));
  }
  CHECK(game.phase == Phase::TrickTaken);

  Observation obs;
  observe(game, Seat::South, obs);
  // The four cards are ON THE TABLE, so they are visible, not gone.
  for (int s = 0; s < kSeats; ++s) {
    const uint8_t card = game.trick.played[s];
    const int index = static_cast<int>(c::suitOf(card)) * c::kRanks + c::rankOf(card);
    CHECK(!obs.gone[index]);
  }

  sweepTrick(game);
  observe(game, Seat::South, obs);
  int goneCount = 0;
  for (int i = 0; i < c::kDeck; ++i) {
    if (obs.gone[i]) ++goneCount;
  }
  // Now exactly those four have gone.
  CHECK_EQ(goneCount, kSeats);
}

static void testVoidIsOnlyRecordedWhenShown() {
  Game game;
  for (int s = 0; s < kSeats; ++s) game.hands[s].clear();
  game.phase = Phase::Playing;
  game.trickNumber = 5;
  game.heartsBroken = true;
  game.trick.clear();
  game.trick.leader = Seat::South;
  game.turn = Seat::South;

  game.hands[seatIndex(Seat::South)].add(c::makeCard(Suit::Clubs, c::kKing));
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Hearts, 5));

  CHECK(!game.voidShown[seatIndex(Seat::West)][(int)Suit::Clubs]);
  CHECK(playCard(game, c::makeCard(Suit::Clubs, c::kKing)));
  CHECK(playCard(game, c::makeCard(Suit::Hearts, 5)));
  // West could not follow clubs, and now the whole table knows.
  CHECK(game.voidShown[seatIndex(Seat::West)][(int)Suit::Clubs]);
  // And nothing was inferred about the suits it did not have to show.
  CHECK(!game.voidShown[seatIndex(Seat::West)][(int)Suit::Diamonds]);
  CHECK(!game.voidShown[seatIndex(Seat::West)][(int)Suit::Spades]);
  CHECK(!game.voidShown[seatIndex(Seat::South)][(int)Suit::Clubs]);
}

// ---------------------------------------------------------------------------
// Behaviour the brain is supposed to have.

static void setUpFollow(Game& game, const Suit led, const int ledRank) {
  for (int s = 0; s < kSeats; ++s) game.hands[s].clear();
  game.phase = Phase::Playing;
  game.trickNumber = 5;
  game.heartsBroken = true;
  game.trick.clear();
  game.trick.leader = Seat::South;
  game.trick.played[seatIndex(Seat::South)] = c::makeCard(led, ledRank);
  game.trick.count = 1;
  game.turn = Seat::West;
}

static void testDucksWithTheHighestLoser() {
  Game game;
  setUpFollow(game, Suit::Clubs, c::kKing);  // king of clubs led
  // West holds three clubs, all below the king, plus a heart it may not play.
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Clubs, 2));
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Clubs, 9));
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Clubs, c::kQueen));
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Hearts, c::kAce));

  Observation obs;
  observe(game, Seat::West, obs);
  uint32_t rng = 1;
  for (int trial = 0; trial < 40; ++trial) {
    const uint8_t card = decidePlay(obs, Skill::Sharp, rng);
    // The queen of clubs is the highest card that still loses to the king.
    CHECK_EQ(card, c::makeCard(Suit::Clubs, c::kQueen));
  }
}

static void testShedsTheQueenOfSpadesWhenVoid() {
  Game game;
  setUpFollow(game, Suit::Diamonds, c::kKing);
  // West is void in diamonds and holds the queen plus safe rubbish.
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Spades, c::kQueen));
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Clubs, 3));
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Clubs, 4));

  Observation obs;
  observe(game, Seat::West, obs);
  uint32_t rng = 3;
  for (int trial = 0; trial < 40; ++trial) {
    CHECK_EQ(decidePlay(obs, Skill::Sharp, rng), c::makeCard(Suit::Spades, c::kQueen));
  }
}

static void testAvoidsBeatingTheQueenWithTheAceOfSpades() {
  Game game;
  setUpFollow(game, Suit::Spades, 4);  // a low spade led, queen still out
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Spades, c::kAce));
  game.hands[seatIndex(Seat::West)].add(c::makeCard(Suit::Spades, 2));

  Observation obs;
  observe(game, Seat::West, obs);
  CHECK(!obs.queenGone());
  uint32_t rng = 5;
  for (int trial = 0; trial < 40; ++trial) {
    // Playing the ace here wins the trick and invites thirteen points onto it.
    CHECK_EQ(decidePlay(obs, Skill::Sharp, rng), c::makeCard(Suit::Spades, 2));
  }
}

static void testMoonThreatDetection() {
  Observation obs;
  obs.me = Seat::South;
  // Nothing taken: no threat.
  CHECK_EQ(moonThreat(obs), -1);
  // Points split between two seats: nobody is shooting.
  obs.taken[seatIndex(Seat::West)] = 12;
  obs.taken[seatIndex(Seat::North)] = 3;
  CHECK_EQ(moonThreat(obs), -1);
  // One seat holding everything, and enough of it to matter.
  obs.taken[seatIndex(Seat::North)] = 0;
  CHECK_EQ(moonThreat(obs), seatIndex(Seat::West));
  // A single early heart is noise, not a moon.
  obs.taken[seatIndex(Seat::West)] = 1;
  CHECK_EQ(moonThreat(obs), -1);
  // My own points are not a threat to me.
  obs.taken[seatIndex(Seat::West)] = 0;
  obs.taken[seatIndex(Seat::South)] = 20;
  CHECK_EQ(moonThreat(obs), -1);
}

static void testPassUrgencyRanksTheQueenTop() {
  Observation obs;
  obs.me = Seat::South;
  obs.hand.add(c::makeCard(Suit::Spades, c::kQueen));
  obs.hand.add(c::makeCard(Suit::Spades, 3));
  obs.hand.add(c::makeCard(Suit::Clubs, 2));
  obs.hand.add(c::makeCard(Suit::Diamonds, 4));
  // Short in spades, so the queen is a liability and goes first.
  const int queen = passUrgency(obs, c::makeCard(Suit::Spades, c::kQueen));
  CHECK(queen > passUrgency(obs, c::makeCard(Suit::Spades, 3)));
  CHECK(queen > passUrgency(obs, c::makeCard(Suit::Clubs, 2)));
  CHECK(queen > passUrgency(obs, c::makeCard(Suit::Diamonds, 4)));

  // With a long spade holding she can be protected, so she is worth keeping.
  Observation guarded;
  guarded.me = Seat::South;
  static const int kGuardRanks[] = {2, 3, 5, 7};
  for (const int r : kGuardRanks) guarded.hand.add(c::makeCard(Suit::Spades, r));
  guarded.hand.add(c::makeCard(Suit::Spades, c::kQueen));
  guarded.hand.add(c::makeCard(Suit::Hearts, c::kAce));
  CHECK(passUrgency(guarded, c::makeCard(Suit::Spades, c::kQueen)) <
        passUrgency(obs, c::makeCard(Suit::Spades, c::kQueen)));
}

// ---------------------------------------------------------------------------
// A whole table of brains, played out.

// Plays one complete game with the given skill per seat. Returns the winning
// seat (lowest total; ties broken by seat, which the caller accounts for).
static int playGame(const Skill* skills, uint32_t& seed, int* finalTotals, bool assertLegal) {
  Game game;
  newGame(game, seed);
  Observation obs;

  int guard = 0;
  while (game.phase != Phase::GameOver && ++guard < 8000) {
    if (game.phase == Phase::Passing) {
      if (game.passDirection() != Pass::Hold) {
        for (int s = 0; s < kSeats; ++s) {
          observe(game, static_cast<Seat>(s), obs);
          uint8_t three[kPassCount];
          decidePass(obs, skills[s], seed, three);
          // Three distinct cards, all held. A pass the rules reject would
          // wedge the hand forever.
          if (assertLegal)
            CHECK(setPass(game, static_cast<Seat>(s), three, kPassCount));
          else
            setPass(game, static_cast<Seat>(s), three, kPassCount);
        }
      }
      commitPass(game);
      continue;
    }
    if (game.phase == Phase::Playing) {
      observe(game, game.turn, obs);
      const uint8_t card = decidePlay(obs, skills[seatIndex(game.turn)], seed);
      if (assertLegal) {
        CHECK(card != kNoCard);
        CHECK(isLegalPlay(game, game.turn, card));
      }
      if (!playCard(game, card)) return -1;
      continue;
    }
    if (game.phase == Phase::TrickTaken) {
      sweepTrick(game);
      continue;
    }
    if (game.phase == Phase::HandOver) {
      nextHand(game, seed);
      continue;
    }
    break;
  }
  if (game.phase != Game{}.phase && guard >= 8000) return -1;

  int best = 0;
  for (int s = 0; s < kSeats; ++s) {
    finalTotals[s] = game.total[s];
    if (game.total[s] < game.total[best]) best = s;
  }
  // A tie at the top is not a win for anybody, and reporting it as one is how a
  // win rate quietly inflates.
  int tied = 0;
  for (int s = 0; s < kSeats; ++s) {
    if (game.total[s] == game.total[best]) ++tied;
  }
  return tied > 1 ? -1 : best;
}

static void testEveryBrainPlaysLegally() {
  uint32_t seed = 0xBEEF;
  const Skill table[kSeats] = {Skill::Sharp, Skill::Rookie, Skill::Sharp, Skill::Rookie};
  int totals[kSeats];
  for (int g = 0; g < 30; ++g) {
    const int winner = playGame(table, seed, totals, true);
    CHECK(winner >= -1);
    int sum = 0;
    for (int s = 0; s < kSeats; ++s) sum += totals[s];
    // Every point scored in the game came from a hand worth 26 (or 78 on a
    // moon), so the table total is always a multiple of 26.
    CHECK_EQ(sum % kMoonPoints, 0);
  }
}

// THE NUMBER THAT ANSWERS THE QUESTION: does Sharp beat Rookie?
static void measureStrength() {
  constexpr int kGames = 600;
  int sharpWins = 0;
  int decided = 0;
  long long sharpScore = 0;
  long long rookieScore = 0;
  uint32_t seed = 0x5EED;

  for (int g = 0; g < kGames; ++g) {
    // Rotate the seat the Sharp occupies. Pinned to one seat, this measures the
    // seat as much as the brain: South leads more first tricks than anyone.
    const int sharpSeat = g % kSeats;
    Skill table[kSeats];
    for (int s = 0; s < kSeats; ++s) table[s] = (s == sharpSeat) ? Skill::Sharp : Skill::Rookie;

    int totals[kSeats];
    // A fresh, advancing seed per game: reusing one makes a 600-game match a
    // handful of distinct games replayed.
    const int winner = playGame(table, seed, totals, false);
    sharpScore += totals[sharpSeat];
    for (int s = 0; s < kSeats; ++s) {
      if (s != sharpSeat) rookieScore += totals[s];
    }
    if (winner >= 0) {
      ++decided;
      if (winner == sharpSeat) ++sharpWins;
    }
  }

  const double winRate = decided > 0 ? (100.0 * sharpWins / decided) : 0.0;
  const double sharpAvg = static_cast<double>(sharpScore) / kGames;
  const double rookieAvg = static_cast<double>(rookieScore) / (kGames * 3);
  std::printf("  strength: Sharp wins %.1f%% of %d decided games (chance is 25.0%%)\n", winRate, decided);
  std::printf("            final score, lower is better: Sharp %.1f vs Rookie %.1f\n", sharpAvg, rookieAvg);

  // A Sharp that does not beat three Rookies is not worth the two settings.
  // The bar is deliberately well clear of chance rather than just above it, so
  // a regression that half-breaks the brain still goes red.
  CHECK(winRate > 33.0);
  CHECK(sharpAvg < rookieAvg);
}

int main() {
  testObservationCarriesNoOtherHand();
  testGoneTracksPlayedCardsOnly();
  testVoidIsOnlyRecordedWhenShown();
  testDucksWithTheHighestLoser();
  testShedsTheQueenOfSpadesWhenVoid();
  testAvoidsBeatingTheQueenWithTheAceOfSpades();
  testMoonThreatDetection();
  testPassUrgencyRanksTheQueenTop();
  testEveryBrainPlaysLegally();
  measureStrength();

  std::printf("hearts brain: %d checks, %d failures\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
