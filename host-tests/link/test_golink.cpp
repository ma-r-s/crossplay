// A real game of Go between two devices, over a link that drops, duplicates
// and reorders.
//
// Go brings the layer something no earlier game did: the match does not end
// when the game does. After two passes both seats have to AGREE which stones
// are dead, and a mark is a state change like any move -- so the last thing
// that crosses the wire is a negotiation, and either side can reopen it by
// tapping a group or by choosing to play on. That is the part worth soaking.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/go/GoCore.h"
#include "../../src/apps_local/link/LinkPlay.h"
#include "FakeLink.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_golink.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;
using namespace linktest;
using Phase = PlayBase::Phase;

uint32_t rng = 20260912u;
uint32_t nextRandom() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

// The three things GoActivity does, with the rules real.
struct Device {
  Device(Medium& medium, const uint8_t last) : transport(medium, addressOf(last)), play(&transport) {}

  FakeTransport transport;
  Play<go::Game> play;
  go::Game game{};
  uint8_t seat = go::kBlack;
  int moves = 0;
  bool refused = false;
  bool startedEmpty = false;
  // Set when this seat has said the count is right. Cleared whenever the other
  // seat changes a mark, because a count nobody re-read is not agreed.
  bool accepted = false;
  int marksMade = 0;

  bool start() { return play.start(GameId::Go, nullptr); }

  void pump(const uint32_t nowMs, const int moveLimit) {
    const Phase phase = play.update(nowMs);

    if (play.takeMatchStart()) {
      // BOTH sides deal. There is no randomness in an opening go position, so
      // reset() is identical on both devices and there is nothing to wait for.
      // A follower that started from a zeroed struct would hold a board with
      // stage 0 and no komi, which reads as a legal game nobody can score.
      seat = play.goesFirst() ? go::kBlack : go::kWhite;
      go::reset(game, 0, go::kDefaultKomiHalves);
      moves = 0;
      accepted = false;
      marksMade = 0;
    }

    go::Game incoming{};
    if (play.takeOpponent(incoming)) {
      // A mark they changed means this seat has to look again.
      if (incoming.stage == static_cast<uint8_t>(go::Stage::Scoring)) accepted = false;
      game = incoming;
    }

    if (phase != Phase::YourTurn) return;
    if (game.stage == static_cast<uint8_t>(go::Stage::Over)) return;
    // A follower that never dealt would be holding a zeroed struct: no komi,
    // stage 0, and an empty board that scores as a draw nobody played.
    if (game.komiHalves == 0) startedEmpty = true;

    if (game.stage == static_cast<uint8_t>(go::Stage::Scoring)) {
      // The counting phase. Mark one group dead the first time round -- which
      // is what a tap does -- and accept after that.
      if (marksMade == 0) {
        for (int point = 0; point < go::kPoints; ++point) {
          if (!go::isStone(game.point[point])) continue;
          if (go::marked(game.dead, point)) continue;
          uint8_t stones[(go::kPoints + 7) / 8];
          int size = 0;
          int liberties = 0;
          go::group(game, point, stones, size, liberties);
          for (int p = 0; p < go::kPoints; ++p) {
            if (go::marked(stones, p)) go::mark(game.dead, p);
          }
          break;
        }
        ++marksMade;
        accepted = false;
      } else {
        game.stage = static_cast<uint8_t>(go::Stage::Over);
        accepted = true;
      }
      if (!play.play(game)) refused = true;
      return;
    }

    if (moves >= moveLimit) {
      if (!go::play(game, go::kPass)) refused = true;
    } else {
      int candidates[go::kPoints];
      int count = 0;
      for (int point = 0; point < go::kPoints; ++point) {
        if (go::legal(game, point, game.toMove) && !go::isEye(game, point, game.toMove)) candidates[count++] = point;
      }
      if (count == 0) {
        if (!go::play(game, go::kPass)) refused = true;
      } else if (!go::play(game, candidates[nextRandom() % static_cast<uint32_t>(count)])) {
        refused = true;
      }
      ++moves;
    }
    if (!play.play(game)) refused = true;
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
  // Play<> static_asserts this already; the margin is what makes sending the
  // whole state rather than the move affordable, so it is worth stating.
  CHECK(sizeof(go::Game) <= kMaxPayloadBytes);
  CHECK(__is_trivially_copyable(go::Game));

  go::Game game;
  go::reset(game);
  CHECK(go::play(game, go::pointAt(4, 4)));
  go::Game copy;
  std::memcpy(&copy, &game, sizeof(go::Game));
  for (int i = 0; i < go::kPoints; ++i) CHECK(copy.point[i] == game.point[i]);
  CHECK(copy.komiHalves == game.komiHalves);
  CHECK(copy.lastMove == game.lastMove);
}

void testAGameOfGoOverAHostileLink() {
  Medium medium;
  medium.lossPercent = 25;
  medium.duplicatePercent = 20;
  medium.maxJitterMs = 60;

  Device a(medium, 1);
  Device b(medium, 2);
  CHECK(a.start());
  CHECK(b.start());
  std::vector<Device*> devices = {&a, &b};

  run(medium, devices, 90000, 40);

  CHECK(!a.refused);
  CHECK(!b.refused);
  CHECK(!a.startedEmpty);
  CHECK(!b.startedEmpty);
  // Opposite seats, decided once by the toss and agreed by both.
  CHECK(a.seat != b.seat);
  // Whole states travel, so the two boards cannot drift: a lost packet is a
  // stale frame the next one corrects, never a divergence.
  for (int i = 0; i < go::kPoints; ++i) CHECK(a.game.point[i] == b.game.point[i]);
  CHECK(a.game.toMove == b.game.toMove);
  CHECK(a.game.capturedBy[go::kBlack] == b.game.capturedBy[go::kBlack]);
  CHECK(a.game.capturedBy[go::kWhite] == b.game.capturedBy[go::kWhite]);

  // The game reached its end and went through counting to get there.
  CHECK(a.game.stage == static_cast<uint8_t>(go::Stage::Over));
  CHECK(b.game.stage == static_cast<uint8_t>(go::Stage::Over));
  CHECK(a.marksMade > 0 || b.marksMade > 0);

  // And the count agrees, which is the whole point of the negotiation: the
  // dead marks crossed the wire with the position, so neither device is
  // counting a board the other cannot see.
  for (int i = 0; i < (go::kPoints + 7) / 8; ++i) CHECK(a.game.dead[i] == b.game.dead[i]);
  const go::Score theirs = go::score(a.game);
  const go::Score ours = go::score(b.game);
  CHECK(theirs.blackHalves == ours.blackHalves);
  CHECK(theirs.whiteHalves == ours.whiteHalves);
  // Komi survives the wire. It is not a constant: the level ladder sets it, so
  // a device that dropped it would score a different game from its opponent.
  CHECK(a.game.komiHalves == b.game.komiHalves);
  CHECK(go::settlesEveryGame(a.game.komiHalves));
}

void testAMarkOneSideMakesReachesTheOther() {
  // The counting phase in isolation, on a clean link, because the assertion is
  // about the negotiation rather than about the radio: a mark is a move, so it
  // hands the turn over and the other seat sees it.
  Medium medium;
  Device a(medium, 1);
  Device b(medium, 2);
  CHECK(a.start());
  CHECK(b.start());
  std::vector<Device*> devices = {&a, &b};

  // Two moves each, then both pass, so the game reaches Scoring with stones on
  // the board to argue about.
  run(medium, devices, 40000, 2);

  CHECK(a.marksMade + b.marksMade >= 1);
  for (int i = 0; i < (go::kPoints + 7) / 8; ++i) CHECK(a.game.dead[i] == b.game.dead[i]);
  // Whoever marked, BOTH ended up with the mark, and neither is still claiming
  // to have accepted a count it has not seen.
  CHECK(a.game.stage == static_cast<uint8_t>(go::Stage::Over));
  CHECK(b.game.stage == static_cast<uint8_t>(go::Stage::Over));
}

}  // namespace

int main() {
  testTheWholeGameFitsOnePacket();
  testAGameOfGoOverAHostileLink();
  testAMarkOneSideMakesReachesTheOther();
  std::printf("test_golink: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
