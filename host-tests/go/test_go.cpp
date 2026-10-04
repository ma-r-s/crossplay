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
#include "GoMichi.h"
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

// Every diagram in this file is nine by nine, so the helpers that used to be
// constants in GoCore are re-made here at that size. The thirteen by thirteen
// board has its own tests below rather than a second copy of every diagram:
// the rules do not know how big the board is, they ask `game.size`.
constexpr int kSize = go::kSmallSize;
constexpr int kPoints = kSize * kSize;
constexpr int pointAt(const int row, const int col) { return go::pointAt(kSize, row, col); }
constexpr int rowOf(const int point) { return go::rowOf(kSize, point); }
constexpr int colOf(const int point) { return go::colOf(kSize, point); }
inline int neighbours(const int point, Point out[4]) { return go::neighbours(kSize, point, out); }

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
        game.put(point, kBlack);
      } else if (cell == 'O') {
        game.put(point, kWhite);
      } else {
        CHECK(cell == '.');
        game.put(point, kEmpty);
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
    if (a.at(i) != b.at(i)) return false;
  }
  return a.toMove == b.toMove;
}

// --- The board itself -------------------------------------------------------

void testNeighboursNeverWrapRoundTheEdge() {
  Point out[4];

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
  CHECK(game.at(pointAt(1, 1)) == kEmpty);
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
    for (int col = 0; col <= 1; ++col) CHECK(game.at(pointAt(row, col)) == kEmpty);
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
  CHECK(game.at(pointAt(1, 1)) == kEmpty);

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
  CHECK(game.at(pointAt(1, 2)) == kEmpty);
  CHECK(game.at(pointAt(1, 1)) == kWhite);
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
  CHECK(game.at(pointAt(2, 2)) == kEmpty);
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
  CHECK(game.at(pointAt(0, 1)) == kEmpty);
  CHECK(game.at(pointAt(0, 2)) == kEmpty);
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
      if (game.at(point) != kEmpty) {
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
  for (int i = 0; i < kPoints; ++i) game.put(i, kBlack);
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
  while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(moveLimit(kSize)) + 50) {
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
  CHECK(game.moveNumber <= moveLimit(kSize));
}

void testRandomGamesFinishAndHoldEveryInvariant() {
  for (int trial = 0; trial < 200; ++trial) {
    Game game;
    reset(game);

    int plies = 0;
    while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(moveLimit(kSize)) + 4) {
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
        CHECK(game.at(chosen) == mover);
      }
      CHECK(game.toMove == other(mover));

      // No stone ever stands without a liberty.
      for (int point = 0; point < kPoints; ++point) {
        if (isStone(game.at(point))) CHECK(libertiesOf(game, point) > 0);
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
          CHECK(game.at(point) == kEmpty);
          CHECK(point != game.ko);
          CHECK(libertiesAfter(game, point, game.toMove) > 0);
          ++superkoOnly;
          continue;
        }
        if (!slowOk) continue;

        for (int i = 0; i < kPoints; ++i) CHECK(slow.at(i) == fast.at(i));
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
  for (int trial = 0; trial < 4; ++trial) {
    Game game;
    reset(game, trial % 2 == 0 ? go::kSmallSize : go::kLargeSize);
    uint32_t seed = 4242u + static_cast<uint32_t>(trial) * 97u;
    // Easy, because this test plays WHOLE games and the stronger levels are the
    // same search with a bigger count. What it is asking -- does the bridge
    // ever hand back a move the rules refuse -- has no level in it.
    const go::Level level = go::Level::Easy;
    int plies = 0;
    while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(moveLimit(game.size)) + 4) {
      const int move = gomichi::chooseMove(game, level, seed);
      CHECK(move == kPass || legal(game, move, game.toMove));
      CHECK(play(game, move));
      ++plies;
    }
    // Both boards finish, and they finish by PASSING rather than by running
    // into the move limit. An engine that never passes fills every neutral
    // point and reads as broken.
    CHECK(game.stage == static_cast<uint8_t>(Stage::Scoring));
    CHECK(game.moveNumber < moveLimit(game.size));
  }
}

void testTheOpponentBeatsARandomMoverAtEveryLevel() {
  // Legal and terminating were both true of an engine whose evaluation was
  // NEGATED, and the suite stayed green. The only assertion that catches that
  // is one about the RESULT.
  for (int levelIndex = 0; levelIndex < 3; ++levelIndex) {
    const go::Level level = static_cast<go::Level>(levelIndex);
    int engineWins = 0;
    constexpr int kGames = 2;
    for (int trial = 0; trial < kGames; ++trial) {
      Game game;
      reset(game);
      uint32_t seed = 31337u + static_cast<uint32_t>(trial) * 131u;
      // Alternating seats, so a level that only ever wins as Black is caught.
      const uint8_t engineSeat = (trial % 2 == 0) ? kBlack : kWhite;
      int plies = 0;
      while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(moveLimit(kSize)) + 4) {
        int move = kPass;
        if (game.toMove == engineSeat) {
          move = gomichi::chooseMove(game, level, seed);
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
    // A clean sweep. A player with no idea at all is not an opponent any of
    // these levels should ever lose to.
    CHECK(engineWins == kGames);
  }
}

// A clock the test controls, so "it stopped when it was told" is a fact rather
// than a stopwatch reading.
uint32_t gFakeMs = 0;
uint32_t gFakeReadings = 0;
uint32_t gFakeStep = 400;
uint32_t fakeClock() {
  // The step is set to just over half the level's budget, so the budget
  // genuinely runs out early in the search. A one millisecond step does not:
  // the search finishes its whole simulation count in three or four readings
  // and the branch this test exists to exercise is never taken -- the test
  // then passes with the budget check deleted. A step of zero freezes the
  // clock, which is the case the test below this one needs.
  ++gFakeReadings;
  gFakeMs += gFakeStep;
  return gFakeMs;
}

// A clock that never runs out must change NOTHING about the search.
//
// It did. The budget used to be applied by slicing the search into a series of
// small tree_search() calls and reading the clock between them, and the size of
// those slices was computed from the clock -- so lending a clock changed the
// work done even when the budget was never reached. It was worse than that:
// BOTH of tree_search's early stops compare the simulations done against the
// count IT was handed, so a slice of eight is "twenty percent read" after two
// simulations and stops itself there. The search asked for five hundred played
// about a hundred.
//
// The clock is inside the search's own loop now, so a clock that never fires is
// a clock that does nothing, and that is exactly what this asserts. It fails on
// the sliced version, whose answer depends on whether a clock was lent at all.
// Two boots, two different games -- and the same seed replays exactly.
//
// michi's generator is a global that starts at 1, and nothing in the engine set
// it. `GoActivity::onEnter` gathered entropy from `millis()` into a seed and
// passed it to `chooseMove`, which discarded it: `(void)seed`. So the answer to
// a given position was fixed for the life of the build, and the first game after
// every power-on was the same first game. The comment in onEnter said the seed
// "has to differ between boots or the computer plays the same game every time",
// which was true and was not happening.
//
// Both halves matter. Different is what a player notices; identical-from-the-
// same-seed is what makes every other test in this file mean anything.
void testADifferentSeedPlaysADifferentGameAndTheSameSeedReplays() {
  const int kOpening = 6;

  auto openingFrom = [&](uint32_t start, int* out) {
    Game game;
    reset(game, kSize, 0, komiForHandicap(0));
    uint32_t seed = start;
    for (int m = 0; m < kOpening; ++m) {
      out[m] = gomichi::chooseMove(game, go::Level::Medium, seed);
      CHECK(out[m] == kPass || legal(game, out[m], game.toMove));
      CHECK(play(game, out[m]));
    }
  };

  int a[kOpening], b[kOpening], again[kOpening];
  openingFrom(0x9E3779B9u, a);
  openingFrom(0x9E3779B9u ^ (1234u * 2654435761u), b);
  openingFrom(0x9E3779B9u, again);

  // Replay: every move, in order.
  for (int m = 0; m < kOpening; ++m) CHECK(a[m] == again[m]);

  // Divergence: somewhere in six moves. Not move by move -- two openings may
  // legitimately share a first move -- but they must not be the same opening.
  bool differs = false;
  for (int m = 0; m < kOpening; ++m) {
    if (a[m] != b[m]) differs = true;
  }
  CHECK(differs);
}

void testAClockThatNeverRunsOutChangesNothing() {
  Game game;
  reset(game);
  CHECK(play(game, pointAt(4, 4)));
  CHECK(play(game, pointAt(2, 6)));

  for (int i = 0; i < 3; ++i) {
    const go::Level level = static_cast<go::Level>(i);
    const uint32_t start = 31337u + static_cast<uint32_t>(i);

    // The SAME starting seed both times. chooseMove advances it once a move, so
    // reusing the variable would compare two different draws and this test would
    // be asserting that two unrelated searches agree.
    uint32_t seed = start;
    gFakeMs = 0;
    gFakeReadings = 0;
    const int withoutClock = gomichi::chooseMove(game, level, seed);
    const int simsWithout = gomichi::lastSimulations();
    CHECK(gFakeReadings == 0);

    // Frozen: every reading is the same millisecond, so the budget can never be
    // reached however many simulations run.
    seed = start;
    gFakeMs = 0;
    gFakeReadings = 0;
    gFakeStep = 0;
    const int withClock = gomichi::chooseMove(game, level, seed, fakeClock);
    CHECK(gFakeReadings > 0);
    CHECK(withClock == withoutClock);
    CHECK(gomichi::lastSimulations() == simsWithout);
  }
  gFakeStep = 400;
}

void testTheClockStopsTheSearchWhateverTheSimulationCountSays() {
  // The count alone is not a budget. The same simulations are a fraction of a
  // second on a laptop and seconds on the device, and which one you get is a
  // property of the machine. Mario played the first build on hardware and said
  // every level was too slow.
  //
  // So the search reads a clock the caller lends, and this proves the clock is
  // actually consulted: with a clock that advances a
  // millisecond per reading, the budget runs out within a few readings and the
  // search must come back having run FAR fewer simulations than its count
  // allows -- and still come back with a legal move.
  Game game;
  reset(game);
  CHECK(play(game, pointAt(4, 4)));

  for (int i = 0; i < 3; ++i) {
    const go::Level level = static_cast<go::Level>(i);
    const gomichi::Settings settings = gomichi::settingsFor(level);
    CHECK(settings.budgetMs > 0);
    // Five seconds is the ceiling Mario set and the reason the budgets exist at
    // all. Held below it with room, because the chunk that is running when the
    // clock expires still has to finish.
    CHECK(settings.budgetMs <= 4500);

    // The same position twice: once with all the time in the world, once with
    // this clock. The comparison is what makes the assertion able to fail --
    // "fewer than the count" is also true of a search michi stopped early by
    // itself, and that is what the first version of this test was measuring.
    const uint32_t start = 9090u + static_cast<uint32_t>(i);
    uint32_t seed = start;
    const int unhurried = gomichi::chooseMove(game, level, seed);
    const int unhurriedSims = gomichi::lastSimulations();
    CHECK(unhurried == kPass || legal(game, unhurried, game.toMove));

    // Same starting seed, so the only difference between the two searches is
    // the clock.
    seed = start;
    gFakeMs = 0;
    gFakeReadings = 0;
    gFakeStep = settings.budgetMs / 2 + 1;
    const int move = gomichi::chooseMove(game, level, seed, fakeClock);
    CHECK(move == kPass || legal(game, move, game.toMove));
    // The clock was read, the budget ran out on it, and the search that was
    // stopped did strictly less work than the one that was not.
    CHECK(gFakeReadings > 1);
    CHECK(gFakeMs >= settings.budgetMs);
    CHECK(gomichi::lastSimulations() < unhurriedSims);
    CHECK(gomichi::lastSimulations() < static_cast<int>(settings.simulations));
  }

  // And with no clock at all it is bounded by the count alone, which is what
  // keeps every other test in this file deterministic.
  gFakeMs = 0;
  gFakeReadings = 0;
  uint32_t seed = 4242u;
  const int move = gomichi::chooseMove(game, go::Level::Easy, seed);
  CHECK(move == kPass || legal(game, move, game.toMove));
  CHECK(gFakeReadings == 0);
}

void testEveryLevelIsADifferentPlayer() {
  // Three levels made of ONE knob, and that is the change this engine brought.
  // The knob is how many simulations the search runs, which is what michi-c2's
  // own strength ladder is measured in; the handicap, the komi and the colour
  // are three SEPARATE settings the player sets, so EASY no longer quietly
  // means "and two free stones" as well.
  const gomichi::Settings easy = gomichi::settingsFor(go::Level::Easy);
  const gomichi::Settings medium = gomichi::settingsFor(go::Level::Medium);
  const gomichi::Settings hard = gomichi::settingsFor(go::Level::Hard);

  CHECK(easy.simulations < medium.simulations);
  CHECK(medium.simulations < hard.simulations);
  // The clock ladder rises with it, or a level that is meant to think harder is
  // cut off before it can.
  CHECK(easy.budgetMs < medium.budgetMs);
  CHECK(medium.budgetMs < hard.budgetMs);
  // Every one of them under the ceiling.
  CHECK(hard.budgetMs <= 4500);

  // The komi is the HANDICAP's, not the level's, and it settles every game a
  // handicap can produce. A draw screen does not exist and must never become
  // necessary.
  CHECK(komiForHandicap(0) == kDefaultKomiHalves);
  for (int stones = 0; stones <= kMaxHandicap; ++stones) {
    CHECK(settlesEveryGame(komiForHandicap(stones)));
  }
  CHECK(komiForHandicap(2) < komiForHandicap(0));
}

void testAHandicapIsStonesOnTheBoardAndWhiteToPlay() {
  for (int stones = 2; stones <= kMaxHandicap; ++stones) {
    Game game;
    reset(game, kSize, stones, 1);
    int placed = 0;
    for (int point = 0; point < kPoints; ++point) {
      if (game.at(point) == kBlack) ++placed;
      CHECK(game.at(point) != kWhite);
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
    Point where[kMaxHandicap];
    CHECK(handicapPoints(kSize, stones, where) == stones);
    for (int i = 0; i < stones; ++i) {
      CHECK(game.at(where[i]) == kBlack);
      for (int j = i + 1; j < stones; ++j) CHECK(where[i] != where[j]);
    }
    // The first two are opposite corners, or a two-stone game is lopsided.
    CHECK(rowOf(where[0]) + rowOf(where[1]) == kSize - 1);
    CHECK(colOf(where[0]) + colOf(where[1]) == kSize - 1);
  }

  // One stone is not a handicap, it is the even game.
  Game even;
  reset(even, kSize, 1);
  CHECK(even.handicap == 0);
  CHECK(even.toMove == kBlack);

  // And the stones are worth what they are supposed to be worth: four stones
  // and a smaller komi has to leave Black ahead on an empty-ish board.
  Game spotted;
  reset(spotted, kSize, 4, 1);
  spotted.stage = static_cast<uint8_t>(Stage::Over);
  Game level;
  reset(level, kSize, 0, kDefaultKomiHalves);
  level.stage = static_cast<uint8_t>(Stage::Over);
  const Score withStones = score(spotted);
  const Score without = score(level);
  CHECK(withStones.blackHalves - withStones.whiteHalves > without.blackHalves - without.whiteHalves);
}

// The number on the board is the number the engine passes on.
//
// Mario, on hardware, after v1.13.1: "I still see the opponent never passes."
// Measured, he was right about what he saw and the machine was right to do it:
// when a player passes with points still belonging to nobody, the machine takes
// them, one a turn, and 86% of those moves are worth a point each. Agreeing
// instead was tried and costs it the game -- 40 wins in 40 became 16 in 40.
//
// So the machine keeps playing and the BOARD explains why. That explanation is
// only worth anything if its number is the engine's number, which is why there
// is one freePoints() and both read it. A screen saying two points are left
// beside an opponent that plays nine more is worse than no screen at all.
// The explanation speaks only when nothing more urgent needs the row.
//
// The board has ONE line to speak on. The line that says a pass was played
// through has to yield to the three that answer a more pressing question, and
// the `thinking` case is the one that actually bites: the flag is still set from
// the previous pass while the next search runs, so without that term the
// explanation replaces THINKING from the second pass onward and the machine
// looks frozen.
void testTheExplanationYieldsToAnythingMoreUrgent() {
  const go::Caution none = go::Caution::None;
  // The case it exists for: your pass was answered with a stone, points remain.
  CHECK(go::explainsPlayedOn(true, 43, false, false, none));

  // Silent when there was no pass to explain, or nothing left to take. A "0
  // FREE POINTS" line would be worse than saying nothing.
  CHECK(!go::explainsPlayedOn(false, 43, false, false, none));
  CHECK(!go::explainsPlayedOn(true, 0, false, false, none));

  // Yields to all three, each on its own so a missing term cannot hide behind
  // another.
  CHECK(!go::explainsPlayedOn(true, 43, true, false, none));
  CHECK(!go::explainsPlayedOn(true, 43, false, true, none));
  CHECK(!go::explainsPlayedOn(true, 43, false, false, go::Caution::FillsOwnEye));
  CHECK(!go::explainsPlayedOn(true, 43, false, false, go::Caution::SelfAtari));
}

void testTheBoardsFreePointCountIsTheOneTheEngineDecidesOn() {
  // A board with a wall down the middle: four points down the third column
  // reach both colours, so they belong to nobody and are worth taking.
  Game game;
  const char* rows[kSize] = {
      "XX.OOOOOO",  //
      "XX.OOOOOO",  //
      "XX.OOOOOO",  //
      "XX.OOOOOO",  //
      "XXXOOOOOO",  //
      "XXXOOOOOO",  //
      "XXXOOOOOO",  //
      "XXXOOOOOO",  //
      "XXXOOOOOO",  //
  };
  setUp(game, rows, kBlack);
  const int free = go::freePoints(game, kBlack);
  CHECK(free == 4);
  CHECK(go::freePoints(game, kWhite) == 4);

  // Every one of them is empty, playable, and owned by nobody: the three
  // conditions the count is made of, checked separately so a count that is
  // right by accident cannot pass.
  uint8_t owner[go::kMaxPoints];
  go::territory(game, owner);
  int checked = 0;
  for (int point = 0; point < game.points(); ++point) {
    if (game.at(point) != kEmpty || owner[point] != kEmpty) continue;
    CHECK(legal(game, point, kBlack));
    ++checked;
  }
  CHECK(checked == free);

  // Filling one leaves one fewer, which is what makes the number on the board
  // count down as the machine takes them.
  CHECK(play(game, pointAt(0, 2)));
  CHECK(go::freePoints(game, kWhite) == 3);

  // A point inside somebody's territory is NOT free: taking it gains nothing
  // under area scoring, and counting it would have the board promising points
  // that are not there.
  Game closed;
  const char* walled[kSize] = {
      "XXXXXXXXX",  //
      "X.......X",  //
      "XXXXXXXXX",  //
      "OOOOOOOOO",  //
      "O.......O",  //
      "OOOOOOOOO",  //
      "XXXXXXXXX",  //
      "OOOOOOOOO",  //
      "OOOOOOOOO",  //
  };
  setUp(closed, walled, kBlack);
  CHECK(go::freePoints(closed, kBlack) == 0);

  // And a point nobody owns that one colour may NOT play. Without this case the
  // legality test in freePoints() is not exercised at all: on every board above,
  // every unowned point is playable by both, so deleting that line leaves all
  // the counts right and the suite green.
  //
  // A ko point does not do it -- the capture leaves it ringed by one colour, so
  // it belongs to that colour and is not free. This one is a corner point
  // wedged between the two: Black there is suicide, because the black stone it
  // would join has no other liberty and nothing is captured; White there is a
  // capture, so it is legal.
  Game wedge;
  const char* wedged[kSize] = {
      ".XO......",  //
      "OO.......",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
      ".........",  //
  };
  setUp(wedge, wedged, kBlack);
  uint8_t wedgeOwner[go::kMaxPoints];
  go::territory(wedge, wedgeOwner);
  CHECK(wedgeOwner[pointAt(0, 0)] == kEmpty);   // nobody's: it touches both
  CHECK(!legal(wedge, pointAt(0, 0), kBlack));  // suicide
  CHECK(legal(wedge, pointAt(0, 0), kWhite));   // captures, so legal
  CHECK(go::freePoints(wedge, kBlack) == go::freePoints(wedge, kWhite) - 1);
}

void testItStopsWhenTheResultIsSettledAndNotBefore() {
  // The Leela Zero rule, and the loudest way a Go program can look broken.
  //
  // A board where Black plainly leads: passing wins for Black and loses for
  // White. The rule has TWO halves and both are tested here, because getting
  // only the second right makes White pass on move two of every game.
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

  // Black, after White passed: passing ends it and Black wins, so it passes.
  game.passes = 1;
  uint32_t seed = 6060u;
  CHECK(gomichi::chooseMove(game, go::Level::Easy, seed) == kPass);

  // White, after Black passed: it passes too, because the result is settled and
  // playing on decides nothing.
  //
  // This assertion used to be the opposite, and the opposite is what made the
  // game feel endless to a beginner: the machine played forty more moves of a
  // game it had already lost. GNU Go 3.8 was measured on this board and passes
  // in EVERY game, while losing in three of eight, once at move 34 with 47 of
  // 81 points still empty. Stopping when the result is settled is what a Go
  // program does, whichever side of it you are on.
  game.toMove = kWhite;
  game.passes = 1;
  for (int trial = 0; trial < 3; ++trial) {
    const int move = gomichi::chooseMove(game, go::Level::Hard, seed);
    CHECK(move == kPass);
  }

  // But NOT when the points still on the table could change who wins. This is
  // the half that keeps "stop when it is settled" from becoming "give up": the
  // margin has to exceed what is left, and here it does not.
  //
  // Black leads by 2 points on the board before komi, and there are four points
  // belonging to nobody down the middle -- more than enough to swing it -- so
  // neither colour may stop.
  {
    Game close;
    const char* tight[kSize] = {
        "XXXX.OOOO",  //
        "XXXX.OOOO",  //
        "XXXX.OOOO",  //
        "XXXX.OOOO",  //
        "XXXXXOOOO",  //
        "XXXX.OOOO",  //
        "XXXX.OOOO",  //
        "XXXX.OOOO",  //
        "XXXX.OOOO",  //
    };
    setUp(close, tight, kBlack);
    close.passes = 1;
    CHECK(gomichi::chooseMove(close, go::Level::Medium, seed) != kPass);
    close.toMove = kWhite;
    CHECK(gomichi::chooseMove(close, go::Level::Medium, seed) != kPass);
  }

  // And nobody passes while the game is still being played, whatever the count
  // says. One black stone on an empty board surrounds the WHOLE board under
  // area scoring, so "passing wins" is true for Black from move one -- and a
  // rule with only the winning half in it ends every game at move two.
  Game opening;
  reset(opening);
  CHECK(play(opening, pointAt(4, 4)));
  CHECK(goengine::passingWins(opening, kBlack));
  CHECK(gomichi::chooseMove(opening, go::Level::Easy, seed) != kPass);
  opening.toMove = kBlack;
  CHECK(gomichi::chooseMove(opening, go::Level::Easy, seed) != kPass);
}

void testTheEngineIsToldAboutTheKo() {
  // The one move on the board that the rules refuse, and the one the engine
  // most wants to play.
  //
  // michi keeps its own position, and the bridge builds it by PLACING stones
  // rather than playing them -- which is right, because the position handed
  // over is already the result of every capture in the game, but it means the
  // ko point does not come with it. Left at zero, michi sees the recapture as
  // an ordinary capture of a stone in atari, which on this board is comfortably
  // the highest-value point there is.
  //
  // What that cost before the ko was carried across: the search offered the
  // recapture, the rules refused it, and chooseMove's fallback was a PASS --
  // in the middle of a ko fight. If the human passed back, the game ended.
  Game game;
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
  // Black took the ko, so White may not take it straight back.
  CHECK(game.toMove == kWhite);
  CHECK(game.ko == pointAt(2, 2));
  CHECK(!legal(game, pointAt(2, 2), kWhite));

  uint32_t seed = 5150u;
  for (int level = 0; level < 3; ++level) {
    const int move = gomichi::chooseMove(game, static_cast<go::Level>(level), seed);
    // Never the ko, never a pass, always legal. That is the guarantee the app
    // rests on, and chooseMove's fallbacks hold it whether or not the engine
    // was told anything -- which is exactly why it is not enough on its own.
    CHECK(move != kPass);
    CHECK(move != pointAt(2, 2));
    CHECK(legal(game, move, kWhite));

    // So ask the engine what it was told. This is the assertion that fails when
    // the ko is not carried across, and the one above is not.
    const gomichi::Context context = gomichi::lastContext(kSize);
    CHECK(context.ko == pointAt(2, 2));
    CHECK(context.lastMove == pointAt(2, 3));
    CHECK(context.moveNumber == game.moveNumber);
  }
}

void testEasyIsWeakWithoutLookingBroken() {
  // Easy is meant to miss what it did not look at. It is NOT meant to pass a
  // game it is winning, nor to play an illegal move, nor to fail to finish:
  // all three read as a fault rather than as a weak player.
  for (int trial = 0; trial < 3; ++trial) {
    Game game;
    reset(game, kSize, 0, komiForHandicap(0));
    uint32_t seed = 8080u + static_cast<uint32_t>(trial) * 17u;
    int plies = 0;
    while (game.stage == static_cast<uint8_t>(Stage::Playing) && plies < static_cast<int>(moveLimit(kSize)) + 4) {
      const uint8_t mover = game.toMove;
      const int move = gomichi::chooseMove(game, go::Level::Easy, seed);
      if (move == kPass) {
        // The search does not get a vote on passing: michi liking a pass at
        // sixty simulations would otherwise end a game this level was winning.
        // So either the opponent has already passed, or there is nothing left
        // to play -- and a pass with neither true is the fault this is looking
        // for.
        //
        // This assertion used to name `goengine::passingWins`, the Leela Zero
        // rule, which the app stopped using when it started stopping at a
        // settled result instead. It kept passing only because these three
        // seeded games never reached the branch; changing the seed made it fail
        // at once. Whether a settled position is settled ENOUGH is
        // testItStopsWhenTheResultIsSettledAndNotBefore's job, on positions
        // built for it, and repeating that arithmetic here would only assert
        // that the rule equals itself.
        CHECK(game.passes >= 1 || !go::hasUsefulMove(game, mover));
      } else {
        CHECK(legal(game, move, mover));
      }
      CHECK(play(game, move));
      ++plies;
    }
    CHECK(game.stage == static_cast<uint8_t>(Stage::Scoring));
  }
}

// --- Thirteen by thirteen ---------------------------------------------------
//
// The rules do not know how big the board is; they ask `game.size`. What that
// buys is that these tests are about the SIZE being carried, not about Go being
// re-implemented -- so they are the shapes where a hard-coded nine would still
// compile and be wrong.

void testTheLargeBoardIsTheSameGameOnMorePoints() {
  Game game;
  reset(game, go::kLargeSize);
  CHECK(game.size == go::kLargeSize);
  CHECK(game.points() == 169);

  // The edge is where a hard-coded nine shows. The point one row below the top
  // right corner has three neighbours on either board, but they are DIFFERENT
  // points, and a `- 9` that should have been a `- 13` wraps.
  Point around[4];
  CHECK(go::neighbours(go::kLargeSize, go::pointAt(go::kLargeSize, 0, 12), around) == 2);
  CHECK(go::neighbours(go::kLargeSize, go::pointAt(go::kLargeSize, 6, 6), around) == 4);
  CHECK(go::neighbours(go::kLargeSize, go::pointAt(go::kLargeSize, 0, 0), around) == 2);
  CHECK(around[0] == go::pointAt(go::kLargeSize, 1, 0));
  CHECK(around[1] == go::pointAt(go::kLargeSize, 0, 1));

  // A capture in the far corner, which on a nine by nine board does not exist.
  const int corner = go::pointAt(go::kLargeSize, 12, 12);
  game.put(corner, kWhite);
  game.put(go::pointAt(go::kLargeSize, 11, 12), kBlack);
  game.toMove = kBlack;
  CHECK(play(game, go::pointAt(go::kLargeSize, 12, 11)));
  CHECK(game.at(corner) == kEmpty);
  CHECK(game.capturedBy[kBlack] == 1);

  // Area scoring counts the whole board.
  Game empty;
  reset(empty, go::kLargeSize);
  for (int i = 0; i < empty.points(); ++i) empty.put(i, kBlack);
  empty.stage = static_cast<uint8_t>(Stage::Over);
  const Score counted = score(empty);
  CHECK(counted.blackHalves == 169 * 2);
  CHECK(counted.whiteHalves == kDefaultKomiHalves);

  // The move limit scales with the board, or a thirteen by thirteen game is
  // counted while it is still being played.
  CHECK(moveLimit(go::kLargeSize) > moveLimit(go::kSmallSize));

  // A handicap goes on the 4-4 points, and the first two are opposite corners.
  Point where[kMaxHandicap];
  CHECK(handicapPoints(go::kLargeSize, 2, where) == 2);
  CHECK(go::rowOf(go::kLargeSize, where[0]) == 9);
  CHECK(go::colOf(go::kLargeSize, where[0]) == 3);
  CHECK(go::rowOf(go::kLargeSize, where[1]) == 3);
  CHECK(go::colOf(go::kLargeSize, where[1]) == 9);
}

void testResetClearsTheTailOfTheLargerBoard() {
  // A thirteen by thirteen game followed by a nine by nine one. The nine only
  // ever touches the first eighty-one points, so a reset that cleared `points()`
  // rather than the whole array would leave the old game's stones sitting in
  // the tail -- invisible on screen, in the save file, and on the wire.
  Game game;
  reset(game, go::kLargeSize);
  for (int i = 0; i < game.points(); ++i) game.put(i, kWhite);

  reset(game, go::kSmallSize);
  CHECK(game.size == go::kSmallSize);
  for (int i = 0; i < go::kMaxPoints; ++i) CHECK(game.at(i) == kEmpty);
}

// --- Nineteen by nineteen ---------------------------------------------------
//
// The board real Go is played on (GitHub #282), and the first one with more
// points than a byte can number: 361. Every point the game STORES -- the ko,
// the last move, a flood fill's stack, a neighbour list -- was a uint8_t, and
// a byte wraps at 256. So these tests live past the 256th point on purpose: a
// test in the top rows passes with the old types.

constexpr int kFull = go::kFullSize;
constexpr int fullAt(const int row, const int col) { return go::pointAt(kFull, row, col); }

void testTheFullBoardNumbersEveryPointPastTheByte() {
  Game game;
  reset(game, kFull);
  CHECK(game.size == kFull);
  CHECK(game.points() == 361);

  // The bottom-right corner is point 360. Its neighbours are 341 and 359, and a
  // byte-sized neighbour list turned them into 85 and 103.
  Point around[4];
  CHECK(go::neighbours(kFull, 360, around) == 2);
  CHECK(around[0] == 341);
  CHECK(around[1] == 359);

  // A capture down there, played through the rules.
  game.put(360, kWhite);
  game.put(341, kBlack);
  game.toMove = kBlack;
  CHECK(play(game, 359));
  CHECK(game.at(360) == kEmpty);
  CHECK(game.capturedBy[kBlack] == 1);
  CHECK(game.lastMove == 359);

  // A group of 360 stones. The flood fill pushes every one of them, and with a
  // byte for a stack the ones past 255 were pushed as somebody else.
  Game snake;
  reset(snake, kFull);
  for (int i = 0; i < snake.points(); ++i) snake.put(i, kBlack);
  snake.put(180, kEmpty);
  int size = 0;
  int liberties = 0;
  group(snake, 0, nullptr, size, liberties);
  CHECK(size == 360);
  CHECK(liberties == 1);

  // One stone, and 360 empty points that only it reaches: the territory fill
  // walks all of them.
  Game lone;
  reset(lone, kFull);
  lone.put(0, kBlack);
  const Score counted = score(lone);
  CHECK(counted.blackHalves == 361 * 2);
  CHECK(counted.whiteHalves == kDefaultKomiHalves);

  CHECK(moveLimit(kFull) > moveLimit(go::kLargeSize));
}

// The ko shape from testTheEngineIsToldAboutTheKo, moved to the bottom of a
// nineteen by nineteen board, where the ko point is 325: stored in a byte it
// was 69, so the rules forbade an empty point at the top and allowed the
// immediate recapture.
void setUpKoPastTheByte(Game& game) {
  reset(game, kFull);
  game.put(fullAt(16, 2), kBlack);
  game.put(fullAt(16, 3), kWhite);
  game.put(fullAt(17, 1), kBlack);
  game.put(fullAt(17, 2), kWhite);
  game.put(fullAt(17, 4), kWhite);
  game.put(fullAt(18, 2), kBlack);
  game.put(fullAt(18, 3), kWhite);
  game.toMove = kBlack;
}

void testKoPastTheByteIsTheRightPoint() {
  Game game;
  setUpKoPastTheByte(game);
  CHECK(play(game, fullAt(17, 3)));
  CHECK(game.ko == fullAt(17, 2));
  CHECK(game.ko == 325);
  CHECK(!legal(game, 325, kWhite));
  CHECK(legal(game, 325 - 256, kWhite));
  CHECK(game.lastMove == 326);

  // And the engine is told the same ko, and never plays it.
  uint32_t seed = 1913u;
  const int move = gomichi::chooseMove(game, go::Level::Easy, seed);
  CHECK(move != kPass);
  CHECK(move != 325);
  CHECK(legal(game, move, kWhite));
  const gomichi::Context context = gomichi::lastContext(kFull);
  CHECK(context.ko == 325);
  CHECK(context.lastMove == 326);
  CHECK(context.moveNumber == game.moveNumber);
}

void testTheFullBoardTakesItsHandicapOnTheFourFourPoints() {
  Point where[kMaxHandicap];
  CHECK(handicapPoints(kFull, 5, where) == 5);
  CHECK(where[0] == fullAt(15, 3));
  CHECK(where[1] == fullAt(3, 15));
  CHECK(where[2] == fullAt(3, 3));
  CHECK(where[3] == fullAt(15, 15));
  CHECK(where[4] == fullAt(9, 9));
  Game game;
  reset(game, kFull, 5, komiForHandicap(5));
  CHECK(game.handicap == 5);
  CHECK(game.toMove == kWhite);
  for (int i = 0; i < 5; ++i) CHECK(game.at(where[i]) == kBlack);
}

void testTheBoardSettingStepsThroughAllThreeAndBack() {
  CHECK(go::nextBoardSize(go::kSmallSize) == go::kLargeSize);
  CHECK(go::nextBoardSize(go::kLargeSize) == kFull);
  CHECK(go::nextBoardSize(kFull) == go::kSmallSize);
  CHECK(go::isBoardSize(kFull));
  CHECK(!go::isBoardSize(11));
  // An unknown size is never kept: reset() falls back to nine.
  Game game;
  reset(game, 11);
  CHECK(game.size == go::kSmallSize);
}

void testAFullBoardSaveComesBackExactlyAndFitsItsLine() {
  Game game;
  setUpKoPastTheByte(game);
  CHECK(play(game, fullAt(17, 3)));
  mark(game.dead, 300);
  mark(game.dead, 360);

  gosave::Save save;
  save.inProgress = true;
  save.game = game;
  save.boardSize = kFull;
  save.lastSize = kFull;
  save.hasHistory = true;
  for (int i = 0; i < go::kMaxPoints; ++i) save.lastPoints[i] = static_cast<uint8_t>(i % 3);
  char line[gosave::kMaxLine];
  CHECK(gosave::pack(save, line, sizeof(line)) > 0);
  gosave::Save back;
  CHECK(gosave::unpack(line, back));
  CHECK(std::memcmp(&back.game, &game, sizeof(Game)) == 0);
  CHECK(back.game.ko == 325);
  CHECK(back.boardSize == kFull);
  CHECK(back.lastSize == kFull);
  CHECK(back.lastPoints[360] == 360 % 3);

  // The longest line pack() can write, against the buffer the activity reads
  // and writes it with. Every number at its widest.
  gosave::Save worst;
  worst.wins = 999999;
  worst.losses = 999999;
  worst.hasHistory = true;
  worst.lastMarginHalves = 722;
  worst.inProgress = true;
  worst.boardSize = kFull;
  worst.lastSize = kFull;
  worst.handicap = kMaxHandicap;
  for (int i = 0; i < go::kMaxPoints; ++i) worst.lastPoints[i] = kWhite;
  reset(worst.game, kFull, kMaxHandicap, komiForHandicap(kMaxHandicap));
  for (int i = 0; i < go::kCellBytes; ++i) worst.game.cell[i] = 0xAA;
  for (int i = 0; i < go::kMaskBytes; ++i) worst.game.dead[i] = 0xFF;
  worst.game.ko = kNoPoint;
  worst.game.lastMove = kPass;
  worst.game.moveNumber = moveLimit(kFull);
  worst.game.capturedBy[kBlack] = 65535;
  worst.game.capturedBy[kWhite] = 65535;
  worst.game.recentCount = kHistory;
  for (int i = 0; i < kHistory; ++i) worst.game.recent[i] = 0xFFFFFFFFu;
  char longest[gosave::kMaxLine];
  const int written = gosave::pack(worst, longest, sizeof(longest));
  CHECK(written > 0);
  CHECK(written < gosave::kMaxLine);
  std::printf("  longest go.sav line: %d of %d bytes\n", written, gosave::kMaxLine);
}

void testTheEngineFindsTheDeadStoneOnTheFullBoard() {
  // Black alive with two far-apart eyes; one white stone with one point of
  // space at the bottom, past the byte. The playouts' own boards (GoEngine's
  // Fast) carried byte-sized stacks and ko too.
  Game game;
  reset(game, kFull);
  for (int i = 0; i < game.points(); ++i) game.put(i, kBlack);
  game.put(fullAt(0, 0), kEmpty);
  game.put(fullAt(9, 9), kEmpty);
  game.put(fullAt(17, 10), kWhite);
  game.put(fullAt(17, 11), kEmpty);
  uint8_t dead[kMaskBytes];
  goengine::opinionOnDead(game, dead);
  CHECK(marked(dead, fullAt(17, 10)));
  for (int i = 0; i < game.points(); ++i) {
    if (game.at(i) == kBlack) CHECK(!marked(dead, i));
  }
}

void testTheOpponentPlaysLegallyOnTheFullBoard() {
  // Easy, and forty plies of each side, because what this asks -- does the
  // bridge hand back a move the rules accept on a board michi now holds at its
  // full size -- has no level in it, and a whole nineteen by nineteen game of
  // searches is minutes of suite.
  Game game;
  reset(game, kFull);
  uint32_t seed = 1919u;
  for (int ply = 0; ply < 80 && game.stage == static_cast<uint8_t>(Stage::Playing); ++ply) {
    const int move = gomichi::chooseMove(game, go::Level::Easy, seed);
    CHECK(move == kPass || legal(game, move, game.toMove));
    CHECK(play(game, move));
  }
  CHECK(game.moveNumber >= 60);
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
  reset(game, kSize, 2, 1);
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
  for (int i = 0; i < go::kMaxPoints; ++i) save.lastPoints[i] = static_cast<uint8_t>(i % 3);
  save.lastSize = go::kLargeSize;
  save.opponent = go::Opponent::Human;
  save.level = go::Level::Hard;
  save.playAs = kWhite;
  save.handicap = 2;
  save.boardSize = go::kLargeSize;
  save.inProgress = true;
  save.game = game;
  save.seat = kWhite;

  char line[1600];
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
  // Three board sizes, and they are three different facts: the setting the next
  // game uses, the board the ornament draws, and the board the game in progress
  // is on. A save that carried one of them for all three would resume a nine by
  // nine game as a thirteen.
  CHECK(back.handicap == 2);
  CHECK(back.boardSize == go::kLargeSize);
  CHECK(back.lastSize == go::kLargeSize);
  CHECK(back.game.size == kSize);
  for (int i = 0; i < kPoints; ++i) CHECK(back.game.at(i) == game.at(i));
  for (int i = 0; i < go::kMaxPoints; ++i) CHECK(back.lastPoints[i] == save.lastPoints[i]);
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
  // Every point answers legality the same way it did before the round trip.
  // That is the ring, the ko point, the board and the side to move at once,
  // and unlike the version this replaces it can fail: dropping `recent[]` or
  // `ko` from pack() makes a refused move legal again, which is a rules bug
  // that only ever appears after a sleep.
  for (int point = 0; point < kPoints; ++point) {
    CHECK(legal(back.game, point, back.game.toMove) == legal(game, point, game.toMove));
  }
  CHECK(back.game.ko == game.ko);
  for (int i = 0; i < kHistory; ++i) CHECK(back.game.recent[i] == game.recent[i]);
}

void testASaveWithNoGameInItStillCarriesTheSettings() {
  // A device that has never started a game. `go::Game{}` is zeroed -- board
  // size 0, komi 0, nobody to move -- and validating it anyway made the whole
  // file unreadable, so setting a board size and backing out lost the setting
  // AND the record. The game is replaced rather than trusted when there is
  // nothing to resume.
  gosave::Save save;
  save.wins = 3;
  save.losses = 1;
  save.level = go::Level::Hard;
  save.boardSize = go::kLargeSize;
  save.handicap = 4;
  save.playAs = kWhite;
  save.inProgress = false;

  char line[1600];
  CHECK(gosave::pack(save, line, sizeof(line)) > 0);

  gosave::Save back;
  CHECK(gosave::unpack(line, back));
  CHECK(back.wins == 3);
  CHECK(back.losses == 1);
  CHECK(back.level == go::Level::Hard);
  CHECK(back.boardSize == go::kLargeSize);
  CHECK(back.handicap == 4);
  CHECK(back.playAs == kWhite);
  CHECK(!back.inProgress);
  // And what came back is a board the rules can be asked about, not a zeroed
  // struct waiting to divide by its own size.
  CHECK(back.game.size == go::kLargeSize);
  CHECK(back.game.points() == 169);
  CHECK(settlesEveryGame(back.game.komiHalves));
  CHECK(legal(back.game, pointAt(4, 4), back.game.toMove));
}

void testAHalfWrittenSaveCostsNothingButTheGame() {
  gosave::Save good;
  good.wins = 3;
  char line[1600];
  CHECK(gosave::pack(good, line, sizeof(line)) > 0);
  // The INTACT line has to parse, or the loop below is measuring nothing: an
  // earlier version of this fixture produced a line the reader refused whole,
  // so every truncation was refused for a reason that had nothing to do with
  // truncation.
  {
    gosave::Save whole;
    CHECK(gosave::unpack(line, whole));
    CHECK(whole.wins == 3);
  }

  // Cut the line anywhere past the header and it must be refused outright, not
  // read as a shorter board.
  for (int cut = 30; cut < 200; cut += 17) {
    char broken[1600];
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

    // go::kMaskBytes, not this board's. Every mask in this app is sized for the
    // LARGER board and clearMask() clears all of it, so a mask sized for nine
    // by nine is eleven bytes of somebody else's stack written on every call.
    // That is what this line was, and it read as a compiler bug: the same code
    // passed under clang and failed twice under g++.
    uint8_t dead[go::kMaskBytes];
    goengine::estimateDead(game, seed, dead);

    int marked = 0;
    for (int point = 0; point < kPoints; ++point) {
      if (go::marked(dead, point)) {
        ++marked;
        // Only White's stones are dead here. Marking a black one would hand the
        // game away.
        CHECK(game.at(point) == kWhite);
      }
    }
    CHECK(marked == expected[shape]);

    // And the count that follows is the true one: Black holds the whole board.
    for (int i = 0; i < go::kMaskBytes; ++i) game.dead[i] = dead[i];
    game.stage = static_cast<uint8_t>(Stage::Over);
    const Score counted = score(game);
    CHECK(counted.blackHalves == kPoints * 2);
    CHECK(counted.whiteHalves == kDefaultKomiHalves);
  }
}

void testYouCannotSetYourOwnScoreAgainstTheMachine() {
  // The complaint this exists for, in Mario's words: "I felt I could change the
  // score there to whatever I wanted. Make me win or lose."
  //
  // He could. One tap recorded agreement for BOTH colours, so whatever the
  // player marked became the result and the machine had no say in its own game.
  CHECK(!go::acceptEndsTheGame(go::Opponent::Computer, false));
  CHECK(go::acceptEndsTheGame(go::Opponent::Computer, true));
  // Two people sharing one device settle it between themselves. They are
  // looking at the same screen, so neither can cheat the other.
  CHECK(go::acceptEndsTheGame(go::Opponent::Human, false));
  CHECK(go::acceptEndsTheGame(go::Opponent::Human, true));
}

void testTheMachinesOpinionIsTheSameEveryTimeItIsAsked() {
  // Seeded from the POSITION, not from a running seed. Without that, the count
  // you were OFFERED before putting the device down can be REFUSED when you
  // pick it up, and the game bounces you back to the board for agreeing with it.
  //
  // The fixture is NOT a settled board, deliberately. On a settled one the
  // guess converges to the same answer whatever seed it is given, so the test
  // passes even when the seed drifts -- which is exactly what the first version
  // of this test did. This position was found by searching random mid-game
  // boards for one where two seeds genuinely disagree; about one in ten does.
  const char* rows[kSize] = {
      "O.X.XXXX.",  //
      "XXOO.O.X.",  //
      ".XXO.X.OX",  //
      "X.XO.OOXO",  //
      "XXXXXOO.O",  //
      ".OO.OXOXO",  //
      "O.XXOX..O",  //
      "XX.OXOOOX",  //
      ".X....XX.",  //
  };
  Game game;
  setUp(game, rows, kBlack);
  game.stage = static_cast<uint8_t>(Stage::Scoring);

  // The fixture earns its keep: two seeds really do disagree here. If this ever
  // stops being true the test below is measuring nothing and says so.
  {
    uint8_t a[go::kMaskBytes];
    uint8_t b[go::kMaskBytes];
    uint32_t s1 = 1u;
    uint32_t s2 = 987654321u;
    goengine::estimateDead(game, s1, a);
    goengine::estimateDead(game, s2, b);
    CHECK(!go::sameMask(a, b));
  }

  // And the opinion is the same answer every time it is asked.
  uint8_t first[go::kMaskBytes];
  uint8_t again[go::kMaskBytes];
  goengine::opinionOnDead(game, first);
  for (int trial = 0; trial < 4; ++trial) {
    goengine::opinionOnDead(game, again);
    CHECK(go::sameMask(first, again));
  }

  // Marking anything else is a disagreement, and a disagreement does not end
  // the game.
  uint8_t mine[go::kMaskBytes];
  for (int i = 0; i < go::kMaskBytes; ++i) mine[i] = first[i];
  go::mark(mine, pointAt(0, 1));
  CHECK(!go::sameMask(first, mine));
  CHECK(!go::acceptEndsTheGame(go::Opponent::Computer, go::sameMask(first, mine)));
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
  testASaveWithNoGameInItStillCarriesTheSettings();
  testAHalfWrittenSaveCostsNothingButTheGame();
  testTheDeadStoneGuessFindsAWholeGroup();
  testYouCannotSetYourOwnScoreAgainstTheMachine();
  testTheMachinesOpinionIsTheSameEveryTimeItIsAsked();
  testACountEndsOnlyWhenBOTHSeatsAgree();
  testACountIsAnAgreementNotAComputation();
  testTheFastBoardIsTheSameGame();
  testTheOpponentOnlyEverPlaysALegalMove();
  testTheClockStopsTheSearchWhateverTheSimulationCountSays();
  testEveryLevelIsADifferentPlayer();
  testAHandicapIsStonesOnTheBoardAndWhiteToPlay();
  testItStopsWhenTheResultIsSettledAndNotBefore();
  testTheBoardsFreePointCountIsTheOneTheEngineDecidesOn();
  testTheExplanationYieldsToAnythingMoreUrgent();
  testTheEngineIsToldAboutTheKo();
  testEasyIsWeakWithoutLookingBroken();
  testTheLargeBoardIsTheSameGameOnMorePoints();
  testResetClearsTheTailOfTheLargerBoard();
  testTheFullBoardNumbersEveryPointPastTheByte();
  testKoPastTheByteIsTheRightPoint();
  testTheFullBoardTakesItsHandicapOnTheFourFourPoints();
  testTheBoardSettingStepsThroughAllThreeAndBack();
  testAFullBoardSaveComesBackExactlyAndFitsItsLine();
  testTheEngineFindsTheDeadStoneOnTheFullBoard();
  testTheOpponentPlaysLegallyOnTheFullBoard();
  testTheOpponentBeatsARandomMoverAtEveryLevel();

  // Last, deliberately: it seeds michi's generator, and every test after it
  // would draw from a different stream than the one it was written against.
  testADifferentSeedPlaysADifferentGameAndTheSameSeedReplays();
  testAClockThatNeverRunsOutChangesNothing();

  std::printf("%d checks, %d failed\n", checks, failures);

  return failures == 0 ? 0 : 1;
}
