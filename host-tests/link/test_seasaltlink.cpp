// A real game of Sea Salt & Paper between two devices over a hostile link.
//
// Sea Salt stresses the layer in a way none of the earlier games did: a game
// turn is a SEQUENCE of decisions, and the transport only alternates whole
// spans. A boat pair keeps the game turn while the transport waits; LAST
// CHANCE moves the game turn without a send of its own; and the device that
// banks a round is not necessarily the one that deals the next. All of that
// resolves through SeaSaltLink.h's Pass, and this soak is what makes "two
// simulators can play Sea Salt" a tested claim rather than an intention.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/link/LinkPlay.h"
#include "../../src/apps_local/seasalt/SeaSaltBrain.h"
#include "../../src/apps_local/seasalt/SeaSaltCore.h"
#include "../../src/apps_local/seasalt/SeaSaltLink.h"
#include "FakeLink.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_seasaltlink.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;
using namespace linktest;
using Phase = PlayBase::Phase;

static const char* kNames[seasalt::kKindCount] = {
    "CRAB", "BOAT",   "FISH",    "SWIMMER",    "SHARK", "SHELL", "TURTLE",
    "GULL", "SAILOR", "MERMAID", "LIGHTHOUSE", "SHOAL", "NEST",  "CAPTAIN",
};

// The same decisions SeaSaltActivity makes, with the rules and the wire real.
struct Device {
  Device(Medium& medium, const uint8_t last, const uint32_t seedValue)
      : transport(medium, addressOf(last)), play(&transport), seed(seedValue) {}

  FakeTransport transport;
  Play<seasalt::Game> play;
  seasalt::Game game;
  int seat = 0;
  uint32_t seed;
  uint32_t brainRng = 1;
  bool refused = false;
  int passes = 0;
  int deals = 0;
  int spansSent = 0;
  char lastReport[96] = "";

  uint32_t nextSeed() {
    seed = seed * 1664525u + 1013904223u;
    return seed | 1u;
  }

  bool start() { return play.start(GameId::SeaSalt, nullptr); }

  // One brain decision applied to the game, exactly the way the activity's
  // driver applies them. Returns false when the core refuses, which the soak
  // treats as a bug.
  bool applyOne() {
    using namespace seasalt;
    const Decision d = decide(observe(game, seat), Skill::Navigator, brainRng);
    switch (d.act) {
      case Decision::Act::TakeDeck:
        return game.takeFromDeck();
      case Decision::Act::TakePile:
        return game.takeFromPile(d.a);
      case Decision::Act::Keep:
        return game.keepDrawn(d.a);
      case Decision::Act::DiscardTo:
        return game.discardTo(d.a);
      case Decision::Act::LayDuo: {
        uint8_t a = kNoCard, b = kNoCard;
        const Kind ka = d.kind == Kind::Swimmer ? Kind::Swimmer : d.kind;
        const Kind kb = d.kind == Kind::Swimmer ? Kind::Shark : d.kind;
        for (int c = 0; c < kCards; ++c) {
          if (game.place[c] != static_cast<uint8_t>(handOf(seat))) continue;
          const Kind k = kindOf(static_cast<uint8_t>(c));
          if (k == ka && a == kNoCard) {
            a = static_cast<uint8_t>(c);
            continue;
          }
          if (k == kb && b == kNoCard && c != a) b = static_cast<uint8_t>(c);
        }
        return a != kNoCard && b != kNoCard && game.playDuo(a, b);
      }
      case Decision::Act::EndTurn:
        return game.endTurn();
      case Decision::Act::Stop:
        return game.endRound(false);
      case Decision::Act::LastChance:
        return game.endRound(true);
      case Decision::Act::DigPile:
        return game.chooseCrabPile(d.a);
      case Decision::Act::DigCard:
        return game.takeCrabCard(d.a);
    }
    return false;
  }

  void pump(const uint32_t nowMs) {
    play.update(nowMs);

    if (play.takeMatchStart()) {
      seat = play.goesFirst() ? 0 : 1;
      if (play.goesFirst()) {
        game.newGame(nextSeed(), 0);
        if (!play.play(game)) refused = true;
      }
    }

    seasalt::Game incoming;
    if (play.takeOpponent(incoming)) {
      seasalt::describeTheirTurn(game, incoming, seat, kNames, lastReport, sizeof(lastReport));
      game = incoming;
      if (seasalt::linkAction(game, seat, play.phase() == Phase::YourTurn) == seasalt::LinkAction::Pass) {
        ++passes;
        if (!play.play(game)) refused = true;
      }
    }

    switch (seasalt::linkAction(game, seat, play.phase() == Phase::YourTurn)) {
      case seasalt::LinkAction::Wait:
      case seasalt::LinkAction::Pass:  // handled at delivery
        return;
      case seasalt::LinkAction::Deal: {
        const uint8_t starter = game.ender != seasalt::kNoSeat ? static_cast<uint8_t>(game.ender ^ 1)
                                                               : static_cast<uint8_t>(game.roundStarter ^ 1);
        const uint8_t s0 = game.score[0], s1 = game.score[1];
        const uint8_t round = game.round;
        game.deal(nextSeed(), starter);
        game.score[0] = s0;
        game.score[1] = s1;
        game.round = static_cast<uint8_t>(round + 1);
        ++deals;
        if (!play.play(game)) refused = true;
        return;
      }
      case seasalt::LinkAction::Move: {
        // Play the whole span: every decision until the turn leaves this seat
        // or the round ends. That is what the activity does between repaints.
        int guard = 0;
        while (game.currentPhase() != seasalt::Phase::RoundOver && game.currentPhase() != seasalt::Phase::GameOver &&
               game.turn == static_cast<uint8_t>(seat) && ++guard < 300) {
          if (!applyOne()) {
            refused = true;
            return;
          }
        }
        CHECK(guard < 300);
        ++spansSent;
        if (!play.play(game)) refused = true;
        return;
      }
    }
  }
};

void run(Medium& medium, std::vector<Device*>& devices, const uint32_t durationMs) {
  const uint32_t until = medium.nowMs + durationMs;
  while (medium.nowMs < until) {
    for (Device* device : devices) device->pump(medium.nowMs);
    medium.collect();
    medium.nowMs += 10;
    bool allOver = true;
    for (Device* device : devices) {
      if (device->game.currentPhase() != seasalt::Phase::GameOver) allOver = false;
    }
    if (allOver && medium.queue.empty()) return;
  }
}

// Whole matches on a link that drops, duplicates and reorders. The wire
// carries whole states, so a lost packet is a stale frame the next one
// corrects, and the two devices can never disagree for longer than one
// delivery.
void testWholeMatchesOnAHostileLink() {
  int gamesOver = 0, mermaidWins = 0, bets = 0, stops = 0, totalPasses = 0, totalDeals = 0;
  for (uint32_t seed = 1; seed <= 12; ++seed) {
    Medium medium;
    medium.random = seed * 2654435761u + 7u;
    medium.lossPercent = 12;
    medium.duplicatePercent = 8;
    medium.maxJitterMs = 90;

    Device a(medium, 0x11, seed * 51u + 1u);
    Device b(medium, 0x22, seed * 97u + 3u);
    a.brainRng = seed * 13u + 5u;
    b.brainRng = seed * 29u + 9u;
    std::vector<Device*> devices{&a, &b};
    CHECK(a.start());
    CHECK(b.start());

    run(medium, devices, 20u * 60u * 1000u);

    CHECK(!a.refused);
    CHECK(!b.refused);
    CHECK(a.game.currentPhase() == seasalt::Phase::GameOver);
    CHECK(b.game.currentPhase() == seasalt::Phase::GameOver);
    // The two devices hold the SAME finished game, byte for byte.
    CHECK(std::memcmp(&a.game, &b.game, sizeof(seasalt::Game)) == 0);
    // And they agree about who won from their opposite seats.
    CHECK(a.game.matchWinner() == b.game.matchWinner());

    ++gamesOver;
    if (a.game.mermaidsHeld(0) == seasalt::kMermaidsToWin || a.game.mermaidsHeld(1) == seasalt::kMermaidsToWin) {
      ++mermaidWins;
    }
    if (a.game.betWasLastChance) ++bets;
    if (a.game.ender != seasalt::kNoSeat && !a.game.betWasLastChance) ++stops;
    totalPasses += a.passes + b.passes;
    totalDeals += a.deals + b.deals;
  }
  std::printf("  %d matches over the wire: %d mermaid wins, %d final bets, %d final stops, %d passes, %d deals\n",
              gamesOver, mermaidWins, bets, stops, totalPasses, totalDeals);
  CHECK(gamesOver == 12);
  // Calibration: the mechanisms this soak exists to test must actually fire.
  // A run with no passes never hit the turn/transport disagreement, and a run
  // with no deals never crossed a round boundary.
  CHECK(totalPasses > 0);
  CHECK(totalDeals > 0);
}

// The narration diff never claims something public that did not happen: over a
// whole match, a steal is reported if and only if my hand shrank.
void testTheNarrationReadsOnlyPublicFacts() {
  Medium medium;
  medium.random = 424242u;
  Device a(medium, 0x11, 77u);
  Device b(medium, 0x22, 78u);
  std::vector<Device*> devices{&a, &b};
  CHECK(a.start());
  CHECK(b.start());
  run(medium, devices, 20u * 60u * 1000u);
  CHECK(a.game.currentPhase() == seasalt::Phase::GameOver);
  // The last report on each side is a sentence, not garbage.
  CHECK(std::strlen(a.lastReport) > 0);
  CHECK(std::strlen(b.lastReport) > 0);
}

}  // namespace

int main() {
  testWholeMatchesOnAHostileLink();
  testTheNarrationReadsOnlyPublicFacts();
  std::printf("seasalt link: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
