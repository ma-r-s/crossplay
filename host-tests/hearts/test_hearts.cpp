// Hearts rules tests. No device: HeartsCore is freestanding C++17.
//
// The soak at the bottom is the real test -- it plays whole games through the
// public API with random legal moves and rechecks every invariant after every
// single card. The named cases above it exist because each one is a rule a
// soak cannot see the absence of: a soak that only ever makes legal moves
// cannot notice that an illegal one was also allowed.

#include <cstdio>
#include <cstdlib>

#include "../../src/apps_local/hearts/HeartsCore.h"

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
    const long long va = static_cast<long long>(a);                                            \
    const long long vb = static_cast<long long>(b);                                            \
    if (va != vb) {                                                                            \
      ++gFailures;                                                                             \
      std::printf("FAIL %s:%d  %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #a, va, #b, vb); \
    }                                                                                          \
  } while (0)

// ---------------------------------------------------------------------------
// The ordering, which is the one thing the deck's own numbering gets wrong.

static void testTrickRank() {
  for (int s = 0; s < c::kSuits; ++s) {
    const Suit suit = static_cast<Suit>(s);
    const uint8_t ace = c::makeCard(suit, c::kAce);
    const uint8_t king = c::makeCard(suit, c::kKing);
    const uint8_t two = c::makeCard(suit, c::kTwo);
    // The ace is ace-LOW in the deck's encoding. If trickRank ever collapses to
    // rankOf, this is the assertion that goes red.
    CHECK(trickRank(ace) > trickRank(king));
    CHECK(trickRank(king) > trickRank(two));
    CHECK_EQ(trickRank(two), 0);
    CHECK_EQ(trickRank(ace), 12);
    CHECK(c::rankOf(ace) < c::rankOf(king));  // the trap itself, stated
  }
  // Every rank maps to a distinct trick rank, 0..12.
  bool seen[c::kRanks] = {};
  for (int r = 0; r < c::kRanks; ++r) {
    const int tr = trickRank(c::makeCard(Suit::Hearts, r));
    CHECK(tr >= 0 && tr < c::kRanks);
    CHECK(!seen[tr]);
    seen[tr] = true;
  }
}

static void testPenalties() {
  CHECK_EQ(penaltyOf(c::makeCard(Suit::Spades, c::kQueen)), 13);
  CHECK_EQ(penaltyOf(c::makeCard(Suit::Hearts, c::kQueen)), 1);
  CHECK_EQ(penaltyOf(c::makeCard(Suit::Hearts, c::kAce)), 1);
  CHECK_EQ(penaltyOf(c::makeCard(Suit::Spades, c::kKing)), 0);
  CHECK_EQ(penaltyOf(c::makeCard(Suit::Clubs, c::kTwo)), 0);
  // The whole hand is worth exactly 26.
  int total = 0;
  for (int s = 0; s < c::kSuits; ++s) {
    for (int r = 0; r < c::kRanks; ++r) total += penaltyOf(c::makeCard(static_cast<Suit>(s), r));
  }
  CHECK_EQ(total, kMoonPoints);
}

static void testPassRotation() {
  CHECK(passForHand(0) == Pass::Left);
  CHECK(passForHand(1) == Pass::Right);
  CHECK(passForHand(2) == Pass::Across);
  CHECK(passForHand(3) == Pass::Hold);
  CHECK(passForHand(4) == Pass::Left);
  // Left is the next seat clockwise, which on this compass is South -> West.
  CHECK(passTarget(Seat::South, Pass::Left) == Seat::West);
  CHECK(passTarget(Seat::South, Pass::Right) == Seat::East);
  CHECK(passTarget(Seat::South, Pass::Across) == Seat::North);
  // Right undoes left, from every seat.
  for (int s = 0; s < kSeats; ++s) {
    const Seat seat = static_cast<Seat>(s);
    CHECK(passTarget(passTarget(seat, Pass::Left), Pass::Right) == seat);
    CHECK(passTarget(passTarget(seat, Pass::Across), Pass::Across) == seat);
  }
}

// ---------------------------------------------------------------------------
// Helpers for hand-built positions.

static void clearHands(Game& game) {
  for (int s = 0; s < kSeats; ++s) game.hands[s].clear();
}

static void give(Game& game, const Seat seat, const Suit suit, const int rank) {
  game.hands[seatIndex(seat)].add(c::makeCard(suit, rank));
}

static void testHandSorting() {
  Hand hand;
  hand.add(c::makeCard(Suit::Hearts, c::kAce));
  hand.add(c::makeCard(Suit::Clubs, c::kKing));
  hand.add(c::makeCard(Suit::Hearts, c::kTwo));
  hand.add(c::makeCard(Suit::Clubs, c::kTwo));
  CHECK_EQ(hand.count, 4);
  // Clubs before hearts, and inside a suit low to high by TRICK rank, so the
  // ace lands at the end of its suit rather than the front.
  CHECK(c::suitOf(hand.at(0)) == Suit::Clubs);
  CHECK(c::suitOf(hand.at(1)) == Suit::Clubs);
  CHECK_EQ(trickRank(hand.at(0)), 0);
  CHECK_EQ(trickRank(hand.at(1)), 11);
  CHECK(c::suitOf(hand.at(2)) == Suit::Hearts);
  CHECK_EQ(trickRank(hand.at(3)), 12);

  CHECK(hand.remove(c::makeCard(Suit::Clubs, c::kTwo)));
  CHECK(!hand.remove(c::makeCard(Suit::Clubs, c::kTwo)));
  CHECK_EQ(hand.count, 3);
  CHECK(hand.hasSuit(Suit::Hearts));
  CHECK(!hand.hasSuit(Suit::Spades));
  CHECK_EQ(hand.countSuit(Suit::Hearts), 2);
  CHECK(!hand.onlyHearts());
  CHECK(hand.remove(c::makeCard(Suit::Clubs, c::kKing)));
  CHECK(hand.onlyHearts());
}

static void testTwoOfClubsOpens() {
  Game game;
  uint32_t seed = 7;
  newGame(game, seed);
  // Skip the pass so we land in Playing with a real deal.
  for (int s = 0; s < kSeats; ++s) {
    uint8_t three[kPassCount] = {game.hands[s].at(0), game.hands[s].at(1), game.hands[s].at(2)};
    CHECK(setPass(game, static_cast<Seat>(s), three, kPassCount));
  }
  CHECK(passReady(game));
  commitPass(game);
  CHECK(game.phase == Phase::Playing);

  // Every hand is thirteen cards again: the pass moved three out and three in.
  for (int s = 0; s < kSeats; ++s) CHECK_EQ(game.hands[s].count, kHandSize);

  // The seat on turn holds the two of clubs and it is its ONLY legal card.
  const Seat opener = game.turn;
  CHECK(game.hands[seatIndex(opener)].has(twoOfClubs()));
  uint8_t legal[kHandSize];
  const int n = legalPlays(game, opener, legal);
  CHECK_EQ(n, 1);
  CHECK_EQ(legal[0], twoOfClubs());
  CHECK(!playCard(game, c::makeCard(Suit::Hearts, c::kAce)));
  CHECK(playCard(game, twoOfClubs()));
}

static void testFollowSuit() {
  Game game;
  clearHands(game);
  game.phase = Phase::Playing;
  game.trickNumber = 3;
  game.heartsBroken = true;
  game.trick.clear();
  game.trick.leader = Seat::South;

  give(game, Seat::South, Suit::Clubs, c::kKing);
  give(game, Seat::West, Suit::Clubs, 5);
  give(game, Seat::West, Suit::Hearts, c::kAce);

  game.turn = Seat::South;
  CHECK(playCard(game, c::makeCard(Suit::Clubs, c::kKing)));
  CHECK(game.turn == Seat::West);

  // West holds a club, so the heart is refused.
  CHECK(!isLegalPlay(game, Seat::West, c::makeCard(Suit::Hearts, c::kAce)));
  CHECK(isLegalPlay(game, Seat::West, c::makeCard(Suit::Clubs, 5)));
  uint8_t legal[kHandSize];
  CHECK_EQ(legalPlays(game, Seat::West, legal), 1);

  // Strip the club and the heart becomes legal: void means discard anything.
  game.hands[seatIndex(Seat::West)].remove(c::makeCard(Suit::Clubs, 5));
  CHECK(isLegalPlay(game, Seat::West, c::makeCard(Suit::Hearts, c::kAce)));
}

static void testNoBloodOnFirstTrick() {
  Game game;
  clearHands(game);
  game.phase = Phase::Playing;
  game.trickNumber = 0;
  game.trick.clear();
  game.trick.leader = Seat::South;
  game.turn = Seat::South;

  give(game, Seat::South, Suit::Clubs, c::kTwo);
  // West is void in clubs and holds blood plus one safe card.
  give(game, Seat::West, Suit::Hearts, 5);
  give(game, Seat::West, Suit::Spades, c::kQueen);
  give(game, Seat::West, Suit::Diamonds, 7);

  CHECK(playCard(game, twoOfClubs()));
  CHECK(!isLegalPlay(game, Seat::West, c::makeCard(Suit::Hearts, 5)));
  CHECK(!isLegalPlay(game, Seat::West, c::makeCard(Suit::Spades, c::kQueen)));
  CHECK(isLegalPlay(game, Seat::West, c::makeCard(Suit::Diamonds, 7)));

  // Take the safe card away and blood becomes forced: a player with nothing
  // else must be able to move.
  game.hands[seatIndex(Seat::West)].remove(c::makeCard(Suit::Diamonds, 7));
  CHECK(isLegalPlay(game, Seat::West, c::makeCard(Suit::Hearts, 5)));
  CHECK(isLegalPlay(game, Seat::West, c::makeCard(Suit::Spades, c::kQueen)));
  uint8_t legal[kHandSize];
  CHECK_EQ(legalPlays(game, Seat::West, legal), 2);
}

static void testHeartsNotLedUntilBroken() {
  Game game;
  clearHands(game);
  game.phase = Phase::Playing;
  game.trickNumber = 4;
  game.heartsBroken = false;
  game.trick.clear();
  game.trick.leader = Seat::South;
  game.turn = Seat::South;

  give(game, Seat::South, Suit::Hearts, 5);
  give(game, Seat::South, Suit::Clubs, 9);
  CHECK(!isLegalPlay(game, Seat::South, c::makeCard(Suit::Hearts, 5)));
  CHECK(isLegalPlay(game, Seat::South, c::makeCard(Suit::Clubs, 9)));

  // A hand of nothing but hearts may lead one; otherwise the game deadlocks.
  game.hands[seatIndex(Seat::South)].remove(c::makeCard(Suit::Clubs, 9));
  CHECK(isLegalPlay(game, Seat::South, c::makeCard(Suit::Hearts, 5)));

  // And once broken, a heart leads freely.
  give(game, Seat::South, Suit::Clubs, 9);
  game.heartsBroken = true;
  CHECK(isLegalPlay(game, Seat::South, c::makeCard(Suit::Hearts, 5)));
}

static void testHeartsBreakOnDiscard() {
  Game game;
  clearHands(game);
  game.phase = Phase::Playing;
  game.trickNumber = 2;
  game.heartsBroken = false;
  game.trick.clear();
  game.trick.leader = Seat::South;
  game.turn = Seat::South;

  give(game, Seat::South, Suit::Clubs, 9);
  give(game, Seat::West, Suit::Hearts, 3);
  CHECK(playCard(game, c::makeCard(Suit::Clubs, 9)));
  CHECK(!game.heartsBroken);
  CHECK(playCard(game, c::makeCard(Suit::Hearts, 3)));
  CHECK(game.heartsBroken);
}

static void testTrickWinnerAndPoints() {
  Trick trick;
  trick.leader = Seat::West;
  trick.played[seatIndex(Seat::West)] = c::makeCard(Suit::Diamonds, 5);
  trick.played[seatIndex(Seat::North)] = c::makeCard(Suit::Diamonds, c::kAce);
  trick.played[seatIndex(Seat::East)] = c::makeCard(Suit::Diamonds, c::kKing);
  // South trumps nothing: an off-suit card cannot win however high.
  trick.played[seatIndex(Seat::South)] = c::makeCard(Suit::Spades, c::kAce);
  trick.count = 4;
  CHECK(trick.complete());
  CHECK(trick.ledSuit() == Suit::Diamonds);
  CHECK(trick.winner() == Seat::North);
  CHECK_EQ(trick.points(), 0);

  trick.played[seatIndex(Seat::South)] = c::makeCard(Suit::Spades, c::kQueen);
  CHECK_EQ(trick.points(), 13);
  trick.played[seatIndex(Seat::West)] = c::makeCard(Suit::Hearts, 4);
  // West led, so the led suit is hearts now and the diamonds cannot win.
  CHECK(trick.ledSuit() == Suit::Hearts);
  CHECK(trick.winner() == Seat::West);
  CHECK_EQ(trick.points(), 14);
}

static void testMoonScoring() {
  Game game;
  for (int s = 0; s < kSeats; ++s) game.total[s] = 10;
  game.taken[seatIndex(Seat::West)] = kMoonPoints;
  game.taken[seatIndex(Seat::South)] = 0;
  game.taken[seatIndex(Seat::North)] = 0;
  game.taken[seatIndex(Seat::East)] = 0;
  scoreHand(game);

  CHECK(game.lastHand.moon);
  CHECK(game.lastHand.shooter == Seat::West);
  CHECK_EQ(game.total[seatIndex(Seat::West)], 10);
  CHECK_EQ(game.total[seatIndex(Seat::South)], 36);
  CHECK_EQ(game.total[seatIndex(Seat::North)], 36);
  CHECK_EQ(game.total[seatIndex(Seat::East)], 36);
  // 36 is nowhere near kTargetScore, so the GAME is not over: only the hand.
  CHECK(game.phase == Phase::HandOver);
  // And the shooter is now winning by 26 despite having taken every point.
  CHECK(leader(game) == Seat::West);
}

static void testOrdinaryScoringAndGameEnd() {
  Game game;
  for (int s = 0; s < kSeats; ++s) game.total[s] = 0;
  game.taken[seatIndex(Seat::South)] = 5;
  game.taken[seatIndex(Seat::West)] = 8;
  game.taken[seatIndex(Seat::North)] = 13;
  game.taken[seatIndex(Seat::East)] = 0;
  scoreHand(game);
  CHECK(!game.lastHand.moon);
  CHECK_EQ(game.total[seatIndex(Seat::North)], 13);
  CHECK(game.phase == Phase::HandOver);
  CHECK(leader(game) == Seat::East);
  CHECK(!isTied(game));

  // Push one seat over the line.
  game.total[seatIndex(Seat::North)] = 95;
  game.taken[seatIndex(Seat::South)] = 0;
  game.taken[seatIndex(Seat::West)] = 0;
  game.taken[seatIndex(Seat::North)] = 13;
  game.taken[seatIndex(Seat::East)] = 13;
  scoreHand(game);
  CHECK_EQ(game.total[seatIndex(Seat::North)], 108);
  CHECK(game.phase == Phase::GameOver);
}

static void testPassMovesThreeEachWay() {
  Game game;
  uint32_t seed = 42;
  newGame(game, seed);
  CHECK(game.passDirection() == Pass::Left);

  uint8_t southSent[kPassCount];
  for (int s = 0; s < kSeats; ++s) {
    uint8_t three[kPassCount] = {game.hands[s].at(0), game.hands[s].at(1), game.hands[s].at(2)};
    if (s == seatIndex(Seat::South)) {
      for (int i = 0; i < kPassCount; ++i) southSent[i] = three[i];
    }
    CHECK(setPass(game, static_cast<Seat>(s), three, kPassCount));
  }
  commitPass(game);

  // Every hand is whole. This is the assertion that catches lifting and
  // dealing seat by seat: done that way a hand ends up with fourteen.
  int dealt = 0;
  for (int s = 0; s < kSeats; ++s) {
    CHECK_EQ(game.hands[s].count, kHandSize);
    dealt += game.hands[s].count;
  }
  CHECK_EQ(dealt, c::kDeck);

  // South passed left, so West holds those three and South does not.
  for (int i = 0; i < kPassCount; ++i) {
    CHECK(game.hands[seatIndex(Seat::West)].has(southSent[i]));
    CHECK(!game.hands[seatIndex(Seat::South)].has(southSent[i]));
  }

  // Nothing is duplicated or lost across the whole table.
  int seen[c::kDeck] = {};
  for (int s = 0; s < kSeats; ++s) {
    for (int i = 0; i < game.hands[s].count; ++i) {
      const uint8_t card = game.hands[s].at(i);
      const int index = static_cast<int>(c::suitOf(card)) * c::kRanks + c::rankOf(card);
      ++seen[index];
    }
  }
  for (int i = 0; i < c::kDeck; ++i) CHECK_EQ(seen[i], 1);
}

static void testHoldHandSkipsThePass() {
  Game game;
  uint32_t seed = 5;
  newGame(game, seed);
  game.handNumber = 3;
  CHECK(game.passDirection() == Pass::Hold);
  CHECK(passReady(game));  // nothing to choose
  uint8_t before[kSeats][kHandSize];
  for (int s = 0; s < kSeats; ++s) {
    for (int i = 0; i < kHandSize; ++i) before[s][i] = game.hands[s].at(i);
  }
  commitPass(game);
  CHECK(game.phase == Phase::Playing);
  for (int s = 0; s < kSeats; ++s) {
    CHECK_EQ(game.hands[s].count, kHandSize);
    for (int i = 0; i < kHandSize; ++i) CHECK_EQ(game.hands[s].at(i), before[s][i]);
  }
}

static void testSetPassRejectsRubbish() {
  Game game;
  uint32_t seed = 11;
  newGame(game, seed);
  const Hand& south = game.hands[seatIndex(Seat::South)];
  uint8_t two[kPassCount] = {south.at(0), south.at(1), kNoCard};
  CHECK(!setPass(game, Seat::South, two, 2));           // wrong count
  CHECK(!setPass(game, Seat::South, two, kPassCount));  // kNoCard is not held
  uint8_t dup[kPassCount] = {south.at(0), south.at(0), south.at(1)};
  CHECK(!setPass(game, Seat::South, dup, kPassCount));  // the same card twice
  uint8_t good[kPassCount] = {south.at(0), south.at(1), south.at(2)};
  CHECK(setPass(game, Seat::South, good, kPassCount));
}

// ---------------------------------------------------------------------------
// The soak.

// The consistency check that guards a restored save. Every assertion here is
// about a position the ACTIVITY could be handed by a torn write, so the test
// drives real games and then corrupts them one field at a time.
static void testConsistency() {
  uint32_t seed = 31337;
  Game game;
  newGame(game, seed);
  CHECK(isConsistent(game));

  for (int s = 0; s < kSeats; ++s) {
    uint8_t three[kPassCount] = {game.hands[s].at(0), game.hands[s].at(1), game.hands[s].at(2)};
    setPass(game, static_cast<Seat>(s), three, kPassCount);
  }
  commitPass(game);
  CHECK(isConsistent(game));

  // A LIVE TRICK IS CONSISTENT. The first version of isConsistent counted a
  // face-up card twice and rejected every game with anything on the table.
  for (int i = 0; i < 3; ++i) {
    uint8_t legal[kHandSize];
    const int n = legalPlays(game, game.turn, legal);
    CHECK(n > 0);
    CHECK(playCard(game, legal[0]));
    CHECK(isConsistent(game));
  }
  CHECK_EQ(game.trick.count, 3);

  // And it stays consistent through a whole hand.
  int guard = 0;
  while (game.phase != Phase::HandOver && game.phase != Phase::GameOver && ++guard < 200) {
    if (game.phase == Phase::TrickTaken) {
      sweepTrick(game);
    } else {
      uint8_t legal[kHandSize];
      const int n = legalPlays(game, game.turn, legal);
      CHECK(n > 0);
      if (n == 0) break;
      CHECK(playCard(game, legal[0]));
    }
    CHECK(isConsistent(game));
  }
  CHECK(game.phase == Phase::HandOver || game.phase == Phase::GameOver);

  // Now the corruptions a torn save produces, one at a time. Each starts from a
  // fresh good game so the checks cannot mask each other.
  const auto fresh = [&seed]() {
    Game g;
    newGame(g, seed);
    return g;
  };
  {
    Game g = fresh();
    g.phase = static_cast<Phase>(99);
    CHECK(!isConsistent(g));
  }
  {
    Game g = fresh();
    g.turn = static_cast<Seat>(5);  // the out-of-bounds hands[5] read
    CHECK(!isConsistent(g));
  }
  {
    Game g = fresh();
    g.hands[1].count = 20;  // reads past cards[13], and past picked[13]
    CHECK(!isConsistent(g));
  }
  {
    Game g = fresh();
    g.trickNumber = 40;
    CHECK(!isConsistent(g));
  }
  {
    Game g = fresh();
    g.taken[2] = 99;
    CHECK(!isConsistent(g));
  }
  {
    Game g = fresh();
    g.total[0] = -5;
    CHECK(!isConsistent(g));
  }
  {
    Game g = fresh();
    // A DUPLICATED CARD, which is the shape a half-written struct really takes:
    // South's first card also appears in West's hand.
    g.hands[1].cards[0] = g.hands[0].at(0);
    CHECK(!isConsistent(g));
  }
  {
    Game g = fresh();
    g.hands[0].count = 12;  // a card that is in no hand and was never played
    CHECK(!isConsistent(g));
  }
  {
    Game g = fresh();
    g.trick.count = 2;  // says two are down while played[] says none are
    CHECK(!isConsistent(g));
  }
}

static void testSoak() {
  uint32_t seed = 0xC0FFEEu;
  constexpr int kGames = 400;
  int moons = 0;
  int handsPlayed = 0;

  for (int g = 0; g < kGames; ++g) {
    Game game;
    newGame(game, seed);

    // A hand is 13 tricks of 4 plays plus a sweep each, plus the pass: 66
    // steps. A game to 100 at ~6.5 points a hand for the leader is about 15
    // hands, and random play runs longer than that, so the ceiling is set well
    // clear of it and asserted below rather than silently ending the game.
    constexpr int kStepCeiling = 6000;
    int guard = 0;
    while (game.phase != Phase::GameOver && ++guard < kStepCeiling) {
      if (game.phase == Phase::Passing) {
        for (int s = 0; s < kSeats; ++s) {
          const Hand& hand = game.hands[s];
          uint8_t three[kPassCount];
          // Three distinct indices, chosen by the same rng the deal uses.
          int picked = 0;
          bool used[kHandSize] = {};
          while (picked < kPassCount) {
            const int at = static_cast<int>(nextRandom(seed) % static_cast<uint32_t>(hand.count));
            if (used[at]) continue;
            used[at] = true;
            three[picked++] = hand.at(at);
          }
          if (game.passDirection() != Pass::Hold) {
            CHECK(setPass(game, static_cast<Seat>(s), three, kPassCount));
          }
        }
        CHECK(passReady(game));
        commitPass(game);
        CHECK(game.phase == Phase::Playing);
        // Whoever leads holds the two of clubs.
        CHECK(game.hands[seatIndex(game.turn)].has(twoOfClubs()));
        ++handsPlayed;
        continue;
      }

      if (game.phase == Phase::Playing) {
        uint8_t legal[kHandSize];
        const int n = legalPlays(game, game.turn, legal);
        // A seat on turn ALWAYS has a legal card. A rule that can leave none is
        // a deadlock, and this is the assertion that finds it.
        CHECK(n > 0);
        if (n == 0) break;
        const uint8_t card = legal[nextRandom(seed) % static_cast<uint32_t>(n)];
        const Seat mover = game.turn;
        const int before = game.hands[seatIndex(mover)].count;
        CHECK(playCard(game, card));
        // Exactly one card left the hand, and it is the one on the table.
        CHECK_EQ(game.hands[seatIndex(mover)].count, before - 1);
        CHECK_EQ(game.trick.played[seatIndex(mover)], card);
        CHECK(!game.hands[seatIndex(mover)].has(card));
        continue;
      }

      if (game.phase == Phase::TrickTaken) {
        CHECK(game.trick.complete());
        CHECK_EQ(game.trick.count, kSeats);
        // Every seat played exactly one card, and all four followed the rules
        // well enough that the led suit is present.
        for (int s = 0; s < kSeats; ++s) CHECK(game.trick.played[s] != kNoCard);
        const int pointsOnTable = game.trick.points();
        const Seat winner = game.trick.winner();
        const int takenBefore = game.taken[seatIndex(winner)];
        sweepTrick(game);
        if (game.phase != Phase::HandOver && game.phase != Phase::GameOver) {
          CHECK_EQ(game.taken[seatIndex(winner)], takenBefore + pointsOnTable);
          CHECK(game.turn == winner);
          CHECK(game.trick.leader == winner);
          CHECK(game.trick.empty());
        }
        continue;
      }

      if (game.phase == Phase::HandOver) {
        // A finished hand accounts for exactly 26 points and thirteen tricks.
        int sum = 0;
        int tricks = 0;
        for (int s = 0; s < kSeats; ++s) {
          sum += game.lastHand.taken[s];
          tricks += game.tricksWon[s];
          CHECK(game.hands[s].empty());
        }
        CHECK_EQ(sum, kMoonPoints);
        CHECK_EQ(tricks, kTricks);
        if (game.lastHand.moon) {
          ++moons;
          // A moon is easy to fake by miscounting, so check the shape of it
          // rather than the number: the shooter holds all 26 and everyone else
          // is empty, and you cannot take 26 points in fewer than four tricks
          // (thirteen hearts spread over at least three, plus the queen).
          const int shooter = seatIndex(game.lastHand.shooter);
          CHECK_EQ(game.lastHand.taken[shooter], kMoonPoints);
          CHECK_EQ(game.lastHand.scored[shooter], 0);
          for (int s = 0; s < kSeats; ++s) {
            if (s == shooter) continue;
            CHECK_EQ(game.lastHand.taken[s], 0);
            CHECK_EQ(game.lastHand.scored[s], kMoonPoints);
          }
          CHECK(game.tricksWon[shooter] >= 4);
        }
        nextHand(game, seed);
        continue;
      }
      break;
    }

    CHECK(guard < kStepCeiling);
    CHECK(game.phase == Phase::GameOver);
    // Somebody is at or past the target, and nobody is negative.
    bool anyOver = false;
    for (int s = 0; s < kSeats; ++s) {
      if (game.total[s] >= kTargetScore) anyOver = true;
      CHECK(game.total[s] >= 0);
    }
    CHECK(anyOver);
  }

  std::printf("  soak: %d games, %d hands, %d moons shot by random play\n", kGames, handsPlayed, moons);
}

int main() {
  testTrickRank();
  testPenalties();
  testPassRotation();
  testHandSorting();
  testTwoOfClubsOpens();
  testFollowSuit();
  testNoBloodOnFirstTrick();
  testHeartsNotLedUntilBroken();
  testHeartsBreakOnDiscard();
  testTrickWinnerAndPoints();
  testMoonScoring();
  testOrdinaryScoringAndGameEnd();
  testPassMovesThreeEachWay();
  testHoldHandSkipsThePass();
  testSetPassRejectsRubbish();
  testConsistency();
  testSoak();

  // "failed", not "failures": check.sh counts sub-suites by grepping for
  // "checks, 0 failed", so this suite RAN, PASSED, and was left out of the
  // "ok (N sub-suite(s))" tally -- and a suite nobody notices is missing
  // looks exactly like one that was never added.
  std::printf("hearts: %d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
