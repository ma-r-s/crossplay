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
  for (int i = 0; i < 7; ++i) CHECK(takenCountOf(list[i]) == 0);
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
    CHECK(takenCountOf(list[i]) > 0);
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
  CHECK(takenCountOf(list[0]) == 2);
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
  CHECK(takenCountOf(list[0]) == 1);
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
        if (takenCountOf(list[i]) > 0) ++jumps;
      }
      CHECK(jumps == 0 || jumps == count);

      const uint8_t mover = game.turn;
      const int before = pieceCount(game, mover == kLight ? kDarkSeat : kLight);
      const Move& chosen = list[nextRandom() % static_cast<uint32_t>(count)];
      const int taken = takenCountOf(chosen);
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
    uint64_t taken[kMaxMoves];
    const int found = destinations(game, square, squares, taken, kMaxMoves);
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
      // The capture mask travels with the destination, so the screen marks the
      // right victims without recomputing the rules.
      CHECK(move.taken == taken[d]);
    }
  }
  // A destination the rules never offered cannot be built at the boundary.
  Move bogus{};
  CHECK(!moveBetween(game, indexOf(0, 5), indexOf(4, 1), bogus));
}

// The screen's two board hints must agree with what the board accepts, or a
// player is told a piece can move and then finds it will not lift.
void testMovableSetAndMustTakeAgreeWithCanPick() {
  Game game{};
  start(game);
  uint32_t rng = 0x51ED270Bu;

  for (int ply = 0; ply < 400 && !over(game); ++ply) {
    const uint64_t mask = movableSquares(game);
    int marked = 0;
    for (int square = 0; square < kCells; ++square) {
      const bool inMask = (mask & (static_cast<uint64_t>(1) << square)) != 0;
      CHECK(inMask == canPick(game, square));
      if (inMask) ++marked;
    }
    CHECK(marked > 0);

    Move list[kMaxMoves];
    const int count = moves(game, list);
    // captureAvailable is a promise about EVERY legal move, not just the first.
    const bool must = captureAvailable(game);
    for (int i = 0; i < count; ++i) CHECK((list[i].taken != 0) == must);

    // And it is only false when no capture exists anywhere for this side.
    if (!must) {
      for (int i = 0; i < count; ++i) CHECK(list[i].taken == 0);
    }

    rng = rng * 1664525u + 1013904223u;
    play(game, list[rng % static_cast<uint32_t>(count)]);
  }
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

// A multi-jump can take far more than three men -- nine is the real maximum in
// English draughts -- and the first version capped the array at three, recorded
// the three-capture PREFIX as a completed move, and handed over the turn. That
// is a half-finished jump, the exact thing this core claims cannot exist.
void testALongChainIsNotTruncated() {
  Game game{};
  clear(game);
  put(game, 1, 0, false, true);  // a light king with a long road home
  put(game, 2, 1, true);
  put(game, 4, 1, true);
  put(game, 6, 1, true);
  put(game, 2, 3, true);
  put(game, 4, 3, true);
  put(game, 6, 3, true);
  put(game, 2, 5, true);
  put(game, 4, 5, true);
  put(game, 6, 5, true);
  game.turn = kLight;

  Move list[kMaxMoves];
  const int count = moves(game, list);
  CHECK(count > 0);

  int longest = 0;
  for (int i = 0; i < count; ++i) {
    const int taken = takenCountOf(list[i]);
    if (taken > longest) longest = taken;
  }
  // Nine, not three. The exact number matters: a cap of any size records a
  // prefix, and a prefix is an illegal position.
  CHECK(longest == 9);

  // And every move offered must be COMPLETE: after playing it, the piece that
  // moved has no further jump available from where it landed.
  for (int i = 0; i < count; ++i) {
    Game after = game;
    CHECK(play(after, list[i]));
    // It is now the opponent's turn, so check the mover's side directly.
    after.turn = kLight;
    Move follow[kMaxMoves];
    const int followCount = moves(after, follow);
    for (int f = 0; f < followCount; ++f) {
      if (follow[f].from == list[i].to) CHECK(follow[f].taken == 0);
    }
  }
}

// A four-capture cycle returns a king to the square it started on. play() used
// to write the piece to `to` and then clear `from` -- the same square -- which
// deleted the piece that had just moved.
void testAMoveThatEndsWhereItStartedKeepsItsPiece() {
  Game game{};
  clear(game);
  put(game, 2, 3, false, true);
  put(game, 3, 2, true);
  put(game, 3, 4, true);
  put(game, 5, 2, true);
  put(game, 5, 4, true);
  game.turn = kLight;

  Move list[kMaxMoves];
  const int count = moves(game, list);
  bool sawCycle = false;
  for (int i = 0; i < count; ++i) {
    if (list[i].to != list[i].from) continue;
    sawCycle = true;
    Game after = game;
    CHECK(play(after, list[i]));
    CHECK(occupied(after, indexOf(2, 3)));
    CHECK(pieceCount(after, kLight) == 1);
    CHECK(pieceCount(after, kDarkSeat) == 0);
  }
  CHECK(sawCycle);
}

// The opponent had no behavioural test at all: negating its evaluation took it
// from 200-0 against a random mover to 0-195, and the suite stayed green.
void testTheOpponentBeatsARandomMoverConvincingly() {
  int brainWins = 0;
  int randomWins = 0;
  for (int match = 0; match < 60; ++match) {
    Game game{};
    start(game);
    // The brain plays light in half the games and dark in the other half, so a
    // sign error in either direction shows up.
    const uint8_t brainSeat = (match % 2 == 0) ? kLight : kDarkSeat;
    int plies = 0;
    while (outcome(game) == Outcome::Running) {
      Move chosen{};
      if (game.turn == brainSeat) {
        if (!chooseMove(game, chosen)) break;
      } else {
        Move list[kMaxMoves];
        const int count = moves(game, list);
        if (count == 0) break;
        chosen = list[nextRandom() % static_cast<uint32_t>(count)];
      }
      if (!play(game, chosen)) break;
      if (++plies > 400) break;
    }
    const Outcome result = outcome(game);
    const bool brainWon = (brainSeat == kLight && result == Outcome::LightWins) ||
                          (brainSeat == kDarkSeat && result == Outcome::DarkWins);
    const bool randomWon = (brainSeat == kLight && result == Outcome::DarkWins) ||
                           (brainSeat == kDarkSeat && result == Outcome::LightWins);
    if (brainWon) ++brainWins;
    if (randomWon) ++randomWins;
  }
  // Deterministic inputs, so this is a fixed number rather than a flaky one.
  // An opponent that does not thrash a random mover is not an opponent.
  CHECK(brainWins > randomWins * 4);
  std::printf("  brain vs random: %d - %d of 60\n", brainWins, randomWins);
}

// The draw constants were unpinned: 80 -> 40 and 80 -> 150 both survived,
// because the only assertion compared plies against the constant under test.
void testTheDrawRuleUsesFortyMovesEach() {
  CHECK(kIdleLimit == 80);

  Game game{};
  clear(game);
  put(game, 0, 0, false, true);
  put(game, 7, 7, true, true);
  game.turn = kLight;
  int plies = 0;
  while (outcome(game) == Outcome::Running) {
    Move list[kMaxMoves];
    const int count = moves(game, list);
    if (count == 0) break;
    CHECK(play(game, list[0]));
    if (++plies > 200) break;
  }
  CHECK(plies == 80);

  // A CAPTURE resets the clock, not only a man moving. That arm was untested.
  Game busy{};
  clear(busy);
  put(busy, 2, 3, false, true);
  put(busy, 3, 4, true);
  busy.turn = kLight;
  busy.idlePlies = kIdleLimit - 2;
  Move list[kMaxMoves];
  const int count = moves(busy, list);
  CHECK(count > 0);
  CHECK(takenCountOf(list[0]) > 0);
  CHECK(play(busy, list[0]));
  CHECK(busy.idlePlies == 0);
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
  testMovableSetAndMustTakeAgreeWithCanPick();
  testPhaseIsDerivedAndOnlyYourTurnAccepts();
  testTheOpponentOnlyEverPlaysALegalMove();
  testTheOpponentIsDeterministicAndPure();
  testTheOpponentMaximisesItsWorstCase();
  testKingsShufflingForeverIsADraw();
  testALongChainIsNotTruncated();
  testAMoveThatEndsWhereItStartedKeepsItsPiece();
  testTheOpponentBeatsARandomMoverConvincingly();
  testTheDrawRuleUsesFortyMovesEach();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
