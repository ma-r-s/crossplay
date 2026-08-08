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

#include "KnucklebonesBrain.h"
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

// --- the opponent ----------------------------------------------------------

void testTheOpponentOnlyEverPlaysALegalColumn() {
  uint32_t seed = 24680u;
  for (int match = 0; match < 2000; ++match) {
    Game game{};
    start(game, seed = seed * 1664525u + 1013904223u);
    int guard = 0;
    while (!over(game)) {
      const int column = chooseColumn(game);
      CHECK(column >= 0 && column < kColumns);
      if (column < 0) break;
      CHECK(canPlace(game, column));
      CHECK(place(game, column));
      CHECK(++guard <= 500);
      if (guard > 500) break;
    }
    CHECK(over(game));
  }
}

void testTheOpponentNeverMutatesTheGameItIsGiven() {
  Game game{};
  start(game, 555u);
  const Game before = game;
  (void)chooseColumn(game);
  // It searches on copies. If it ever played on the real board, a match would
  // desync the moment the opponent thought.
  CHECK(std::memcmp(&before, &game, sizeof(Game)) == 0);
}

void testTheOpponentIsDeterministic() {
  // Same position, same answer, every time and on every device. A random
  // tiebreak would make a seeded match unreplayable, which is what both the
  // two-device path and a reproducible screenshot depend on.
  Game game{};
  start(game, 31337u);
  while (!over(game)) {
    const int first = chooseColumn(game);
    for (int repeat = 0; repeat < 8; ++repeat) CHECK(chooseColumn(game) == first);
    if (first < 0) break;
    CHECK(place(game, first));
  }
}

void testTheOpponentTakesAnObviousStackAndAnObviousWreck() {
  // A three waiting to join two threes: 3*9 = 27 against 3*4 = 12, so the
  // column it is already in is worth 15 more than starting a new one.
  Game build{};
  start(build, 1u);
  const uint8_t mine[kColumns][kRows] = {{3, 3, kEmpty}, {kEmpty, kEmpty, kEmpty}, {kEmpty, kEmpty, kEmpty}};
  build.grid[0] = gridOf(mine);
  build.turn = 0;
  build.die = 3;
  CHECK(chooseColumn(build) == 0);

  // Now the same die, but column 2 holds three of the opponent's fives -- 75
  // points -- and this device has nothing to build. Wrecking beats stacking.
  Game wreck{};
  start(wreck, 1u);
  const uint8_t empty[kColumns][kRows] = {{kEmpty, kEmpty, kEmpty}, {kEmpty, kEmpty, kEmpty}, {kEmpty, kEmpty, kEmpty}};
  const uint8_t theirs[kColumns][kRows] = {{kEmpty, kEmpty, kEmpty}, {kEmpty, kEmpty, kEmpty}, {5, 5, 5}};
  wreck.grid[0] = gridOf(empty);
  wreck.grid[1] = gridOf(theirs);
  wreck.turn = 0;
  wreck.die = 5;
  CHECK(chooseColumn(wreck) == 2);
}

// --- what a cold critic proved the suite could not see ---------------------

void testMatchingDiceScoreWhereverTheySitInTheColumn() {
  // Every hand-built literal elsewhere puts duplicates in ADJACENT slots, and
  // the soak's score assertion is verbatim score()'s own body, so it cannot
  // fail. A columnScore that counted only adjacent runs survived the entire
  // suite -- and split pairs are not exotic: compaction preserves order, so
  // destruction leaves [3,5,3] in 62% of random matches.
  const uint8_t split[kColumns][kRows] = {{3, 5, 3}, {4, 2, 4}, {6, 6, 1}};
  const Grid grid = gridOf(split);
  CHECK(columnScore(grid, 0) == 17);  // 3*2*2 + 5
  CHECK(columnScore(grid, 1) == 18);  // 4*2*2 + 2
  CHECK(columnScore(grid, 2) == 25);  // 6*2*2 + 1
}

void testTheDieIsActuallyASixSidedDie() {
  // Nothing asserted this. A roll() returning only 1..3, and one returning a
  // plain 1-6 cycle with no randomness at all, both survived the whole suite.
  int seen[kFaces + 1] = {};
  uint32_t state = 4242u;
  for (int i = 0; i < 60000; ++i) {
    const uint8_t value = roll(state);
    CHECK(value >= 1 && value <= kFaces);
    ++seen[value];
  }
  // Every face appears, and none dominates. Loose enough never to flake, tight
  // enough that a three-sided die or a fixed cycle fails.
  for (int face = 1; face <= kFaces; ++face) {
    CHECK(seen[face] > 60000 / kFaces / 2);
    CHECK(seen[face] < 60000 / kFaces * 2);
  }
  // A cycle would have zero repeats; real dice repeat about one roll in six.
  int repeats = 0;
  state = 99u;
  uint8_t previous = roll(state);
  for (int i = 0; i < 6000; ++i) {
    const uint8_t value = roll(state);
    if (value == previous) ++repeats;
    previous = value;
  }
  CHECK(repeats > 300);
}

void testCompactionKeepsTheOrderOfTheSurvivors() {
  // A compact() that REVERSED the survivors survived the suite: the only other
  // compaction test leaves a single survivor, and scoring is order-independent,
  // so nothing but the panel would ever show it.
  Grid grid{};
  grid.cell[0][0] = 2;
  grid.cell[0][1] = 5;
  grid.cell[0][2] = 3;
  grid.cell[0][1] = kEmpty;
  compact(grid, 0);
  CHECK(grid.cell[0][0] == 2);
  CHECK(grid.cell[0][1] == 3);
  CHECK(grid.cell[0][2] == kEmpty);
}

void testAPlacementNeedsADie() {
  // A value-initialised Game is exactly what a device holds while waiting for
  // the first move of a nearby match. place() used to return true on it,
  // place nothing, and consume the turn.
  Game blank{};
  CHECK(!canPlace(blank, 0));
  const Game before = blank;
  CHECK(!place(blank, 0));
  CHECK(std::memcmp(&before, &blank, sizeof(Game)) == 0);
}

void testAnImplausibleStateIsRejected() {
  // The Game is the wire format and the layer beneath checks length only, so a
  // corrupt turn byte would index past grid[2].
  Game good{};
  start(good, 7u);
  CHECK(plausible(good));

  Game badTurn = good;
  badTurn.turn = 9;
  CHECK(!plausible(badTurn));

  Game badFace = good;
  badFace.grid[0].cell[0][0] = 7;
  CHECK(!plausible(badFace));

  // A die floating above a gap cannot have come from this build.
  Game floating = good;
  floating.grid[0].cell[1][1] = 4;
  CHECK(!plausible(floating));

  // An untouched board is legitimate: it is what the follower holds.
  Game fresh{};
  CHECK(plausible(fresh));
}

void testTheOpponentSpreadsItsOpening() {
  // With an empty board every column evaluates identically, so the TIEBREAK
  // decides -- and a cold review measured it at 43% of all its moves. Breaking
  // toward the lowest index put its first three dice in column 0 in 100% of
  // games, burying the opening where it could not start a second stack.
  int firstColumn[kColumns] = {};
  uint32_t seed = 8080u;
  for (int match = 0; match < 300; ++match) {
    Game game{};
    start(game, seed = seed * 1664525u + 1013904223u);
    const int column = chooseColumn(game);
    CHECK(column >= 0 && column < kColumns);
    if (column >= 0) ++firstColumn[column];
  }
  // Not uniformity -- the die still decides plenty. Just that no single column
  // takes every opening.
  for (int column = 0; column < kColumns; ++column) CHECK(firstColumn[column] < 300);
}

void testTheOpponentCannotSeeTheDiceComing() {
  // The state it is handed carries the generator, and the generator advances
  // once per placement, so that field is the whole future. Two positions that
  // differ ONLY in the die stream must play the same, or the opponent is
  // reading the future whether it means to or not.
  Game a{};
  start(a, 11111u);
  Game b = a;
  b.rng = 99999u;
  CHECK(chooseColumn(a) == chooseColumn(b));
}

void testTheOpponentIsBetterThanChance() {
  // The brain had no strength test at all, only legality and purity -- which is
  // how a tiebreak that dumps its first three dice into one column sat there.
  int brainWins = 0;
  int randomWins = 0;
  uint32_t seed = 777u;
  for (int match = 0; match < 400; ++match) {
    Game game{};
    start(game, seed = seed * 1664525u + 1013904223u);
    uint32_t coin = seed;
    int guard = 0;
    while (!over(game)) {
      int column;
      if (game.turn == 0) {
        column = chooseColumn(game);
      } else {
        int legal[kColumns];
        int count = 0;
        for (int c = 0; c < kColumns; ++c) {
          if (canPlace(game, c)) legal[count++] = c;
        }
        column = count > 0 ? legal[nextRandom(coin) % static_cast<uint32_t>(count)] : -1;
      }
      if (column < 0) break;
      if (!place(game, column)) break;
      if (++guard > 500) break;
    }
    const int mine = score(game.grid[0]);
    const int theirs = score(game.grid[1]);
    if (mine > theirs) ++brainWins;
    if (theirs > mine) ++randomWins;
  }
  // Deterministic inputs, so this is a fixed number rather than a flaky one.
  // A brain that loses to coin-flipping is not an opponent.
  CHECK(brainWins > randomWins * 2);
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
  testTheOpponentOnlyEverPlaysALegalColumn();
  testTheOpponentNeverMutatesTheGameItIsGiven();
  testTheOpponentIsDeterministic();
  testTheOpponentTakesAnObviousStackAndAnObviousWreck();
  testMatchingDiceScoreWhereverTheySitInTheColumn();
  testTheDieIsActuallyASixSidedDie();
  testCompactionKeepsTheOrderOfTheSurvivors();
  testAPlacementNeedsADie();
  testAnImplausibleStateIsRejected();
  testTheOpponentSpreadsItsOpening();
  testTheOpponentCannotSeeTheDiceComing();
  testTheOpponentIsBetterThanChance();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
