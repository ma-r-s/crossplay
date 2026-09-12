// The Go rulebook, checked without a panel.
//
// Go has four rules and every one of them is a trap:
//
//  - A liberty is a POINT, not a contact. Counting it once per adjacent stone
//    is the oldest bug in every implementation of this game and it makes big
//    groups immortal.
//  - Suicide is illegal only AFTER captures are resolved, so the move that
//    fills your own last liberty while taking the group around it is legal and
//    is how half of all life-and-death problems are solved.
//  - Ko is not "you may not repeat a position", it is "you may not repeat it
//    NOW", and arming it on any capture rather than on the one shape that can
//    repeat silently forbids legal moves.
//  - Area scoring counts a point only when ONE colour surrounds it, which is
//    what lets a human stop playing before the board is full.
//
// Positions are written as diagrams because a rule bug is a shape, and a shape
// written as a list of indices is a shape nobody can see.

#include <cstdio>
#include <cstring>

#include "GoCore.h"

using namespace go;

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

uint32_t rng = 20260912u;
uint32_t nextRandom() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

// Nine rows of nine characters: '.' empty, 'X' black, 'O' white. Anything else
// is a typo in the test rather than a state, so it fails loudly.
void setUp(Game& game, const char* rows[kSize], const uint8_t toMove = kBlack) {
  reset(game);
  for (int row = 0; row < kSize; ++row) {
    const char* line = rows[row];
    CHECK(std::strlen(line) == static_cast<size_t>(kSize));
    for (int col = 0; col < kSize; ++col) {
      const char cell = line[col];
      const int point = pointAt(row, col);
      if (cell == 'X') {
        game.point[point] = kBlack;
      } else if (cell == 'O') {
        game.point[point] = kWhite;
      } else {
        CHECK(cell == '.');
        game.point[point] = kEmpty;
      }
    }
  }
  game.toMove = toMove;
}

int libertiesOf(const Game& game, const int point) {
  int size = 0;
  int liberties = 0;
  group(game, point, nullptr, size, liberties);
  return liberties;
}

int sizeOf(const Game& game, const int point) {
  int size = 0;
  int liberties = 0;
  group(game, point, nullptr, size, liberties);
  return size;
}

bool samePosition(const Game& a, const Game& b) {
  for (int i = 0; i < kPoints; ++i) {
    if (a.point[i] != b.point[i]) return false;
  }
  return a.toMove == b.toMove;
}

// --- The board itself -------------------------------------------------------

void testNeighboursNeverWrapRoundTheEdge() {
  uint8_t out[4];

  // A corner has two, an edge three, the middle four. The count is the easy
  // half; the point of this test is that the LEFT neighbour of column 0 is not
  // the right-hand end of the row above, which is what index arithmetic with no
  // edge test produces and what makes a game that is subtly not Go.
  CHECK(neighbours(pointAt(0, 0), out) == 2);
  CHECK(neighbours(pointAt(0, 8), out) == 2);
  CHECK(neighbours(pointAt(8, 0), out) == 2);
  CHECK(neighbours(pointAt(8, 8), out) == 2);
  CHECK(neighbours(pointAt(0, 4), out) == 3);
  CHECK(neighbours(pointAt(4, 0), out) == 3);
  CHECK(neighbours(pointAt(4, 4), out) == 4);

  for (int point = 0; point < kPoints; ++point) {
    const int count = neighbours(point, out);
    for (int i = 0; i < count; ++i) {
      const int rowStep = rowOf(out[i]) - rowOf(point);
      const int colStep = colOf(out[i]) - colOf(point);
      // Exactly one step, on exactly one axis. A wrap shows up here as a jump
      // of eight columns.
      CHECK((rowStep == 0 && (colStep == 1 || colStep == -1)) ||
            (colStep == 0 && (rowStep == 1 || rowStep == -1)));
    }
  }
}

void testALibertyIsAPointAndIsCountedOnce() {
  Game game;
  const char* rows[kSize] = {
      ".........",  //
      "..XXX....",  //
      "..X.X....",  //
      "..XXX....",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, rows);

  // Eight stones in a ring. Twelve liberties outside it plus the one in the
  // middle is thirteen. Counting per contact instead of per point gives
  // sixteen, and the ring becomes far harder to kill than it is.
  CHECK(sizeOf(game, pointAt(1, 2)) == 8);
  CHECK(libertiesOf(game, pointAt(1, 2)) == 13);

  // Diagonals do not connect. Four stones around one point are four groups of
  // one, not a group of four, and the point between them is a liberty of each
  // of them separately.
  const char* diagonals[kSize] = {
      ".........",  //
      "...X.....",  //
      "..X.X....",  //
      "...X.....",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, diagonals);
  CHECK(sizeOf(game, pointAt(1, 3)) == 1);
  CHECK(libertiesOf(game, pointAt(1, 3)) == 4);
  CHECK(sizeOf(game, pointAt(2, 2)) == 1);
}

void testAStoneWithNoLibertyIsLifted() {
  Game game;
  const char* rows[kSize] = {
      ".X.......",  //
      "XOX......",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, rows, kBlack);

  CHECK(libertiesOf(game, pointAt(1, 1)) == 1);
  CHECK(play(game, pointAt(2, 1)));
  CHECK(game.point[pointAt(1, 1)] == kEmpty);
  CHECK(game.capturedBy[kBlack] == 1);
  CHECK(game.capturedBy[kWhite] == 0);
}

void testAWholeGroupGoesAtOnce() {
  Game game;
  const char* rows[kSize] = {
      "XX.......",  //
      "OOX......",  //
      "OOX......",  //
      "XX.......",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, rows, kBlack);

  CHECK(sizeOf(game, pointAt(1, 0)) == 4);
  CHECK(libertiesOf(game, pointAt(1, 0)) == 0);
  // Already dead on the diagram, so build the real thing: put the last stone in
  // rather than asserting on a position that could not arise.
  const char* alive[kSize] = {
      "XX.......",  //
      "OOX......",  //
      "OO.......",  //
      "XX.......",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, alive, kBlack);
  CHECK(libertiesOf(game, pointAt(1, 0)) == 1);
  CHECK(play(game, pointAt(2, 2)));
  for (int row = 1; row <= 2; ++row) {
    for (int col = 0; col <= 1; ++col) CHECK(game.point[pointAt(row, col)] == kEmpty);
  }
  CHECK(game.capturedBy[kBlack] == 4);
}

void testSuicideIsIllegalUnlessItCaptures() {
  Game game;
  // The classic: White's single point is surrounded by Black, so Black filling
  // it is suicide.
  const char* rows[kSize] = {
      ".X.......",  //
      "X.X......",  //
      ".X.......",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, rows, kWhite);
  CHECK(!legal(game, pointAt(1, 1), kWhite));
  CHECK(!play(game, pointAt(1, 1)));
  CHECK(game.point[pointAt(1, 1)] == kEmpty);

  // Now the same shape where the move takes the surrounding group first. The
  // stone that would have no liberties has four the instant Black is lifted,
  // and that ordering is the whole of life and death.
  const char* capturing[kSize] = {
      ".XO......",  //
      "X.XO.....",  //
      ".XO......",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, capturing, kWhite);
  // Black's three stones at (0,1) (1,0) (1,2) are three separate groups; the
  // one at (1,2) is in atari with its only liberty at (1,1).
  CHECK(libertiesOf(game, pointAt(1, 2)) == 1);
  CHECK(legal(game, pointAt(1, 1), kWhite));
  CHECK(play(game, pointAt(1, 1)));
  CHECK(game.point[pointAt(1, 2)] == kEmpty);
  CHECK(game.point[pointAt(1, 1)] == kWhite);
}

void testSimpleKoForbidsTheImmediateRecaptureAndOnlyThat() {
  Game game;
  // The textbook ko shape. Black plays (2,3), which is surrounded by four white
  // stones and would be suicide if it did not first take the white stone at
  // (2,2). What is left is a lone black stone with exactly one liberty, and
  // White taking it back would put the board where it was a move ago.
  const char* rows[kSize] = {
      ".........",  //
      "..XO.....",  //
      ".XO.O....",  //
      "..XO.....",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, rows, kBlack);

  CHECK(play(game, pointAt(2, 3)));
  CHECK(game.point[pointAt(2, 2)] == kEmpty);
  CHECK(game.capturedBy[kBlack] == 1);
  CHECK(sizeOf(game, pointAt(2, 3)) == 1);
  CHECK(libertiesOf(game, pointAt(2, 3)) == 1);
  CHECK(game.ko == pointAt(2, 2));
  CHECK(!legal(game, pointAt(2, 2), kWhite));

  // A move elsewhere, a reply elsewhere, and the ko is open again. Ko that
  // never clears is a different game.
  CHECK(play(game, pointAt(7, 7)));
  CHECK(game.ko == kNoPoint);
  CHECK(play(game, pointAt(8, 8)));
  CHECK(legal(game, pointAt(2, 2), kWhite));
}

void testKoDoesNotArmOnAnOrdinaryCapture() {
  Game game;
  // Two stones taken at once cannot be recreated by a single reply, so there is
  // nothing to forbid. Arming ko here would refuse a legal move, which on the
  // panel looks exactly like the board ignoring a tap.
  const char* twoStones[kSize] = {
      "XOO......",  //
      "XXX......",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, twoStones, kBlack);
  CHECK(libertiesOf(game, pointAt(0, 1)) == 1);
  CHECK(play(game, pointAt(0, 3)));
  CHECK(game.point[pointAt(0, 1)] == kEmpty);
  CHECK(game.point[pointAt(0, 2)] == kEmpty);
  CHECK(game.capturedBy[kBlack] == 2);
  CHECK(game.ko == kNoPoint);

  // And the harder half: ONE stone taken, but by a capturing stone that is not
  // itself down to a single liberty, so nothing White can play puts the board
  // back. Ko must not arm here either. Arming on the capture count alone passes
  // the case above and fails this one, and what it costs is a legal White move
  // silently refused -- which on the panel is a tap that does nothing.
  const char* oneStone[kSize] = {
      "XO.......",  //
      "XX.......",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, oneStone, kBlack);
  CHECK(libertiesOf(game, pointAt(0, 1)) == 1);
  CHECK(play(game, pointAt(0, 2)));
  CHECK(game.capturedBy[kBlack] == 1);
  CHECK(sizeOf(game, pointAt(0, 2)) == 1);
  CHECK(libertiesOf(game, pointAt(0, 2)) == 3);
  CHECK(game.ko == kNoPoint);
  // The point just vacated is refused, but for the rules' own reason: three
  // black neighbours make it suicide, not ko. Checking that keeps this test
  // honest -- a refusal is only evidence when you know which rule refused.
  CHECK(libertiesAfter(game, pointAt(0, 1), kWhite) == 0);
  CHECK(legal(game, pointAt(1, 2), kWhite));
  CHECK(legal(game, pointAt(0, 3), kWhite));
}

void testSuperkoRefusesToRecreateAnyRememberedPosition() {
  Game game;
  reset(game);

  // Play a handful of moves, remembering every position by its BOARD rather
  // than by its hash, then assert that no legal move ever reproduces one. This
  // is the property the ring of keys exists to enforce, checked against the
  // thing the keys are a shorthand for -- so a hash that lies fails here.
  Game seen[64];
  int seenCount = 0;
  seen[seenCount++] = game;

  for (int move = 0; move < 40; ++move) {
    int candidates[kPoints + 1];
    int count = 0;
    for (int point = 0; point < kPoints; ++point) {
      if (legal(game, point, game.toMove)) candidates[count++] = point;
    }
    if (count == 0) break;
    const int chosen = candidates[nextRandom() % static_cast<uint32_t>(count)];
    CHECK(play(game, chosen));

    for (int i = 0; i < seenCount; ++i) CHECK(!samePosition(seen[i], game));
    if (seenCount < 64) seen[seenCount++] = game;
  }
}

void testLibertiesAfterAgreesWithActuallyPlayingTheMove() {
  Game game;
  reset(game);

  for (int move = 0; move < 120; ++move) {
    for (int point = 0; point < kPoints; ++point) {
      const int predicted = libertiesAfter(game, point, game.toMove);
      if (game.point[point] != kEmpty) {
        CHECK(predicted == -1);
        continue;
      }
      // Zero predicted liberties is exactly the definition of suicide, so the
      // two answers have to agree about legality as well as about the count.
      if (predicted == 0) CHECK(!legal(game, point, game.toMove));
      if (!legal(game, point, game.toMove)) continue;

      Game copy = game;
      CHECK(play(copy, point));
      CHECK(libertiesOf(copy, point) == predicted);
    }

    int candidates[kPoints];
    int count = 0;
    for (int point = 0; point < kPoints; ++point) {
      if (legal(game, point, game.toMove) && !isEye(game, point, game.toMove)) candidates[count++] = point;
    }
    if (count == 0) break;
    CHECK(play(game, candidates[nextRandom() % static_cast<uint32_t>(count)]));
  }
}

// --- Eyes -------------------------------------------------------------------

void testAnEyeInTheMiddleToleratesOneHostileDiagonalAndAnEdgeEyeNone() {
  Game game;
  // A true eye in the middle: four orthogonal neighbours, all four diagonals
  // friendly.
  const char* trueEye[kSize] = {
      ".........",  //
      "...XXX...",  //
      "...X.X...",  //
      "...XXX...",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, trueEye);
  CHECK(isEye(game, pointAt(2, 4), kBlack));
  CHECK(!isEye(game, pointAt(2, 4), kWhite));

  // One hostile diagonal in the middle is still an eye: the opponent needs both
  // of a diagonal pair to break it.
  const char* oneHostile[kSize] = {
      ".........",  //
      "...OXX...",  //
      "...X.X...",  //
      "...XXX...",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, oneHostile);
  CHECK(isEye(game, pointAt(2, 4), kBlack));

  // Two, and it is false.
  const char* twoHostile[kSize] = {
      ".........",  //
      "...OXO...",  //
      "...X.X...",  //
      "...XXX...",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, twoHostile);
  CHECK(!isEye(game, pointAt(2, 4), kBlack));

  // On the edge there are only two diagonals and ONE hostile stone breaks it.
  // Getting this wrong is how a playout fills a false eye on the second line
  // and kills the group it was keeping alive.
  const char* edgeTrue[kSize] = {
      "..X.X....",  //
      "..XXX....",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, edgeTrue);
  CHECK(isEye(game, pointAt(0, 3), kBlack));

  const char* edgeFalse[kSize] = {
      "..X.X....",  //
      "..XOX....",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, edgeFalse);
  CHECK(!isEye(game, pointAt(0, 3), kBlack));
}

// --- Passing and the end ----------------------------------------------------

void testTwoPassesEndThePlayingPhaseAndOneDoesNot() {
  Game game;
  reset(game);
  CHECK(game.phase == static_cast<uint8_t>(Phase::Playing));

  CHECK(play(game, kPass));
  CHECK(game.passes == 1);
  CHECK(game.phase == static_cast<uint8_t>(Phase::Playing));
  CHECK(game.toMove == kWhite);

  CHECK(play(game, pointAt(4, 4)));
  CHECK(game.passes == 0);

  CHECK(play(game, kPass));
  CHECK(play(game, kPass));
  CHECK(game.phase == static_cast<uint8_t>(Phase::Scoring));
  // Scoring is not Playing: no further stone goes down by accident.
  CHECK(!play(game, pointAt(0, 0)));
}

void testAPassIsAlwaysLegalEvenOnAFullBoard() {
  Game game;
  reset(game);
  CHECK(legal(game, kPass, kBlack));
  for (int i = 0; i < kPoints; ++i) game.point[i] = kBlack;
  CHECK(legal(game, kPass, kWhite));
}

// --- Scoring ----------------------------------------------------------------

void testAreaScoringCountsStonesPlusSoleSurroundedPoints() {
  Game game;
  // A wall down the middle. Black owns the left, White the right, and the two
  // points on the wall's own column belong to whoever stands there.
  const char* rows[kSize] = {
      "....XO...",  //
      "....XO...",  //
      "....XO...",  //
      "....XO...",  //
      "....XO...",  //
      "....XO...",  //
      "....XO...",  //
      "....XO...",  //
      "....XO...",  //
  };
  setUp(game, rows);
  game.phase = static_cast<uint8_t>(Phase::Over);

  uint8_t owner[kPoints];
  territory(game, owner);
  for (int row = 0; row < kSize; ++row) {
    for (int col = 0; col < 5; ++col) CHECK(owner[pointAt(row, col)] == kBlack);
    for (int col = 5; col < kSize; ++col) CHECK(owner[pointAt(row, col)] == kWhite);
  }

  const Score counted = score(game);
  CHECK(counted.blackHalves == 45 * 2);
  CHECK(counted.whiteHalves == 36 * 2 + kKomiHalves);
  CHECK(outcome(game) == Outcome::BlackWins);
  CHECK(marginHalves(game) == 45 * 2 - (36 * 2 + kKomiHalves));
}

void testAPointBothColoursReachCountsForNobody() {
  Game game;
  // Two stones far apart on an otherwise empty board. Every empty point is
  // reachable from both, so the whole board is neutral and the score is one
  // stone each plus komi. This is the rule that lets a human stop playing.
  const char* rows[kSize] = {
      "X........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      "........O",  //
  };
  setUp(game, rows);
  game.phase = static_cast<uint8_t>(Phase::Over);

  uint8_t owner[kPoints];
  territory(game, owner);
  int neutral = 0;
  for (int i = 0; i < kPoints; ++i) {
    if (owner[i] == kEmpty) ++neutral;
  }
  CHECK(neutral == kPoints - 2);

  const Score counted = score(game);
  CHECK(counted.blackHalves == 2);
  CHECK(counted.whiteHalves == 2 + kKomiHalves);
  CHECK(outcome(game) == Outcome::WhiteWins);
}

void testADeadStoneIsWorthTwoPointsToItsCaptor() {
  Game game;
  // One white stone in the corner with Black all round it. Alive it is a point
  // of White's area; dead it is a point of Black's. That is a two point swing
  // on one stone, which is why agreeing the dead stones IS the endgame and why
  // it cannot be a detail the app decides quietly on the players' behalf.
  const char* rows[kSize] = {
      "OX.......",  //
      "XX.......",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(game, rows);
  game.phase = static_cast<uint8_t>(Phase::Over);

  const Score alive = score(game);
  CHECK(alive.blackHalves == 80 * 2);
  CHECK(alive.whiteHalves == 1 * 2 + kKomiHalves);

  mark(game.dead, pointAt(0, 0));
  const Score dead = score(game);
  CHECK(dead.blackHalves == 81 * 2);
  CHECK(dead.whiteHalves == 0 + kKomiHalves);

  CHECK(dead.blackHalves - alive.blackHalves == 1 * 2);
  CHECK(alive.whiteHalves - dead.whiteHalves == 1 * 2);
}

void testKomiGoesToWhiteAndNoGameCanTie() {
  Game game;
  reset(game);
  game.phase = static_cast<uint8_t>(Phase::Over);

  // An empty board: every point is neutral, so the only score is komi.
  const Score counted = score(game);
  CHECK(counted.blackHalves == 0);
  CHECK(counted.whiteHalves == kKomiHalves);

  // Komi is an ODD number of half points, so White's total is odd and Black's
  // is even however the stones fall. They cannot come out level, and the draw
  // screen this game would otherwise need does not have to exist. A flat 7.0
  // komi would tie on a 44/37 split, which is an ordinary result.
  CHECK(kKomiHalves % 2 == 1);
  for (int black = 0; black <= kPoints; ++black) {
    const int white = kPoints - black;
    CHECK(black * 2 != white * 2 + kKomiHalves);
  }
}

// --- The whole thing --------------------------------------------------------

void testRandomGamesFinishAndHoldEveryInvariant() {
  for (int trial = 0; trial < 200; ++trial) {
    Game game;
    reset(game);

    int plies = 0;
    while (game.phase == static_cast<uint8_t>(Phase::Playing) && plies < 400) {
      int candidates[kPoints];
      int count = 0;
      for (int point = 0; point < kPoints; ++point) {
        // Refusing to fill one's own eyes is not a rule; it is the one thing a
        // random player must be told or the game never ends, because filling an
        // eye is always legal and always kills the group.
        if (legal(game, point, game.toMove) && !isEye(game, point, game.toMove)) candidates[count++] = point;
      }
      const uint8_t mover = game.toMove;
      if (count == 0) {
        CHECK(play(game, kPass));
      } else {
        const int chosen = candidates[nextRandom() % static_cast<uint32_t>(count)];
        CHECK(play(game, chosen));
        CHECK(game.point[chosen] == mover);
      }
      CHECK(game.toMove == other(mover));

      // No stone ever stands without a liberty.
      for (int point = 0; point < kPoints; ++point) {
        if (isStone(game.point[point])) CHECK(libertiesOf(game, point) > 0);
      }
      ++plies;
    }

    // A game of Go on a finite board under superko terminates. If this ever
    // trips, the eye rule or the pass rule is wrong, not the board size.
    CHECK(game.phase == static_cast<uint8_t>(Phase::Scoring));

    game.phase = static_cast<uint8_t>(Phase::Over);
    const Score counted = score(game);
    // Area scoring: every point is counted at most once, and the two areas plus
    // the neutral points are the whole board.
    uint8_t owner[kPoints];
    territory(game, owner);
    int black = 0;
    int white = 0;
    int neutral = 0;
    for (int i = 0; i < kPoints; ++i) {
      if (owner[i] == kBlack) ++black;
      if (owner[i] == kWhite) ++white;
      if (owner[i] == kEmpty) ++neutral;
    }
    CHECK(black + white + neutral == kPoints);
    CHECK(counted.blackHalves == black * 2);
    CHECK(counted.whiteHalves == white * 2 + kKomiHalves);
    CHECK(outcome(game) != Outcome::Running);
  }
}

void testTheStateFitsAPacketAndCopiesAsBytes() {
  // The link layer takes a trivially copyable state of at most 192 bytes, and
  // this struct is also the save payload. Both facts are checked here rather
  // than discovered at the point a field is added.
  CHECK(sizeof(Game) <= 192);
  CHECK(__is_trivially_copyable(Game));

  Game game;
  reset(game);
  CHECK(play(game, pointAt(4, 4)));
  Game copy;
  std::memcpy(&copy, &game, sizeof(Game));
  CHECK(samePosition(copy, game));
  CHECK(copy.lastMove == game.lastMove);
  CHECK(copy.moveNumber == game.moveNumber);
}

}  // namespace

int main() {
  testNeighboursNeverWrapRoundTheEdge();
  testALibertyIsAPointAndIsCountedOnce();
  testAStoneWithNoLibertyIsLifted();
  testAWholeGroupGoesAtOnce();
  testSuicideIsIllegalUnlessItCaptures();
  testSimpleKoForbidsTheImmediateRecaptureAndOnlyThat();
  testKoDoesNotArmOnAnOrdinaryCapture();
  testSuperkoRefusesToRecreateAnyRememberedPosition();
  testLibertiesAfterAgreesWithActuallyPlayingTheMove();
  testAnEyeInTheMiddleToleratesOneHostileDiagonalAndAnEdgeEyeNone();
  testTwoPassesEndThePlayingPhaseAndOneDoesNot();
  testAPassIsAlwaysLegalEvenOnAFullBoard();
  testAreaScoringCountsStonesPlusSoleSurroundedPoints();
  testAPointBothColoursReachCountsForNobody();
  testADeadStoneIsWorthTwoPointsToItsCaptor();
  testKomiGoesToWhiteAndNoGameCanTie();
  testRandomGamesFinishAndHoldEveryInvariant();
  testTheStateFitsAPacketAndCopiesAsBytes();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
