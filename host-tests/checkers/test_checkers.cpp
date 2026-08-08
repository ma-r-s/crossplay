// The Checkers rulebook, checked without a panel.
//
// The rule under most scrutiny is the mandatory capture, because it is BUILT IN
// rather than validated: moves() returns jumps only when a jump exists, so an
// illegal quiet move is not something the core can express. A test has to hold
// that claim to account rather than restate it.

#include <cstdio>
#include <cstring>

#include "CheckersCore.h"

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
  game.pad0 = 0;
  game.pad1 = 0;
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
  CHECK(winner(empty) == kDarkSeat);

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
  CHECK(winner(blocked) == kDarkSeat);
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

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
