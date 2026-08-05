// A real game of chess, played between two devices over a hostile link.
//
// test_play.cpp proves the flow layer against a toy state. This proves the one
// that ships: two boards driven by the actual chess rules, exchanging the actual
// FEN serialization the activity sends, over a link that drops, duplicates and
// reorders. If the wiring between the game and the layer is wrong, it is wrong
// here rather than on Mario's desk.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/chess/ChessCore.h"
#include "../../src/apps_local/chess/ChessWire.h"
#include "../../src/apps_local/link/LinkPlay.h"
#include "FakeLink.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_chesslink.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;
using namespace linktest;
using Phase = PlayBase::Phase;

// The same three things the activity does, with the board and the rules real.
struct Device {
  Device(Medium& medium, const uint8_t last) : transport(medium, addressOf(last)), play(&transport) {}

  FakeTransport transport;
  Play<ChessWire> play;
  chess::Position position;
  chess::MoveList legal;
  bool white = false;
  int plies = 0;
  bool badFen = false;
  bool refused = false;

  bool start() { return play.start(GameId::Chess, nullptr); }

  void pump(const uint32_t nowMs, const int moveLimit) {
    const Phase phase = play.update(nowMs);

    if (play.takeMatchStart()) {
      // Whoever the toss put first plays White, computed identically on both.
      white = play.goesFirst();
      chess::setStartPosition(position);
      chess::generateLegalMoves(position, legal);
      plies = 0;
    }

    ChessWire incoming;
    if (play.takeOpponent(incoming)) {
      chess::Position next;
      if (!chess::parseFen(incoming.fen, next)) {
        badFen = true;
      } else {
        position = next;
        chess::generateLegalMoves(position, legal);
      }
    }

    if (phase != Phase::YourTurn || plies >= moveLimit || legal.count == 0) return;

    // A deterministic but position-dependent choice, so the games differ.
    const chess::Move move = legal.moves[static_cast<int>(nowMs / 10) % legal.count];
    chess::Undo undo;
    chess::makeMove(position, move, undo);
    chess::generateLegalMoves(position, legal);

    ChessWire wire = {};
    chess::positionToFen(position, wire.fen);
    std::memcpy(wire.lastMove, "MOVE", 5);
    if (!play.play(wire)) refused = true;
    plies++;
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

void testTheWireFitsAndRoundTrips() {
  // The static_assert in Play<> already refuses anything too big, but the margin
  // is worth stating: this is what makes whole-state-not-moves affordable.
  CHECK(sizeof(ChessWire) <= kMaxPayloadBytes);

  chess::Position start;
  chess::setStartPosition(start);
  ChessWire wire = {};
  chess::positionToFen(start, wire.fen);
  CHECK(std::strlen(wire.fen) < sizeof(wire.fen));

  chess::Position restored;
  CHECK(chess::parseFen(wire.fen, restored));
  char again[90];
  chess::positionToFen(restored, again);
  CHECK(std::strcmp(wire.fen, again) == 0);
}

void testAGameOfChessOverAHostileLink() {
  Medium medium;
  medium.lossPercent = 25;
  medium.duplicatePercent = 20;
  medium.maxJitterMs = 60;
  Device a(medium, 0x21);
  Device b(medium, 0x32);
  std::vector<Device*> devices = {&a, &b};

  CHECK(a.start());
  CHECK(b.start());
  run(medium, devices, 40000, 16);

  CHECK(a.play.phase() != Phase::Searching);
  // Exactly one of them is White, and neither had to be told.
  CHECK(a.white != b.white);

  // The two boards are the same board. This is the assertion the whole design
  // exists to make cheap: no move log, no replay, no reconciliation.
  char fenA[90];
  char fenB[90];
  chess::positionToFen(a.position, fenA);
  chess::positionToFen(b.position, fenB);
  CHECK(std::strcmp(fenA, fenB) == 0);

  // A real game happened, and the plies alternated between the two devices.
  CHECK(a.plies + b.plies >= 24);
  CHECK(a.plies > 0);
  CHECK(b.plies > 0);
  const int gap = a.plies > b.plies ? a.plies - b.plies : b.plies - a.plies;
  CHECK(gap <= 1);

  CHECK(!a.badFen);
  CHECK(!b.badFen);
  CHECK(!a.refused);
  CHECK(!b.refused);
}

void testManyGames() {
  constexpr int kGames = 30;
  int agreed = 0;
  int fewest = 1000;
  for (int game = 0; game < kGames; ++game) {
    Medium medium;
    medium.random = static_cast<uint32_t>(game * 2654435761u) | 1u;
    medium.nowMs = static_cast<uint32_t>(game) * 811u;
    medium.lossPercent = game % 28;
    medium.duplicatePercent = game % 15;
    medium.maxJitterMs = game % 45;

    Device a(medium, static_cast<uint8_t>(0x50 + (game % 6)));
    Device b(medium, static_cast<uint8_t>(0xC0 + (game % 4)));
    std::vector<Device*> devices = {&a, &b};
    a.start();
    b.start();
    run(medium, devices, 40000, 12);

    char fenA[90];
    char fenB[90];
    chess::positionToFen(a.position, fenA);
    chess::positionToFen(b.position, fenB);
    if (std::strcmp(fenA, fenB) == 0 && !a.badFen && !b.badFen && !a.refused && !b.refused) agreed++;
    const int total = a.plies + b.plies;
    if (total < fewest) fewest = total;
  }
  CHECK(agreed == kGames);
  // Every game got properly under way, rather than agreeing by never starting.
  CHECK(fewest >= 20);
}

}  // namespace

int main() {
  testTheWireFitsAndRoundTrips();
  testAGameOfChessOverAHostileLink();
  testManyGames();
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
