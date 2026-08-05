// The flow layer, tested as a game author would meet it.
//
// test_link.cpp proves the protocol. This file proves the thing built on top of
// it: that a game which knows nothing about networking cannot get multiplayer
// wrong. So the harness below deliberately writes the loop the *wrong* way
// round -- move first, take delivery after -- because that is what somebody
// would plausibly write, and the layer's whole job is to survive it.
//
// Under LinkSession alone that ordering silently corrupted both boards into
// agreeing on a game where a move never happened. Here it cannot: YourTurn is
// only ever reported when the opponent's last move is already applied.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/link/LinkPlay.h"
#include "FakeLink.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_play.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;
using namespace linktest;
using Phase = PlayBase::Phase;
using Ending = PlayBase::Ending;

// A whole game in 33 bytes: the move history, so any divergence between the two
// devices is visible by comparing bytes.
struct Board {
  uint8_t moves[32];
  uint8_t count;
};

struct Seat {
  Seat(Medium& medium, const uint8_t last, const char* name, const uint8_t mark)
      : transport(medium, addressOf(last)), play(&transport), displayName(name), mark(mark) {}

  FakeTransport transport;
  Play<Board> play;
  const char* displayName;
  uint8_t mark;
  Board board = {};
  int movesMade = 0;
  int matchStarts = 0;
  bool wentFirst = false;

  bool start() { return play.start(GameId::Test, displayName); }

  // One loop pass, written the wrong way round on purpose. A game author who
  // moves before taking delivery gets a stale board under the raw session; here
  // the phase machine makes it unrepresentable, so this ordering is safe.
  void pump(const uint32_t nowMs, const int moveLimit) {
    const Phase phase = play.update(nowMs);
    if (play.takeMatchStart()) {
      matchStarts++;
      wentFirst = play.goesFirst();
    }
    if (phase == Phase::YourTurn && movesMade < moveLimit && board.count < 32) {
      Board next = board;
      next.moves[next.count++] = mark;
      if (play.play(next)) {
        board = next;
        movesMade++;
      }
    }
    play.takeOpponent(board);
  }
};

void run(Medium& medium, std::vector<Seat*>& seats, const uint32_t durationMs, const int moveLimit = 0) {
  constexpr uint32_t kStepMs = 10;
  const uint32_t until = medium.nowMs + durationMs;
  while (medium.nowMs < until) {
    for (Seat* seat : seats) seat->pump(medium.nowMs, moveLimit);
    medium.collect();
    medium.nowMs += kStepMs;
  }
}

// Runs the sessions only, so a test can watch what is waiting rather than have
// Seat::pump() consume it.
void runSeats(Medium& medium, std::vector<Seat*>& seats, const uint32_t durationMs) {
  const uint32_t until = medium.nowMs + durationMs;
  while (medium.nowMs < until) {
    for (Seat* seat : seats) seat->play.update(medium.nowMs);
    medium.collect();
    medium.nowMs += 10;
  }
}

// The invariant every match has to end holding: identical boards, and the marks
// strictly alternate, which is what a lost or reordered move would break.
void checkAgreed(const Seat& a, const Seat& b, const int line) {
  check(a.board.count == b.board.count, "boards are the same length", line);
  const uint8_t shared = a.board.count < b.board.count ? a.board.count : b.board.count;
  check(memcmp(a.board.moves, b.board.moves, shared) == 0, "boards agree", line);
  bool alternating = true;
  for (uint8_t i = 1; i < shared; ++i)
    if (a.board.moves[i] == a.board.moves[i - 1]) alternating = false;
  check(alternating, "moves strictly alternate", line);
}

#define CHECK_AGREED(a, b) checkAgreed((a), (b), __LINE__)

// ---------------------------------------------------------------------------

void testTappingMultiplayerIsTheWholeSetup() {
  // The claim the project exists to make: two devices, one call each, no
  // configuration, and the next thing either of them sees is a board.
  Medium medium;
  Seat a(medium, 0x10, "ALICE", 'A');
  Seat b(medium, 0x20, "BOB", 'B');
  std::vector<Seat*> seats = {&a, &b};

  CHECK(a.play.phase() == Phase::Off);
  CHECK(a.start());
  CHECK(b.start());
  CHECK(a.play.phase() == Phase::Searching);

  run(medium, seats, 3000);

  CHECK(a.play.phase() != Phase::Searching);
  CHECK(b.play.phase() != Phase::Searching);
  CHECK(strcmp(a.play.opponentName(), "BOB") == 0);
  CHECK(strcmp(b.play.opponentName(), "ALICE") == 0);

  // Exactly one of them is on the clock, and each was told once which side it is.
  CHECK((a.play.phase() == Phase::YourTurn) != (b.play.phase() == Phase::YourTurn));
  CHECK(a.matchStarts == 1);
  CHECK(b.matchStarts == 1);
  CHECK(a.wentFirst != b.wentFirst);
  CHECK(a.wentFirst == (a.play.phase() == Phase::YourTurn));

  // And a live match keeps the device awake, or the sleep timer would take it
  // while the opponent is thinking.
  CHECK(a.play.wantsAwake());
}

void testAWholeGameOverAHostileLink() {
  Medium medium;
  medium.lossPercent = 25;
  medium.duplicatePercent = 20;
  medium.maxJitterMs = 60;
  Seat a(medium, 0x31, "A", 'A');
  Seat b(medium, 0x42, "B", 'B');
  std::vector<Seat*> seats = {&a, &b};

  CHECK(a.start());
  CHECK(b.start());
  run(medium, seats, 40000, 12);

  CHECK_AGREED(a, b);
  // Real progress, not a match that agreed by never starting.
  CHECK(a.board.count >= 20);
  CHECK(a.movesMade > 0);
  CHECK(b.movesMade > 0);
}

void testPlayIsRefusedWhenItIsNotYourTurn() {
  Medium medium;
  Seat a(medium, 0x11, "A", 'A');
  Seat b(medium, 0x22, "B", 'B');
  std::vector<Seat*> seats = {&a, &b};
  CHECK(a.start());
  CHECK(b.start());
  run(medium, seats, 3000);

  Seat& waiting = a.play.phase() == Phase::YourTurn ? b : a;
  Seat& onTheClock = a.play.phase() == Phase::YourTurn ? a : b;
  CHECK(waiting.play.phase() == Phase::TheirTurn);

  Board attempt = waiting.board;
  attempt.moves[attempt.count++] = 'X';
  CHECK(!waiting.play.play(attempt));

  // Searching and Over refuse just the same.
  Seat lonely(medium, 0x99, "C", 'C');
  CHECK(lonely.start());
  CHECK(lonely.play.update(medium.nowMs) == Phase::Searching);
  CHECK(!lonely.play.play(attempt));

  // The side that is on the clock is the only one that can move.
  Board legal = onTheClock.board;
  legal.moves[legal.count++] = 'A';
  CHECK(onTheClock.play.play(legal));
  // ...and not twice.
  CHECK(!onTheClock.play.play(legal));
}

void testThePhaseIsCorrectWithinTheSameLoopPass() {
  // A game draws from phase() straight after acting on it. On e-ink a wrong
  // frame costs the player half a second of looking at a lie, so play() and
  // takeOpponent() have to move the phase there and then rather than leaving it
  // to the next update().
  Medium medium;
  Seat a(medium, 0x19, "A", 'A');
  Seat b(medium, 0x2A, "B", 'B');
  CHECK(a.start());
  CHECK(b.start());

  // Drive the sessions only: these seats must not play themselves.
  auto pumpBoth = [&](const uint32_t forMs) {
    const uint32_t until = medium.nowMs + forMs;
    while (medium.nowMs < until) {
      a.play.update(medium.nowMs);
      b.play.update(medium.nowMs);
      medium.collect();
      medium.nowMs += 10;
    }
  };
  pumpBoth(3000);

  Seat& mover = a.play.phase() == Phase::YourTurn ? a : b;
  Seat& other = a.play.phase() == Phase::YourTurn ? b : a;
  CHECK(mover.play.phase() == Phase::YourTurn);

  Board next = mover.board;
  next.moves[next.count++] = mover.mark;
  CHECK(mover.play.play(next));
  // Immediately, without waiting for another update().
  CHECK(mover.play.phase() == Phase::TheirTurn);

  pumpBoth(400);

  // The other side has their move but has not applied it, so it is still, as
  // far as its board is concerned, their turn.
  CHECK(other.play.phase() == Phase::TheirTurn);
  CHECK(other.play.takeOpponent(other.board));
  // And applying it hands over the clock in the same pass.
  CHECK(other.play.phase() == Phase::YourTurn);
  CHECK(other.board.count == 1);
  CHECK(other.board.moves[0] == mover.mark);
  // Nothing left waiting, so a second take finds nothing.
  CHECK(!other.play.takeOpponent(other.board));
}

void testANoteDoesNotTouchTheTurn() {
  // The primitive the rematch is built on. Chess needed it the moment one player
  // hit NEW GAME: their board reset, the other sat waiting, and there was no
  // turn to carry the news because the game had just ended.
  Medium medium;
  medium.lossPercent = 30;
  medium.duplicatePercent = 20;
  medium.maxJitterMs = 50;
  Seat a(medium, 0x35, "A", 'A');
  Seat b(medium, 0x46, "B", 'B');
  std::vector<Seat*> seats = {&a, &b};
  CHECK(a.start());
  CHECK(b.start());
  runSeats(medium, seats, 3000);

  Seat& waiting = a.play.phase() == Phase::YourTurn ? b : a;
  const Phase before = waiting.play.phase();
  CHECK(before == Phase::TheirTurn);

  // The side NOT on the clock can still speak, which is the entire point.
  CHECK(waiting.play.say(7));
  CHECK(waiting.play.phase() == before);

  uint8_t note = 0;
  bool delivered = false;
  for (int step = 0; step < 400 && !delivered; ++step) {
    for (Seat* seat : seats) seat->play.update(medium.nowMs);
    Seat& other = &waiting == &a ? b : a;
    if (other.play.heard(note)) delivered = true;
    medium.collect();
    medium.nowMs += 10;
  }
  CHECK(delivered);
  CHECK(note == 7);
  // Once per note, even though a duplicating link carried several copies.
  uint8_t again = 0;
  Seat& other = &waiting == &a ? b : a;
  CHECK(!other.play.heard(again));
  // And neither side's turn moved.
  CHECK(a.play.phase() != b.play.phase());
}

void testNotesAndMovesDoNotCollide() {
  // They run on separate sequences on purpose: either side may raise a note at
  // any moment, so sharing the move counter would let a note and a move race.
  Medium medium;
  Seat a(medium, 0x37, "A", 'A');
  Seat b(medium, 0x48, "B", 'B');
  std::vector<Seat*> seats = {&a, &b};
  CHECK(a.start());
  CHECK(b.start());
  runSeats(medium, seats, 3000);

  Seat& mover = a.play.phase() == Phase::YourTurn ? a : b;
  Seat& other = a.play.phase() == Phase::YourTurn ? b : a;

  CHECK(other.play.say(3));
  Board next = mover.board;
  next.moves[next.count++] = mover.mark;
  CHECK(mover.play.play(next));

  uint8_t note = 0;
  bool gotNote = false;
  bool gotMove = false;
  for (int step = 0; step < 400 && !(gotNote && gotMove); ++step) {
    for (Seat* seat : seats) seat->play.update(medium.nowMs);
    if (mover.play.heard(note)) gotNote = true;
    if (other.play.takeOpponent(other.board)) gotMove = true;
    medium.collect();
    medium.nowMs += 10;
  }
  // Both arrived, neither ate the other.
  CHECK(gotNote);
  CHECK(note == 3);
  CHECK(gotMove);
  CHECK(other.board.count == 1);
  CHECK(other.play.phase() == Phase::YourTurn);
}

void testForgettingToTakeDeliveryStallsRatherThanDesyncs() {
  // A game that never calls takeOpponent() is broken, but it must be broken the
  // safe way: parked on their turn with both boards still in agreement, not
  // quietly building on a position the opponent never played.
  Medium medium;
  Seat good(medium, 0x51, "GOOD", 'G');
  Seat forgetful(medium, 0x62, "OOPS", 'O');
  std::vector<Seat*> seats = {&good, &forgetful};
  CHECK(good.start());
  CHECK(forgetful.start());

  constexpr uint32_t kStepMs = 10;
  const uint32_t until = medium.nowMs + 12000;
  while (medium.nowMs < until) {
    good.pump(medium.nowMs, 20);
    // The same loop with the one call missing.
    const Phase phase = forgetful.play.update(medium.nowMs);
    forgetful.play.takeMatchStart();
    if (phase == Phase::YourTurn && forgetful.board.count < 32) {
      Board next = forgetful.board;
      next.moves[next.count++] = forgetful.mark;
      if (forgetful.play.play(next)) forgetful.board = next;
    }
    medium.collect();
    medium.nowMs += kStepMs;
  }

  // It is stuck, and it stayed honest while stuck.
  CHECK(forgetful.play.phase() == Phase::TheirTurn);
  CHECK(forgetful.board.count <= 1);
  const uint8_t shared = forgetful.board.count;
  CHECK(memcmp(good.board.moves, forgetful.board.moves, shared) == 0);
  // The healthy side is waiting on them, not racing ahead alone.
  CHECK(good.board.count <= 2);
  CHECK(good.play.phase() != Phase::Over);
}

void testOpponentLeavingIsSaidNotGuessed() {
  Medium medium;
  Seat a(medium, 0x13, "A", 'A');
  Seat b(medium, 0x24, "B", 'B');
  std::vector<Seat*> seats = {&a, &b};
  CHECK(a.start());
  CHECK(b.start());
  run(medium, seats, 3000, 4);
  CHECK(a.play.phase() != Phase::Searching);

  b.play.stop();
  CHECK(b.play.phase() == Phase::Off);
  CHECK(!b.play.wantsAwake());

  run(medium, seats, 500);
  CHECK(a.play.phase() == Phase::Over);
  CHECK(a.play.ending() == Ending::OpponentLeft);
  // Their name survives the disconnect, because the screen has to say who left.
  CHECK(strcmp(a.play.opponentName(), "B") == 0);
}

void testAnOpponentWhoVanishesIsNoticed() {
  // Flat battery, out of range, or a crash: no Bye arrives, only silence.
  Medium medium;
  Seat a(medium, 0x15, "A", 'A');
  Seat b(medium, 0x26, "B", 'B');
  std::vector<Seat*> seats = {&a, &b};
  CHECK(a.start());
  CHECK(b.start());
  run(medium, seats, 3000);
  CHECK(a.play.phase() != Phase::Searching);

  medium.hasBlackhole = true;
  medium.blackholeSource = addressOf(0x26);
  run(medium, seats, 12000);

  CHECK(a.play.phase() == Phase::Over);
  CHECK(a.play.ending() == Ending::OpponentLost);
}

void testStoppingIsIdempotentAndKeepsTheReason() {
  Medium medium;
  Seat a(medium, 0x17, "A", 'A');
  Seat b(medium, 0x28, "B", 'B');
  std::vector<Seat*> seats = {&a, &b};
  CHECK(a.start());
  CHECK(b.start());
  run(medium, seats, 3000);

  b.play.stop();
  run(medium, seats, 500);
  CHECK(a.play.ending() == Ending::OpponentLeft);

  // The Activity calls stop() on its way out, after the "they left" screen has
  // been read. That must not rewrite what the player was just shown.
  a.play.stop();
  CHECK(a.play.ending() == Ending::OpponentLeft);
  a.play.stop();
  a.play.stop();
  CHECK(a.play.phase() == Phase::Off);
  CHECK(!a.play.wantsAwake());

  // Leaving of your own accord, with nothing else having happened, says so.
  Seat lonely(medium, 0x9A, "C", 'C');
  CHECK(lonely.start());
  lonely.play.update(medium.nowMs);
  lonely.play.stop();
  CHECK(lonely.play.ending() == Ending::YouLeft);
}

void testSearchingNeverGivesUpOnItsOwn() {
  // The DS spun until you pressed B. There is no timeout, no "no games found",
  // and no retry button, because an error the player has to interpret is a bug
  // and they can already see whether anyone else is in the room.
  Medium medium;
  Seat alone(medium, 0x71, "ALONE", 'A');
  CHECK(alone.start());
  std::vector<Seat*> seats = {&alone};
  run(medium, seats, 120000);

  CHECK(alone.play.phase() == Phase::Searching);
  CHECK(alone.play.ending() == Ending::None);
  CHECK(alone.play.wantsAwake());
}

void testGamesDoNotCrossOver() {
  // Two apps on the same channel hear every one of each other's broadcasts and
  // must never pair. GameId carries a layout version for the same reason: a
  // half-flashed pair should fail to find each other, not misread each other.
  Medium medium;
  Seat chess(medium, 0x81, "A", 'A');
  Seat four(medium, 0x82, "B", 'B');
  CHECK(chess.play.start(GameId::Chess, "A"));
  CHECK(four.play.start(GameId::ConnectFour, "B"));
  std::vector<Seat*> seats = {&chess, &four};
  run(medium, seats, 5000);

  CHECK(chess.play.phase() == Phase::Searching);
  CHECK(four.play.phase() == Phase::Searching);
  CHECK(static_cast<uint16_t>(GameId::Chess) != static_cast<uint16_t>(GameId::ConnectFour));
}

void testRestartingLooksForSomebodyNew() {
  Medium medium;
  Seat a(medium, 0x91, "A", 'A');
  Seat b(medium, 0x92, "B", 'B');
  std::vector<Seat*> seats = {&a, &b};
  CHECK(a.start());
  CHECK(b.start());
  run(medium, seats, 3000, 4);
  CHECK(a.play.phase() != Phase::Searching);
  const int startsBefore = a.matchStarts;

  // A leaves, so B is told and lands on Over. Playing again is both of them
  // tapping MULTIPLAYER a second time, which is the same two calls as the first
  // time: there is no reconnect, no rematch negotiation, and nothing kept.
  a.play.stop();
  run(medium, seats, 500);
  CHECK(b.play.phase() == Phase::Over);
  CHECK(b.play.ending() == Ending::OpponentLeft);

  CHECK(a.start());
  CHECK(b.start());
  CHECK(a.play.phase() == Phase::Searching);
  CHECK(a.play.ending() == Ending::None);  // a fresh start is not still ended

  run(medium, seats, 6000, 8);
  // They found each other again, and the game was told to set up a new board.
  CHECK(a.play.phase() != Phase::Searching);
  CHECK(a.matchStarts == startsBefore + 1);
}

void testASoakOfWholeGames() {
  // Many matches, each on its own hostile link, all played through the wrong
  // loop ordering. Correctness assertions cannot see a packet storm -- both
  // sides agree, they just stop -- so progress and traffic are asserted too.
  constexpr int kMatches = 60;
  constexpr long kTrafficCeiling = 900;
  int agreed = 0;
  int fewestMoves = 1000;
  long busiest = 0;

  for (int match = 0; match < kMatches; ++match) {
    Medium medium;
    medium.random = static_cast<uint32_t>(match * 2654435761u) | 1u;
    medium.nowMs = static_cast<uint32_t>(match) * 977u;
    medium.lossPercent = match % 30;
    medium.duplicatePercent = match % 17;
    medium.maxJitterMs = match % 50;

    Seat a(medium, static_cast<uint8_t>(0x40 + (match % 7)), "A", 'A');
    Seat b(medium, static_cast<uint8_t>(0xB0 + (match % 5)), "B", 'B');
    std::vector<Seat*> seats = {&a, &b};
    a.start();
    b.start();
    run(medium, seats, 40000, 10);

    const uint8_t shared = a.board.count < b.board.count ? a.board.count : b.board.count;
    bool alternating = true;
    for (uint8_t i = 1; i < shared; ++i)
      if (a.board.moves[i] == a.board.moves[i - 1]) alternating = false;
    if (a.board.count == b.board.count && memcmp(a.board.moves, b.board.moves, shared) == 0 && alternating) agreed++;

    if (a.board.count < fewestMoves) fewestMoves = a.board.count;
    if (medium.carried > busiest) busiest = medium.carried;
  }

  CHECK(agreed == kMatches);
  // Every match ran its move cap out. A stall shows up here and nowhere else.
  CHECK(fewestMoves >= 20);
  // And nobody was shouting. A reflection loop reads as a deadlock from the
  // outside, so the only assertion that can see one is on traffic.
  CHECK(busiest <= kTrafficCeiling);
  if (busiest > kTrafficCeiling) std::printf("  (soak: busiest match carried %ld packets)\n", busiest);
}

}  // namespace

int main() {
  testTappingMultiplayerIsTheWholeSetup();
  testAWholeGameOverAHostileLink();
  testPlayIsRefusedWhenItIsNotYourTurn();
  testThePhaseIsCorrectWithinTheSameLoopPass();
  testANoteDoesNotTouchTheTurn();
  testNotesAndMovesDoNotCollide();
  testForgettingToTakeDeliveryStallsRatherThanDesyncs();
  testOpponentLeavingIsSaidNotGuessed();
  testAnOpponentWhoVanishesIsNoticed();
  testStoppingIsIdempotentAndKeepsTheReason();
  testSearchingNeverGivesUpOnItsOwn();
  testGamesDoNotCrossOver();
  testRestartingLooksForSomebodyNew();
  testASoakOfWholeGames();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
