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
#include "GoEngine.h"
#include "GoFlow.h"
#include "GoSave.h"

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
      CHECK((rowStep == 0 && (colStep == 1 || colStep == -1)) || (colStep == 0 && (rowStep == 1 || rowStep == -1)));
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
  CHECK(game.stage == static_cast<uint8_t>(Stage::Playing));

  CHECK(play(game, kPass));
  CHECK(game.passes == 1);
  CHECK(game.stage == static_cast<uint8_t>(Stage::Playing));
  CHECK(game.toMove == kWhite);

  CHECK(play(game, pointAt(4, 4)));
  CHECK(game.passes == 0);

  CHECK(play(game, kPass));
  CHECK(play(game, kPass));
  CHECK(game.stage == static_cast<uint8_t>(Stage::Scoring));
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
  game.stage = static_cast<uint8_t>(Stage::Over);

  uint8_t owner[kPoints];
  territory(game, owner);
  for (int row = 0; row < kSize; ++row) {
    for (int col = 0; col < 5; ++col) CHECK(owner[pointAt(row, col)] == kBlack);
    for (int col = 5; col < kSize; ++col) CHECK(owner[pointAt(row, col)] == kWhite);
  }

  const Score counted = score(game);
  CHECK(counted.blackHalves == 45 * 2);
  CHECK(counted.whiteHalves == 36 * 2 + kDefaultKomiHalves);
  CHECK(outcome(game) == Outcome::BlackWins);
  CHECK(marginHalves(game) == 45 * 2 - (36 * 2 + kDefaultKomiHalves));
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
  game.stage = static_cast<uint8_t>(Stage::Over);

  uint8_t owner[kPoints];
  territory(game, owner);
  int neutral = 0;
  for (int i = 0; i < kPoints; ++i) {
    if (owner[i] == kEmpty) ++neutral;
  }
  CHECK(neutral == kPoints - 2);

  const Score counted = score(game);
  CHECK(counted.blackHalves == 2);
  CHECK(counted.whiteHalves == 2 + kDefaultKomiHalves);
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
  game.stage = static_cast<uint8_t>(Stage::Over);

  const Score alive = score(game);
  CHECK(alive.blackHalves == 80 * 2);
  CHECK(alive.whiteHalves == 1 * 2 + kDefaultKomiHalves);

  mark(game.dead, pointAt(0, 0));
  const Score dead = score(game);
  CHECK(dead.blackHalves == 81 * 2);
  CHECK(dead.whiteHalves == 0 + kDefaultKomiHalves);

  CHECK(dead.blackHalves - alive.blackHalves == 1 * 2);
  CHECK(alive.whiteHalves - dead.whiteHalves == 1 * 2);
}

void testKomiGoesToWhiteAndNoGameCanTie() {
  Game game;
  reset(game);
  game.stage = static_cast<uint8_t>(Stage::Over);

  // An empty board: every point is neutral, so the only score is komi.
  const Score counted = score(game);
  CHECK(counted.blackHalves == 0);
  CHECK(counted.whiteHalves == kDefaultKomiHalves);

  // Komi is an ODD number of half points, so White's total is odd and Black's
  // is even however the stones fall. They cannot come out level, and the draw
  // screen this game would otherwise need does not have to exist. A flat 7.0
  // komi would tie on a 44/37 split, which is an ordinary result.
  CHECK(kDefaultKomiHalves % 2 == 1);
  for (int black = 0; black <= kPoints; ++black) {
    const int white = kPoints - black;
    CHECK(black * 2 != white * 2 + kDefaultKomiHalves);
  }
}

// --- The whole thing --------------------------------------------------------

void testNoGameCanRunForever() {
  // The house limit, and the reason it exists: the superko ring remembers eight
  // positions, not every one, so a long cycle is not forbidden by the rules as
  // implemented -- and the opponent will not pass out of one while it is losing.
  Game game;
  reset(game);
  int plies = 0;
  while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(kMoveLimit) + 50) {
    // Two passes would end it honestly, so this drives it the other way: always
    // a stone while a stone is legal, which is what a losing engine does.
    int played = kNoPoint;
    for (int point = 0; point < kPoints && played == kNoPoint; ++point) {
      if (legal(game, point, game.toMove) && play(game, point)) played = point;
    }
    if (played == kNoPoint) CHECK(play(game, kPass));
    ++plies;
  }
  CHECK(game.stage != static_cast<uint8_t>(Stage::Playing));
  CHECK(game.moveNumber <= kMoveLimit);
}

void testRandomGamesFinishAndHoldEveryInvariant() {
  for (int trial = 0; trial < 200; ++trial) {
    Game game;
    reset(game);

    int plies = 0;
    while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(kMoveLimit) + 4) {
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
    CHECK(game.stage == static_cast<uint8_t>(Stage::Scoring));

    game.stage = static_cast<uint8_t>(Stage::Over);
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
    CHECK(counted.whiteHalves == white * 2 + kDefaultKomiHalves);
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

// --- The second board ------------------------------------------------------

void testTheFastBoardIsTheSameGame() {
  // The search plays on its own board. Two implementations of one rulebook is
  // the shape that drifts, and nothing about writing them carefully prevents
  // it: what prevents it is playing hundreds of thousands of positions through
  // both and asserting the results are identical.
  //
  // Legality is compared where the two are allowed to agree. The fast board
  // knows SIMPLE ko and the game knows superko, so a move the game refuses for
  // repetition is one the fast board may legally accept; that difference is
  // asserted to be the ONLY one, which is what pins it as a decision rather
  // than a bug.
  int checked = 0;
  int superkoOnly = 0;
  for (int trial = 0; trial < 200; ++trial) {
    Game game;
    reset(game);

    for (int ply = 0; ply < 200 && game.stage == static_cast<uint8_t>(Stage::Playing); ++ply) {
      for (int point = 0; point < kPoints; ++point) {
        Game slow = game;
        Game fast = game;
        const bool slowOk = play(slow, point);
        const bool fastOk = goengine::fastPlayForTest(fast, point);
        ++checked;

        if (slowOk != fastOk) {
          // The only licensed disagreement: the game refused a repetition the
          // fast board cannot see. Anything else is a rules bug in one of them.
          CHECK(fastOk && !slowOk);
          CHECK(game.point[point] == kEmpty);
          CHECK(point != game.ko);
          CHECK(libertiesAfter(game, point, game.toMove) > 0);
          ++superkoOnly;
          continue;
        }
        if (!slowOk) continue;

        for (int i = 0; i < kPoints; ++i) CHECK(slow.point[i] == fast.point[i]);
        CHECK(slow.toMove == fast.toMove);
        CHECK(slow.ko == fast.ko);
      }

      int candidates[kPoints];
      int count = 0;
      for (int point = 0; point < kPoints; ++point) {
        if (legal(game, point, game.toMove) && !isEye(game, point, game.toMove)) candidates[count++] = point;
      }
      if (count == 0) {
        CHECK(play(game, kPass));
        continue;
      }
      CHECK(play(game, candidates[nextRandom() % static_cast<uint32_t>(count)]));
    }
  }
  // The comparison has to have actually happened. A loop that exits on its
  // first iteration passes every assertion inside it.
  CHECK(checked > 500000);
  std::printf("  fast board: %d positions compared, %d superko-only differences\n", checked, superkoOnly);
}

// Kept deliberately small, and the reason is worth writing down: this suite
// runs on every gate and in CI, and a whole game at the Hard level is eight
// thousand playouts a move for a hundred moves. The first version ran thirty of
// them and took eight and a half MINUTES, which is longer than every other
// suite in this repository put together.
//
// What these assertions catch is an engine that is BROKEN -- illegal moves,
// games that never end, a level that lost its evaluation. None of that needs
// thirty games to show up. How STRONG it is is measured against GNU Go offline,
// which is where a number that needs sixty games belongs.
void testTheOpponentOnlyEverPlaysALegalMove() {
  for (int trial = 0; trial < 6; ++trial) {
    Game game;
    reset(game);
    uint32_t seed = 4242u + static_cast<uint32_t>(trial) * 97u;
    const go::Level level = static_cast<go::Level>(trial % 3);
    int plies = 0;
    while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(kMoveLimit) + 4) {
      const int move = goengine::chooseMove(game, level, seed);
      CHECK(move == kPass || legal(game, move, game.toMove));
      CHECK(play(game, move));
      ++plies;
    }
    CHECK(game.stage == static_cast<uint8_t>(Stage::Scoring));
  }
}

void testTheOpponentBeatsARandomMoverAtEveryLevel() {
  // Legal and terminating were both true of an engine whose evaluation was
  // NEGATED, and the suite stayed green. The only assertion that catches that
  // is one about the RESULT.
  for (int levelIndex = 0; levelIndex < 3; ++levelIndex) {
    const go::Level level = static_cast<go::Level>(levelIndex);
    int engineWins = 0;
    constexpr int kGames = 4;
    for (int trial = 0; trial < kGames; ++trial) {
      Game game;
      reset(game);
      uint32_t seed = 31337u + static_cast<uint32_t>(trial) * 131u;
      // Alternating seats, so a level that only ever wins as Black is caught.
      const uint8_t engineSeat = (trial % 2 == 0) ? kBlack : kWhite;
      int plies = 0;
      while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(kMoveLimit) + 4) {
        int move = kPass;
        if (game.toMove == engineSeat) {
          move = goengine::chooseMove(game, level, seed);
        } else {
          int candidates[kPoints];
          int count = 0;
          for (int point = 0; point < kPoints; ++point) {
            if (legal(game, point, game.toMove) && !isEye(game, point, game.toMove)) candidates[count++] = point;
          }
          if (count > 0) move = candidates[nextRandom() % static_cast<uint32_t>(count)];
        }
        CHECK(play(game, move));
        ++plies;
      }
      game.stage = static_cast<uint8_t>(Stage::Over);
      const Score counted = score(game);
      const uint8_t winner = counted.blackHalves > counted.whiteHalves ? kBlack : kWhite;
      if (winner == engineSeat) ++engineWins;
    }
    std::printf("  level %d beat a random mover %d-%d\n", levelIndex, engineWins, kGames - engineWins);
    // Even the easy level throws away only a third of its moves, so anything
    // below a clean sweep against a player with no idea at all is a bug.
    CHECK(engineWins >= kGames - 1);
  }
}

void testEveryLevelIsADifferentPlayer() {
  // Three levels that differ only in how long they take are not three levels:
  // the whole playout range this device can reach is about three and a half
  // ranks, so thinking time alone cannot make a level a beginner beats.
  const goengine::Settings easy = goengine::settingsFor(go::Level::Easy);
  const goengine::Settings medium = goengine::settingsFor(go::Level::Medium);
  const goengine::Settings hard = goengine::settingsFor(go::Level::Hard);

  CHECK(easy.playouts < medium.playouts);
  CHECK(medium.playouts < hard.playouts);
  // Easy is the only level that is spotted stones and the only one that is
  // blind. Medium and Hard play the same opening and differ in how thoroughly
  // and how decisively they search.
  CHECK(easy.handicap >= 2);
  CHECK(medium.handicap == 0 && hard.handicap == 0);
  CHECK(easy.blindPerMille > 0);
  CHECK(medium.blindPerMille == 0 && hard.blindPerMille == 0);
  // Easy's komi is smaller as well as its handicap larger: both point the same
  // way, so the two knobs cannot cancel.
  CHECK(easy.komiHalves < medium.komiHalves);

  // Every komi this app can set settles the game. A draw screen does not exist
  // and must never become necessary.
  CHECK(settlesEveryGame(easy.komiHalves));
  CHECK(settlesEveryGame(medium.komiHalves));
  CHECK(settlesEveryGame(hard.komiHalves));

  // And the opening the activity is handed matches the level it asked for.
  for (int i = 0; i < 3; ++i) {
    const go::Level level = static_cast<go::Level>(i);
    int handicap = -1;
    int16_t komi = 0;
    goengine::openingFor(level, handicap, komi);
    CHECK(handicap == goengine::settingsFor(level).handicap);
    CHECK(komi == goengine::settingsFor(level).komiHalves);
  }
}

void testAHandicapIsStonesOnTheBoardAndWhiteToPlay() {
  for (int stones = 2; stones <= kMaxHandicap; ++stones) {
    Game game;
    reset(game, stones, 1);
    int placed = 0;
    for (int point = 0; point < kPoints; ++point) {
      if (game.point[point] == kBlack) ++placed;
      CHECK(game.point[point] != kWhite);
    }
    CHECK(placed == stones);
    CHECK(game.handicap == stones);
    // White moves first. A handicap where Black also opened would be a stone
    // and a half, which is not a rung on any ladder.
    CHECK(game.toMove == kWhite);
    CHECK(game.moveNumber == 0);
    CHECK(game.lastMove == kNoPoint);
    CHECK(game.komiHalves == 1);

    // The stones are on star points, and no two on the same one.
    uint8_t where[kMaxHandicap];
    CHECK(handicapPoints(stones, where) == stones);
    for (int i = 0; i < stones; ++i) {
      CHECK(game.point[where[i]] == kBlack);
      for (int j = i + 1; j < stones; ++j) CHECK(where[i] != where[j]);
    }
    // The first two are opposite corners, or a two-stone game is lopsided.
    CHECK(rowOf(where[0]) + rowOf(where[1]) == kSize - 1);
    CHECK(colOf(where[0]) + colOf(where[1]) == kSize - 1);
  }

  // One stone is not a handicap, it is the even game.
  Game even;
  reset(even, 1);
  CHECK(even.handicap == 0);
  CHECK(even.toMove == kBlack);

  // And the stones are worth what they are supposed to be worth: four stones
  // and a smaller komi has to leave Black ahead on an empty-ish board.
  Game spotted;
  reset(spotted, 4, 1);
  spotted.stage = static_cast<uint8_t>(Stage::Over);
  Game level;
  reset(level, 0, kDefaultKomiHalves);
  level.stage = static_cast<uint8_t>(Stage::Over);
  const Score withStones = score(spotted);
  const Score without = score(level);
  CHECK(withStones.blackHalves - withStones.whiteHalves > without.blackHalves - without.whiteHalves);
}

void testItNeverPassesAWonGameAway() {
  // The Leela Zero rule, and the loudest way a Go program can look broken.
  //
  // A board where Black plainly leads: passing wins for Black and loses for
  // White, so the engine must be willing to pass as Black and must not as
  // White, whatever its search happened to like.
  Game game;
  const char* rows[kSize] = {
      "XXXXXXXXX",  //
      "XXXXXXXXX",  //
      "XXXXXXXXX",  //
      "XXXXXXXXX",  //
      "XXXXXXXXX",  //
      "XXXXXXXXX",  //
      ".........",  //
      ".........",  //
      "OOOOOOOOO",  //
  };
  setUp(game, rows, kBlack);
  CHECK(goengine::passingWins(game, kBlack));
  CHECK(!goengine::passingWins(game, kWhite));

  // Now from White's seat, with White to move: the search may well think the
  // position is over, and passing would hand Black the game.
  game.toMove = kWhite;
  uint32_t seed = 6060u;
  for (int trial = 0; trial < 3; ++trial) {
    const int move = goengine::chooseMove(game, go::Level::Hard, seed);
    CHECK(move != kPass);
  }
}

void testEasyMissesThingsWithoutLookingBroken() {
  // Easy is meant to miss what it did not look at. It is NOT meant to fill its
  // own eye or to pass a game it is winning: both read as a fault rather than as
  // a weak player, and the whole reason the weakening is blindness rather than
  // blunder injection is that every move it plays is one it considered.
  for (int trial = 0; trial < 5; ++trial) {
    Game game;
    int handicap = 0;
    int16_t komi = kDefaultKomiHalves;
    goengine::openingFor(go::Level::Easy, handicap, komi);
    reset(game, handicap, komi);
    uint32_t seed = 8080u + static_cast<uint32_t>(trial) * 17u;
    int plies = 0;
    while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(kMoveLimit) + 4) {
      const uint8_t mover = game.toMove;
      const int move = goengine::chooseMove(game, go::Level::Easy, seed);
      if (move == kPass) {
        // Passing is allowed when it wins, and when there is nothing else left
        // to play. Those are the only two, and "nothing else" is the position
        // every game ends in.
        CHECK(goengine::passingWins(game, mover) || !go::hasUsefulMove(game, mover));
      } else {
        CHECK(!isEye(game, move, mover));
        CHECK(legal(game, move, mover));
      }
      CHECK(play(game, move));
      ++plies;
    }
  }
}

// --- Navigation and what is written down -----------------------------------

void testBackIsTotalAndAlwaysReachesTheTop() {
  const go::Screen screens[] = {go::Screen::Menu, go::Screen::Settings, go::Screen::Board, go::Screen::Count,
                                go::Screen::Result};
  for (const go::Screen start : screens) {
    go::Screen at = start;
    int steps = 0;
    while (!go::leavesApp(at) && steps < 8) {
      const go::Screen next = go::back(at);
      CHECK(next != at);
      at = next;
      ++steps;
    }
    CHECK(go::leavesApp(at));
  }
  // Exactly one screen leaves the app. Two exits is how a player ends up on
  // Home when they meant to stop playing.
  int exits = 0;
  for (const go::Screen s : screens) {
    if (go::leavesApp(s)) ++exits;
  }
  CHECK(exits == 1);
}

void testASavedGameComesBackExactly() {
  Game game;
  // A handicap game with a level's own komi, because those two were the fields
  // the first version of this test did not name and the first version of pack()
  // did not write. A round-trip test that enumerates fields BY HAND cannot see
  // a field nobody wrote, which is why the whole-struct comparison below
  // matters more than the named ones.
  reset(game, 2, 1);
  uint32_t local = 5150u;
  for (int i = 0; i < 40; ++i) {
    int candidates[kPoints];
    int count = 0;
    for (int point = 0; point < kPoints; ++point) {
      if (legal(game, point, game.toMove) && !isEye(game, point, game.toMove)) candidates[count++] = point;
    }
    if (count == 0) break;
    local ^= local << 13;
    local ^= local >> 17;
    local ^= local << 5;
    CHECK(play(game, candidates[local % static_cast<uint32_t>(count)]));
  }
  mark(game.dead, 7);
  mark(game.dead, 80);
  accept(game, kBlack);

  gosave::Save save;
  save.wins = 11;
  save.losses = 4;
  save.hasHistory = true;
  save.lastWon = true;
  save.lastMarginHalves = 13;
  for (int i = 0; i < kPoints; ++i) save.lastPoints[i] = static_cast<uint8_t>(i % 3);
  save.opponent = go::Opponent::Human;
  save.level = go::Level::Hard;
  save.playAs = kWhite;
  save.inProgress = true;
  save.game = game;
  save.seat = kWhite;

  char line[1400];
  const int bytes = gosave::pack(save, line, sizeof(line));
  CHECK(bytes > 0);

  gosave::Save back;
  CHECK(gosave::unpack(line, back));
  CHECK(back.wins == 11);
  CHECK(back.losses == 4);
  CHECK(back.lastMarginHalves == 13);
  CHECK(back.opponent == go::Opponent::Human);
  CHECK(back.level == go::Level::Hard);
  CHECK(back.playAs == kWhite);
  CHECK(back.seat == kWhite);
  CHECK(back.inProgress);
  for (int i = 0; i < kPoints; ++i) CHECK(back.game.point[i] == game.point[i]);
  for (int i = 0; i < kPoints; ++i) CHECK(back.lastPoints[i] == save.lastPoints[i]);
  CHECK(back.game.toMove == game.toMove);
  CHECK(back.game.ko == game.ko);
  CHECK(back.game.moveNumber == game.moveNumber);
  CHECK(back.game.capturedBy[kBlack] == game.capturedBy[kBlack]);
  CHECK(back.game.capturedBy[kWhite] == game.capturedBy[kWhite]);
  CHECK(back.game.recentCount == game.recentCount);
  CHECK(back.game.komiHalves == game.komiHalves);
  CHECK(back.game.handicap == game.handicap);
  CHECK(back.game.handicap == 2);

  // And the assertion no enumeration can rot past: every byte of the game comes
  // back. A field added to Game and not to pack() fails HERE whether or not
  // anybody remembers to name it above.
  CHECK(std::memcmp(&back.game, &game, sizeof(Game)) == 0);

  // The promise the whole ruleset rests on has to survive the card. A resumed
  // game scored with komi 0 is a different game, and it can end in the draw
  // this app has no screen for.
  CHECK(settlesEveryGame(back.game.komiHalves));
  back.game.stage = static_cast<uint8_t>(Stage::Over);
  game.stage = static_cast<uint8_t>(Stage::Over);
  CHECK(score(back.game).whiteHalves == score(game).whiteHalves);
  CHECK(score(back.game).blackHalves == score(game).blackHalves);
  for (int i = 0; i < kHistory; ++i) CHECK(back.game.recent[i] == game.recent[i]);
  for (int i = 0; i < (kPoints + 7) / 8; ++i) CHECK(back.game.dead[i] == game.dead[i]);
  // Who agreed the count survives too. A resumed count that forgot it would
  // show WAITING to a seat nobody is waiting for, or end on one more tap.
  CHECK(back.game.accepted == game.accepted);
  CHECK(hasAccepted(back.game, kBlack));
  CHECK(!hasAccepted(back.game, kWhite));

  // The superko ring surviving the round trip is not a detail: a resumed game
  // that forgot it accepts a repetition the same game refused a minute before
  // the device went to sleep.
  CHECK(!legal(back.game, back.game.lastMove, back.game.toMove) || back.game.lastMove == kPass ||
        back.game.point[back.game.lastMove] != kEmpty);
}

void testAHalfWrittenSaveCostsNothingButTheGame() {
  gosave::Save good;
  good.wins = 3;
  char line[1400];
  CHECK(gosave::pack(good, line, sizeof(line)) > 0);

  // Cut the line anywhere past the header and it must be refused outright, not
  // read as a shorter board.
  for (int cut = 30; cut < 200; cut += 17) {
    char broken[1400];
    std::memcpy(broken, line, static_cast<size_t>(cut));
    broken[cut] = '\0';
    gosave::Save into;
    into.wins = 99;
    CHECK(!gosave::unpack(broken, into));
    CHECK(into.wins == 99);
  }
  CHECK(!gosave::unpack("", good));
  CHECK(!gosave::unpack("nonsense", good));
}

void testTheDeadStoneGuessFindsAWholeGroup() {
  // The commonest endgame shape there is, and the one the first version of
  // estimateDead got wrong in every case: a small enemy group sitting inside
  // finished territory. It counted which STONE was on each point at the end of
  // a playout, and a captured group leaves its points EMPTY -- so a lone dead
  // stone was never called dead at all, and a dead pair was called half dead.
  //
  // Half a dead group is the worse outcome of the two: the board draws one live
  // stone beside one ghost, which is a position nobody can read.
  // SETTLED boards, and getting them settled took two tries. The first draft put
  // the white group in a corner of an EMPTY board, where whether it lives is
  // genuinely open -- a playout from an empty board is a whole game. The second
  // filled the rest with black, which put black's own eighty-stone group in
  // ATARI: white answered by capturing the entire board, so the playouts were
  // right and the position was wrong.
  //
  // These are the real thing. Black is alive with two eyes far apart, the white
  // group has one point of space and no way to make a second, and the only
  // question left on the board is the one being asked.
  const char* rows[3][kSize] = {
      {
          "O.XXXXXXX",  // one stone, one point of space
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXX.XXXX",  // black's first eye
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXXXXXX.",  // and its second, far from the first
      },
      {
          "OO.XXXXXX",  // a pair
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXX.XXXX",  // black's first eye
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXXXXXX.",  // and its second, far from the first
      },
      {
          "OOO.XXXXX",  // three, still one eye, still dead
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXX.XXXX",  // black's first eye
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXXXXXXX",  //
          "XXXXXXXX.",  // and its second, far from the first
      },
  };
  const int expected[3] = {1, 2, 3};

  for (int shape = 0; shape < 3; ++shape) {
    Game game;
    setUp(game, rows[shape]);
    game.stage = static_cast<uint8_t>(Stage::Scoring);
    uint32_t seed = 424242u + static_cast<uint32_t>(shape) * 7919u;

    uint8_t dead[(kPoints + 7) / 8];
    goengine::estimateDead(game, seed, dead);

    int marked = 0;
    for (int point = 0; point < kPoints; ++point) {
      if (go::marked(dead, point)) {
        ++marked;
        // Only White's stones are dead here. Marking a black one would hand the
        // game away.
        CHECK(game.point[point] == kWhite);
      }
    }
    CHECK(marked == expected[shape]);

    // And the count that follows is the true one: Black holds the whole board.
    for (int i = 0; i < (kPoints + 7) / 8; ++i) game.dead[i] = dead[i];
    game.stage = static_cast<uint8_t>(Stage::Over);
    const Score counted = score(game);
    CHECK(counted.blackHalves == kPoints * 2);
    CHECK(counted.whiteHalves == kDefaultKomiHalves);
  }
}

void testACountEndsOnlyWhenBOTHSeatsAgree() {
  // The one thing a match's endgame must not do: end because one seat pressed
  // ACCEPT. The first version did exactly that, while the button it pressed
  // relabelled itself to WAITING -- so the screen promised a negotiation the
  // code did not hold.
  //
  // It lives in the GAME because it has to cross the wire. An agreement held
  // only on the device that made it is not an agreement.
  Game game;
  reset(game);
  CHECK(game.accepted == 0);
  CHECK(!hasAccepted(game, kBlack));
  CHECK(!hasAccepted(game, kWhite));

  CHECK(!accept(game, kBlack));
  CHECK(hasAccepted(game, kBlack));
  CHECK(!hasAccepted(game, kWhite));
  // Saying it twice is not saying it for both.
  CHECK(!accept(game, kBlack));
  CHECK(accept(game, kWhite));

  // And changing a mark takes both agreements back, because a count that moved
  // is a count nobody has read.
  withdrawAcceptance(game);
  CHECK(!hasAccepted(game, kBlack));
  CHECK(!hasAccepted(game, kWhite));
  CHECK(!accept(game, kWhite));

  // A fresh game agrees to nothing, whatever the last one settled.
  reset(game);
  CHECK(game.accepted == 0);
}

void testACountIsAnAgreementNotAComputation() {
  // The one thing the counting screen must get right: marking a group dead
  // moves the result, and marking it back moves it back exactly.
  Game game;
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
  game.stage = static_cast<uint8_t>(Stage::Over);

  const Score before = score(game);
  mark(game.dead, pointAt(0, 0));
  const Score during = score(game);
  unmark(game.dead, pointAt(0, 0));
  const Score after = score(game);
  CHECK(before.blackHalves == after.blackHalves);
  CHECK(before.whiteHalves == after.whiteHalves);
  CHECK(during.blackHalves != before.blackHalves);
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
  testNoGameCanRunForever();
  testTheStateFitsAPacketAndCopiesAsBytes();
  testBackIsTotalAndAlwaysReachesTheTop();
  testASavedGameComesBackExactly();
  testAHalfWrittenSaveCostsNothingButTheGame();
  testTheDeadStoneGuessFindsAWholeGroup();
  testACountEndsOnlyWhenBOTHSeatsAgree();
  testACountIsAnAgreementNotAComputation();
  testTheFastBoardIsTheSameGame();
  testTheOpponentOnlyEverPlaysALegalMove();
  testEveryLevelIsADifferentPlayer();
  testAHandicapIsStonesOnTheBoardAndWhiteToPlay();
  testItNeverPassesAWonGameAway();
  testEasyMissesThingsWithoutLookingBroken();
  testTheOpponentBeatsARandomMoverAtEveryLevel();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
