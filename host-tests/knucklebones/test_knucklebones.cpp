// The Knucklebones rulebook, checked without a panel. KnucklebonesCore is
// freestanding C++17, so the whole game is testable on a laptop and only the
// screens and the activity need hardware.
//
// Two kinds of test here. The first half pins each rule against a hand-built
// position, because a soak that only checks invariants will happily agree that
// a wrong multiplier is consistently wrong. The second half plays whole random
// matches and asserts the invariants after every single move, which is what
// catches the states nobody thought to hand-build.

#include <cstdio>
#include <cstring>

#include "KnucklebonesCore.h"
#include "KnucklebonesFlow.h"

using namespace knucklebones;

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

// A grid from a literal, column by column, near end first. Zero is empty, so
// {{4,4,4},{0,0,0},{0,0,0}} is a full left column and nothing else.
Grid gridOf(const uint8_t cells[kColumns][kRows]) {
  Grid grid{};
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) grid.cell[column][row] = cells[column][row];
  }
  return grid;
}

bool noHoles(const Grid& grid) {
  for (int column = 0; column < kColumns; ++column) {
    bool seenEmpty = false;
    for (int row = 0; row < kRows; ++row) {
      if (grid.cell[column][row] == kEmpty) seenEmpty = true;
      // A die above a gap: gravity broken, and it would draw floating.
      else if (seenEmpty)
        return false;
    }
  }
  return true;
}

void testAColumnMultipliesItsMatches() {
  const uint8_t cells[kColumns][kRows] = {{4, 4, 4}, {3, 3, 5}, {6, 0, 0}};
  const Grid grid = gridOf(cells);

  // Three fours are 4*3*3, not 4+4+4. That multiplier is the whole game, so it
  // is asserted as a number rather than as "more than the sum".
  CHECK(columnScore(grid, 0) == 36);
  // A pair and a loose die: 3*2*2 + 5.
  CHECK(columnScore(grid, 1) == 17);
  CHECK(columnScore(grid, 2) == 6);
  CHECK(score(grid) == 59);

  const Grid empty{};
  CHECK(score(empty) == 0);
}

void testPlacingDestroysOnlyTheMatchingColumn() {
  Game game{};
  start(game, 1234);

  const uint8_t mine[kColumns][kRows] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  // Same value in the target column and in another one, plus a different value
  // alongside it, so the test can tell "destroys fives" from "destroys column".
  const uint8_t theirs[kColumns][kRows] = {{5, 2, 5}, {5, 0, 0}, {0, 0, 0}};
  game.grid[0] = gridOf(mine);
  game.grid[1] = gridOf(theirs);
  game.turn = 0;
  game.die = 5;

  CHECK(place(game, 0));

  // Both fives in column 0 are gone, the two survives, and it has fallen to the
  // near end rather than leaving a hole where the first five was.
  CHECK(game.grid[1].cell[0][0] == 2);
  CHECK(game.grid[1].cell[0][1] == kEmpty);
  CHECK(game.grid[1].cell[0][2] == kEmpty);
  // The five in the next column is untouched: destruction reaches down one
  // column, never across the board.
  CHECK(game.grid[1].cell[1][0] == 5);
  CHECK(noHoles(game.grid[1]));

  CHECK(game.grid[0].cell[0][0] == 5);
  CHECK(game.turn == 1);
}

void testDiceStackFromTheNearEnd() {
  Game game{};
  start(game, 99);
  game.turn = 0;

  game.die = 1;
  CHECK(place(game, 2));
  game.turn = 0;
  game.die = 2;
  CHECK(place(game, 2));

  CHECK(game.grid[0].cell[2][0] == 1);
  CHECK(game.grid[0].cell[2][1] == 2);
  CHECK(game.grid[0].cell[2][2] == kEmpty);
  CHECK(columnCount(game.grid[0], 2) == 2);
}

void testAFullColumnRefusesAndChangesNothing() {
  Game game{};
  start(game, 7);
  const uint8_t mine[kColumns][kRows] = {{1, 2, 3}, {0, 0, 0}, {0, 0, 0}};
  game.grid[0] = gridOf(mine);
  game.turn = 0;
  game.die = 4;

  const Game before = game;
  CHECK(!canPlace(game, 0));
  CHECK(!place(game, 0));
  // Refusing is not enough: an illegal move that half-applied would desync a
  // two-device match, so the whole state has to be untouched.
  CHECK(std::memcmp(&before, &game, sizeof(Game)) == 0);

  // Out of range is refused the same way rather than running off the grid.
  CHECK(!place(game, -1));
  CHECK(!place(game, kColumns));
  CHECK(std::memcmp(&before, &game, sizeof(Game)) == 0);
}

void testTheGameEndsWhenEitherGridFills() {
  Game game{};
  start(game, 42);
  const uint8_t nearlyFull[kColumns][kRows] = {{1, 2, 3}, {1, 2, 3}, {1, 2, kEmpty}};
  game.grid[0] = gridOf(nearlyFull);
  game.turn = 0;
  game.die = 6;

  CHECK(!over(game));
  CHECK(place(game, 2));
  CHECK(over(game));
  // No roll once it is over: a die left in the state would draw on the final
  // screen with nowhere to put it.
  CHECK(game.die == kEmpty);
  CHECK(!canPlace(game, 0));
}

void testTheWinnerIsTheHigherTotal() {
  Game game{};
  const uint8_t big[kColumns][kRows] = {{6, 6, 6}, {0, 0, 0}, {0, 0, 0}};
  const uint8_t small[kColumns][kRows] = {{1, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  game.grid[0] = gridOf(big);
  game.grid[1] = gridOf(small);
  CHECK(winner(game) == 0);

  game.grid[0] = gridOf(small);
  game.grid[1] = gridOf(big);
  CHECK(winner(game) == 1);

  game.grid[1] = gridOf(small);
  CHECK(winner(game) == -1);
}

void testTheSameSeedDealsTheSameGame() {
  Game a{};
  Game b{};
  start(a, 20260808u);
  start(b, 20260808u);
  // Every roll, not just the first: a core that reached for a clock anywhere
  // would diverge here, and a match between two devices depends on it not.
  for (int move = 0; move < 9 && !over(a); ++move) {
    CHECK(a.die == b.die);
    for (int column = 0; column < kColumns; ++column) {
      if (canPlace(a, column)) {
        CHECK(place(a, column));
        CHECK(place(b, column));
        break;
      }
    }
  }
  CHECK(std::memcmp(&a, &b, sizeof(Game)) == 0);
}

// Whole matches of random legal play, with every invariant checked after every
// move. This is the half that finds the positions nobody thought to write down.
void testRandomMatchesHoldEveryInvariant() {
  uint32_t seed = 13579u;
  int longestMatch = 0;
  for (int match = 0; match < 2000; ++match) {
    Game game{};
    start(game, seed = seed * 1664525u + 1013904223u);

    int moves = 0;
    while (!over(game)) {
      const int seat = game.turn;
      CHECK(seat == 0 || seat == 1);
      CHECK(game.die >= 1 && game.die <= kFaces);

      // Pick uniformly among the legal columns, so the soak explores rather
      // than always filling the leftmost.
      int legal[kColumns];
      int count = 0;
      for (int column = 0; column < kColumns; ++column) {
        if (canPlace(game, column)) legal[count++] = column;
      }
      CHECK(count > 0);
      if (count == 0) break;

      const int chosen = legal[nextRandom(seed) % static_cast<uint32_t>(count)];
      const int before = columnCount(game.grid[seat], chosen);
      CHECK(place(game, chosen));
      CHECK(columnCount(game.grid[seat], chosen) == before + 1);
      CHECK(game.turn == (over(game) ? seat : 1 - seat));

      for (int s = 0; s < kSeats; ++s) {
        CHECK(noHoles(game.grid[s]));
        for (int column = 0; column < kColumns; ++column) {
          for (int row = 0; row < kRows; ++row) {
            const uint8_t value = game.grid[s].cell[column][row];
            CHECK(value == kEmpty || (value >= 1 && value <= kFaces));
          }
        }
        // Derived rather than tracked, so this asserts the two agree.
        int recomputed = 0;
        for (int column = 0; column < kColumns; ++column) recomputed += columnScore(game.grid[s], column);
        CHECK(score(game.grid[s]) == recomputed);
      }

      // NOT bounded by the eighteen slots on the board: a placement fills one
      // of the mover's own, but it can destroy up to three of the opponent's,
      // so a match can run much longer than the grids are big. The cap is a
      // liveness guard against a rule change that makes a game unable to end;
      // the real length is reported below so a future change that lengthens
      // matches is visible rather than silently absorbed.
      CHECK(++moves <= 500);
      if (moves > 500) break;
    }
    CHECK(over(game));
    CHECK(game.die == kEmpty);
    CHECK(winner(game) >= -1 && winner(game) <= 1);
    if (moves > longestMatch) longestMatch = moves;
  }
  std::printf("  longest match over 2000: %d placements\n", longestMatch);
}

// --- the two state machines ------------------------------------------------
//
// The bug this project keeps writing is "Back went to the wrong screen", and it
// is not one you find by reading an activity. These assert the properties that
// make the navigation correct rather than merely written down.

void testBackIsTotalAndAlwaysReachesTheTop() {
  const Screen every[] = {Screen::Menu, Screen::HowTo, Screen::Board, Screen::Result};

  for (const Screen screen : every) {
    // Walk Back repeatedly. Every screen must reach the top, and it must do so
    // without cycling: a pair of screens that Back into each other is exactly
    // the trap that strands a player, and it would spin here instead.
    Screen at = screen;
    int steps = 0;
    while (!leavesApp(at)) {
      at = back(at);
      CHECK(++steps <= 4);
      if (steps > 4) break;
    }
    CHECK(leavesApp(at));
  }

  // Exactly one screen leaves the app. If a second one did, Back would mean
  // "quit" somewhere the player expected "go up".
  int exits = 0;
  for (const Screen screen : every) {
    if (leavesApp(screen)) ++exits;
  }
  CHECK(exits == 1);

  // And the specific answers, so a reshuffle that keeps the properties but
  // breaks the intent still fails.
  CHECK(back(Screen::HowTo) == Screen::Menu);
  CHECK(back(Screen::Result) == Screen::Menu);
  // Abandoning a match goes to the menu, not out of the app: the first Back
  // means stop playing, the second means leave.
  CHECK(back(Screen::Board) == Screen::Menu);
  CHECK(!leavesApp(Screen::Board));
}

void testPhaseIsDerivedAndOnlyYourTurnAccepts() {
  CHECK(phaseFor(false, true) == Phase::Yours);
  CHECK(phaseFor(false, false) == Phase::Theirs);
  // Finished beats whose turn it is, in both directions: a match that ended on
  // the opponent's placement is still over.
  CHECK(phaseFor(true, true) == Phase::Finished);
  CHECK(phaseFor(true, false) == Phase::Finished);

  // The board is drawn in every phase -- you watch the opponent place -- but it
  // takes a column in exactly one of them.
  CHECK(acceptsPlacement(Phase::Yours));
  CHECK(!acceptsPlacement(Phase::Theirs));
  CHECK(!acceptsPlacement(Phase::Finished));
}

// The two machines must not be able to disagree with the rules. This walks a
// real game and checks the phase the flow reports against the core's own view
// after every move, which is what stops the board accepting a tap on a turn
// that is not yours.
void testTheFlowAgreesWithTheRulesEveryMove() {
  Game game{};
  start(game, 2468u);
  int guard = 0;
  while (!over(game)) {
    const Phase phase = phaseFor(over(game), game.turn == 0);
    CHECK(phase != Phase::Finished);
    CHECK(acceptsPlacement(phase) == (game.turn == 0));
    for (int column = 0; column < kColumns; ++column) {
      if (canPlace(game, column)) {
        CHECK(place(game, column));
        break;
      }
    }
    CHECK(++guard <= 500);
    if (guard > 500) break;
  }
  CHECK(phaseFor(over(game), game.turn == 0) == Phase::Finished);
  CHECK(!acceptsPlacement(phaseFor(over(game), game.turn == 0)));
}

}  // namespace

int main() {
  testAColumnMultipliesItsMatches();
  testPlacingDestroysOnlyTheMatchingColumn();
  testDiceStackFromTheNearEnd();
  testAFullColumnRefusesAndChangesNothing();
  testTheGameEndsWhenEitherGridFills();
  testTheWinnerIsTheHigherTotal();
  testTheSameSeedDealsTheSameGame();
  testRandomMatchesHoldEveryInvariant();
  testBackIsTotalAndAlwaysReachesTheTop();
  testPhaseIsDerivedAndOnlyYourTurnAccepts();
  testTheFlowAgreesWithTheRulesEveryMove();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
