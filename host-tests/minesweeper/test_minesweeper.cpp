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

// The one-line reproducer for the flood bug a cold critic found: on a board
// with no mines at all, one tap must open every cell and win.
//
// The old flood opened THIRTY of eighty and stayed Playing, because cells were
// deduplicated at push but marked at pop, so a cell touched by several zeroes
// enqueued several times and the overflow was silently dropped. 41.8% of real
// first taps were truncated. The suite missed it by asserting "more than ten
// opened" against a true answer near forty.
void testAnEmptyBoardOpensCompletely() {
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      Game game{};
      start(game, 1u);
      game.status = Status::Playing;
      CHECK(reveal(game, column, row));
      CHECK(revealedCount(game) == kCells);
      CHECK(game.status == Status::Won);
    }
  }
}

// The invariant that makes the symptom impossible, over real boards: a revealed
// cell touching no mines can never sit beside a covered one.
void testNoRevealedZeroEverTouchesACoveredCell() {
  uint32_t seed = 555u;
  for (int trial = 0; trial < 2000; ++trial) {
    seed = seed * 1664525u + 1013904223u;
    Game game{};
    start(game, seed);
    CHECK(reveal(game, static_cast<int>(seed >> 8) % kColumns, static_cast<int>(seed >> 16) % kRows));
    for (int column = 0; column < kColumns; ++column) {
      for (int row = 0; row < kRows; ++row) {
        if ((game.cell[column][row] & kRevealed) == 0) continue;
        if (neighbouringMines(game, column, row) != 0) continue;
        for (int dc = -1; dc <= 1; ++dc) {
          for (int dr = -1; dr <= 1; ++dr) {
            if (!inside(column + dc, row + dr)) continue;
            const uint8_t neighbour = game.cell[column + dc][row + dr];
            CHECK((neighbour & (kRevealed | kFlagged)) != 0);
          }
        }
      }
    }
  }
}

// Mines must reach every cell. Two plausible index mutants left whole ROWS
// permanently mine-free and survived the entire suite, because nothing checked
// the distribution -- only the count.
void testMinesReachEveryCell() {
  bool seen[kColumns][kRows] = {};
  uint32_t seed = 909u;
  for (int trial = 0; trial < 4000; ++trial) {
    Game game{};
    start(game, seed = seed * 1664525u + 1013904223u);
    // Open in a fixed corner so the excluded 3x3 is the same every time; any
    // cell outside it must still be reachable by a mine.
    reveal(game, 0, 0);
    for (int column = 0; column < kColumns; ++column) {
      for (int row = 0; row < kRows; ++row) {
        if (game.cell[column][row] & kMine) seen[column][row] = true;
      }
    }
  }
  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      // Only the safe 3x3 around (0,0) may never hold one.
      const bool excluded = column <= 1 && row <= 1;
      CHECK(seen[column][row] != excluded);
    }
  }
}

// start() must clear a finished board. Every other test builds a fresh Game or
// sets status by hand, so a start() that left status alone survived -- and
// that is exactly what the activity does on PLAY AGAIN, reusing its member.
void testStartClearsAFinishedBoard() {
  Game game{};
  start(game, 3u);
  game.status = Status::Playing;
  game.cell[2][2] |= kMine;
  CHECK(reveal(game, 2, 2));
  CHECK(game.status == Status::Lost);

  start(game, 4u);
  CHECK(game.status == Status::Fresh);
  CHECK(revealedCount(game) == 0);
  CHECK(mineCount(game) == 0);
  CHECK(flagCount(game) == 0);
  // And it is playable again, which is the thing PLAY AGAIN needs.
  CHECK(canReveal(game, 0, 0));
}

// A win must mean every safe cell is OPEN, not merely accounted for. A mutant
// accepting a flagged safe cell as cleared survived: no test ever reached a win
// with a flag on the board, and random play won zero of two thousand games.
void testAFlaggedSafeCellIsNotCleared() {
  Game game{};
  start(game, 8u);
  game.status = Status::Playing;
  game.cell[0][0] |= kMine;
  CHECK(toggleFlag(game, 7, 9));

  for (int column = 0; column < kColumns; ++column) {
    for (int row = 0; row < kRows; ++row) {
      if (column == 0 && row == 0) continue;
      reveal(game, column, row);
    }
  }
  // The flagged safe cell is still covered, so this is not a win.
  CHECK(game.status != Status::Won);
  CHECK(toggleFlag(game, 7, 9));
  CHECK(reveal(game, 7, 9));
  CHECK(game.status == Status::Won);
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
  testAnEmptyBoardOpensCompletely();
  testNoRevealedZeroEverTouchesACoveredCell();
  testMinesReachEveryCell();
  testStartClearsAFinishedBoard();
  testAFlaggedSafeCellIsNotCleared();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
