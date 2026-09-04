// The end of a match, played out between two devices over the hostile link.
//
// Two bugs lived at the same instant and this file is about both of them.
//
//   The loser saw zero frames of the final board. The winning state was applied
//   and the rematch screen went up in the same pass, and the repaint that would
//   have shown the board never happened, so the move that ended the game was
//   never on the panel.
//
//   And no link match was ever counted. Five games recorded their result in a
//   part of gameLoop() that multiplayer returns before reaching, so the tally
//   simply stayed at zero. Nothing crashed and nothing logged.
//
// Both are Endgame's now (src/apps_local/link/LinkEndgame.h), which is why they
// can be tested here at all: the state machine is freestanding, the link
// underneath it is the real one, and the game on top is the real Connect Four
// rules. What the seats below reproduce from LinkActivity is only its ORDERING
// -- take delivery, drive the endgame, then either the link screen or the game
// -- because everything with a decision in it was deliberately put inside
// Endgame::update() rather than left in the caller for a test to re-derive.
//
// The assertions that matter are the two the bugs were:
//
//   recordCalls == 1 and the tally is no longer zero, on BOTH devices, after a
//   finished MATCH -- the record exists, rather than a code path having run.
//
//   the LOSER painted the final board, and it stayed on the panel for at least
//   the hold before anything replaced it.

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/apps_local/connectfour/ConnectFourCore.h"
#include "../../src/apps_local/link/LinkEndgame.h"
#include "../../src/apps_local/link/LinkPlay.h"
#include "FakeLink.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  checksRun++;
  if (condition) return;
  checksFailed++;
  std::printf("FAIL test_endgame.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using namespace linkplay;
using namespace linktest;
namespace c4 = connectfour;
using Phase = PlayBase::Phase;

// One device: the real link, the real rules, the real Endgame, and the parts of
// LinkActivity a game cannot be tested without.
struct Seat {
  Seat(Medium& medium, const uint8_t last) : transport(medium, addressOf(last)), play(&transport) {}

  FakeTransport transport;
  Play<c4::Game> play;
  c4::Game game{};
  Endgame endgame;
  uint8_t seat = c4::kLight;
  bool requested = false;
  bool rematch = false;
  // Set by the test to make a player tap ANOTHER GAME while the board is still
  // up, which is the one thing allowed to cut the hold short.
  bool tapsPlayAgain = false;

  // --- the record, which is the whole of card #35 -------------------------
  // ConnectFourActivity keeps exactly these and writes them to the SD card.
  // After a finished MATCH they must not all still be zero.
  int wins = 0;
  int losses = 0;
  int draws = 0;
  bool hasHistory = false;
  int recordCalls = 0;

  // --- what was actually on the panel, which is the whole of card #36 -----
  enum class Screen { Board, Result };
  Screen screen = Screen::Board;
  int framesOfFinalBoard = 0;
  // Only the link screen that REPLACES a finished game. The searching screen is
  // the link screen too, and counting it made the first version of this test
  // compare the end of the match against a frame from before it started.
  int framesOfOfferScreen = 0;
  uint32_t firstFinalPaintMs = 0;
  uint32_t offerScreenAtMs = 0;

  bool start() { return play.start(GameId::ConnectFour, "TEST"); }

  Phase phase() const { return play.phase(); }
  bool inMatch() const { return requested && phase() != Phase::Off && phase() != Phase::Searching; }
  bool linkYourTurn() const { return phase() == Phase::YourTurn; }

  // --- what Endgame asks of its host --------------------------------------

  bool matchGameOver() const { return c4::over(game); }

  void onMatchEnded() {
    recordCalls++;
    // ConnectFourActivity::recordResult() in miniature: which way the outcome
    // fell, read from this device's own seat.
    const bool won = (seat == c4::kLight && game.outcome == c4::Outcome::LightWins) ||
                     (seat == c4::kDark && game.outcome == c4::Outcome::DarkWins);
    if (game.outcome == c4::Outcome::Draw)
      draws++;
    else if (won)
      wins++;
    else
      losses++;
    hasHistory = true;
    // And the screen the solo game ends on, which in a match was unreachable.
    screen = Screen::Result;
  }

  void onEndgameChanged() {}

  // --- LinkActivity's ordering ---------------------------------------------

  bool linkOwnsScreen() const {
    if (!requested) return false;
    if (endgame.showingFinal()) return false;
    return phase() == Phase::Searching || phase() == Phase::Over || rematch;
  }

  void onMatchStart(const bool goesFirst) {
    seat = goesFirst ? c4::kLight : c4::kDark;
    c4::start(game);
    screen = Screen::Board;
  }

  void pump(const uint32_t nowMs) {
    play.update(nowMs);
    if (play.takeMatchStart()) onMatchStart(play.goesFirst());
    play.takeOpponent(game);

    endgame.update(*this, inMatch(), nowMs);
    if (endgame.offering() && !rematch) rematch = true;

    if (linkOwnsScreen()) return;  // driveLink() returns false; gameLoop() is skipped
    gameLoop();
  }

  void gameLoop() {
    if (tapsPlayAgain && endgame.showingFinal()) {
      // proposeRematch(): the player has seen enough.
      endgame.skip();
      rematch = true;
      return;
    }
    if (!inMatch() || !linkYourTurn() || c4::over(game)) return;
    // Light stacks column 0 and dark stacks column 1, so light wins on its
    // fourth drop -- the seventh move of the game. Deterministic on purpose:
    // the move that ends it is the last one either device sends, which is
    // exactly the frame the loser was never shown.
    const int column = seat == c4::kLight ? 0 : 1;
    if (!c4::drop(game, column)) return;
    play.play(game);
  }

  void render(const uint32_t nowMs) {
    // Read before the frame is built, exactly as LinkActivity::render() does.
    const Endgame::Stage stageAtBuild = endgame.stage();
    if (linkOwnsScreen()) {
      if (c4::over(game)) {
        framesOfOfferScreen++;
        if (offerScreenAtMs == 0) offerScreenAtMs = nowMs;
      }
      return;
    }
    // gameRender(): whatever screen the game is on reaches the panel here.
    if (screen == Screen::Result && c4::over(game)) {
      framesOfFinalBoard++;
      if (firstFinalPaintMs == 0) firstFinalPaintMs = nowMs;
    }
    endgame.notePainted(stageAtBuild, nowMs);
  }
};

// `paintEveryMs` is the panel. Zero means it never paints at all, which is the
// case the ceiling exists for.
void run(Medium& medium, std::vector<Seat*>& seats, const uint32_t durationMs, const uint32_t paintEveryMs) {
  const uint32_t until = medium.nowMs + durationMs;
  while (medium.nowMs < until) {
    for (Seat* seat : seats) seat->pump(medium.nowMs);
    if (paintEveryMs != 0 && medium.nowMs % paintEveryMs == 0) {
      for (Seat* seat : seats) seat->render(medium.nowMs);
    }
    medium.collect();
    medium.nowMs += 10;
  }
}

Seat* winnerOf(Seat& a, Seat& b) { return a.seat == c4::kLight ? &a : &b; }
Seat* loserOf(Seat& a, Seat& b) { return a.seat == c4::kLight ? &b : &a; }

// ---------------------------------------------------------------------------

// The whole of both cards, on a panel that repaints every 200ms.
void aFinishedMatchIsCountedAndSeen() {
  Medium medium;
  medium.lossPercent = 10;
  medium.duplicatePercent = 10;
  medium.maxJitterMs = 30;
  Seat a(medium, 0x11);
  Seat b(medium, 0x22);
  CHECK(a.start());
  CHECK(b.start());
  a.requested = true;
  b.requested = true;
  std::vector<Seat*> seats{&a, &b};
  run(medium, seats, 20000, 200);

  // The game actually finished, or nothing below means anything.
  CHECK(c4::over(a.game));
  CHECK(c4::over(b.game));
  CHECK(a.game.outcome == b.game.outcome);
  CHECK(a.game.outcome == c4::Outcome::LightWins);

  // Card #35. The record EXISTS, on both devices, exactly once.
  CHECK(a.recordCalls == 1);
  CHECK(b.recordCalls == 1);
  CHECK(a.hasHistory);
  CHECK(b.hasHistory);
  CHECK(a.wins + a.losses + a.draws == 1);
  CHECK(b.wins + b.losses + b.draws == 1);
  Seat* winner = winnerOf(a, b);
  Seat* loser = loserOf(a, b);
  CHECK(winner->wins == 1);
  CHECK(winner->losses == 0);
  CHECK(loser->losses == 1);
  CHECK(loser->wins == 0);

  // Card #36. The loser saw the board that beat them, and it stayed up.
  CHECK(loser->framesOfFinalBoard > 0);
  CHECK(winner->framesOfFinalBoard > 0);
  CHECK(loser->firstFinalPaintMs != 0);
  CHECK(loser->offerScreenAtMs > loser->firstFinalPaintMs);
  CHECK(loser->offerScreenAtMs - loser->firstFinalPaintMs >= Endgame::kHoldMs);
  CHECK(winner->offerScreenAtMs - winner->firstFinalPaintMs >= Endgame::kHoldMs);

  // And the offer does arrive: a hold that never ends is the worse bug.
  CHECK(loser->framesOfOfferScreen > 0);
  CHECK(winner->framesOfOfferScreen > 0);
  CHECK(loser->rematch);
  CHECK(winner->rematch);
}

// The hold is counted from the paint, not from the move. A panel this slow
// would have spent the entire hold repainting under a timer started earlier.
void theHoldIsCountedFromThePaint() {
  Medium medium;
  Seat a(medium, 0x31);
  Seat b(medium, 0x32);
  CHECK(a.start());
  CHECK(b.start());
  a.requested = true;
  b.requested = true;
  std::vector<Seat*> seats{&a, &b};
  run(medium, seats, 20000, 1500);

  Seat* loser = loserOf(a, b);
  CHECK(loser->framesOfFinalBoard > 0);
  CHECK(loser->offerScreenAtMs - loser->firstFinalPaintMs >= Endgame::kHoldMs);
}

// A panel that never reports a paint must still reach the offer, or the player
// is stuck on a finished board with no way to ask for another game.
void anUnpaintedHoldStillEnds() {
  Medium medium;
  Seat a(medium, 0x41);
  Seat b(medium, 0x42);
  CHECK(a.start());
  CHECK(b.start());
  a.requested = true;
  b.requested = true;
  std::vector<Seat*> seats{&a, &b};
  run(medium, seats, 20000, 0);

  CHECK(a.recordCalls == 1);
  CHECK(b.recordCalls == 1);
  CHECK(a.endgame.offering());
  CHECK(b.endgame.offering());
  CHECK(a.rematch);
  CHECK(b.rematch);
}

// Tapping ANOTHER GAME while the board is up ends the hold rather than being
// swallowed by it.
void askingForAnotherGameSkipsTheHold() {
  Medium medium;
  Seat a(medium, 0x51);
  Seat b(medium, 0x52);
  CHECK(a.start());
  CHECK(b.start());
  a.requested = true;
  b.requested = true;
  a.tapsPlayAgain = true;
  std::vector<Seat*> seats{&a, &b};
  run(medium, seats, 20000, 100);

  // Still counted -- skipping the look does not skip the record.
  CHECK(a.recordCalls == 1);
  CHECK(a.rematch);
  CHECK(!a.endgame.showingFinal());
  // The other device did not tap anything, so it got its full look.
  CHECK(b.offerScreenAtMs - b.firstFinalPaintMs >= Endgame::kHoldMs);
}

// A finished SOLO game is not the link layer's business, and must not be
// counted twice: the game records that one itself.
void aSoloGameIsNeverCountedByTheLayer() {
  Medium medium;
  Seat a(medium, 0x61);
  a.seat = c4::kLight;
  c4::start(a.game);
  // Play it out with no link at all.
  for (int i = 0; i < 7 && !c4::over(a.game); ++i) c4::drop(a.game, i % 2);
  CHECK(c4::over(a.game));
  for (uint32_t now = 0; now < 20000; now += 10) {
    a.endgame.update(a, a.inMatch(), now);
    a.endgame.notePainted(a.endgame.stage(), now);
  }
  CHECK(a.recordCalls == 0);
  CHECK(!a.endgame.showingFinal());
  CHECK(!a.endgame.offering());
}

// A frame that reached the panel while the game was still running is not the
// final board. Left pending it would be picked up by the transition and cut the
// hold to nothing, which is the original bug wearing a timer.
void aPaintFromBeforeTheEndDoesNotCountAsTheFinalBoard() {
  Medium medium;
  Seat a(medium, 0x71);
  // Straight to the state machine: no link, no rules, just the ordering.
  struct Host {
    bool over = false;
    int ended = 0;
    bool matchGameOver() const { return over; }
    void onMatchEnded() { ended++; }
    void onEndgameChanged() {}
  } host;
  Endgame endgame;
  // 1000ms of a live game, painting all the way.
  for (uint32_t now = 0; now < 1000; now += 10) {
    endgame.update(host, true, now);
    endgame.notePainted(endgame.stage(), now);
  }
  CHECK(host.ended == 0);
  // It ends at 1000, and nothing is painted after that.
  host.over = true;
  endgame.update(host, true, 1000);
  CHECK(host.ended == 1);
  CHECK(endgame.showingFinal());
  // The stale 990ms paint must not have started a hold that is already over.
  endgame.update(host, true, 1010 + Endgame::kHoldMs);
  CHECK(endgame.showingFinal());
  // Only the ceiling releases it.
  endgame.update(host, true, 1000 + Endgame::kUnpaintedHoldMs);
  CHECK(endgame.offering());
  (void)a;
}

// A repaint of the old board can already be in flight when the match ends.
// Reporting it as the final board would start the hold from a frame that never
// showed the winning move -- the original bug, wearing a timer.
void aRepaintInFlightWhenTheMatchEndsIsNotTheFinalBoard() {
  struct Host {
    bool over = false;
    int ended = 0;
    bool matchGameOver() const { return over; }
    void onMatchEnded() { ended++; }
    void onEndgameChanged() {}
  } host;
  Endgame endgame;
  // A frame starts being drawn at 900, while the game is still running.
  const Endgame::Stage stageAtBuild = endgame.stage();
  CHECK(stageAtBuild == Endgame::Stage::Live);
  // The match ends at 1000, before that frame reaches the panel.
  host.over = true;
  endgame.update(host, true, 1000);
  CHECK(endgame.showingFinal());
  // Now it lands. It is not the final board, so it starts nothing.
  endgame.notePainted(stageAtBuild, 1100);
  endgame.update(host, true, 1110);
  endgame.update(host, true, 1100 + Endgame::kHoldMs);
  CHECK(endgame.showingFinal());
  // The real final board lands at 2000 and gets its full hold from there.
  endgame.notePainted(Endgame::Stage::Final, 2000);
  endgame.update(host, true, 2010);
  endgame.update(host, true, 2000 + Endgame::kHoldMs - 10);
  CHECK(endgame.showingFinal());
  endgame.update(host, true, 2000 + Endgame::kHoldMs);
  CHECK(endgame.offering());
}

// A rematch is a different end of a game, so the next one is counted too.
void aRematchIsCountedAgain() {
  Medium medium;
  Seat a(medium, 0x81);
  struct Host {
    bool over = false;
    int ended = 0;
    bool matchGameOver() const { return over; }
    void onMatchEnded() { ended++; }
    void onEndgameChanged() {}
  } host;
  Endgame endgame;
  host.over = true;
  endgame.update(host, true, 100);
  CHECK(host.ended == 1);
  endgame.notePainted(Endgame::Stage::Final, 200);
  endgame.update(host, true, 210);
  endgame.update(host, true, 200 + Endgame::kHoldMs);
  CHECK(endgame.offering());

  // Both said yes: LinkActivity::startRematch() resets, and the game restarts.
  endgame.reset();
  host.over = false;
  endgame.update(host, true, 5000);
  CHECK(!endgame.showingFinal());
  CHECK(host.ended == 1);

  host.over = true;
  endgame.update(host, true, 6000);
  CHECK(host.ended == 2);
  CHECK(endgame.showingFinal());

  // And a rematch that reset nothing is still a new game.
  host.over = false;
  endgame.update(host, true, 6010);
  CHECK(!endgame.showingFinal());
  host.over = true;
  endgame.update(host, true, 6020);
  CHECK(host.ended == 3);
  (void)a;
}

// millis() rolls over about every 49 days and a match can straddle it.
void theHoldSurvivesAMillisRollover() {
  struct Host {
    bool over = true;
    int ended = 0;
    bool matchGameOver() const { return over; }
    void onMatchEnded() { ended++; }
    void onEndgameChanged() {}
  } host;
  Endgame endgame;
  const uint32_t justBeforeWrap = 0xFFFFFFFFu - 1000;
  endgame.update(host, true, justBeforeWrap);
  CHECK(host.ended == 1);
  endgame.notePainted(Endgame::Stage::Final, justBeforeWrap);
  endgame.update(host, true, justBeforeWrap + 10);
  CHECK(endgame.showingFinal());
  // Past the wrap but inside the hold.
  endgame.update(host, true, static_cast<uint32_t>(justBeforeWrap + 1500));
  CHECK(endgame.showingFinal());
  endgame.update(host, true, static_cast<uint32_t>(justBeforeWrap + Endgame::kHoldMs));
  CHECK(endgame.offering());
}

// Leaving mid-hold puts the machine back where a fresh match expects it.
void leavingDuringTheHoldResets() {
  struct Host {
    bool over = true;
    int ended = 0;
    bool matchGameOver() const { return over; }
    void onMatchEnded() { ended++; }
    void onEndgameChanged() {}
  } host;
  Endgame endgame;
  endgame.update(host, true, 100);
  CHECK(endgame.showingFinal());
  // requested_ goes false, so inMatch() does.
  endgame.update(host, false, 110);
  CHECK(!endgame.showingFinal());
  CHECK(!endgame.offering());
  // And the next match counts its own end.
  endgame.update(host, true, 120);
  CHECK(host.ended == 2);
}

}  // namespace

int main() {
  aFinishedMatchIsCountedAndSeen();
  theHoldIsCountedFromThePaint();
  anUnpaintedHoldStillEnds();
  askingForAnotherGameSkipsTheHold();
  aSoloGameIsNeverCountedByTheLayer();
  aPaintFromBeforeTheEndDoesNotCountAsTheFinalBoard();
  aRepaintInFlightWhenTheMatchEndsIsNotTheFinalBoard();
  aRematchIsCountedAgain();
  theHoldSurvivesAMillisRollover();
  leavingDuringTheHoldResets();

  std::printf("test_endgame: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
