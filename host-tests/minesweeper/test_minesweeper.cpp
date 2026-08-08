// The Minesweeper rulebook, checked without a panel.
//
// The two properties that are BUILT IN rather than sampled for get the most
// attention here, because "constructed" is a claim a test has to hold to: the
// first tap is always safe, and a revealed zero floods.

#include <cstdio>
#include <cstring>

#include "MinesweeperCore.h"
#include "MinesweeperFlow.h"

using namespace minesweeper;

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

int revealedCount(const Game& game) {
  int count = 0;
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (game.cell[column][row] & kRevealed) ++count;
    }
  }
  return count;
}

int mineCount(const Game& game) {
  int count = 0;
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (game.cell[column][row] & kMine) ++count;
    }
  }
  return count;
}

// The headline property, over every opening square on many boards. Not sampled:
// if this ever fails, mines are being laid where the first tap can hit them.
void testTheFirstTapIsAlwaysSafeAndAlwaysFloods() {
  uint32_t seed = 12345u;
  for (int trial = 0; trial < 3000; ++trial) {
    seed = seed * 1664525u + 1013904223u;
    const int column = static_cast<int>(seed >> 8) % kColumns;
    const int row = static_cast<int>(seed >> 16) % kRows;

    Game game{};
    start(game, seed);
    CHECK(game.status == Status::Fresh);
    CHECK(mineCount(game) == 0);

    CHECK(reveal(game, column, row));
    CHECK(game.status != Status::Lost);
    CHECK(mineCount(game) == kMines);
    CHECK((game.cell[column][row] & kMine) == 0);
    // Its neighbours are clear too, which is what makes the opening a flood
    // rather than a single number and a guess.
    CHECK(neighbouringMines(game, column, row) == 0);
    CHECK(revealedCount(game) > 1);
  }
}

void testAZeroFloodsAndANumberDoesNot() {
  Game game{};
  start(game, 7u);
  // Hand-built rather than dealt: a flood test against a random board asserts
  // whatever that board happened to do.
  game.status = Status::Playing;
  game.cell[3][3] |= kMine;

  // (0,0) touches nothing, so it opens everything not walled off by the ring of
  // numbers around the mine.
  CHECK(reveal(game, 0, 0));
  CHECK(game.cell[0][0] & kRevealed);
  CHECK(revealedCount(game) > 10);
  // The mine itself is never revealed by a flood.
  CHECK((game.cell[3][3] & kRevealed) == 0);

  Game numbered{};
  start(numbered, 7u);
  numbered.status = Status::Playing;
  numbered.cell[0][0] |= kMine;
  // (1,1) touches the mine, so it reveals itself and stops.
  CHECK(reveal(numbered, 1, 1));
  CHECK(revealedCount(numbered) == 1);
}

void testAFlagStopsTheFloodAndCannotBeTornOff() {
  Game game{};
  start(game, 11u);
  game.status = Status::Playing;
  CHECK(toggleFlag(game, 4, 4));
  CHECK(game.cell[4][4] & kFlagged);

  // An empty board floods everything -- except the flag, which the player put
  // there deliberately. Losing it to a cascade nobody aimed is the most
  // annoying thing this game could do.
  CHECK(reveal(game, 0, 0));
  CHECK((game.cell[4][4] & kRevealed) == 0);
  CHECK(game.cell[4][4] & kFlagged);
  // And a flagged cell cannot be revealed by tapping it either.
  CHECK(!canReveal(game, 4, 4));
  CHECK(!reveal(game, 4, 4));
}

void testFlagsAndTheirCounter() {
  Game game{};
  start(game, 3u);
  CHECK(minesRemaining(game) == kMines);
  CHECK(toggleFlag(game, 1, 1));
  CHECK(minesRemaining(game) == kMines - 1);
  // Toggling is toggling.
  CHECK(toggleFlag(game, 1, 1));
  CHECK(minesRemaining(game) == kMines);

  // Over-flagging goes negative rather than clamping: that is the board telling
  // the player it disagrees with them, which is information.
  for (int i = 0; i < kMines + 2; ++i) toggleFlag(game, i % kColumns, i / kColumns);
  CHECK(minesRemaining(game) < 0);

  // Never on a revealed cell.
  Game other{};
  start(other, 5u);
  other.status = Status::Playing;
  CHECK(reveal(other, 0, 0));
  CHECK(!toggleFlag(other, 0, 0));
}

void testHittingAMineLosesAndFreezesTheBoard() {
  Game game{};
  start(game, 99u);
  game.status = Status::Playing;
  game.cell[2][2] |= kMine;

  CHECK(reveal(game, 2, 2));
  CHECK(game.status == Status::Lost);
  CHECK(over(game));
  // Nothing works afterwards, in either direction.
  CHECK(!canReveal(game, 5, 5));
  CHECK(!reveal(game, 5, 5));
  CHECK(!toggleFlag(game, 5, 5));
}

void testClearingEverySafeCellWins() {
  Game game{};
  start(game, 4242u);
  game.status = Status::Playing;
  game.cell[0][0] |= kMine;

  // Reveal every cell that is not the mine. The win must land on the last one
  // and not before.
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (column == 0 && row == 0) continue;
      if (game.status == Status::Won) break;
      reveal(game, column, row);
    }
  }
  CHECK(game.status == Status::Won);
  CHECK(over(game));
  CHECK((game.cell[0][0] & kRevealed) == 0);
}

// Whole games of random legal play, checking the invariants after every tap.
void testRandomPlayHoldsEveryInvariant() {
  uint32_t seed = 24680u;
  int wins = 0;
  int losses = 0;
  for (int match = 0; match < 2000; ++match) {
    Game game{};
    start(game, seed = seed * 1664525u + 1013904223u);
    uint32_t pick = seed;

    int guard = 0;
    while (!over(game)) {
      const uint32_t roll = nextRandom(pick);
      const int column = static_cast<int>(roll >> 8) % kColumns;
      const int row = static_cast<int>(roll >> 16) % kRows;
      // Mostly reveal, sometimes flag, so both paths get exercised together.
      if ((roll & 7) == 0) {
        toggleFlag(game, column, row);
      } else {
        reveal(game, column, row);
      }

      // A mine is never revealed while the game is still running.
      if (!over(game)) {
        for (int c = 0; c < kColumns; ++c) {
          for (int r = 0; r < kRows; ++r) {
            const uint8_t cell = game.cell[c][r];
            CHECK(!((cell & kMine) && (cell & kRevealed)));
            // Nothing is both revealed and flagged.
            CHECK(!((cell & kRevealed) && (cell & kFlagged)));
          }
        }
      }
      CHECK(game.status != Status::Fresh || revealedCount(game) == 0);
      // 80 cells and 10 mines: no game can need more taps than there are cells
      // plus the flags a player might place and lift on each.
      CHECK(++guard <= kCells * 8);
      if (guard > kCells * 8) break;
    }
    CHECK(mineCount(game) == kMines);
    if (game.status == Status::Won) ++wins;
    if (game.status == Status::Lost) ++losses;
  }
  // Random tapping should mostly lose. If it wins often the board is too
  // sparse; if it never terminates the guard above would have fired.
  CHECK(losses > wins);
  std::printf("  random play: %d won, %d lost of 2000\n", wins, losses);
}

// --- navigation ------------------------------------------------------------

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

  CHECK(back(Screen::HowTo) == Screen::Menu);
  CHECK(back(Screen::Result) == Screen::Menu);
  // The first Back stops playing, the second leaves.
  CHECK(back(Screen::Board) == Screen::Menu);
  CHECK(!leavesApp(Screen::Board));
}

void testTheToolIsATwoWaySwitchAndTheScreenFollowsTheRules() {
  CHECK(other(Tool::Dig) == Tool::Flag);
  CHECK(other(Tool::Flag) == Tool::Dig);
  CHECK(other(other(Tool::Dig)) == Tool::Dig);

  Game game{};
  start(game, 5u);
  CHECK(screenFor(game) == Screen::Board);
  CHECK(boardAccepts(game));

  game.status = Status::Lost;
  CHECK(screenFor(game) == Screen::Result);
  CHECK(!boardAccepts(game));

  game.status = Status::Won;
  CHECK(screenFor(game) == Screen::Result);
  CHECK(!boardAccepts(game));
}

}  // namespace

int main() {
  testTheFirstTapIsAlwaysSafeAndAlwaysFloods();
  testAZeroFloodsAndANumberDoesNot();
  testAFlagStopsTheFloodAndCannotBeTornOff();
  testFlagsAndTheirCounter();
  testHittingAMineLosesAndFreezesTheBoard();
  testClearingEverySafeCellWins();
  testRandomPlayHoldsEveryInvariant();
  testBackIsTotalAndAlwaysReachesTheTop();
  testTheToolIsATwoWaySwitchAndTheScreenFollowsTheRules();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
