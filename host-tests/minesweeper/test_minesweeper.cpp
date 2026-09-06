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

// testTheScreenFollowsTheRules is gone with the helpers it pinned (screenFor,
// boardAccepts): a settled game no longer implies the Result screen -- the
// board stays, wearing its verdict, and Result is a door the player takes.
// The new rule is pinned where it lives, in the ui suite
// (testTheSettledBoardStaysAndWearsItsVerdict). Deleting the helpers without
// this file noticing is also how the whole suite went dark for a release:
// the compile error sat in two check logs whose summaries were read through
// a tail window that cut it off.

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

// --- chording ---------------------------------------------------------------
//
// Tapping a revealed number whose flags already equal it opens every remaining
// neighbour at once. The card called it "the difference between playing and
// clicking".
//
// The rule the owner picked is the unforgiving one, which is the real game: a
// chord trusts the player's flags, so chording a number whose flags are in the
// WRONG places opens a mine and loses. The gentler version cannot be built
// without the game telling the player something it should not know.
//
// A chord is a SHORTCUT, not a new kind of move, and that is asserted directly
// rather than described: it goes through reveal() cell by cell, so the flood,
// the loss, the win and any saved board see exactly the sequence of taps it
// replaces. testAChordIsExactlyTheTapsItReplaces is that claim.

// A board with one mine at (3,0) and its neighbour (2,1) open, showing 1.
// Flagging the mine satisfies (2,1), so a chord there opens the rest.
Game satisfiedOne() {
  Game game{};
  start(game, 21u);
  game.status = Status::Playing;
  game.cell[3][0] |= kMine;
  // Reveal by hand rather than by reveal(), which would flood the whole board
  // and leave nothing for the chord to open.
  game.cell[2][1] |= kRevealed;
  return game;
}

// Every neighbour opens -- ALL of them, checked one at a time.
//
// The first version of this test could not see the difference between a chord
// and a chord that opened only its first neighbour. Its board had a single mine
// in a corner, so the first neighbour in reading order touched nothing, flooded
// the entire field and WON; every later reveal() was then refused because the
// status was Won, and the assertions passed on cells the flood had opened
// rather than the chord. A mutant revealing one neighbour and stopping survived
// it. (The comment also claimed five neighbours, the array listed six, and the
// true count is seven.)
//
// So: mines in the two far corners as well, which walls the flood off, and the
// expected set is DERIVED from the board rather than typed out.
void testAChordOpensEveryNeighbourWhenTheFlagsMatch() {
  Game game{};
  start(game, 21u);
  game.status = Status::Playing;
  game.cell[3][0] |= kMine;
  // Three more mines placed so that no neighbour of (2,1) is a zero -- each
  // opened cell reveals itself and stops, so nothing floods and the chord is
  // the only thing that could have opened all seven.
  //
  // They must sit OUTSIDE (2,1)'s own eight, or they change the number under
  // test. The first attempt put them at (1,2) and (3,2), which are two of the
  // neighbours, making the cell a 3 that the single flag no longer satisfied.
  // These three are all two cells away in one axis.
  game.cell[0][0] |= kMine;
  game.cell[1][3] |= kMine;
  game.cell[4][3] |= kMine;
  game.cell[2][1] |= kRevealed;

  CHECK(neighbouringMines(game, 2, 1) == 1);
  CHECK(toggleFlag(game, 3, 0));
  CHECK(neighbouringFlags(game, 2, 1) == 1);

  // The expected set, derived: every neighbour that is covered and unflagged.
  int expected[8][2];
  int expectedCount = 0;
  for (int dc = -1; dc <= 1; ++dc) {
    for (int dr = -1; dr <= 1; ++dr) {
      if (dc == 0 && dr == 0) continue;
      const int c = 2 + dc;
      const int r = 1 + dr;
      if (!inside(c, r)) continue;
      if (game.cell[c][r] & (kRevealed | kFlagged)) continue;
      expected[expectedCount][0] = c;
      expected[expectedCount][1] = r;
      ++expectedCount;
    }
  }
  // Seven: eight neighbours less the flagged mine. Asserted so a board edited
  // later cannot quietly shrink what this test covers.
  CHECK(expectedCount == 7);

  const int before = revealedCount(game);
  CHECK(chord(game, 2, 1));
  CHECK(game.status == Status::Playing);
  for (int i = 0; i < expectedCount; ++i) CHECK(game.cell[expected[i][0]][expected[i][1]] & kRevealed);
  // And nothing beyond them: no flood ran, so exactly seven cells changed. This
  // is the half that catches "opened the first one and stopped" in the other
  // direction -- a chord that opened too much would fail here.
  CHECK(revealedCount(game) - before == expectedCount);
  CHECK((game.cell[3][0] & kRevealed) == 0);
  CHECK(game.cell[3][0] & kFlagged);
  // The other mines are still buried: a chord opens neighbours, never mines it
  // was not pointed at.
  CHECK((game.cell[0][0] & kRevealed) == 0);
  CHECK((game.cell[1][3] & kRevealed) == 0);
  CHECK((game.cell[4][3] & kRevealed) == 0);
  // Every neighbour it opened is a number, which is what makes "exactly seven"
  // meaningful rather than lucky.
  for (int i = 0; i < expectedCount; ++i) {
    CHECK(neighbouringMines(game, expected[i][0], expected[i][1]) > 0);
  }
}

void testAChordWithTooFewFlagsDoesNothingAtAll() {
  Game game = satisfiedOne();
  // The number says 1 and no flag has been planted, so the move is refused.
  // Silently: the activity repaints only when the rules report a change, so
  // "false and nothing touched" is how this game says no.
  Game before = game;
  CHECK(neighbouringFlags(game, 2, 1) == 0);
  CHECK(!chord(game, 2, 1));
  CHECK(std::memcmp(&before, &game, sizeof(Game)) == 0);

  // Too MANY flags is refused the same way, and does not partially open.
  CHECK(toggleFlag(game, 3, 0));
  CHECK(toggleFlag(game, 2, 0));
  before = game;
  CHECK(neighbouringFlags(game, 2, 1) == 2);
  CHECK(!chord(game, 2, 1));
  CHECK(std::memcmp(&before, &game, sizeof(Game)) == 0);
}

void testAChordNeedsARevealedNumberAndALiveGame() {
  Game game = satisfiedOne();
  CHECK(toggleFlag(game, 3, 0));

  // A covered cell is not a number yet, whatever it will say later.
  CHECK(!chord(game, 1, 1));
  CHECK((game.cell[1][1] & kRevealed) == 0);
  // Nor is a flagged one.
  CHECK(!chord(game, 3, 0));
  // Nor is anywhere off the board.
  CHECK(!chord(game, -1, 0));
  CHECK(!chord(game, kColumns, kRows));

  // And a settled game accepts nothing, exactly as reveal() and toggleFlag()
  // do not.
  Game settled = satisfiedOne();
  CHECK(toggleFlag(settled, 3, 0));
  settled.status = Status::Lost;
  Game before = settled;
  CHECK(!chord(settled, 2, 1));
  CHECK(std::memcmp(&before, &settled, sizeof(Game)) == 0);
}

void testAChordOnAWrongFlagLosesThroughTheSamePathAsATap() {
  Game game{};
  start(game, 77u);
  game.status = Status::Playing;
  game.cell[3][0] |= kMine;
  game.cell[2][1] |= kRevealed;
  // The count is right and the flag is in the wrong place: the player has
  // asserted (2,0) is the mine, so the chord opens (3,0) and detonates.
  CHECK(toggleFlag(game, 2, 0));
  CHECK(neighbouringFlags(game, 2, 1) == 1);

  CHECK(chord(game, 2, 1));
  CHECK(game.status == Status::Lost);
  CHECK(over(game));
  // The losing path is reveal()'s, so the board is frozen the same way and the
  // struck mine is bared the same way -- there is no second way to lose here.
  CHECK(game.cell[3][0] & kRevealed);
  CHECK(game.cell[3][0] & kMine);
  CHECK(!reveal(game, 7, 9));
  CHECK(!toggleFlag(game, 7, 9));
  CHECK(!chord(game, 2, 1));
}

// --- an independent model of the rules --------------------------------------
//
// The invariant below is the headline claim, and the first version of it could
// not fail. Its reference was a helper called byHand() that was chord()'s loop
// body transcribed, so `chord(a) == byHand(b)` held for ANY implementation of
// reveal, any board and any status -- 585 green checks proving that chord's
// loop is byHand's loop. A test derived from the code's own assumption cannot
// falsify it.
//
// So the reference is written from the RULEBOOK instead, sharing no code with
// MinesweeperCore: its own mine count, its own recursive flood (the core uses
// an explicit queue), its own win check, its own loss handling. A bug anywhere
// in the core's flood, ordering, win detection or loss path now makes the two
// disagree.

int modelMinesAround(const Game& game, const int column, const int row) {
  int count = 0;
  for (int c = column - 1; c <= column + 1; ++c) {
    for (int r = row - 1; r <= row + 1; ++r) {
      if (c == column && r == row) continue;
      if (c < 0 || c >= kColumns || r < 0 || r >= kRows) continue;
      if (game.cell[c][r] & kMine) ++count;
    }
  }
  return count;
}

int modelFlagsAround(const Game& game, const int column, const int row) {
  int count = 0;
  for (int c = column - 1; c <= column + 1; ++c) {
    for (int r = row - 1; r <= row + 1; ++r) {
      if (c == column && r == row) continue;
      if (c < 0 || c >= kColumns || r < 0 || r >= kRows) continue;
      if (game.cell[c][r] & kFlagged) ++count;
    }
  }
  return count;
}

bool modelWon(const Game& game) {
  for (int c = 0; c < kColumns; ++c) {
    for (int r = 0; r < kRows; ++r) {
      if (game.cell[c][r] & kMine) continue;
      if ((game.cell[c][r] & kRevealed) == 0) return false;
    }
  }
  return true;
}

// Recursion, where the core uses a queue. Eighty cells is nothing on a host.
void modelOpen(Game& game, const int column, const int row) {
  if (column < 0 || column >= kColumns || row < 0 || row >= kRows) return;
  if (game.cell[column][row] & (kRevealed | kFlagged)) return;
  game.cell[column][row] |= kRevealed;
  if (modelMinesAround(game, column, row) != 0) return;
  for (int c = column - 1; c <= column + 1; ++c) {
    for (int r = row - 1; r <= row + 1; ++r) {
      if (c == column && r == row) continue;
      modelOpen(game, c, r);
    }
  }
}

// One dig, as the rulebook describes it.
void modelDig(Game& game, const int column, const int row) {
  if (game.status != Status::Playing) return;
  if (column < 0 || column >= kColumns || row < 0 || row >= kRows) return;
  if (game.cell[column][row] & (kRevealed | kFlagged)) return;
  if (game.cell[column][row] & kMine) {
    game.cell[column][row] |= kRevealed;
    game.status = Status::Lost;
    return;
  }
  modelOpen(game, column, row);
  if (modelWon(game)) game.status = Status::Won;
}

// The chord, as the rulebook describes it: a live revealed number carrying
// exactly its flags opens each remaining neighbour, in reading order, and stops
// wherever the game stops.
bool modelChord(Game& game, const int column, const int row) {
  if (game.status != Status::Playing) return false;
  if (column < 0 || column >= kColumns || row < 0 || row >= kRows) return false;
  if ((game.cell[column][row] & kRevealed) == 0) return false;
  if (modelFlagsAround(game, column, row) != modelMinesAround(game, column, row)) return false;
  for (int c = column - 1; c <= column + 1; ++c) {
    for (int r = row - 1; r <= row + 1; ++r) {
      if (c == column && r == row) continue;
      modelDig(game, c, r);
    }
  }
  return true;
}

bool sameGame(const Game& a, const Game& b) { return std::memcmp(&a, &b, sizeof(Game)) == 0; }

// The invariant worth asserting directly: a chord reaches the IDENTICAL board
// state to revealing its remaining neighbours one at a time, in the same order.
// Not a similar state, not the same count -- the same bytes, status and all. It
// is what makes the win check, the loss, the flood and the saved board correct
// for free rather than by a second implementation that has to agree.
//
// Equivalence is to REVEAL, not to dig: a chord does not recurse, so a
// neighbour that becomes satisfied is not itself chorded. modelChord says so by
// calling modelDig rather than itself.
void testAChordIsExactlyTheRevealsItReplaces() {
  // A zero among the neighbours, so the chord has to cascade and not merely
  // uncover six cells. One mine in the corner, one flag on it, and (1,1) is
  // the 1 that gets chorded.
  Game chorded{};
  start(chorded, 31u);
  chorded.status = Status::Playing;
  chorded.cell[0][0] |= kMine;
  chorded.cell[1][1] |= kRevealed;
  CHECK(toggleFlag(chorded, 0, 0));
  Game modelled = chorded;

  CHECK(chord(chorded, 1, 1));
  CHECK(modelChord(modelled, 1, 1));
  CHECK(sameGame(chorded, modelled));
  // It really did cascade rather than stop at the eight neighbours: everything
  // but the flagged mine is open, and that is a win.
  CHECK(revealedCount(chorded) == kCells - 1);
  CHECK(chorded.status == Status::Won);

  // The same equivalence where the chord LOSES, which is the case a shortcut
  // is most tempting to special-case.
  Game losing{};
  start(losing, 32u);
  losing.status = Status::Playing;
  losing.cell[3][0] |= kMine;
  losing.cell[2][1] |= kRevealed;
  CHECK(toggleFlag(losing, 2, 0));
  Game losingModel = losing;
  CHECK(chord(losing, 2, 1));
  CHECK(modelChord(losingModel, 2, 1));
  CHECK(losing.status == Status::Lost);
  CHECK(sameGame(losing, losingModel));
}

// And over real dealt boards rather than three hand-built ones: play games,
// scan the whole board for every legal chord, and check each one against the
// model. A special case that only bites on a board nobody hand-wrote is exactly
// what this catches.
//
// The driver flags CORRECTLY most of the time -- it may look at the mines,
// being a test and not a player -- because a purely random flagger satisfies a
// number by accident about once a game, and almost always wrongly. That found
// 46 chords over 600 games, 35 of them losses, which is not a sweep of the
// winning path at all.
void testChordMatchesTheModelOnRealBoards() {
  uint32_t seed = 13579u;
  int chords = 0;
  int chordLosses = 0;
  int chordWins = 0;
  int cascades = 0;
  for (int match = 0; match < 400; ++match) {
    Game game{};
    start(game, seed = seed * 1664525u + 1013904223u);
    uint32_t pick = seed;
    reveal(game, static_cast<int>(pick >> 8) % kColumns, static_cast<int>(pick >> 16) % kRows);

    int guard = 0;
    while (!over(game) && ++guard <= kCells * 3) {
      // Every chord the board currently offers, compared against the model.
      // The first legal one is played so the game moves on.
      bool played = false;
      for (int column = 0; column < kColumns && !played; ++column) {
        for (int row = 0; row < kRows && !played; ++row) {
          Game viaChord = game;
          if (!chord(viaChord, column, row)) continue;
          Game viaModel = game;
          CHECK(modelChord(viaModel, column, row));
          CHECK(sameGame(viaChord, viaModel));
          ++chords;
          if (viaChord.status == Status::Lost) ++chordLosses;
          if (viaChord.status == Status::Won) ++chordWins;
          if (revealedCount(viaChord) - revealedCount(game) > 8) ++cascades;
          game = viaChord;
          played = true;
        }
      }
      if (played) continue;

      const uint32_t roll = nextRandom(pick);
      const int column = static_cast<int>(roll >> 8) % kColumns;
      const int row = static_cast<int>(roll >> 16) % kRows;
      if ((roll & 3) == 0) {
        // Mostly a correct flag, so numbers actually become satisfied;
        // sometimes a wrong one, so chords that detonate happen too.
        const bool honest = (roll & 4) != 0;
        int planted = -1;
        for (int i = 0; i < kCells && planted < 0; ++i) {
          const int c = (column + i) % kColumns;
          const int r = (row + i / kColumns) % kRows;
          const uint8_t cell = game.cell[c][r];
          if (cell & (kRevealed | kFlagged)) continue;
          if (((cell & kMine) != 0) != honest) continue;
          toggleFlag(game, c, r);
          planted = 1;
        }
        if (planted < 0) dig(game, column, row);
      } else {
        dig(game, column, row);
      }
    }
  }
  // The sweep is worthless if it never found a chord to make, and EACH outcome
  // it is meant to cover must actually have occurred, or the comparison above
  // proved nothing about it. A chord that wins is its own case: it is the one
  // where reveal() stops answering part way through for a reason that is not a
  // loss.
  CHECK(chords > 500);
  CHECK(chordLosses > 0);
  CHECK(cascades > 0);
  // NOT asserted here, and the absence is the point: this driver plants a wrong
  // flag one time in eight, so its games end in detonation and it produced ZERO
  // winning chords across 585. A chord that WINS is covered by
  // testAChordCanWinTheGame below, which plays perfectly to reach one. Naming
  // the gap beats a counter that quietly reads zero.
  std::printf("  (winning chords are not reachable here: %d seen; see testAChordCanWinTheGame)\n", chordWins);
  std::printf("  chords on dealt boards: %d vs model, %d lost, %d cascaded\n", chords, chordLosses, cascades);
}

// A chord that WINS, which is the third outcome and the one no sweep above
// reaches: the driver there mis-flags deliberately, so its games all detonate.
//
// This one plays perfectly -- it may read the mines, being a test -- and its
// last move is usually a chord. Winning matters separately from losing because
// it is the other way reveal() stops answering part way through a chord: once
// allSafeCellsRevealed sets Won, canReveal refuses the rest, and a chord that
// assumed it could keep going would diverge from the model exactly there.
void testAChordCanWinTheGame() {
  uint32_t seed = 8642u;
  int wins = 0;
  int chordWins = 0;
  int chords = 0;
  for (int match = 0; match < 200; ++match) {
    Game game{};
    start(game, seed = seed * 1664525u + 1013904223u);
    reveal(game, static_cast<int>(seed >> 8) % kColumns, static_cast<int>(seed >> 16) % kRows);
    // Flag every mine, correctly. Now no chord can ever detonate.
    for (int c = 0; c < kColumns; ++c) {
      for (int r = 0; r < kRows; ++r) {
        if (game.cell[c][r] & kMine) toggleFlag(game, c, r);
      }
    }

    int guard = 0;
    while (!over(game) && ++guard <= kCells * 3) {
      bool played = false;
      for (int c = 0; c < kColumns && !played; ++c) {
        for (int r = 0; r < kRows && !played; ++r) {
          Game viaChord = game;
          if (!chord(viaChord, c, r)) continue;
          Game viaModel = game;
          CHECK(modelChord(viaModel, c, r));
          CHECK(sameGame(viaChord, viaModel));
          ++chords;
          if (viaChord.status == Status::Won) ++chordWins;
          game = viaChord;
          played = true;
        }
      }
      if (played) continue;
      // No chord available: open a safe cell by hand and look again.
      bool dug = false;
      for (int c = 0; c < kColumns && !dug; ++c) {
        for (int r = 0; r < kRows && !dug; ++r) {
          if (game.cell[c][r] & (kMine | kRevealed | kFlagged)) continue;
          reveal(game, c, r);
          dug = true;
        }
      }
      if (!dug) break;
    }
    CHECK(game.status != Status::Lost);
    if (game.status == Status::Won) ++wins;
  }
  // Every game must be won -- with every mine flagged there is no way to lose --
  // and a chord must have been the winning move in some of them.
  CHECK(wins == 200);
  CHECK(chordWins > 0);
  std::printf("  perfect play: %d/200 won, %d chords, %d of them the winning move\n", wins, chords, chordWins);
}

// The routing decision is a rule too, so it is proved here rather than in the
// activity where nothing can reach it: one tap of the DIG tool digs a covered
// cell and chords a satisfied number, and a tap that means neither changes
// nothing.
void testDigRoutesATapToTheMoveItMeans() {
  Game game = satisfiedOne();
  // Covered: a plain dig.
  CHECK(dig(game, 0, 5));
  CHECK(game.cell[0][5] & kRevealed);

  // A revealed number with its flag planted: a chord.
  Game ready = satisfiedOne();
  CHECK(toggleFlag(ready, 3, 0));
  CHECK(dig(ready, 2, 1));
  CHECK(ready.cell[1][1] & kRevealed);

  // The same number without the flag: nothing, and nothing touched.
  Game idle = satisfiedOne();
  Game before = idle;
  CHECK(!dig(idle, 2, 1));
  CHECK(std::memcmp(&before, &idle, sizeof(Game)) == 0);

  // A flagged cell is still protected from the dig tool.
  Game guarded = satisfiedOne();
  CHECK(toggleFlag(guarded, 3, 0));
  CHECK(!dig(guarded, 3, 0));
  CHECK((guarded.cell[3][0] & kRevealed) == 0);
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
  testAnEmptyBoardOpensCompletely();
  testNoRevealedZeroEverTouchesACoveredCell();
  testMinesReachEveryCell();
  testStartClearsAFinishedBoard();
  testAFlaggedSafeCellIsNotCleared();
  testAChordOpensEveryNeighbourWhenTheFlagsMatch();
  testAChordWithTooFewFlagsDoesNothingAtAll();
  testAChordNeedsARevealedNumberAndALiveGame();
  testAChordOnAWrongFlagLosesThroughTheSamePathAsATap();
  testAChordIsExactlyTheRevealsItReplaces();
  testChordMatchesTheModelOnRealBoards();
  testAChordCanWinTheGame();
  testDigRoutesATapToTheMoveItMeans();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
