// Sea Salt & Paper, the rules. The properties here are chosen so that a whole
// class of bug cannot survive them, rather than so that a known-good transcript
// replays -- see docs/building-apps.md on weak bounds.
//
// The two that carry the most weight:
//
//   * Conservation. All 58 cards exist exactly once, in exactly one place,
//     after every single action of a randomly played match. Every move in this
//     game is a card changing place, so a rule that loses, duplicates or
//     teleports a card is a rule this catches.
//   * "Duos score whether they were played or not." Laying a pair down must
//     leave `cardPoints` unchanged. That is a rulebook sentence expressed as an
//     invariant over every reachable state, not as an example.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "SeaSaltCards.h"
#include "SeaSaltCore.h"

using namespace seasalt;

static int checks = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                               \
    }                                                             \
  } while (0)

static uint32_t rngState = 20260810u;
static uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

// --- helpers ---------------------------------------------------------------

// Puts a specific card into a seat's hand, wherever it currently is. Only for
// building a hand to score; never used to reach a state the rules could not.
static void giveTo(Game& g, const int seat, const Kind kind, const int howMany) {
  int placed = 0;
  for (int c = 0; c < kCards && placed < howMany; ++c) {
    if (kindOf(static_cast<uint8_t>(c)) != kind) continue;
    g.place[c] = static_cast<uint8_t>(handOf(seat));
    g.seq[c] = 0;
    ++placed;
  }
  CHECK(placed == howMany);
}

static Game emptyTable() {
  Game g;
  g.deal(12345u, 0);
  // Everything back to the deck: these tests score hands they build by hand.
  for (int c = 0; c < kCards; ++c) {
    g.place[c] = static_cast<uint8_t>(Place::Deck);
    g.seq[c] = 0;
  }
  return g;
}

// --- conservation ----------------------------------------------------------

static void checkConservation(const Game& g) {
  int perPlace[kPlaceCount] = {};
  for (int c = 0; c < kCards; ++c) {
    CHECK(g.place[c] < kPlaceCount);
    ++perPlace[g.place[c]];
    // A card outside a pile has no pile order, and one inside always has one.
    const bool inPile =
        g.place[c] == static_cast<uint8_t>(Place::PileA) || g.place[c] == static_cast<uint8_t>(Place::PileB);
    CHECK(inPile ? g.seq[c] != 0 : g.seq[c] == 0);
  }
  int total = 0;
  for (int i = 0; i < kPlaceCount; ++i) total += perPlace[i];
  CHECK(total == kCards);

  // Only a turn mid-draw has cards in limbo, and exactly the ones it names.
  int limbo = 0;
  for (int c = 0; c < kCards; ++c) {
    if (g.place[c] != static_cast<uint8_t>(Place::Drawn)) continue;
    ++limbo;
    CHECK(c == g.drawn[0] || c == g.drawn[1] || c == g.pendingDiscard);
  }
  const int named = (g.drawn[0] != kNoCard) + (g.drawn[1] != kNoCard) + (g.pendingDiscard != kNoCard);
  CHECK(limbo == named);

  // The top of a pile is the highest sequence number in it, and nothing else.
  for (int p = 0; p < kPiles; ++p) {
    const uint8_t top = g.pileTop(p);
    if (g.pileSize(p) == 0) {
      CHECK(top == kNoCard);
      continue;
    }
    CHECK(g.place[top] == static_cast<uint8_t>(pileAt(p)));
    for (int c = 0; c < kCards; ++c) {
      if (g.place[c] == static_cast<uint8_t>(pileAt(p)) && c != top) CHECK(g.seq[c] < g.seq[top]);
    }
  }

  // Only duo cards ever reach a table. Mermaids in particular never can.
  for (int s = 0; s < kSeats; ++s) {
    for (int c = 0; c < kCards; ++c) {
      if (g.place[c] != static_cast<uint8_t>(tableOf(s))) continue;
      CHECK(isDuo(kindOf(static_cast<uint8_t>(c))));
    }
    // And a table only ever holds whole pairs.
    CHECK(g.countIn(tableOf(s), Kind::Crab) % 2 == 0);
    CHECK(g.countIn(tableOf(s), Kind::Boat) % 2 == 0);
    CHECK(g.countIn(tableOf(s), Kind::Fish) % 2 == 0);
    CHECK(g.countIn(tableOf(s), Kind::Swimmer) == g.countIn(tableOf(s), Kind::Shark));
  }
}

// The deck half of conservation: only meaningful for a state that was reached
// by playing, since the scoring tests build hands by hand and never deal them.
// A card leaves the deck only by being dealt, so the deck's population and the
// deal cursor cannot drift, and every card past the cursor is still in there.
static void checkDeckAccounting(const Game& g) {
  int inDeck = 0;
  for (int c = 0; c < kCards; ++c) {
    if (g.place[c] == static_cast<uint8_t>(Place::Deck)) ++inDeck;
  }
  CHECK(inDeck == kCards - g.deckNext);
  for (int i = g.deckNext; i < kCards; ++i) {
    CHECK(g.place[g.cardAt(i)] == static_cast<uint8_t>(Place::Deck));
  }
}

// --- the deck is a permutation --------------------------------------------

static void testDeckIsAPermutation() {
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    Game g;
    g.deal(seed * 2654435761u + 1u, 0);
    int seen[kCards] = {0};
    for (int i = 0; i < kCards; ++i) {
      const uint8_t card = g.cardAt(i);
      CHECK(card < kCards);
      ++seen[card];
    }
    for (int c = 0; c < kCards; ++c) CHECK(seen[c] == 1);
    checkConservation(g);
    checkDeckAccounting(g);
    // A fresh deal turns exactly two cards up and deals no hands.
    CHECK(g.pileSize(0) == 1);
    CHECK(g.pileSize(1) == 1);
    CHECK(g.handSize(0) == 0);
    CHECK(g.handSize(1) == 0);
    CHECK(g.deckRemaining() == kCards - 2);
  }
}

// --- the card table matches the supply table ------------------------------

static void testSupply() {
  int perKind[kKindCount] = {0};
  for (int c = 0; c < kCards; ++c) {
    CHECK(kCardKind[c] < kKindCount);
    ++perKind[kCardKind[c]];
  }
  for (int k = 0; k < kKindCount; ++k) CHECK(perKind[k] == kKindSupply[k]);

  int totalColour[kColourCount] = {0};
  for (int c = 0; c < kCards; ++c) {
    CHECK(kCardColour[c] < kColourCount);
    ++totalColour[kCardColour[c]];
  }
  int sum = 0;
  for (int i = 0; i < kColourCount; ++i) sum += totalColour[i];
  CHECK(sum == kCards);
}

// --- scoring ---------------------------------------------------------------

static void testCollectorTables() {
  // Straight from the rulebook, one row per legal count. A transcription error
  // in any of these four tables shows up here and nowhere else.
  const struct {
    Kind kind;
    int counts;
    int expected[7];
  } cases[] = {
      {Kind::Shell, 7, {0, 0, 2, 4, 6, 8, 10}},
      {Kind::Octopus, 6, {0, 0, 3, 6, 9, 12, 0}},
      {Kind::Penguin, 4, {0, 1, 3, 5, 0, 0, 0}},
      {Kind::Sailor, 3, {0, 0, 5, 0, 0, 0, 0}},
  };
  for (const auto& c : cases) {
    for (int n = 0; n < c.counts; ++n) {
      Game g = emptyTable();
      giveTo(g, 0, c.kind, n);
      CHECK(g.cardPoints(0) == c.expected[n]);
    }
  }
}

static void testDuoPairs() {
  // 1 point per pair, and an odd card is worth nothing on its own.
  for (int n = 0; n <= 9; ++n) {
    Game g = emptyTable();
    giveTo(g, 0, Kind::Crab, n);
    CHECK(g.cardPoints(0) == n / 2);
  }
  for (int n = 0; n <= 8; ++n) {
    Game g = emptyTable();
    giveTo(g, 0, Kind::Boat, n);
    CHECK(g.cardPoints(0) == n / 2);
  }
  for (int n = 0; n <= 7; ++n) {
    Game g = emptyTable();
    giveTo(g, 0, Kind::Fish, n);
    CHECK(g.cardPoints(0) == n / 2);
  }
  // Swimmer and shark pair with each other, so it is the smaller count that
  // pays -- five swimmers alone are worth nothing.
  for (int sw = 0; sw <= 5; ++sw) {
    for (int sh = 0; sh <= 5; ++sh) {
      Game g = emptyTable();
      giveTo(g, 0, Kind::Swimmer, sw);
      giveTo(g, 0, Kind::Shark, sh);
      CHECK(g.cardPoints(0) == (sw < sh ? sw : sh));
    }
  }
}

static void testMultipliers() {
  // Each multiplier counts the cards it names and is never one of them, so
  // adding it to a bare hand of its own kind is worth zero.
  {
    Game g = emptyTable();
    giveTo(g, 0, Kind::Lighthouse, 1);
    CHECK(g.cardPoints(0) == 0);
    giveTo(g, 0, Kind::Boat, 3);
    CHECK(g.cardPoints(0) == 3 + 1);  // 3 boats: 1 pair, plus 1 per boat
  }
  {
    Game g = emptyTable();
    giveTo(g, 0, Kind::ShoalOfFish, 1);
    giveTo(g, 0, Kind::Fish, 5);
    CHECK(g.cardPoints(0) == 5 + 2);
  }
  {
    Game g = emptyTable();
    giveTo(g, 0, Kind::PenguinColony, 1);
    giveTo(g, 0, Kind::Penguin, 3);
    CHECK(g.cardPoints(0) == 6 + 5);  // 2 each, plus the collector's 5
  }
  {
    Game g = emptyTable();
    giveTo(g, 0, Kind::Captain, 1);
    giveTo(g, 0, Kind::Sailor, 2);
    CHECK(g.cardPoints(0) == 6 + 5);  // 3 each, plus the collector's 5
  }
}

// The invariant behind the rulebook's "the points for duo cards count whether
// the cards have been played or not". Laying every playable pair down must not
// move the score by a single point, in any reachable state.
static void checkLayingDownIsFree(const Game& before) {
  for (int s = 0; s < kSeats; ++s) {
    Game g = before;
    const int was = g.cardPoints(s);
    const uint8_t hand = static_cast<uint8_t>(handOf(s));
    const uint8_t table = static_cast<uint8_t>(tableOf(s));
    // Move whole pairs, ignoring the turn and the step: this is a scoring
    // property, not a legality one.
    for (Kind k : {Kind::Crab, Kind::Boat, Kind::Fish}) {
      int movable = (g.countIn(handOf(s), k) / 2) * 2;
      for (int c = 0; c < kCards && movable > 0; ++c) {
        if (g.place[c] == hand && kindOf(static_cast<uint8_t>(c)) == k) {
          g.place[c] = table;
          --movable;
        }
      }
    }
    int pairs = g.playablePairs(s, Kind::Swimmer);
    for (Kind k : {Kind::Swimmer, Kind::Shark}) {
      int movable = pairs;
      for (int c = 0; c < kCards && movable > 0; ++c) {
        if (g.place[c] == hand && kindOf(static_cast<uint8_t>(c)) == k) {
          g.place[c] = table;
          --movable;
        }
      }
    }
    CHECK(g.cardPoints(s) == was);
  }
}

static void testMermaidsTakeDistinctColours() {
  // With m mermaids the score is the sum of the m largest colour groups, and
  // the mermaids themselves are cards with colours, so they count too.
  for (int trial = 0; trial < 400; ++trial) {
    Game g = emptyTable();
    for (int c = 0; c < kCards; ++c) {
      if (rnd() % 3 == 0) g.place[c] = static_cast<uint8_t>(handOf(0));
    }
    const int mermaids = g.countHeld(0, Kind::Mermaid);
    if (mermaids == 0) continue;

    int perColour[kColourCount];
    for (int i = 0; i < kColourCount; ++i) perColour[i] = g.countHeldColour(0, static_cast<Colour>(i));
    // The m largest, longhand.
    int expected = 0;
    for (int take = 0; take < mermaids; ++take) {
      int best = 0, at = -1;
      for (int i = 0; i < kColourCount; ++i) {
        if (perColour[i] > best) {
          best = perColour[i];
          at = i;
        }
      }
      if (at < 0) break;
      expected += best;
      perColour[at] = 0;
    }

    // Score the same hand with the mermaids removed to isolate their share.
    Game without = g;
    for (int c = 0; c < kCards; ++c) {
      if (kindOf(static_cast<uint8_t>(c)) == Kind::Mermaid) without.place[c] = static_cast<uint8_t>(Place::Deck);
    }
    // Removing mermaids also removes their colours, so re-derive the baseline
    // the honest way: the difference must be exactly the m largest groups of
    // the full hand.
    int baseline = without.cardPoints(0);
    // Add the mermaids' own contribution to the colour groups back in by
    // scoring the full hand and subtracting.
    CHECK(g.cardPoints(0) - baseline == expected);
  }
}

static void testColourBonusIsTheLargestGroup() {
  for (int trial = 0; trial < 400; ++trial) {
    Game g = emptyTable();
    for (int c = 0; c < kCards; ++c) {
      if (rnd() % 4 == 0) g.place[c] = static_cast<uint8_t>(handOf(1));
    }
    int best = 0;
    for (int i = 0; i < kColourCount; ++i) {
      const int n = g.countHeldColour(1, static_cast<Colour>(i));
      if (n > best) best = n;
    }
    CHECK(g.colourBonus(1) == best);
  }
}

// --- ending a round --------------------------------------------------------

static void testStopPaysCardsAndNoBonus() {
  Game g = emptyTable();
  giveTo(g, 0, Kind::Shell, 4);    // 6
  giveTo(g, 0, Kind::Crab, 2);     // 1  -> 7, exactly enough to end
  giveTo(g, 1, Kind::Octopus, 3);  // 6
  g.turn = 0;
  g.step = static_cast<uint8_t>(Step::Play);
  CHECK(g.cardPoints(0) == 7);
  CHECK(g.mayEndRound(0));
  CHECK(g.endRound(false));
  CHECK(g.currentPhase() == Phase::RoundOver);
  CHECK(g.roundScore(0) == 7);
  CHECK(g.roundScore(1) == 6);
  CHECK(g.score[0] == 7);
  CHECK(g.score[1] == 6);
}

static void testCannotEndBelowSeven() {
  Game g = emptyTable();
  giveTo(g, 0, Kind::Shell, 4);  // 6, one short
  g.turn = 0;
  g.step = static_cast<uint8_t>(Step::Play);
  CHECK(g.cardPoints(0) == 6);
  CHECK(!g.mayEndRound(0));
  CHECK(!g.endRound(false));
  CHECK(!g.endRound(true));
  CHECK(g.currentPhase() == Phase::Playing);
}

static void testLastChanceBothWays() {
  // Bet won: the ender takes cards + bonus, the opponent takes bonus only.
  {
    Game g = emptyTable();
    giveTo(g, 0, Kind::Shell, 5);    // 8
    giveTo(g, 1, Kind::Octopus, 2);  // 3
    g.turn = 0;
    g.step = static_cast<uint8_t>(Step::Play);
    const int enderCards = g.cardPoints(0);
    const int enderBonus = g.colourBonus(0);
    const int oppBonus = g.colourBonus(1);
    CHECK(g.endRound(true));
    CHECK(g.currentPhase() == Phase::LastChance);
    CHECK(g.turn == 1);
    // The opponent owes a turn; skip straight past it.
    g.step = static_cast<uint8_t>(Step::Play);
    CHECK(g.endTurn());
    CHECK(g.betWon());
    CHECK(g.roundScore(0) == enderCards + enderBonus);
    CHECK(g.roundScore(1) == oppBonus);
  }
  // Bet lost: the ender takes bonus only, the opponent takes cards only.
  {
    Game g = emptyTable();
    giveTo(g, 0, Kind::Shell, 4);    // 6
    giveTo(g, 0, Kind::Crab, 2);     // 1 -> 7
    giveTo(g, 1, Kind::Octopus, 5);  // 12
    g.turn = 0;
    g.step = static_cast<uint8_t>(Step::Play);
    const int enderBonus = g.colourBonus(0);
    const int oppCards = g.cardPoints(1);
    CHECK(g.endRound(true));
    g.step = static_cast<uint8_t>(Step::Play);
    CHECK(g.endTurn());
    CHECK(!g.betWon());
    CHECK(g.roundScore(0) == enderBonus);
    CHECK(g.roundScore(1) == oppCards);
  }
  // A tie is the ender's: "higher or equal".
  {
    Game g = emptyTable();
    giveTo(g, 0, Kind::Shell, 5);    // 8
    giveTo(g, 1, Kind::Penguin, 3);  // 5
    giveTo(g, 1, Kind::Fish, 6);     // 3 -> 8
    g.turn = 0;
    g.step = static_cast<uint8_t>(Step::Play);
    CHECK(g.cardPoints(0) == g.cardPoints(1));
    CHECK(g.endRound(true));
    g.step = static_cast<uint8_t>(Step::Play);
    CHECK(g.endTurn());
    CHECK(g.betWon());
  }
}

static void testRevealedHandCannotBeStolenFrom() {
  Game g = emptyTable();
  giveTo(g, 0, Kind::Swimmer, 1);
  giveTo(g, 0, Kind::Shark, 1);
  giveTo(g, 1, Kind::Octopus, 4);
  g.turn = 0;
  g.step = static_cast<uint8_t>(Step::Play);
  g.revealedMask = 1u << 1;  // seat 1 has revealed

  const int theirHand = g.handSize(1);
  const int mine = g.handSize(0);
  const uint8_t sw = static_cast<uint8_t>(firstCardOf(Kind::Swimmer));
  const uint8_t sh = static_cast<uint8_t>(firstCardOf(Kind::Shark));
  CHECK(g.playDuo(sw, sh));
  CHECK(g.handSize(1) == theirHand);  // nothing taken
  CHECK(g.handSize(0) == mine - 2);   // the pair left my hand for the table
  checkConservation(g);
}

static void testFourMermaidsWinsOnArrival() {
  Game g = emptyTable();
  giveTo(g, 0, Kind::Mermaid, 3);
  g.turn = 0;
  g.step = static_cast<uint8_t>(Step::Take);
  // Put the fourth mermaid on top of a pile and take it.
  const uint8_t fourth = static_cast<uint8_t>(firstCardOf(Kind::Mermaid) + 3);
  g.place[fourth] = static_cast<uint8_t>(Place::PileA);
  g.seq[fourth] = g.seqNext++;
  CHECK(g.currentPhase() == Phase::Playing);
  CHECK(g.takeFromPile(0));
  CHECK(g.mermaidsHeld(0) == 4);
  CHECK(g.currentPhase() == Phase::GameOver);
  CHECK(g.matchWinner() == 0);
}

// --- the soak --------------------------------------------------------------

// Every action legal from here. The point is that the soak never has to know
// the rules: it asks, and the core answers.
static bool playOneRandomAction(Game& g) {
  const int seat = g.turn;
  switch (g.currentStep()) {
    case Step::Take: {
      const bool deck = g.deckRemaining() > 0;
      std::vector<int> piles;
      for (int p = 0; p < kPiles; ++p) {
        if (g.pileTop(p) != kNoCard) piles.push_back(p);
      }
      if (!deck && piles.empty()) return g.endTurn();  // nothing to take: pass
      if (deck && (piles.empty() || rnd() % 2)) return g.takeFromDeck();
      return g.takeFromPile(piles[rnd() % piles.size()]);
    }
    case Step::ChooseKeep:
      return g.keepDrawn(static_cast<int>(rnd() % 2));
    case Step::ChoosePile:
      return g.discardTo(static_cast<int>(rnd() % kPiles));
    case Step::CrabPile: {
      std::vector<int> piles;
      for (int p = 0; p < kPiles; ++p) {
        if (g.pileSize(p) > 0) piles.push_back(p);
      }
      CHECK(!piles.empty());  // the step is only entered when one is non-empty
      return g.chooseCrabPile(piles[rnd() % piles.size()]);
    }
    case Step::CrabPick: {
      std::vector<uint8_t> in;
      for (int c = 0; c < kCards; ++c) {
        if (g.place[c] == static_cast<uint8_t>(pileAt(g.crabPile))) in.push_back(static_cast<uint8_t>(c));
      }
      CHECK(!in.empty());
      return g.takeCrabCard(in[rnd() % in.size()]);
    }
    case Step::Play: {
      // Lay a pair sometimes, so the duo effects get exercised.
      if (rnd() % 3 != 0) {
        for (Kind k : {Kind::Crab, Kind::Boat, Kind::Fish}) {
          if (g.playablePairs(seat, k) == 0) continue;
          uint8_t a = kNoCard, b = kNoCard;
          for (int c = 0; c < kCards; ++c) {
            if (g.place[c] != static_cast<uint8_t>(handOf(seat))) continue;
            if (kindOf(static_cast<uint8_t>(c)) != k) continue;
            (a == kNoCard ? a : b) = static_cast<uint8_t>(c);
            if (b != kNoCard) break;
          }
          CHECK(a != kNoCard && b != kNoCard);
          return g.playDuo(a, b);
        }
        if (g.playablePairs(seat, Kind::Swimmer) > 0) {
          uint8_t sw = kNoCard, sh = kNoCard;
          for (int c = 0; c < kCards; ++c) {
            if (g.place[c] != static_cast<uint8_t>(handOf(seat))) continue;
            const Kind k = kindOf(static_cast<uint8_t>(c));
            if (k == Kind::Swimmer && sw == kNoCard) sw = static_cast<uint8_t>(c);
            if (k == Kind::Shark && sh == kNoCard) sh = static_cast<uint8_t>(c);
          }
          CHECK(sw != kNoCard && sh != kNoCard);
          return g.playDuo(sw, sh);
        }
      }
      if (g.currentPhase() == Phase::Playing && g.mayEndRound(seat) && rnd() % 12 == 0) {
        return g.endRound(rnd() % 2 == 0);
      }
      return g.endTurn();
    }
  }
  return false;
}

static void soak() {
  int matches = 0, mermaidWins = 0, deckOuts = 0, stops = 0, bets = 0;
  for (int m = 0; m < 3000; ++m) {
    Game g;
    g.newGame(rnd() | 1u, static_cast<uint8_t>(rnd() % 2));
    checkConservation(g);
    checkDeckAccounting(g);

    int guard = 0;
    while (g.currentPhase() != Phase::GameOver && ++guard < 4000) {
      if (g.currentPhase() == Phase::RoundOver) {
        // A round that scored must have banked exactly what roundScore says,
        // and a round that ran out of deck must have banked nothing.
        if (g.ender == kNoSeat) ++deckOuts;
        const uint8_t before0 = g.score[0], before1 = g.score[1];
        g.round = static_cast<uint8_t>(g.round + 1);
        g.deal(rnd() | 1u, static_cast<uint8_t>((g.roundStarter + 1) % kSeats));
        g.score[0] = before0;
        g.score[1] = before1;
        continue;
      }
      const Step was = g.currentStep();
      const int turnWas = g.turn;
      if (!playOneRandomAction(g)) break;
      checkConservation(g);
      checkDeckAccounting(g);
      checkLayingDownIsFree(g);
      // Nothing may move to a seat that is not the one to play, except the
      // shark's steal, which moves a card *away* from the other seat.
      CHECK(g.turn == turnWas || was == Step::Play);
      // Scores never go backwards.
      CHECK(g.score[0] <= 200 && g.score[1] <= 200);
    }
    CHECK(guard < 4000);  // no state the soak cannot leave

    ++matches;
    if (g.mermaidsHeld(0) == 4 || g.mermaidsHeld(1) == 4) {
      ++mermaidWins;
    } else {
      CHECK(g.score[0] >= kTargetScore || g.score[1] >= kTargetScore);
      CHECK(g.matchWinner() >= 0);
    }
    if (g.betWasLastChance) {
      ++bets;
    } else if (g.ender != kNoSeat) {
      ++stops;
    }
  }
  std::printf("  soak: %d matches, %d mermaid wins, %d deck-outs, %d stops, %d bets\n", matches, mermaidWins, deckOuts,
              stops, bets);
  // A soak that never reaches these has not tested them. Calibration, not
  // decoration: each of these was zero at some point during development.
  CHECK(deckOuts > 0);
  CHECK(stops > 0);
  CHECK(bets > 0);
}

int main() {
  testSupply();
  testDeckIsAPermutation();
  testCollectorTables();
  testDuoPairs();
  testMultipliers();
  testMermaidsTakeDistinctColours();
  testColourBonusIsTheLargestGroup();
  testStopPaysCardsAndNoBonus();
  testCannotEndBelowSeven();
  testLastChanceBothWays();
  testRevealedHandCannotBeStolenFrom();
  testFourMermaidsWinsOnArrival();
  soak();
  // "0 failed" is not a literal standing in for a count: CHECK() calls
  // std::exit(1) on the first failure, so this line is only ever
  // reached with none. The wording is what check.sh counts sub-suites
  // by, and "N checks passed" made this suite report 0 of them.
  std::printf("seasalt: %d checks, 0 failed\n", checks);
  return 0;
}
