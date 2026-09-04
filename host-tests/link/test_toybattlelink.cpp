// A real game of Toy Battle between two devices over a hostile link.
//
// Three things about this game stress the layer, and all three are asserted
// here rather than argued about:
//
//   * The opening is dealt, not fixed. `Game` carries the seed, the terrain and
//     whether special bases are on, so the leader deals and everything two
//     devices must agree on rides along in the first state packet. The follower
//     must NOT deal: a randomised opening would be a different game on each
//     device, and a zeroed one has no legal move at all.
//   * A zeroed `Game` reads as finished. `matchGameOver()` is polled every pass
//     and latches the rematch screen the moment it is true, permanently -- so a
//     follower that has not been dealt to yet must not answer yes.
//     testAFollowerThatHasNotBeenDealtToIsNotFinished pins it.
//   * A turn can be several placements. Cap'n places a second troop, and the
//     whole chain is one `Move` applied to one state, so the wire never sees a
//     half-finished turn.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/link/LinkPlay.h"
#include "../../src/apps_local/toybattle/ToyBattleBrain.h"
#include "../../src/apps_local/toybattle/ToyBattleCore.h"
#include "FakeLink.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_toybattlelink.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;
using namespace linktest;
using Phase = PlayBase::Phase;
namespace tb = toybattle;

// The same moves ToyBattleActivity makes, with the rules and the wire real.
struct Device {
  Device(Medium& medium, const uint8_t last, const uint32_t seedValue, const int terrainIndex)
      : transport(medium, addressOf(last)), play(&transport), seed(seedValue), terrain(terrainIndex) {}

  FakeTransport transport;
  Play<tb::Game> play;
  tb::Game game;
  uint8_t seat = 0;
  // "Nothing has been dealt yet", which is a different thing from "the game is
  // over". Without it a follower answers yes to matchGameOver() on its very
  // first pass, because a zeroed board has no legal move.
  bool dealt = false;
  uint32_t seed = 1;
  int terrain = 0;
  int moves = 0;
  bool refused = false;
  bool rejectedAPacket = false;

  bool start() { return play.start(GameId::ToyBattle, nullptr); }

  bool matchGameOver() const { return dealt && game.currentPhase() != tb::Phase::Playing; }

  bool canAct() const {
    const bool mine = dealt && game.currentPhase() == tb::Phase::Playing && game.turn == seat;
    return mine && play.phase() == Phase::YourTurn;
  }

  void pump(const uint32_t nowMs, const int moveLimit) {
    play.update(nowMs);

    if (play.takeMatchStart()) {
      seat = play.goesFirst() ? 0 : 1;
      moves = 0;
      if (play.goesFirst()) {
        // The leader deals and keeps the turn. Its first placement carries the
        // opening -- map, special bases, both reserves -- to the other device.
        game.newGame(seed, terrain, 0, true);
        dealt = true;
      } else {
        // The follower waits. Not a fresh deal, not a zeroed board it treats as
        // playable: nothing until the opening arrives.
        game = tb::Game{};
        dealt = false;
      }
    }

    tb::Game arriving{};
    if (play.takeOpponent(arriving)) {
      // The layer checks the payload length and copies. Everything about the
      // contents is the game's, and a corrupt packet arrives here as a `turn`
      // of 200 indexing a two-seat array.
      if (arriving.terrain >= tb::kTerrainCount || arriving.turn >= tb::kSeats || !arriving.isWellFormed()) {
        rejectedAPacket = true;
      } else {
        game = arriving;
        dealt = true;
      }
    }

    if (!canAct()) return;
    if (moves >= moveLimit) return;

    const tb::Observation obs = tb::observe(game, seat);
    const tb::Move move = tb::chooseMove(obs, tb::Skill::Sergeant);
    if (!game.apply(move)) {
      refused = true;
      return;
    }
    if (!play.play(game)) refused = true;
    ++moves;
  }
};

void run(Medium& medium, std::vector<Device*>& devices, const uint32_t durationMs, const int moveLimit) {
  const uint32_t until = medium.nowMs + durationMs;
  while (medium.nowMs < until) {
    for (Device* device : devices) device->pump(medium.nowMs, moveLimit);
    medium.collect();
    medium.nowMs += 10;
  }
}

void testTheWholeGameFitsOnePacket() {
  // 148 bytes against a 192-byte payload, and it is the same struct the save
  // file holds. Nothing about a Toy Battle position is described twice.
  CHECK(sizeof(tb::Game) <= kMaxPayloadBytes);
  CHECK(std::is_trivially_copyable<tb::Game>::value);
  std::printf("  a whole position is %d bytes of a %d byte payload\n", static_cast<int>(sizeof(tb::Game)),
              static_cast<int>(kMaxPayloadBytes));
}

void testAFollowerThatHasNotBeenDealtToIsNotFinished() {
  // The trap that cost another game in this fork a whole match: a board nobody
  // has dealt has no legal move, so it reads as over, so the layer latches the
  // rematch screen and never lets go.
  //
  // This engine does not fall into it by that route, and what it does instead
  // is worse. A zeroed `Game` reports Playing, has no legal placement -- and
  // believes it CAN DRAW, because a reserve is whatever has not been seen yet
  // and nothing has. So an undealt follower is not idle, it is playable: given
  // a tap it would draw from an imaginary reserve and send that board as a
  // move. `dealt` is load-bearing here, not belt and braces.
  tb::Game zeroed{};
  tb::Step steps[8];
  CHECK(zeroed.currentPhase() == tb::Phase::Playing);
  CHECK(zeroed.legalPlacements(0, steps, 8) == 0);
  CHECK(zeroed.canDraw(0));

  Medium medium;
  Device lonely(medium, 1, 0xABCDu, 0);
  CHECK(lonely.start());
  std::vector<Device*> devices{&lonely};
  run(medium, devices, 5000, 4);

  // Nobody answered, so nothing was dealt -- and "not dealt" must not read as
  // "finished" even though the board it is holding would say so.
  CHECK(!lonely.dealt);
  CHECK(!lonely.matchGameOver());
  CHECK(lonely.moves == 0);
}

void testAGameOfToyBattleOverAHostileLink() {
  Medium medium;
  medium.lossPercent = 20;
  medium.duplicatePercent = 10;
  medium.maxJitterMs = 60;

  Device a(medium, 1, 0xC0FFEEu, 0);
  Device b(medium, 2, 0xBEEFu, 0);
  std::vector<Device*> devices{&a, &b};
  CHECK(a.start());
  CHECK(b.start());

  run(medium, devices, 240000, 400);

  CHECK(!a.refused);
  CHECK(!b.refused);
  CHECK(!a.rejectedAPacket);
  CHECK(!b.rejectedAPacket);
  // Both devices dealt with, and holding the same position byte for byte.
  CHECK(a.dealt);
  CHECK(b.dealt);
  CHECK(std::memcmp(&a.game, &b.game, sizeof(tb::Game)) == 0);
  // Agreeing by never starting would satisfy everything above.
  CHECK(a.moves + b.moves >= 10);
  // Exactly one of them dealt. Both dealing is the follower-deals bug, and it
  // shows up as two different openings that then agree only by luck.
  CHECK(a.seat != b.seat);
  std::printf("  hostile link: %d + %d moves, positions %s\n", a.moves, b.moves,
              std::memcmp(&a.game, &b.game, sizeof(tb::Game)) == 0 ? "identical" : "DIVERGED");
}

void testTheSettingsTravelWithTheDeal() {
  // Special bases and the terrain are not app settings that each device applies
  // for itself -- they live in the state, so whichever device deals decides and
  // the other adopts. A device that kept its own would misplay every special
  // base on the board.
  Medium medium;
  Device a(medium, 1, 0x1234u, static_cast<int>(tb::TerrainId::ProvingGround));
  Device b(medium, 2, 0x5678u, static_cast<int>(tb::TerrainId::CastleField));
  std::vector<Device*> devices{&a, &b};
  CHECK(a.start());
  CHECK(b.start());

  run(medium, devices, 60000, 6);

  CHECK(a.dealt && b.dealt);
  CHECK(a.game.terrain == b.game.terrain);
  CHECK(a.game.seed == b.game.seed);
  CHECK(a.game.specialBases == b.game.specialBases);
  // And it is the leader's map that survived, not a merge of the two.
  const Device& leader = a.seat == 0 ? a : b;
  CHECK(a.game.terrain == leader.terrain);
  std::printf("  the deal carried map %d to both devices\n", static_cast<int>(a.game.terrain));
}

void testACorruptPositionIsRefused() {
  // The layer validates the payload LENGTH and copies. `isWellFormed` is the
  // only thing between a corrupt packet and a live board, and it has to be
  // called into a scratch rather than over the position being played.
  tb::Game good;
  good.newGame(99u, 0, 0, true);
  CHECK(good.isWellFormed());

  tb::Game wrongTurn = good;
  wrongTurn.turn = 200;
  CHECK(wrongTurn.turn >= tb::kSeats);

  tb::Game wrongTerrain = good;
  wrongTerrain.terrain = 90;
  CHECK(wrongTerrain.terrain >= tb::kTerrainCount);

  // Every byte of the struct flipped one bit at a time: whatever survives
  // isWellFormed must at least be a position the rules can still run, because
  // the alternative is a board that crashes three moves later.
  int rejected = 0, accepted = 0;
  for (size_t byte = 0; byte < sizeof(tb::Game); ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      tb::Game corrupt = good;
      reinterpret_cast<uint8_t*>(&corrupt)[byte] ^= static_cast<uint8_t>(1u << bit);
      const bool ok = corrupt.terrain < tb::kTerrainCount && corrupt.turn < tb::kSeats && corrupt.isWellFormed();
      if (!ok) {
        ++rejected;
        continue;
      }
      ++accepted;
      // If it was accepted it has to be playable, not merely well-shaped.
      tb::Step steps[64];
      const int n = corrupt.legalPlacements(corrupt.turn, steps, 64);
      CHECK(n >= 0);
    }
  }
  std::printf("  corruption: %d of %d single-bit flips refused\n", rejected, rejected + accepted);
  CHECK(rejected > 0);
}

void testManyGames() {
  constexpr int kGames = 12;
  int agreed = 0, finished = 0, fewest = 1 << 20;
  for (int i = 0; i < kGames; ++i) {
    Medium medium;
    medium.lossPercent = 10;
    medium.duplicatePercent = 5;
    medium.maxJitterMs = 40;
    Device a(medium, 1, static_cast<uint32_t>(0x1000 + i * 7919), 0);
    Device b(medium, 2, static_cast<uint32_t>(0x2000 + i * 6271), 0);
    std::vector<Device*> devices{&a, &b};
    a.start();
    b.start();
    run(medium, devices, 400000, 400);

    const bool same = std::memcmp(&a.game, &b.game, sizeof(tb::Game)) == 0;
    if (same && !a.refused && !b.refused && !a.rejectedAPacket && !b.rejectedAPacket) ++agreed;
    if (a.matchGameOver() && a.game.winner == b.game.winner) ++finished;
    const int total = a.moves + b.moves;
    if (total < fewest) fewest = total;
  }
  std::printf("  soak: %d/%d games agreed, %d played to a result, fewest %d moves\n", agreed, kGames, finished, fewest);
  CHECK(agreed == kGames);
  // Agreeing by never starting would pass everything above.
  CHECK(fewest >= 10);
}

}  // namespace

int main() {
  testTheWholeGameFitsOnePacket();
  testAFollowerThatHasNotBeenDealtToIsNotFinished();
  testAGameOfToyBattleOverAHostileLink();
  testTheSettingsTravelWithTheDeal();
  testACorruptPositionIsRefused();
  testManyGames();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
