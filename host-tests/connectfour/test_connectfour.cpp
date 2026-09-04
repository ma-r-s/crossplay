// Connect Four's rules, its two state machines and its opponent.
//
// The bounds here are deliberately tight. A Minesweeper suite once asserted
// "more than ten cells opened" against a true answer near forty, and a flood
// that truncated in 41.8% of real games stayed green for it. A loose bound is
// not a weak test, it is an absent one.

#include <cstdio>
#include <cstdlib>

#include "ConnectFourBrain.h"
#include "ConnectFourCore.h"
#include "ConnectFourFlow.h"

using namespace connectfour;

namespace {

int checks = 0;
int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// A board from a picture, so a test position reads as the board it is.
// Rows are given TOP first, as the screen draws them; '.' empty, 'L', 'D'.
void paint(Game& game, const char* rows[kRows]) {
  start(game);
  for (int line = 0; line < kRows; ++line) {
    const int row = kRows - 1 - line;
    for (int column = 0; column < kColumns; ++column) {
      const char c = rows[line][column];
      game.cell[column][row] = c == 'L' ? kLight : (c == 'D' ? kDark : kEmpty);
    }
  }
}

void testTheOpeningBoardIsEmptyAndLightOpens() {
  Game game{};
  start(game);
  CHECK(discCount(game) == 0);
  CHECK(game.turn == kLight);
  CHECK(!over(game));
  CHECK(game.outcome == Outcome::Running);
  CHECK(game.lastColumn == kNoColumn);
  for (int column = 0; column < kColumns; ++column) {
    CHECK(canDrop(game, column));
    CHECK(landingRow(game, column) == 0);
  }
  CHECK(!canDrop(game, -1));
  CHECK(!canDrop(game, kColumns));
}

void testDiscsStackAndAColumnFillsAtSix() {
  Game game{};
  start(game);
  for (int i = 0; i < kRows; ++i) {
    CHECK(landingRow(game, 3) == i);
    CHECK(drop(game, 3));
    CHECK(game.lastColumn == 3);
    CHECK(game.lastRow == i);
  }
  CHECK(landingRow(game, 3) == kNoColumn);
  CHECK(!canDrop(game, 3));
  // A refused drop changes NOTHING, so a caller can route a tap straight here.
  const Game before = game;
  CHECK(!drop(game, 3));
  CHECK(__builtin_memcmp(&before, &game, sizeof(Game)) == 0);
  // And the neighbours are untouched by six drops next door.
  for (int column = 0; column < kColumns; ++column) {
    if (column != 3) CHECK(landingRow(game, column) == 0);
  }
}

void testNothingCanFloat() {
  // The one property the storage layout is chosen for. Play a thousand random
  // legal games and assert no cell ever sits above a gap.
  uint32_t rng = 0xC0FFEE11u;
  for (int trial = 0; trial < 1000; ++trial) {
    Game game{};
    start(game);
    while (!over(game)) {
      int legal[kColumns];
      int count = 0;
      for (int column = 0; column < kColumns; ++column) {
        if (canDrop(game, column)) legal[count++] = column;
      }
      CHECK(count > 0);
      CHECK(drop(game, legal[nextRandom(rng) % static_cast<uint32_t>(count)]));
      for (int column = 0; column < kColumns; ++column) {
        bool sawEmpty = false;
        for (int row = 0; row < kRows; ++row) {
          if (game.cell[column][row] == kEmpty)
            sawEmpty = true;
          else
            CHECK(!sawEmpty);
        }
      }
    }
    CHECK(plausible(game));
  }
}

void testFourInALineWinsInEveryDirection() {
  {  // horizontal
    Game game{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", ".......", "LLLL..."};
    paint(game, rows);
    CHECK(winningLine(game, kLight, nullptr));
    CHECK(!winningLine(game, kDark, nullptr));
  }
  {  // vertical
    Game game{};
    const char* rows[kRows] = {".......", ".......", "..D....", "..D....", "..D....", "..D...."};
    paint(game, rows);
    CHECK(winningLine(game, kDark, nullptr));
    CHECK(!winningLine(game, kLight, nullptr));
  }
  {  // rising diagonal
    Game game{};
    const char* rows[kRows] = {".......", ".......", "...L...", "..LD...", ".LDD...", "LDDD..."};
    paint(game, rows);
    CHECK(winningLine(game, kLight, nullptr));
  }
  {  // falling diagonal
    Game game{};
    const char* rows[kRows] = {".......", ".......", "...L...", "....L..", ".DDDL..", "DDLLL.."};
    paint(game, rows);
    // Light's falling diagonal from (3,3) down-right: (3,3)(4,2)(5,1)(6,0)?
    // Not present. This board is here for the negative: three is not four.
    CHECK(!winningLine(game, kDark, nullptr));
  }
  {  // a real falling diagonal
    Game game{};
    const char* rows[kRows] = {".......", ".......", "L......", "DL.....", "DDL....", "DDDL..."};
    paint(game, rows);
    CHECK(winningLine(game, kLight, nullptr));
    CHECK(!winningLine(game, kDark, nullptr));
  }
  {  // three is not four, in every direction
    Game game{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", ".......", "LLL.LLL"};
    paint(game, rows);
    CHECK(!winningLine(game, kLight, nullptr));
  }
}

void testTheWinningLineReportedIsTheLineThatWon() {
  Game game{};
  const char* rows[kRows] = {".......", ".......", ".......", ".......", "..D.D..", ".LLLL.."};
  paint(game, rows);
  int line[kLine] = {-1, -1, -1, -1};
  CHECK(winningLine(game, kLight, line));
  // Every reported cell really holds light, and they really are four in a row.
  for (const int index : line) {
    CHECK(index >= 0 && index < kCells);
    CHECK(game.cell[index / kRows][index % kRows] == kLight);
  }
  const int dc = line[1] / kRows - line[0] / kRows;
  const int dr = line[1] % kRows - line[0] % kRows;
  CHECK(dc != 0 || dr != 0);
  for (int step = 1; step < kLine; ++step) {
    CHECK(line[step] / kRows - line[step - 1] / kRows == dc);
    CHECK(line[step] % kRows - line[step - 1] % kRows == dr);
  }
}

void testAWinEndsTheGameImmediatelyAndKeepsTheTurn() {
  Game game{};
  start(game);
  // Light builds a column while dark answers next door.
  for (int i = 0; i < 3; ++i) {
    CHECK(drop(game, 2));
    CHECK(drop(game, 3));
  }
  CHECK(!over(game));
  CHECK(game.turn == kLight);
  CHECK(drop(game, 2));
  CHECK(game.outcome == Outcome::LightWins);
  CHECK(over(game));
  // The turn stays with the winner. Everything else reads "whose move" from it,
  // and a winner that hands the turn over announces the wrong name.
  CHECK(game.turn == kLight);
  // And a settled board accepts nothing.
  for (int column = 0; column < kColumns; ++column) CHECK(!canDrop(game, column));
  const Game before = game;
  CHECK(!drop(game, 5));
  CHECK(__builtin_memcmp(&before, &game, sizeof(Game)) == 0);
}

void testAFullBoardWithNoLineIsADraw() {
  // A hand-built full board with no four anywhere. Column pattern LLDDLLDD...
  // per column, offset so no line forms.
  Game game{};
  const char* rows[kRows] = {"DLDLDLD", "DLDLDLD", "LDLDLDL", "LDLDLDL", "DLDLDLD", "DLDLDLD"};
  paint(game, rows);
  CHECK(boardFull(game));
  CHECK(!winningLine(game, kLight, nullptr));
  CHECK(!winningLine(game, kDark, nullptr));
  // And drop() reaches the same verdict when it fills the last cell itself.
  Game live{};
  const char* holed[kRows] = {".LDLDLD", "DLDLDLD", "LDLDLDL", "LDLDLDL", "DLDLDLD", "DLDLDLD"};
  paint(live, holed);
  live.turn = kDark;
  CHECK(drop(live, 0));
  CHECK(live.outcome == Outcome::Draw);
  CHECK(over(live));
}

void testEveryRandomGameEndsAndEndsCorrectly() {
  uint32_t rng = 0x1234ABCDu;
  int light = 0;
  int dark = 0;
  int draws = 0;
  for (int trial = 0; trial < 3000; ++trial) {
    Game game{};
    start(game);
    int plies = 0;
    while (!over(game)) {
      int legal[kColumns];
      int count = 0;
      for (int column = 0; column < kColumns; ++column) {
        if (canDrop(game, column)) legal[count++] = column;
      }
      CHECK(drop(game, legal[nextRandom(rng) % static_cast<uint32_t>(count)]));
      ++plies;
      // A game cannot outlast the board.
      CHECK(plies <= kCells);
    }
    CHECK(discCount(game) == plies);
    switch (game.outcome) {
      case Outcome::LightWins:
        ++light;
        CHECK(winningLine(game, kLight, nullptr));
        CHECK(!winningLine(game, kDark, nullptr));
        break;
      case Outcome::DarkWins:
        ++dark;
        CHECK(winningLine(game, kDark, nullptr));
        CHECK(!winningLine(game, kLight, nullptr));
        break;
      case Outcome::Draw:
        ++draws;
        CHECK(boardFull(game));
        CHECK(!winningLine(game, kLight, nullptr));
        CHECK(!winningLine(game, kDark, nullptr));
        break;
      case Outcome::Running:
        CHECK(false);
        break;
    }
  }
  // Random play does reach all three outcomes; if one of these ever goes to
  // zero the generator has stopped exploring and the assertions above are
  // running on a narrower set of boards than they look like they are.
  CHECK(light > 0);
  CHECK(dark > 0);
  CHECK(draws > 0);
  std::printf("  random games: %d light, %d dark, %d draws\n", light, dark, draws);
}

void testPlausibleRejectsWhatPlayCannotProduce() {
  Game good{};
  start(good);
  CHECK(plausible(good));
  for (int i = 0; i < 10; ++i) CHECK(drop(good, i % kColumns));
  CHECK(plausible(good));

  {  // a floating disc
    Game bad = good;
    bad.cell[6][4] = kLight;
    CHECK(!plausible(bad));
  }
  {  // a colour that does not exist
    Game bad = good;
    bad.cell[0][0] = 9;
    CHECK(!plausible(bad));
  }
  {  // dark two ahead of light
    Game bad{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", ".......", "DDD...."};
    paint(bad, rows);
    bad.turn = kLight;
    CHECK(!plausible(bad));
  }
  {  // the wrong side to move for the disc count
    Game bad = good;
    bad.turn = other(bad.turn);
    CHECK(!plausible(bad));
  }
  {  // both sides holding a line
    Game bad{};
    const char* rows[kRows] = {".......", ".......", ".......", "DDDD...", "LLLL...", "LLLLDDD"};
    paint(bad, rows);
    CHECK(!plausible(bad));
  }
  {  // a last column off the board
    Game bad = good;
    bad.lastColumn = 9;
    CHECK(!plausible(bad));
  }
}

// --- gaps a mutation run found: assertions that were not asserting -----------

// Swapping the win check and the draw check in drop() survived the whole suite.
// It is not a rare case: 2,408 of 3,000,000 random games end with the
// forty-second disc completing a line, and every one of them would have been
// announced as a draw.
void testAWinOnTheVeryLastDiscIsAWinNotADraw() {
  Game game{};
  const char* rows[kRows] = {".LDLDLD", "DLDLDLD", "LDLDLDL", "LDLDLDL", "DLDLDLD", "DLDLDLD"};
  paint(game, rows);
  CHECK(!boardFull(game));
  CHECK(discCount(game) == kCells - 1);
  // The one free cell completes light's column-0 pair upward into a four.
  game.cell[0][5] = kEmpty;
  game.cell[0][4] = kLight;
  game.cell[0][3] = kLight;
  game.cell[0][2] = kLight;
  game.turn = kLight;
  game.outcome = Outcome::Running;
  CHECK(landingRow(game, 0) == 5);
  CHECK(drop(game, 0));
  CHECK(boardFull(game));
  // Both conditions are true at once. The win has to win.
  CHECK(game.outcome == Outcome::LightWins);
  CHECK(winningLine(game, kLight, nullptr));
}

// Every clause of plausible() must be the one doing the rejecting for at least
// one board, or it is untested however many boards the suite feeds it. The
// suite's own "floating disc" case was being caught by turn parity, and its
// "two ahead" case built a board three ahead.
void testEachPlausibleClauseRejectsSomethingOnItsOwn() {
  // Gravity, with the counts and the turn left consistent.
  {
    Game bad{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", "L......", ".D....."};
    paint(bad, rows);
    bad.turn = kLight;
    bad.lastColumn = 1;
    bad.lastRow = 0;
    CHECK(!plausible(bad));
  }
  // Exactly two ahead, not three.
  {
    Game bad{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", ".......", "LL....."};
    paint(bad, rows);
    bad.turn = kDark;
    bad.lastColumn = 1;
    bad.lastRow = 0;
    CHECK(!plausible(bad));
  }
  // A last-move row off the board, with the column in range.
  {
    Game bad{};
    start(bad);
    drop(bad, 3);
    bad.lastRow = 9;
    CHECK(!plausible(bad));
  }
  // A last move pointing at an empty cell. The board reads this to draw its
  // marker, so it would render a disc that is not there.
  {
    Game bad{};
    start(bad);
    drop(bad, 3);
    bad.lastColumn = 6;
    CHECK(!plausible(bad));
  }
  // Discs on the board and no last move recorded at all.
  {
    Game bad{};
    start(bad);
    drop(bad, 3);
    bad.lastColumn = kNoColumn;
    bad.lastRow = kNoColumn;
    CHECK(!plausible(bad));
  }
  // The outcome disagreeing with the board, in both directions.
  {
    Game bad{};
    start(bad);
    for (int i = 0; i < 3; ++i) {
      drop(bad, 2);
      drop(bad, 3);
    }
    drop(bad, 2);
    CHECK(plausible(bad));
    CHECK(bad.outcome == Outcome::LightWins);
    Game running = bad;
    running.outcome = Outcome::Running;
    CHECK(!plausible(running));
    Game wrongWinner = bad;
    wrongWinner.outcome = Outcome::DarkWins;
    CHECK(!plausible(wrongWinner));
    Game claimedDraw = bad;
    claimedDraw.outcome = Outcome::Draw;
    CHECK(!plausible(claimedDraw));
    // A win that handed the turn over.
    Game handedOver = bad;
    handedOver.turn = kDark;
    CHECK(!plausible(handedOver));
  }
  // A full board still claiming to be running: the wedge. over() is false, no
  // column accepts, chooseColumn returns nothing, and the activity loops
  // forever without repainting.
  {
    Game bad{};
    const char* rows[kRows] = {"DLDLDLD", "DLDLDLD", "LDLDLDL", "LDLDLDL", "DLDLDLD", "DLDLDLD"};
    paint(bad, rows);
    bad.turn = kLight;
    bad.lastColumn = 0;
    bad.lastRow = 5;
    bad.outcome = Outcome::Running;
    CHECK(boardFull(bad));
    CHECK(!plausible(bad));
  }
}

// A failed search must leave the caller's array alone. It used to write each
// cell before testing the next, so a losing direction scribbled partial
// indices: 8,132,315 times over 4,265,889 positions of random play.
void testAFailedWinningLineSearchWritesNothing() {
  Game game{};
  const char* rows[kRows] = {".......", ".......", ".......", ".......", ".......", "LLD...."};
  paint(game, rows);
  int line[kLine] = {-1, -1, -1, -1};
  CHECK(!winningLine(game, kLight, line));
  for (const int index : line) CHECK(index == -1);
  CHECK(!winningLine(game, kDark, line));
  for (const int index : line) CHECK(index == -1);

  // And across real play, where the failing searches actually happen.
  uint32_t rng = 0x0DDBA11u;
  for (int trial = 0; trial < 300; ++trial) {
    Game live{};
    start(live);
    while (!over(live)) {
      int probe[kLine] = {-1, -1, -1, -1};
      const bool light = winningLine(live, kLight, probe);
      if (!light) {
        for (const int index : probe) CHECK(index == -1);
      }
      int legal[kColumns];
      int count = 0;
      for (int c = 0; c < kColumns; ++c) {
        if (canDrop(live, c)) legal[count++] = c;
      }
      drop(live, legal[nextRandom(rng) % static_cast<uint32_t>(count)]);
    }
  }
}

// The evaluation has to be zero-sum, or the search's own parity decides which
// side its asymmetry favours. It was not: the two sides disagreed in 63,392 of
// 138,663 reachable positions, worst gap 300.
void testTheEvaluationIsZeroSum() {
  uint32_t rng = 0x5EED1234u;
  for (int trial = 0; trial < 400; ++trial) {
    Game game{};
    start(game);
    while (!over(game)) {
      CHECK(evaluate(game, kLight) == -evaluate(game, kDark));
      int legal[kColumns];
      int count = 0;
      for (int c = 0; c < kColumns; ++c) {
        if (canDrop(game, c)) legal[count++] = c;
      }
      drop(game, legal[nextRandom(rng) % static_cast<uint32_t>(count)]);
    }
  }
}

void testBackIsTotalAndAlwaysReachesTheTop() {
  const Screen all[] = {Screen::Menu, Screen::HowTo, Screen::Board, Screen::Result};
  for (const Screen screen : all) {
    // Every screen reaches Menu, and in at most one step, so there is no depth
    // a player can get stranded at.
    CHECK(back(screen) == Screen::Menu);
  }
  CHECK(leavesApp(Screen::Menu));
  CHECK(!leavesApp(Screen::HowTo));
  CHECK(!leavesApp(Screen::Board));
  CHECK(!leavesApp(Screen::Result));
}

void testPhaseAndScreenAreDerivedNotStored() {
  CHECK(phaseFor(false, true) == Phase::Yours);
  CHECK(phaseFor(false, false) == Phase::Theirs);
  CHECK(phaseFor(true, true) == Phase::Settled);
  CHECK(phaseFor(true, false) == Phase::Settled);

  Game game{};
  start(game);
  CHECK(screenFor(game) == Screen::Board);
  for (int i = 0; i < 3; ++i) {
    drop(game, 2);
    drop(game, 3);
  }
  drop(game, 2);
  CHECK(over(game));
  CHECK(screenFor(game) == Screen::Result);
}

void testOpenColumnsAgreeWithWhatTheBoardAccepts() {
  uint32_t rng = 0x77AA33BBu;
  for (int trial = 0; trial < 200; ++trial) {
    Game game{};
    start(game);
    while (!over(game)) {
      const uint8_t mask = openColumns(game);
      int open = 0;
      for (int column = 0; column < kColumns; ++column) {
        const bool marked = (mask & (1 << column)) != 0;
        // The hint and the rule must never disagree: a column drawn as a target
        // that refuses a tap is the failure Checkers already paid for.
        CHECK(marked == canDrop(game, column));
        if (marked) ++open;
      }
      CHECK(open > 0);
      int legal[kColumns];
      int count = 0;
      for (int column = 0; column < kColumns; ++column) {
        if (canDrop(game, column)) legal[count++] = column;
      }
      drop(game, legal[nextRandom(rng) % static_cast<uint32_t>(count)]);
    }
    // A settled board offers nothing, however much room is left.
    CHECK(openColumns(game) == 0);
  }
}

void testTheBrainOnlyEverPlaysALegalColumn() {
  uint32_t rng = 0x9911EE22u;
  for (int trial = 0; trial < 40; ++trial) {
    Game game{};
    start(game);
    while (!over(game)) {
      const int column = chooseColumn(game);
      CHECK(column != kNoColumn);
      CHECK(canDrop(game, column));
      CHECK(drop(game, column));
      if (over(game)) break;
      int legal[kColumns];
      int count = 0;
      for (int c = 0; c < kColumns; ++c) {
        if (canDrop(game, c)) legal[count++] = c;
      }
      CHECK(drop(game, legal[nextRandom(rng) % static_cast<uint32_t>(count)]));
    }
    CHECK(chooseColumn(game) == kNoColumn);
  }
}

void testTheBrainIsDeterministicAndPure() {
  uint32_t rng = 0x5A5A0F0Fu;
  for (int trial = 0; trial < 30; ++trial) {
    Game game{};
    start(game);
    for (int ply = 0; ply < 12 && !over(game); ++ply) {
      int legal[kColumns];
      int count = 0;
      for (int c = 0; c < kColumns; ++c) {
        if (canDrop(game, c)) legal[count++] = c;
      }
      drop(game, legal[nextRandom(rng) % static_cast<uint32_t>(count)]);
    }
    if (over(game)) continue;
    const Game before = game;
    const int first = chooseColumn(game);
    // Asked twice, it answers the same. And it did not touch the board: every
    // screenshot recipe in this repo replays a game by repeating taps.
    CHECK(__builtin_memcmp(&before, &game, sizeof(Game)) == 0);
    CHECK(chooseColumn(game) == first);
    Game copy = before;
    CHECK(chooseColumn(copy) == first);
  }
}

void testTheBrainTakesAWinAndBlocksALoss() {
  {  // a win in one, on the board it can complete
    Game game{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", ".DDD...", "LLL...."};
    paint(game, rows);
    game.turn = kLight;
    CHECK(chooseColumn(game) == 3);
  }
  {  // the same shape for dark, so this is not an artefact of one colour
    Game game{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", "LLL....", "DDD...."};
    paint(game, rows);
    game.turn = kDark;
    CHECK(chooseColumn(game) == 3);
  }
  {  // no win available: it must stop the other side's
    Game game{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", ".......", "LLL...D"};
    paint(game, rows);
    game.turn = kDark;
    CHECK(chooseColumn(game) == 3);
  }
  {  // taking its own win beats blocking theirs
    Game game{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", "DDD....", "LLL...."};
    paint(game, rows);
    game.turn = kLight;
    // Light completes at column 3 rather than blocking dark at column 3 too --
    // same column here, so use a board where they differ.
    CHECK(chooseColumn(game) == 3);
  }
  {  // its win and their win are in different columns
    Game game{};
    const char* rows[kRows] = {".......", ".......", ".......", ".......", "...DDD.", "LLL...."};
    paint(game, rows);
    game.turn = kLight;
    // Light can finish at 3; dark threatens at 6 (and at 2, blocked by light).
    // Winning now ends the game, so it must not spend the move defending.
    CHECK(chooseColumn(game) == 3);
  }
}

void testTheBrainRefusesToHandOverAWin() {
  // Dropping into column 2 here would put dark's winning disc within reach on
  // top of it. A brain that only counts material plays it.
  Game game{};
  const char* rows[kRows] = {".......", ".......", ".......", "..D....", "..L....", "LDLD..."};
  paint(game, rows);
  game.turn = kLight;
  const int column = chooseColumn(game);
  Game after = game;
  CHECK(drop(after, column));
  CHECK(!over(after));
  // Whatever it chose, dark must not have an immediate four.
  bool darkCanWin = false;
  for (int c = 0; c < kColumns; ++c) {
    if (!canDrop(after, c)) continue;
    Game reply = after;
    drop(reply, c);
    if (reply.outcome == Outcome::DarkWins) darkCanWin = true;
  }
  CHECK(!darkCanWin);
}

void testTheBrainBeatsARandomMoverConvincingly() {
  // The test that matters. Legal and deterministic were both true of a brain
  // whose evaluation had been negated; only this one noticed.
  uint32_t rng = 0xBEEF7777u;
  int brainWins = 0;
  int randomWins = 0;
  int draws = 0;
  constexpr int kGames = 40;

  for (int trial = 0; trial < kGames; ++trial) {
    Game game{};
    start(game);
    // Alternate who opens, because light opening is a real advantage and a
    // brain that always went first would be measuring the rules, not itself.
    const uint8_t brainSide = (trial % 2 == 0) ? kLight : kDark;
    while (!over(game)) {
      int column;
      if (game.turn == brainSide) {
        column = chooseColumn(game);
      } else {
        int legal[kColumns];
        int count = 0;
        for (int c = 0; c < kColumns; ++c) {
          if (canDrop(game, c)) legal[count++] = c;
        }
        column = legal[nextRandom(rng) % static_cast<uint32_t>(count)];
      }
      CHECK(drop(game, column));
    }
    const bool brainWon = (game.outcome == Outcome::LightWins && brainSide == kLight) ||
                          (game.outcome == Outcome::DarkWins && brainSide == kDark);
    if (game.outcome == Outcome::Draw)
      ++draws;
    else if (brainWon)
      ++brainWins;
    else
      ++randomWins;
  }
  std::printf("  brain vs random: %d - %d (%d draws) of %d\n", brainWins, randomWins, draws, kGames);
  // Not "more than half". A brain looking seven plies ahead against a mover
  // with no plan should lose essentially never, and a bound that a coin could
  // pass is not a bound.
  CHECK(randomWins == 0);
  CHECK(brainWins >= kGames - 2);
}

}  // namespace

int main() {
  testTheOpeningBoardIsEmptyAndLightOpens();
  testDiscsStackAndAColumnFillsAtSix();
  testNothingCanFloat();
  testFourInALineWinsInEveryDirection();
  testTheWinningLineReportedIsTheLineThatWon();
  testAWinEndsTheGameImmediatelyAndKeepsTheTurn();
  testAFullBoardWithNoLineIsADraw();
  testEveryRandomGameEndsAndEndsCorrectly();
  testPlausibleRejectsWhatPlayCannotProduce();
  testAWinOnTheVeryLastDiscIsAWinNotADraw();
  testEachPlausibleClauseRejectsSomethingOnItsOwn();
  testAFailedWinningLineSearchWritesNothing();
  testTheEvaluationIsZeroSum();
  testBackIsTotalAndAlwaysReachesTheTop();
  testPhaseAndScreenAreDerivedNotStored();
  testOpenColumnsAgreeWithWhatTheBoardAccepts();
  testTheBrainOnlyEverPlaysALegalColumn();
  testTheBrainIsDeterministicAndPure();
  testTheBrainTakesAWinAndBlocksALoss();
  testTheBrainRefusesToHandOverAWin();
  testTheBrainBeatsARandomMoverConvincingly();

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
