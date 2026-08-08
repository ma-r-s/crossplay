// The Checkers rulebook, checked without a panel.
//
// The rule under most scrutiny is the mandatory capture, because it is BUILT IN
// rather than validated: moves() returns jumps only when a jump exists, so an
// illegal quiet move is not something the core can express. A test has to hold
// that claim to account rather than restate it.

#include <cstdio>
#include <cstring>

#include "CheckersBrain.h"
#include "CheckersCore.h"
#include "CheckersFlow.h"

using namespace checkers;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

uint32_t rng = 20260808u;
uint32_t nextRandom() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

void clear(Game& game) {
  for (int i = 0; i < kCells; ++i) game.cell[i] = kEmpty;
  game.turn = kLight;
  game.idlePlies = 0;
  game.pad0 = 0;
}

void put(Game& game, const int file, const int rank, const bool dark, const bool king = false) {
  game.cell[indexOf(file, rank)] = static_cast<uint8_t>(kPiece | (dark ? kDark : 0) | (king ? kKing : 0));
}

void testTheOpeningPositionIsTheStandardOne() {
  Game game{};
  start(game);
  CHECK(pieceCount(game, kLight) == 12);
  CHECK(pieceCount(game, kDarkSeat) == 12);
  CHECK(game.turn == kLight);

  // Only dark squares are used, and the two middle rows are empty.
  for (int rank = 0; rank < kSize; ++rank) {
    for (int file = 0; file < kSize; ++file) {
      const int index = indexOf(file, rank);
      if (!playable(file, rank)) CHECK(!occupied(game, index));
      if (rank == 3 || rank == 4) CHECK(!occupied(game, index));
    }
  }
  // Seven opening moves, as in the printed game.
  Move list[kMaxMoves];
  CHECK(moves(game, list) == 7);
  for (int i = 0; i < 7; ++i) CHECK(list[i].takenCount == 0);
}

// The headline rule. With a jump available, moves() must offer ONLY jumps --
// not offer them alongside quiet moves and hope the caller prefers them.
void testACaptureIsMandatoryAndNothingElseIsOffered() {
  Game game{};
  clear(game);
  put(game, 2, 5, false);  // light man with a jump
  put(game, 3, 4, true);   // dark man to take
  put(game, 6, 5, false);  // another light man with only quiet moves
  game.turn = kLight;

  Move list[kMaxMoves];
  const int count = moves(game, list);
  CHECK(count > 0);
  for (int i = 0; i < count; ++i) {
    CHECK(list[i].takenCount > 0);
    // And every offered jump belongs to the piece that can actually jump.
    CHECK(list[i].from == indexOf(2, 5));
  }

  // The quiet move that exists on the board is not merely deprioritised -- it
  // cannot be played at all.
  Move quiet{};
  quiet.from = static_cast<uint8_t>(indexOf(6, 5));
  quiet.to = static_cast<uint8_t>(indexOf(5, 4));
  const Game before = game;
  CHECK(!play(game, quiet));
  CHECK(std::memcmp(&before, &game, sizeof(Game)) == 0);
}

void testAMultiJumpIsOneMoveEndingOnTheFinalSquare() {
  Game game{};
  clear(game);
  put(game, 1, 6, false);
  put(game, 2, 5, true);
  put(game, 4, 3, true);
  game.turn = kLight;

  Move list[kMaxMoves];
  const int count = moves(game, list);
  CHECK(count == 1);
  CHECK(list[0].takenCount == 2);
  CHECK(list[0].from == indexOf(1, 6));
  CHECK(list[0].to == indexOf(5, 2));

  CHECK(play(game, list[0]));
  // Both men are gone, and there is no half-finished state anywhere.
  CHECK(pieceCount(game, kDarkSeat) == 0);
  CHECK(pieceCount(game, kLight) == 1);
  CHECK(occupied(game, indexOf(5, 2)));
}

void testAManPromotesOnTheFarRowAndStops() {
  Game game{};
  clear(game);
  put(game, 1, 1, false);
  game.turn = kLight;

  Move list[kMaxMoves];
  const int count = moves(game, list);
  CHECK(count == 2);
  for (int i = 0; i < count; ++i) {
    if (list[i].to / kSize != 0) continue;
    CHECK(play(game, list[i]));
    CHECK(isKing(game, list[i].to));
    return;
  }
  CHECK(false);
}

// A jump that promotes must END there, rather than continuing with powers the
// piece did not have when the move began.
void testPromotionEndsAJumpChain() {
  Game game{};
  clear(game);
  put(game, 2, 2, false);
  put(game, 3, 1, true);
  put(game, 3, 3, true);  // would be jumpable backwards, but only by a king
  game.turn = kLight;

  Move list[kMaxMoves];
  const int count = moves(game, list);
  CHECK(count == 1);
  CHECK(list[0].takenCount == 1);
  CHECK(list[0].to == indexOf(4, 0));
  CHECK(play(game, list[0]));
  CHECK(isKing(game, indexOf(4, 0)));
  CHECK(pieceCount(game, kDarkSeat) == 1);
}

void testAManCannotStepOrJumpBackwardsButAKingCan() {
  Game game{};
  clear(game);
  put(game, 3, 4, false);
  game.turn = kLight;
  Move list[kMaxMoves];
  int count = moves(game, list);
  CHECK(count == 2);
  for (int i = 0; i < count; ++i) CHECK(list[i].to / kSize == 3);

  clear(game);
  put(game, 3, 4, false, true);
  game.turn = kLight;
  count = moves(game, list);
  CHECK(count == 4);
  int forward = 0;
  int backward = 0;
  for (int i = 0; i < count; ++i) {
    if (list[i].to / kSize == 3) ++forward;
    if (list[i].to / kSize == 5) ++backward;
  }
  CHECK(forward == 2);
  CHECK(backward == 2);
}

void testNoMoveIsALossWhetherBlockedOrCaptured() {
  // Captured: nothing left at all.
  Game empty{};
  clear(empty);
  put(empty, 0, 0, true);
  empty.turn = kLight;
  CHECK(over(empty));
  CHECK(outcome(empty) == Outcome::DarkWins);

  // Blocked: pieces remain, but the side to play cannot move. Checkers treats
  // both as a loss for the side that cannot move.
  Game blocked{};
  clear(blocked);
  put(blocked, 0, 7, false);
  put(blocked, 1, 6, true);
  put(blocked, 2, 5, true);
  blocked.turn = kLight;
  CHECK(pieceCount(blocked, kLight) == 1);
  CHECK(over(blocked));
  CHECK(outcome(blocked) == Outcome::DarkWins);
}

// Whole games of random legal play, checking the invariants after every move.
void testRandomGamesHoldEveryInvariant() {
  int longest = 0;
  for (int match = 0; match < 2000; ++match) {
    Game game{};
    start(game);
    int plies = 0;
    while (!over(game)) {
      Move list[kMaxMoves];
      const int count = moves(game, list);
      CHECK(count > 0);
      if (count == 0) break;

      // Mandatory capture, checked every single ply: either every move offered
      // is a jump, or none of them is.
      int jumps = 0;
      for (int i = 0; i < count; ++i) {
        if (list[i].takenCount > 0) ++jumps;
      }
      CHECK(jumps == 0 || jumps == count);

      const uint8_t mover = game.turn;
      const int before = pieceCount(game, mover == kLight ? kDarkSeat : kLight);
      const Move& chosen = list[nextRandom() % static_cast<uint32_t>(count)];
      const int taken = chosen.takenCount;
      CHECK(play(game, chosen));
      CHECK(game.turn != mover);
      CHECK(pieceCount(game, mover == kLight ? kDarkSeat : kLight) == before - taken);

      // Nothing ever sits on a light square, and no side exceeds twelve.
      for (int rank = 0; rank < kSize; ++rank) {
        for (int file = 0; file < kSize; ++file) {
          if (!playable(file, rank)) CHECK(!occupied(game, indexOf(file, rank)));
        }
      }
      CHECK(pieceCount(game, kLight) <= 12);
      CHECK(pieceCount(game, kDarkSeat) <= 12);

      // Random checkers can shuffle kings for a long time; this is a liveness
      // guard against a game that cannot end, not a claim about real play.
      CHECK(++plies <= 4000);
      if (plies > 4000) break;
    }
    if (plies > longest) longest = plies;
  }
  std::printf("  longest random game: %d plies\n", longest);
}

// --- navigation, selection and the opponent --------------------------------

void testBackIsTotalAndAlwaysReachesTheTop() {
  const Screen every[] = {Screen::Menu, Screen::HowTo, Screen::Board, Screen::Result};
  for (const Screen screen : every) {
    Screen at = screen;
    int steps = 0;
    while (!leavesApp(at)) {
      at = back(at);
      CHECK(++steps <= 4);
      if (steps > 4) break;
    }
    CHECK(leavesApp(at));
  }
  int exits = 0;
  for (const Screen screen : every) {
    if (leavesApp(screen)) ++exits;
  }
  CHECK(exits == 1);
  CHECK(!leavesApp(Screen::Board));
  CHECK(back(Screen::Board) == Screen::Menu);
}

// Selection is derived from the legal move list, not from ownership. Under the
// mandatory-capture rule most of your pieces usually CANNOT move, and offering
// to pick one up would be a tap that leads nowhere.
void testOnlyAPieceWithAMoveCanBePicked() {
  Game game{};
  clear(game);
  put(game, 2, 5, false);  // can jump
  put(game, 3, 4, true);
  put(game, 6, 5, false);  // has quiet moves, but a jump exists elsewhere
  game.turn = kLight;

  CHECK(canPick(game, indexOf(2, 5)));
  // Not selectable: the capture is compulsory, so this piece has no legal move.
  CHECK(!canPick(game, indexOf(6, 5)));
  // Not yours, and not empty either.
  CHECK(!canPick(game, indexOf(3, 4)));
  CHECK(!canPick(game, indexOf(0, 0)));
}

void testDestinationsMatchWhatCanActuallyBePlayed() {
  Game game{};
  start(game);
  Move list[kMaxMoves];
  const int count = moves(game, list);

  for (int square = 0; square < kCells; ++square) {
    uint8_t squares[kMaxMoves];
    const int found = destinations(game, square, squares, kMaxMoves);
    int expected = 0;
    for (int i = 0; i < count; ++i) {
      if (list[i].from == square) ++expected;
    }
    CHECK(found == expected);
    // And every destination offered really is playable from there.
    for (int d = 0; d < found; ++d) {
      Move move{};
      CHECK(moveBetween(game, square, squares[d], move));
      CHECK(move.from == square);
      CHECK(move.to == squares[d]);
    }
  }
  // A destination the rules never offered cannot be built at the boundary.
  Move bogus{};
  CHECK(!moveBetween(game, indexOf(0, 5), indexOf(4, 1), bogus));
}

void testPhaseIsDerivedAndOnlyYourTurnAccepts() {
  CHECK(phaseFor(false, true) == Phase::Yours);
  CHECK(phaseFor(false, false) == Phase::Theirs);
  CHECK(phaseFor(true, true) == Phase::Finished);
  CHECK(phaseFor(true, false) == Phase::Finished);
  CHECK(acceptsTap(Phase::Yours));
  CHECK(!acceptsTap(Phase::Theirs));
  CHECK(!acceptsTap(Phase::Finished));
}

void testTheOpponentOnlyEverPlaysALegalMove() {
  for (int match = 0; match < 200; ++match) {
    Game game{};
    start(game);
    int plies = 0;
    while (!over(game)) {
      Move chosen{};
      CHECK(chooseMove(game, chosen));
      Move legal[kMaxMoves];
      const int count = moves(game, legal);
      bool found = false;
      for (int i = 0; i < count; ++i) {
        if (legal[i].from == chosen.from && legal[i].to == chosen.to) found = true;
      }
      CHECK(found);
      CHECK(play(game, chosen));
      CHECK(++plies <= 4000);
      if (plies > 4000) break;
    }
  }
}

void testTheOpponentIsDeterministicAndPure() {
  Game game{};
  start(game);
  const Game before = game;
  Move first{};
  CHECK(chooseMove(game, first));
  // It searches on copies: the real board is never speculatively mutated, or a
  // match would desync the moment the opponent thought.
  CHECK(std::memcmp(&before, &game, sizeof(Game)) == 0);
  for (int repeat = 0; repeat < 8; ++repeat) {
    Move again{};
    CHECK(chooseMove(game, again));
    CHECK(again.from == first.from);
    CHECK(again.to == first.to);
  }
}

// Two ply exists to see traps. Checkers punishes greed hard: a capture is
// compulsory, so the reply to a greedy take is forced and can be a multi-jump.
//
// Asserted as the PROPERTY rather than against a hand-built trap, after the
// first attempt built a position whose "poisoned capture" was not actually
// available -- the landing square was occupied, so the test asserted something
// the board could not do. The property is what two ply means: of every legal
// move, it picks one whose worst reply is the best available.
void testTheOpponentMaximisesItsWorstCase() {
  uint32_t seed = 4242u;
  for (int trial = 0; trial < 60; ++trial) {
    Game game{};
    start(game);
    // Walk a few random plies to reach a position with real choices.
    const int depth = static_cast<int>(nextRandom() % 14u) + 2;
    for (int ply = 0; ply < depth && !over(game); ++ply) {
      Move list[kMaxMoves];
      const int count = moves(game, list);
      if (count == 0) break;
      play(game, list[nextRandom() % static_cast<uint32_t>(count)]);
    }
    if (over(game)) continue;

    Move chosen{};
    CHECK(chooseMove(game, chosen));

    const uint8_t seat = game.turn;
    Game played = game;
    CHECK(play(played, chosen));
    const int chosenScore = worstReply(played, seat);

    Move list[kMaxMoves];
    const int count = moves(game, list);
    for (int i = 0; i < count; ++i) {
      Game trial2 = game;
      if (!play(trial2, list[i])) continue;
      // Nothing on the board beats what it picked.
      CHECK(worstReply(trial2, seat) <= chosenScore);
    }
    (void)seed;
  }
}

// Kings alone cannot force a win, so checkers calls it a draw after forty moves
// each with nothing taken and no man advanced. Without this, two deterministic
// opponents played past four thousand plies -- a game that cannot end, on a
// device with a sleep timer.
void testKingsShufflingForeverIsADraw() {
  Game game{};
  clear(game);
  put(game, 0, 0, false, true);
  put(game, 7, 7, true, true);
  game.turn = kLight;

  int plies = 0;
  while (outcome(game) == Outcome::Running) {
    Move list[kMaxMoves];
    const int count = moves(game, list);
    CHECK(count > 0);
    if (count == 0) break;
    CHECK(play(game, list[0]));
    CHECK(++plies <= 200);
    if (plies > 200) break;
  }
  CHECK(outcome(game) == Outcome::Draw);
  CHECK(plies == kIdleLimit);

  // A capture or a man moving resets the clock, so a live game never drifts
  // into a draw it did not earn.
  Game busy{};
  clear(busy);
  put(busy, 0, 0, false, true);
  put(busy, 2, 5, false);
  put(busy, 7, 7, true, true);
  busy.turn = kLight;
  Move list[kMaxMoves];
  moves(busy, list);
  busy.idlePlies = kIdleLimit - 1;
  for (int i = 0; i < kMaxMoves; ++i) {
    const int count = moves(busy, list);
    for (int m = 0; m < count; ++m) {
      if (isKing(busy, list[m].from)) continue;
      CHECK(play(busy, list[m]));
      CHECK(busy.idlePlies == 0);
      return;
    }
    break;
  }
  CHECK(false);
}

}  // namespace

int main() {
  testTheOpeningPositionIsTheStandardOne();
  testACaptureIsMandatoryAndNothingElseIsOffered();
  testAMultiJumpIsOneMoveEndingOnTheFinalSquare();
  testAManPromotesOnTheFarRowAndStops();
  testPromotionEndsAJumpChain();
  testAManCannotStepOrJumpBackwardsButAKingCan();
  testNoMoveIsALossWhetherBlockedOrCaptured();
  testRandomGamesHoldEveryInvariant();
  testBackIsTotalAndAlwaysReachesTheTop();
  testOnlyAPieceWithAMoveCanBePicked();
  testDestinationsMatchWhatCanActuallyBePlayed();
  testPhaseIsDerivedAndOnlyYourTurnAccepts();
  testTheOpponentOnlyEverPlaysALegalMove();
  testTheOpponentIsDeterministicAndPure();
  testTheOpponentMaximisesItsWorstCase();
  testKingsShufflingForeverIsADraw();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
