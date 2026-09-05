// The opponent, and the observation it sees through.
//
// The observation properties matter more than the play strength: `observe()`
// is the wall between the AI and the opponent's hand, so the tests here prove
// the wall is real -- nothing hidden leaks through it, and nothing public is
// lost behind it -- before asking whether the brain plays well.

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "SeaSaltBrain.h"
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

static uint32_t rngState = 424242u;
static uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

// --- the wall --------------------------------------------------------------

static void checkObservation(const Game& g, const int seat) {
  const Observation obs = observe(g, seat);

  // Nothing hidden leaks: every card in the opponent's unrevealed hand, and
  // every card in the deck, is fog. Fog is also exactly one bucket -- the
  // observation must not let you tell those two apart by any field.
  const bool revealed = (g.revealedMask >> (seat ^ 1)) & 1u;
  int fog = 0;
  for (int c = 0; c < kCards; ++c) {
    const uint8_t v = obs.view[c];
    if (g.place[c] == static_cast<uint8_t>(handOf(seat ^ 1)) && !revealed)
      CHECK(v == static_cast<uint8_t>(View::Unknown));
    if (g.place[c] == static_cast<uint8_t>(Place::Deck)) CHECK(v == static_cast<uint8_t>(View::Unknown));
    if (v == static_cast<uint8_t>(View::Unknown)) ++fog;
    // Their drawn cards mid-decision are fog too.
    if (g.place[c] == static_cast<uint8_t>(Place::Drawn) && g.turn != seat)
      CHECK(v == static_cast<uint8_t>(View::Unknown));
  }
  // And nothing public is lost: the fog is exactly the deck plus their
  // unrevealed hand plus their in-flight draws, no bigger.
  int expected = g.deckRemaining() + (revealed ? 0 : g.handSize(seat ^ 1));
  if (g.turn != seat) {
    for (int c = 0; c < kCards; ++c) {
      if (g.place[c] == static_cast<uint8_t>(Place::Drawn)) ++expected;
    }
  }
  CHECK(fog == expected);

  // The observed points equal the true points: observe() lost nothing the
  // scoring is entitled to.
  CHECK(obs.myPoints() == g.cardPoints(seat));
}

// --- the driver ------------------------------------------------------------

// Translates a Decision into the core call it names. Returns false only if the
// core rejected it, which the tests treat as a brain bug.
static bool applyDecision(Game& g, const Decision& d) {
  const int seat = g.turn;
  switch (d.act) {
    case Decision::Act::TakeDeck:
      return g.takeFromDeck();
    case Decision::Act::TakePile:
      return g.takeFromPile(d.a);
    case Decision::Act::Keep:
      return g.keepDrawn(d.a);
    case Decision::Act::DiscardTo:
      return g.discardTo(d.a);
    case Decision::Act::LayDuo: {
      uint8_t a = kNoCard, b = kNoCard;
      const Kind ka = d.kind == Kind::Swimmer ? Kind::Swimmer : d.kind;
      const Kind kb = d.kind == Kind::Swimmer ? Kind::Shark : d.kind;
      for (int c = 0; c < kCards; ++c) {
        if (g.place[c] != static_cast<uint8_t>(handOf(seat))) continue;
        const Kind k = kindOf(static_cast<uint8_t>(c));
        if (k == ka && a == kNoCard) {
          a = static_cast<uint8_t>(c);
          if (ka == kb) {
            continue;  // the second of the same kind still needs finding
          }
        } else if (k == kb && (b == kNoCard) && (ka != kb || c != a)) {
          b = static_cast<uint8_t>(c);
        }
        if (ka == kb && a != kNoCard && b == kNoCard && k == ka && c != a) b = static_cast<uint8_t>(c);
      }
      if (a == kNoCard || b == kNoCard) return false;
      return g.playDuo(a, b);
    }
    case Decision::Act::EndTurn:
      return g.endTurn();
    case Decision::Act::Stop:
      return g.endRound(false);
    case Decision::Act::LastChance:
      return g.endRound(true);
    case Decision::Act::DigPile:
      return g.chooseCrabPile(d.a);
    case Decision::Act::DigCard:
      return g.takeCrabCard(d.a);
  }
  return false;
}

// A random-legal player, the baseline the brain has to beat.
static Decision randomDecision(const Game& g) {
  Decision d;
  switch (g.currentStep()) {
    case Step::Take: {
      const bool deckOk = g.deckRemaining() > 0;
      int piles[2], n = 0;
      for (int p = 0; p < kPiles; ++p) {
        if (g.pileTop(p) != kNoCard) piles[n++] = p;
      }
      if (deckOk && (n == 0 || rnd() % 2)) {
        d.act = Decision::Act::TakeDeck;
      } else if (n > 0) {
        d.act = Decision::Act::TakePile;
        d.a = static_cast<uint8_t>(piles[rnd() % n]);
      } else {
        d.act = Decision::Act::EndTurn;
      }
      return d;
    }
    case Step::ChooseKeep:
      d.act = Decision::Act::Keep;
      d.a = static_cast<uint8_t>(rnd() % 2);
      return d;
    case Step::ChoosePile:
      d.act = Decision::Act::DiscardTo;
      d.a = static_cast<uint8_t>(rnd() % kPiles);
      return d;
    case Step::CrabPile: {
      int piles[2], n = 0;
      for (int p = 0; p < kPiles; ++p) {
        if (g.pileSize(p) > 0) piles[n++] = p;
      }
      d.act = Decision::Act::DigPile;
      d.a = static_cast<uint8_t>(piles[rnd() % n]);
      return d;
    }
    case Step::CrabPick: {
      uint8_t in[kCards];
      int n = 0;
      for (int c = 0; c < kCards; ++c) {
        if (g.place[c] == static_cast<uint8_t>(pileAt(g.crabPile))) in[n++] = static_cast<uint8_t>(c);
      }
      d.act = Decision::Act::DigCard;
      d.a = in[rnd() % n];
      return d;
    }
    case Step::Play: {
      const int seat = g.turn;
      if (rnd() % 3 == 0) {
        for (Kind k : {Kind::Boat, Kind::Fish, Kind::Crab, Kind::Swimmer}) {
          const bool crabUseless = k == Kind::Crab && g.pileSize(0) == 0 && g.pileSize(1) == 0;
          const bool fishUseless = k == Kind::Fish && g.deckRemaining() == 0;
          if (g.playablePairs(seat, k) > 0 && !crabUseless && !fishUseless) {
            d.act = Decision::Act::LayDuo;
            d.kind = k;
            return d;
          }
        }
      }
      if (g.currentPhase() == Phase::Playing && g.mayEndRound(seat) && rnd() % 8 == 0) {
        d.act = rnd() % 2 ? Decision::Act::Stop : Decision::Act::LastChance;
        return d;
      }
      d.act = Decision::Act::EndTurn;
      return d;
    }
  }
  d.act = Decision::Act::EndTurn;
  return d;
}

// Plays one full match, brain seats given per seat (-1 = random player).
// Returns the winner. Checks the observation wall at every single state.
static int playMatch(const int skill0, const int skill1) {
  Game g;
  g.newGame(rnd() | 1u, static_cast<uint8_t>(rnd() % 2));
  uint32_t brainRng = rnd();

  int guard = 0;
  while (g.currentPhase() != Phase::GameOver && ++guard < 6000) {
    if (g.currentPhase() == Phase::RoundOver) {
      const uint8_t s0 = g.score[0], s1 = g.score[1];
      g.round = static_cast<uint8_t>(g.round + 1);
      g.deal(rnd() | 1u, static_cast<uint8_t>((g.roundStarter + 1) % kSeats));
      g.score[0] = s0;
      g.score[1] = s1;
      continue;
    }
    checkObservation(g, 0);
    checkObservation(g, 1);
    const int skill = g.turn == 0 ? skill0 : skill1;
    Decision d;
    if (skill < 0) {
      d = randomDecision(g);
    } else {
      d = decide(observe(g, g.turn), static_cast<Skill>(skill), brainRng);
    }
    // The brain must never hand the core something it refuses.
    const bool ok = applyDecision(g, d);
    if (skill >= 0) CHECK(ok);
    if (!ok) break;  // a random player can pick an empty-pile take; just stop
  }
  CHECK(guard < 6000);
  return g.matchWinner();
}

int main() {
  // The brain never makes an illegal move, and the wall holds, across whole
  // matches at both skills.
  int navigatorWins = 0, beachcomberWins = 0;
  const int kMatches = 400;
  for (int m = 0; m < kMatches; ++m) {
    const int w = playMatch(1, -1);  // Navigator vs random
    if (w == 0) ++navigatorWins;
  }
  for (int m = 0; m < kMatches; ++m) {
    const int w = playMatch(0, -1);  // Beachcomber vs random
    if (w == 0) ++beachcomberWins;
  }
  std::printf("  navigator %d/%d vs random, beachcomber %d/%d vs random\n", navigatorWins, kMatches, beachcomberWins,
              kMatches);
  // Not a vanity bar: a brain that has forgotten how to score, or bets every
  // round, loses to random often enough to fail this.
  CHECK(navigatorWins * 100 >= kMatches * 65);
  CHECK(beachcomberWins * 100 >= kMatches * 55);

  // The two skills are actually different players: Navigator must beat
  // Beachcomber over a long series, or the enum is a lie.
  int navOverBeach = 0;
  for (int m = 0; m < kMatches; ++m) {
    if (playMatch(1, 0) == 0) ++navOverBeach;
  }
  std::printf("  navigator %d/%d vs beachcomber\n", navOverBeach, kMatches);
  CHECK(navOverBeach * 100 >= kMatches * 52);

  // "0 failed" is not a literal standing in for a count: CHECK() calls
  // std::exit(1) on the first failure, so this line is only ever
  // reached with none. The wording is what check.sh counts sub-suites
  // by, and "N checks passed" made this suite report 0 of them.
  std::printf("seasalt brain: %d checks, 0 failed\n", checks);
  return 0;
}
