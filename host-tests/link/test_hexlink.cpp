// A real game of Hex between two devices, over a link that drops, duplicates
// and reorders.
//
// What Hex brings the layer that no earlier game did is a shared state carrying
// a DERIVED structure: the union-find that says who is connected to which edge
// travels with the board rather than being rebuilt on arrival. That is a
// deliberate choice (see HexCore.h) and it is the thing worth soaking, because
// the failure it would have is silent -- two devices agreeing about every stone
// and disagreeing about whether the game is over.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/hex/HexCore.h"
#include "../../src/apps_local/link/LinkPlay.h"
#include "FakeLink.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_hexlink.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;
using namespace linktest;
using Phase = PlayBase::Phase;

uint32_t rng = 20260913u;
uint32_t nextRandom() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

// What HexActivity does, with the rules real and the screens absent.
struct Device {
  Device(Medium& medium, const uint8_t last) : transport(medium, addressOf(last)), play(&transport) {}

  FakeTransport transport;
  Play<hex::Game> play;
  hex::Game game{};
  uint8_t seat = hex::kBlack;
  int moves = 0;
  bool refused = false;
  bool startedEmpty = false;
  bool sawTheEnd = false;
  int endingsCounted = 0;

  bool start() { return play.start(GameId::Hex, nullptr); }

  void pump(const uint32_t nowMs) {
    const Phase phase = play.update(nowMs);

    if (play.takeMatchStart()) {
      // BOTH sides deal. There is no randomness in an opening Hex position, so
      // reset() is identical on both devices and there is nothing to wait for.
      // A follower starting from a zeroed struct would hold a board whose
      // union-find made every node its own root -- a game nobody can ever win.
      seat = play.goesFirst() ? hex::kBlack : hex::kWhite;
      hex::reset(game);
      moves = 0;
      sawTheEnd = false;
    }

    hex::Game incoming{};
    if (play.takeOpponent(incoming)) {
      game = incoming;
      // The result is counted HERE, on the pass the match ended, and not at the
      // end of a move handler: multiplayer returns before reaching that, which
      // is how five games in this fork shipped counting zero matches.
      if (hex::over(game) && !sawTheEnd) {
        sawTheEnd = true;
        ++endingsCounted;
      }
    }

    if (phase != Phase::YourTurn) return;
    if (hex::over(game)) return;
    // A follower that never dealt would hold a ZEROED struct: no union-find at
    // all, and nobody to move. It plays perfectly well and never ends, which is
    // the shape of failure worth naming rather than waiting to observe.
    if (game.toMove != hex::kBlack && game.toMove != hex::kWhite) startedEmpty = true;

    int candidates[hex::kCells];
    int count = 0;
    for (int cell = 0; cell < hex::kCells; ++cell) {
      if (hex::legal(game, cell)) candidates[count++] = cell;
    }
    if (count == 0) return;
    if (!hex::play(game, candidates[nextRandom() % static_cast<uint32_t>(count)])) refused = true;
    ++moves;
    if (hex::over(game) && !sawTheEnd) {
      sawTheEnd = true;
      ++endingsCounted;
    }
    if (!play.play(game)) refused = true;
  }
};

void run(Medium& medium, std::vector<Device*>& devices, const uint32_t durationMs) {
  const uint32_t until = medium.nowMs + durationMs;
  while (medium.nowMs < until) {
    for (Device* device : devices) device->pump(medium.nowMs);
    medium.collect();
    medium.nowMs += 10;
  }
}

void testTheWholeGameFitsOnePacket() {
  // Play<> static_asserts this already; the margin is what makes sending the
  // whole state rather than the move affordable, so it is worth stating.
  CHECK(sizeof(hex::Game) <= kMaxPayloadBytes);
  CHECK(__is_trivially_copyable(hex::Game));

  hex::Game game;
  hex::reset(game);
  CHECK(hex::play(game, hex::cellAt(5, 5)));
  hex::Game copy;
  std::memcpy(&copy, &game, sizeof(hex::Game));
  for (int i = 0; i < hex::kCells; ++i) CHECK(copy.at(i) == game.at(i));
  CHECK(copy.toMove == game.toMove);
  CHECK(copy.lastMove == game.lastMove);
  // And the connectivity too, which is the half a rebuild-on-arrival design
  // would have had to get right twice.
  for (int i = 0; i < hex::kNodes; ++i) CHECK(copy.parent[i] == game.parent[i]);
}

void testAGameOfHexOverAHostileLink() {
  Medium medium;
  medium.lossPercent = 25;
  medium.duplicatePercent = 20;
  medium.maxJitterMs = 60;

  Device a(medium, 1);
  Device b(medium, 2);
  CHECK(a.start());
  CHECK(b.start());
  std::vector<Device*> devices = {&a, &b};

  run(medium, devices, 120000);

  CHECK(!a.refused);
  CHECK(!b.refused);
  CHECK(!a.startedEmpty);
  CHECK(!b.startedEmpty);
  // Opposite seats, decided once by the toss and agreed by both.
  CHECK(a.seat != b.seat);

  // Whole states travel, so the two boards cannot drift: a lost packet is a
  // stale frame the next one corrects, never a divergence.
  for (int i = 0; i < hex::kCells; ++i) CHECK(a.game.at(i) == b.game.at(i));
  CHECK(a.game.toMove == b.game.toMove);
  CHECK(a.game.moveNumber == b.game.moveNumber);

  // The game finished, both devices know it finished, and they agree who won.
  CHECK(hex::over(a.game));
  CHECK(hex::over(b.game));
  CHECK(a.game.winner == b.game.winner);

  // And the connection that ended it is the same one on both, which is the
  // assertion that the derived structure survived the wire rather than merely
  // the stones.
  uint8_t chainA[hex::kMaskBytes];
  uint8_t chainB[hex::kMaskBytes];
  CHECK(hex::winningChain(a.game, chainA));
  CHECK(hex::winningChain(b.game, chainB));
  for (int i = 0; i < hex::kMaskBytes; ++i) CHECK(chainA[i] == chainB[i]);

  // Counted exactly once on each device, on the pass it happened. A tally that
  // stays at zero is the failure this hook exists to prevent, and a tally that
  // counts twice is the other half of the same bug.
  CHECK(a.endingsCounted == 1);
  CHECK(b.endingsCounted == 1);
}

void testTheLoserIsToldOnAPerfectLinkToo() {
  // The same ending on a clean link, because the assertion is about the
  // handover and not about the radio: the winning stone crosses as an ordinary
  // move, and the device that did not play it has to reach `over` from the
  // packet alone.
  Medium medium;
  Device a(medium, 1);
  Device b(medium, 2);
  CHECK(a.start());
  CHECK(b.start());
  std::vector<Device*> devices = {&a, &b};

  run(medium, devices, 60000);

  CHECK(hex::over(a.game));
  CHECK(hex::over(b.game));
  CHECK(a.game.winner == b.game.winner);
  CHECK(a.endingsCounted == 1);
  CHECK(b.endingsCounted == 1);
  // One of them played the last stone and the other took delivery of it, so
  // both arms of the count above were exercised rather than only the mover's.
  CHECK(a.moves > 0 && b.moves > 0);
}

}  // namespace

int main() {
  testTheWholeGameFitsOnePacket();
  testAGameOfHexOverAHostileLink();
  testTheLoserIsToldOnAPerfectLinkToo();
  std::printf("test_hexlink: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
