// A real game of battleship, played between two devices over a hostile link.
//
// This is the second game on the layer, so it is also the test of whether the
// layer was a layer. Two things about battleship stress it in ways chess never
// did, and both are asserted here rather than argued about:
//
//   * Hidden information. The shared state carries BOTH fleets, and a device
//     keeps the one it is arranging OUTSIDE that state until it hands it over.
//     Adopting an opponent's state is a whole-struct copy, so a fleet parked in
//     the shared state before it was sent would be silently wiped by their
//     first packet -- and the boards would still agree, which is what makes it
//     the nastiest bug available here. testYourFleetSurvivesTheirFirstPacket
//     pins it.
//   * A setup phase that the box plays simultaneously and the wire can only
//     alternate. Whoever goes first sends their fleet as their first move, the
//     other answers with theirs, and only then does anyone fire. No new
//     primitive; the test that matters is the one where a player dawdles.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/battleship/BattleshipCore.h"
#include "../../src/apps_local/link/LinkPlay.h"
#include "FakeLink.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_battleshiplink.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;
using namespace linktest;
using Phase = PlayBase::Phase;

// The same moves BattleshipActivity makes, with the rules and the wire real.
struct Device {
  Device(Medium& medium, const uint8_t last, const uint32_t seedValue)
      : transport(medium, addressOf(last)), play(&transport), seed(seedValue) {}

  FakeTransport transport;
  Play<bship::Game> play;
  bship::Game game;
  // The fleet while it is still being arranged. Outside `game` on purpose; see
  // the note at the top of the file.
  bship::Fleet myFleet;
  uint8_t mySide = 0;
  bool committed = false;
  // How long this player spends arranging, in milliseconds. A real one takes
  // seconds; the point is that the other device waits rather than starting
  // without them.
  uint32_t arrangeUntilMs = 0;
  uint32_t matchedAtMs = 0;
  uint32_t seed = 1;
  int shots = 0;
  bool refused = false;

  bool start() { return play.start(GameId::Battleship, nullptr); }

  void sendFleetIfReady() {
    if (!committed) return;
    if (game.side[mySide].placed != 0) return;
    if (play.phase() != Phase::YourTurn) return;
    if (!bship::place(game, mySide, myFleet)) {
      refused = true;
      return;
    }
    if (!play.play(game)) refused = true;
  }

  void pump(const uint32_t nowMs, const int shotLimit) {
    play.update(nowMs);

    if (play.takeMatchStart()) {
      // Side 0 is whoever holds the first turn: the rules only let a side place
      // its fleet on its own turn.
      mySide = play.goesFirst() ? 0 : 1;
      bship::reset(game);
      bship::randomFleet(myFleet, seed);
      committed = false;
      matchedAtMs = nowMs;
      shots = 0;
    }

    bship::Game incoming;
    if (play.takeOpponent(incoming)) {
      game = incoming;
      // No check here, deliberately, and the reason is worth writing down: the
      // only packet that can carry a fleet is the one in which its owner places
      // it, so there is no earlier packet for a leak to ride on. A guard here
      // would be a guard that cannot fire, which this project counts as worse
      // than no guard at all.
      //
      // What IS true and is not a secret: the second player's device holds the
      // first player's fleet while the second is still arranging, because the
      // packet arrives when it arrives. Nothing draws it, and fleet placement
      // does not interact, so it buys nothing -- but it is the honest shape of
      // "both fleets travel", not something the tests pretend away.
    }

    if (!committed && matchedAtMs != 0 && nowMs >= matchedAtMs + arrangeUntilMs) committed = true;
    sendFleetIfReady();

    if (play.phase() != Phase::YourTurn) return;
    if (!bship::bothPlaced(game) || bship::over(game)) return;
    if (shots >= shotLimit) return;

    const int cell = bship::chooseShot(game.side[mySide ^ 1], seed);
    if (cell < 0) return;
    if (!bship::fire(game, cell)) {
      refused = true;
      return;
    }
    if (!play.play(game)) refused = true;
    shots++;
  }
};

void run(Medium& medium, std::vector<Device*>& devices, const uint32_t durationMs, const int shotLimit) {
  const uint32_t until = medium.nowMs + durationMs;
  while (medium.nowMs < until) {
    for (Device* device : devices) device->pump(medium.nowMs, shotLimit);
    medium.collect();
    medium.nowMs += 10;
  }
}

void testTheWholeGameFitsOnePacket() {
  // Chess sends 100 bytes of FEN. A whole battleship -- two fleets, two shot
  // maps, the turn -- is half that, which is why sending the state rather than
  // the move was never in question for this game.
  CHECK(sizeof(bship::Game) <= kMaxPayloadBytes);
  std::printf("  wire: sizeof(bship::Game) = %d of %d bytes\n", static_cast<int>(sizeof(bship::Game)),
              static_cast<int>(kMaxPayloadBytes));
}

void testAGameOfBattleshipOverAHostileLink() {
  Medium medium;
  medium.lossPercent = 25;
  medium.duplicatePercent = 20;
  medium.maxJitterMs = 60;
  Device a(medium, 0x21, 7777);
  Device b(medium, 0x32, 424242);
  std::vector<Device*> devices = {&a, &b};

  CHECK(a.start());
  CHECK(b.start());
  run(medium, devices, 60000, 60);

  CHECK(a.play.phase() != Phase::Searching);
  // Exactly one of them is side 0, and neither had to be told.
  CHECK(a.mySide != b.mySide);

  // Setting up completed, both ways round.
  CHECK(bship::bothPlaced(a.game));
  CHECK(bship::bothPlaced(b.game));

  // The two boards are the same board, byte for byte. This is the assertion the
  // whole design exists to make cheap: no move log, no replay, no
  // reconciliation, and no way for a lost packet to leave a difference behind.
  CHECK(std::memcmp(&a.game, &b.game, sizeof(bship::Game)) == 0);

  // A real game happened and the shots alternated.
  CHECK(a.shots + b.shots >= 20);
  CHECK(a.shots > 0);
  CHECK(b.shots > 0);
  const int gap = a.shots > b.shots ? a.shots - b.shots : b.shots - a.shots;
  CHECK(gap <= 1);

  CHECK(!a.refused);
  CHECK(!b.refused);
}

// The bug this design could produce and no divergence check would catch: your
// own fleet, wiped by the opponent's first packet, on two devices that then
// agree perfectly about a game where your ships were never there.
void testYourFleetSurvivesTheirFirstPacket() {
  Medium medium;
  medium.lossPercent = 15;
  medium.duplicatePercent = 10;
  medium.maxJitterMs = 40;
  Device a(medium, 0x41, 31337);
  Device b(medium, 0x52, 90210);
  std::vector<Device*> devices = {&a, &b};
  a.start();
  b.start();
  run(medium, devices, 60000, 40);

  CHECK(bship::bothPlaced(a.game));
  // The fleet in the shared state is the fleet each device arranged, not a
  // zeroed one that arrived from the other side.
  CHECK(std::memcmp(&a.game.side[a.mySide].fleet, &a.myFleet, sizeof(bship::Fleet)) == 0);
  CHECK(std::memcmp(&b.game.side[b.mySide].fleet, &b.myFleet, sizeof(bship::Fleet)) == 0);
  // And each device's copy of the opponent's fleet is the one that opponent
  // actually arranged.
  CHECK(std::memcmp(&a.game.side[b.mySide].fleet, &b.myFleet, sizeof(bship::Fleet)) == 0);
  CHECK(std::memcmp(&b.game.side[a.mySide].fleet, &a.myFleet, sizeof(bship::Fleet)) == 0);
  // Seventeen cells of fleet on each side, wherever they came from.
  for (int side = 0; side < 2; ++side) {
    for (int i = 0; i < bship::kShipCount; ++i) {
      CHECK(bship::canPlace(a.game.side[side].fleet, i, a.game.side[side].fleet.ships[i]));
    }
  }
}

// One player dawdles over their fleet. The other must wait, and the match must
// still start -- this is the phase the box plays simultaneously, and the wire
// can only alternate.
void testTheOtherPlayerWaitsWhileYouArrange() {
  for (int who = 0; who < 2; ++who) {
    Medium medium;
    medium.lossPercent = 10;
    medium.maxJitterMs = 30;
    Device a(medium, 0x61, 5150);
    Device b(medium, 0x72, 8675309);
    // Twelve seconds of arranging, which is a slow but entirely ordinary human.
    (who == 0 ? a : b).arrangeUntilMs = 12000;
    std::vector<Device*> devices = {&a, &b};
    a.start();
    b.start();

    // Long enough to matter but short of the arranging time: nobody has fired.
    run(medium, devices, 8000, 40);
    CHECK(!bship::bothPlaced(a.game));
    CHECK(a.shots == 0);
    CHECK(b.shots == 0);
    // The wait does not end the match. A player thinking is not a player gone.
    CHECK(a.play.phase() != Phase::Over);
    CHECK(b.play.phase() != Phase::Over);

    run(medium, devices, 40000, 40);
    CHECK(bship::bothPlaced(a.game));
    CHECK(std::memcmp(&a.game, &b.game, sizeof(bship::Game)) == 0);
    CHECK(a.shots + b.shots >= 20);
    CHECK(!a.refused);
    CHECK(!b.refused);
  }
}

void testManyGames() {
  constexpr int kGames = 30;
  int agreed = 0;
  int finished = 0;
  int fewest = 1000;
  for (int game = 0; game < kGames; ++game) {
    Medium medium;
    medium.random = static_cast<uint32_t>(game * 2654435761u) | 1u;
    medium.nowMs = static_cast<uint32_t>(game) * 811u;
    medium.lossPercent = game % 28;
    medium.duplicatePercent = game % 15;
    medium.maxJitterMs = game % 45;

    Device a(medium, static_cast<uint8_t>(0x50 + (game % 6)), static_cast<uint32_t>(game) * 7919u + 1u);
    Device b(medium, static_cast<uint8_t>(0xC0 + (game % 4)), static_cast<uint32_t>(game) * 104729u + 3u);
    // Somebody is always still arranging when the other is ready.
    a.arrangeUntilMs = static_cast<uint32_t>(game % 5) * 900u;
    b.arrangeUntilMs = static_cast<uint32_t>(game % 3) * 1300u;
    std::vector<Device*> devices = {&a, &b};
    a.start();
    b.start();
    run(medium, devices, 90000, 100);

    const bool same = std::memcmp(&a.game, &b.game, sizeof(bship::Game)) == 0;
    if (same && !a.refused && !b.refused) agreed++;
    // Played out to a winner, which both devices name the same way.
    if (bship::over(a.game) && bship::winner(a.game) == bship::winner(b.game)) finished++;
    const int total = a.shots + b.shots;
    if (total < fewest) fewest = total;
  }
  std::printf("  soak: %d/%d games agreed, %d played to a winner, fewest %d shots\n", agreed, kGames, finished, fewest);
  CHECK(agreed == kGames);
  // Agreeing by never starting would pass everything above.
  CHECK(fewest >= 20);
  CHECK(finished >= kGames - 2);
}

}  // namespace

int main() {
  testTheWholeGameFitsOnePacket();
  testAGameOfBattleshipOverAHostileLink();
  testYourFleetSurvivesTheirFirstPacket();
  testTheOtherPlayerWaitsWhileYouArrange();
  testManyGames();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
