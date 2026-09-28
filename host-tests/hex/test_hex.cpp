// Hex: the rules, the brain and the save file, on a laptop.
//
// The interesting assertions here are the ones about a PROPERTY rather than an
// example, because Hex has two that no other game on this shelf does and both
// of them are what everything else leans on:
//
//   * a full board has exactly one winner, always -- which is what lets a
//     playout skip every legality check and every terminal test;
//   * the union-find in `Game` and a plain flood fill have to agree about who
//     won, on every position, or the incremental answer is quietly wrong on the
//     positions nobody wrote a case for.
//
// So the flood fill below is written out again rather than called from the
// source: a test that asks the implementation whether the implementation is
// right is a test that cannot fail.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>

#include "HexBrain.h"
#include "HexCore.h"
#include "HexFlow.h"
#include "HexSave.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (condition) return;
  ++checksFailed;
  std::printf("FAIL test_hex.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

uint32_t rng = 20260913u;
uint32_t nextRandom() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

// --- an independent answer to "who won" ------------------------------------
//
// A flood fill over a plain byte board, sharing nothing with hex::Game but the
// neighbour offsets. Every union-find assertion below is against this.
bool floodConnects(const uint8_t board[hex::kCells], const uint8_t colour) {
  bool seen[hex::kCells] = {};
  int stack[hex::kCells];
  int top = 0;
  for (int i = 0; i < hex::kSize; ++i) {
    const int cell = colour == hex::kBlack ? hex::cellAt(0, i) : hex::cellAt(i, 0);
    if (board[cell] != colour || seen[cell]) continue;
    seen[cell] = true;
    stack[top++] = cell;
  }
  while (top > 0) {
    const int cell = stack[--top];
    const bool arrived =
        colour == hex::kBlack ? hex::rowOf(cell) == hex::kSize - 1 : hex::colOf(cell) == hex::kSize - 1;
    if (arrived) return true;
    for (int dir = 0; dir < 6; ++dir) {
      const int next = hex::neighbour(cell, dir);
      if (next == hex::kNoCell || board[next] != colour || seen[next]) continue;
      seen[next] = true;
      stack[top++] = next;
    }
  }
  return false;
}

void flatten(const hex::Game& game, uint8_t out[hex::kCells]) {
  for (int cell = 0; cell < hex::kCells; ++cell) out[cell] = game.at(cell);
}

// --- the board -------------------------------------------------------------

void testAResetBoardIsTheSameBYTESWhateverWasThereBefore() {
  // `Game` is copied as bytes by the link layer and compared as bytes by the
  // tests, so a byte reset() does not write is a byte that travels and a byte
  // that makes two identical positions differ. `moveNumber` wants two-byte
  // alignment, so the compiler would leave one before it -- named `reserved`
  // and zeroed, for exactly this.
  hex::Game dirty;
  std::memset(&dirty, 0xAA, sizeof(dirty));
  hex::reset(dirty);
  hex::Game clean;
  std::memset(&clean, 0x00, sizeof(clean));
  hex::reset(clean);
  CHECK(std::memcmp(&dirty, &clean, sizeof(hex::Game)) == 0);

  // And it survives a move, which is what actually crosses the wire.
  CHECK(hex::play(dirty, hex::cellAt(3, 4)));
  CHECK(hex::play(clean, hex::cellAt(3, 4)));
  CHECK(std::memcmp(&dirty, &clean, sizeof(hex::Game)) == 0);
}

void testTheEmptyBoardIsEmptyAndBlackMovesFirst() {
  hex::Game game;
  hex::reset(game);
  CHECK(game.toMove == hex::kBlack);
  CHECK(!hex::over(game));
  CHECK(!hex::full(game));
  CHECK(game.moveNumber == 0);
  CHECK(game.lastMove == hex::kNoCell);
  for (int cell = 0; cell < hex::kCells; ++cell) {
    CHECK(game.at(cell) == hex::kEmpty);
    CHECK(hex::legal(game, cell));
  }
  // A cell that does not exist is not a move, on either side of the range.
  CHECK(!hex::legal(game, -1));
  CHECK(!hex::legal(game, hex::kCells));
  CHECK(!hex::play(game, hex::kCells));
}

void testAStonePlacedStaysAndTheTurnPasses() {
  hex::Game game;
  hex::reset(game);
  CHECK(hex::play(game, hex::cellAt(5, 5)));
  CHECK(game.at(hex::cellAt(5, 5)) == hex::kBlack);
  CHECK(game.toMove == hex::kWhite);
  CHECK(game.lastMove == hex::cellAt(5, 5));
  CHECK(game.moveNumber == 1);

  // The same cell twice is nothing at all: no stone, no turn, no repaint. The
  // I/O matrix calls a repaint here a bug, and this is the assertion under it.
  const hex::Game before = game;
  CHECK(!hex::play(game, hex::cellAt(5, 5)));
  CHECK(std::memcmp(&before, &game, sizeof(hex::Game)) == 0);
  CHECK(hex::tapMeaning(game, hex::cellAt(5, 5), true) == hex::Tap::Ignore);
  CHECK(hex::tapMeaning(game, hex::cellAt(5, 6), true) == hex::Tap::Place);
  // Not your turn is the other half of the same answer, and it is the flow's
  // to give rather than the rules': the board does not know about seats.
  CHECK(hex::tapMeaning(game, hex::cellAt(5, 6), false) == hex::Tap::Ignore);
}

void testTheNeighbourhoodIsSymmetricAndSixWide() {
  for (int cell = 0; cell < hex::kCells; ++cell) {
    int found = 0;
    for (int dir = 0; dir < 6; ++dir) {
      const int next = hex::neighbour(cell, dir);
      if (next == hex::kNoCell) continue;
      ++found;
      // Whatever is a neighbour of me has me as a neighbour. The cycle order
      // the bridge table is derived from makes this the opposite direction,
      // three steps round.
      CHECK(hex::neighbour(next, (dir + 3) % 6) == cell);
    }
    const int row = hex::rowOf(cell);
    const int col = hex::colOf(cell);
    const bool interior = row > 0 && row < hex::kSize - 1 && col > 0 && col < hex::kSize - 1;
    if (interior) CHECK(found == 6);
    // The two obtuse corners of a Hex rhombus have three neighbours and the two
    // acute ones have two. Nothing on the board has fewer.
    CHECK(found >= 2);
  }
}

void testAColumnOfBlackJoinsTopToBottom() {
  hex::Game game;
  hex::reset(game);
  for (int row = 0; row < hex::kSize; ++row) {
    CHECK(hex::play(game, hex::cellAt(row, 5)));
    if (row < hex::kSize - 1) {
      CHECK(!hex::over(game));
      // White, out of the way, down a column of its own. It cannot win with
      // these: a column is Black's shape, not White's.
      CHECK(hex::play(game, hex::cellAt(row, 9)));
      CHECK(!hex::over(game));
    }
  }
  CHECK(hex::over(game));
  CHECK(game.winner == hex::kBlack);
  // The turn stays where it is once the game is over, so nothing downstream can
  // read `toMove` as "who would have played next".
  CHECK(game.toMove == hex::kBlack);
  // And the board is frozen: further taps do nothing, which is the acceptance
  // criterion for a finished game.
  const hex::Game frozen = game;
  CHECK(!hex::play(game, hex::cellAt(0, 0)));
  CHECK(std::memcmp(&frozen, &game, sizeof(hex::Game)) == 0);
  CHECK(hex::tapMeaning(game, hex::cellAt(0, 0), true) == hex::Tap::Ignore);
}

void testARowOfWhiteJoinsLeftToRight() {
  hex::Game game;
  hex::reset(game);
  // Black first, somewhere it cannot finish, then White walks a row.
  for (int col = 0; col < hex::kSize; ++col) {
    CHECK(hex::play(game, hex::cellAt(0, col)));
    CHECK(!hex::over(game));
    CHECK(hex::play(game, hex::cellAt(5, col)));
    if (col < hex::kSize - 1) CHECK(!hex::over(game));
  }
  CHECK(hex::over(game));
  CHECK(game.winner == hex::kWhite);
}

void testTheUnionFindAndAFloodFillAlwaysAgree() {
  // Random legal games, to the end. The union-find says who won incrementally;
  // the flood fill says who won by looking at the finished board, and the two
  // have to be the same answer every time.
  int filled = 0;
  for (int trial = 0; trial < 400; ++trial) {
    hex::Game game;
    hex::reset(game);
    int guard = 0;
    while (!hex::over(game) && guard++ <= hex::kCells) {
      int empty[hex::kCells];
      int count = 0;
      for (int cell = 0; cell < hex::kCells; ++cell) {
        if (game.at(cell) == hex::kEmpty) empty[count++] = cell;
      }
      if (count == 0) break;
      CHECK(hex::play(game, empty[nextRandom() % static_cast<uint32_t>(count)]));
    }
    // **No draws.** A game of Hex cannot run out of cells without a winner, so
    // reaching a full board with nobody having won is the one result this whole
    // design would be wrong about. A random game DOES sometimes end on its last
    // cell, which is the case worth counting rather than excluding: it is the
    // one where "full" and "over" have to be true together.
    CHECK(hex::over(game));
    if (hex::full(game)) ++filled;

    uint8_t board[hex::kCells];
    flatten(game, board);
    CHECK(floodConnects(board, game.winner));
    CHECK(!floodConnects(board, hex::other(game.winner)));
  }
  std::printf("  %d of 400 random games ran the board out of cells\n", filled);
}

void testAFullBoardHasExactlyOneWinnerWhateverIsOnIt() {
  // The Hex theorem itself, over colourings nobody played: no alternation, no
  // turn order, just every cell taken. This is the property the brain's playout
  // relies on when it fills the board and reads the winner off in one pass.
  for (int trial = 0; trial < 2000; ++trial) {
    uint8_t board[hex::kCells];
    for (int cell = 0; cell < hex::kCells; ++cell) {
      board[cell] = (nextRandom() & 1u) ? hex::kBlack : hex::kWhite;
    }
    const bool black = floodConnects(board, hex::kBlack);
    const bool white = floodConnects(board, hex::kWhite);
    CHECK(black != white);
    CHECK(hexbrain::winnerOfFilledForTest(board) == (black ? hex::kBlack : hex::kWhite));
  }
}

void testAWinningChainIsAPathBetweenTheWinnersTwoEdges() {
  for (int trial = 0; trial < 60; ++trial) {
    hex::Game game;
    hex::reset(game);
    int guard = 0;
    while (!hex::over(game) && guard++ <= hex::kCells) {
      int empty[hex::kCells];
      int count = 0;
      for (int cell = 0; cell < hex::kCells; ++cell) {
        if (game.at(cell) == hex::kEmpty) empty[count++] = cell;
      }
      if (count == 0) break;
      hex::play(game, empty[nextRandom() % static_cast<uint32_t>(count)]);
    }
    CHECK(hex::over(game));

    uint8_t chain[hex::kMaskBytes];
    CHECK(hex::winningChain(game, chain));
    int marked = 0;
    bool touchesFirst = false;
    bool touchesSecond = false;
    for (int cell = 0; cell < hex::kCells; ++cell) {
      if (!hex::marked(chain, cell)) continue;
      ++marked;
      // Every marked cell is the winner's, and every one of them has a marked
      // neighbour unless the chain is one cell long: this is a connection, not
      // a selection.
      CHECK(game.at(cell) == game.winner);
      int neighbours = 0;
      for (int dir = 0; dir < 6; ++dir) {
        const int next = hex::neighbour(cell, dir);
        if (next != hex::kNoCell && hex::marked(chain, next)) ++neighbours;
      }
      CHECK(neighbours >= 1);
      if (game.winner == hex::kBlack) {
        if (hex::rowOf(cell) == 0) touchesFirst = true;
        if (hex::rowOf(cell) == hex::kSize - 1) touchesSecond = true;
      } else {
        if (hex::colOf(cell) == 0) touchesFirst = true;
        if (hex::colOf(cell) == hex::kSize - 1) touchesSecond = true;
      }
    }
    CHECK(touchesFirst);
    CHECK(touchesSecond);
    // A shortest path across eleven rows cannot be shorter than eleven cells,
    // and if it were as long as the whole board it would not be a path.
    CHECK(marked >= hex::kSize);
    CHECK(marked < hex::kCells);
  }

  // Nothing to draw while the game is running, and the mask is cleared rather
  // than left holding whatever the caller had.
  hex::Game running;
  hex::reset(running);
  uint8_t chain[hex::kMaskBytes];
  for (int i = 0; i < hex::kMaskBytes; ++i) chain[i] = 0xFF;
  CHECK(!hex::winningChain(running, chain));
  for (int i = 0; i < hex::kMaskBytes; ++i) CHECK(chain[i] == 0);
}

void testAnImmediateWinIsVisibleBeforeItIsPlayed() {
  hex::Game game;
  hex::reset(game);
  // Black one cell short of a column.
  for (int row = 0; row < hex::kSize; ++row) {
    if (row == 4) continue;
    game.toMove = hex::kBlack;
    CHECK(hex::play(game, hex::cellAt(row, 5)));
  }
  game.toMove = hex::kBlack;
  CHECK(!hex::over(game));

  int winners = 0;
  for (int cell = 0; cell < hex::kCells; ++cell) {
    if (game.at(cell) != hex::kEmpty) continue;
    if (hex::winsImmediately(game, cell, hex::kBlack)) ++winners;
  }
  CHECK(winners == 1);
  CHECK(hex::winsImmediately(game, hex::cellAt(4, 5), hex::kBlack));
  // Asking does not answer on the caller's board: the probe is a copy.
  CHECK(!hex::over(game));
  CHECK(game.at(hex::cellAt(4, 5)) == hex::kEmpty);
}

void testTheBridgeTableIsTheRealPattern() {
  for (int cell = 0; cell < hex::kCells; ++cell) {
    for (int dir = 0; dir < 6; ++dir) {
      const hex::Bridge& bridge = hex::kBridges.at[cell][dir];
      if (bridge.partner == hex::kNoCell) {
        CHECK(bridge.end[0] == hex::kNoCell);
        CHECK(bridge.end[1] == hex::kNoCell);
        continue;
      }
      const int first = bridge.end[0];
      const int second = bridge.end[1];
      const int partner = bridge.partner;
      CHECK(first != second);
      CHECK(partner != cell);
      // The two ends are NOT neighbours of each other -- that is what makes it
      // a bridge rather than a touching pair -- and their only two common
      // neighbours are this cell and its partner.
      int common = 0;
      bool sawCell = false;
      bool sawPartner = false;
      bool adjacent = false;
      for (int d = 0; d < 6; ++d) {
        const int next = hex::neighbour(first, d);
        if (next == hex::kNoCell) continue;
        if (next == second) adjacent = true;
        for (int e = 0; e < 6; ++e) {
          if (hex::neighbour(second, e) == next) {
            ++common;
            if (next == cell) sawCell = true;
            if (next == partner) sawPartner = true;
          }
        }
      }
      CHECK(!adjacent);
      CHECK(common == 2);
      CHECK(sawCell);
      CHECK(sawPartner);
    }
  }
}

// --- the save file ---------------------------------------------------------

hexsave::Save aSaveWithAGameInIt() {
  hexsave::Save save;
  save.wins = 7;
  save.losses = 3;
  save.opponent = hex::Opponent::Human;
  save.level = hex::Level::Hard;
  save.playAs = hex::kWhite;
  save.hasHistory = true;
  save.lastWon = true;
  for (int i = 0; i < hex::kCellBytes; ++i) save.lastCells[i] = static_cast<uint8_t>(i * 7 + 1);
  save.inProgress = true;
  save.seat = hex::kWhite;
  hex::reset(save.game);
  for (int move = 0; move < 12; ++move) hex::play(save.game, hex::cellAt(move % 11, (move * 3) % 11));
  return save;
}

void testASaveComesBackTheWayItWentIn() {
  const hexsave::Save save = aSaveWithAGameInIt();
  char line[hexsave::kMaxLineBytes];
  const int bytes = hexsave::pack(save, line, sizeof(line));
  CHECK(bytes > 0);
  CHECK(line[bytes - 1] == '\n');

  hexsave::Save back;
  CHECK(hexsave::unpack(line, back));
  CHECK(back.wins == save.wins);
  CHECK(back.losses == save.losses);
  CHECK(back.opponent == save.opponent);
  CHECK(back.level == save.level);
  CHECK(back.playAs == save.playAs);
  CHECK(back.hasHistory == save.hasHistory);
  CHECK(back.lastWon == save.lastWon);
  for (int i = 0; i < hex::kCellBytes; ++i) CHECK(back.lastCells[i] == save.lastCells[i]);
  CHECK(back.inProgress);
  CHECK(back.seat == save.seat);
  // The whole position, union-find included: a resumed game has to know who is
  // already connected to which edge, and rebuilding that on load would be a
  // second implementation of the one fact the file is about.
  CHECK(std::memcmp(&back.game, &save.game, sizeof(hex::Game)) == 0);
}

void testAShortLineKeepsWhatItReachedAndDefaultsTheRest() {
  const hexsave::Save save = aSaveWithAGameInIt();
  char line[hexsave::kMaxLineBytes];
  CHECK(hexsave::pack(save, line, sizeof(line)) > 0);

  // Cut after the settings. A build that wrote no ornament and no game is a
  // build this one can still take a record and a level from -- which is the
  // whole reason the format has checkpoints instead of a refusal.
  char truncated[hexsave::kMaxLineBytes];
  std::snprintf(truncated, sizeof(truncated), "%d %d %d %d %d %d\n", hexsave::kVersion, save.wins, save.losses,
                static_cast<int>(save.opponent), static_cast<int>(save.level), save.playAs);
  hexsave::Save back;
  CHECK(hexsave::unpack(truncated, back));
  CHECK(back.wins == save.wins);
  CHECK(back.level == save.level);
  CHECK(back.playAs == save.playAs);
  CHECK(!back.hasHistory);
  CHECK(!back.inProgress);

  // Cut in the MIDDLE of the position. The record and the ornament survive and
  // the resume does not, which is the rule: a screen is only meaningful with
  // the state behind it.
  char half[hexsave::kMaxLineBytes];
  const int keep = static_cast<int>(std::strlen(line)) - 200;
  std::memcpy(half, line, static_cast<size_t>(keep));
  half[keep] = '\n';
  half[keep + 1] = '\0';
  hexsave::Save partial;
  CHECK(hexsave::unpack(half, partial));
  CHECK(partial.wins == save.wins);
  CHECK(partial.hasHistory);
  CHECK(!partial.inProgress);
}

void testAFileFromAnotherFormatIsRefusedRatherThanRead() {
  hexsave::Save save;
  save.wins = 99;
  const hexsave::Save untouched = save;

  // A different version is not a short line: the fields may have MOVED, so
  // reading as far as it goes would shift a whole board along by one and the
  // game would load and be wrong.
  char other[64];
  std::snprintf(other, sizeof(other), "%d 1 2 0 1 1\n", hexsave::kVersion + 1);
  CHECK(!hexsave::unpack(other, save));
  CHECK(save.wins == untouched.wins);

  CHECK(!hexsave::unpack("", save));
  CHECK(!hexsave::unpack("not a save file at all\n", save));
  CHECK(!hexsave::unpack(nullptr, save));
  CHECK(save.wins == untouched.wins);

  // A buffer too small gets nothing rather than a truncated line, because a
  // truncated line is exactly what this format accepts.
  const hexsave::Save full = aSaveWithAGameInIt();
  char tiny[40];
  CHECK(hexsave::pack(full, tiny, sizeof(tiny)) == 0);
}

void testAMatchCountedInMemorySurvivesTheLinkTeardown() {
  // The failure this stands in for is silent and shipped twice in this fork.
  // A nearby game is counted by onMatchEnded() in MEMORY -- writeSave() refuses
  // for the whole length of a match, because the position on screen is the
  // shared game and the file is what the solo game resumes from -- and then the
  // teardown reloads the card to put the solo game back. Reload the record with
  // it and the match is gone: no crash, no log, a tally that never moves.
  //
  // The whole sequence, with the card real: a save on the card, a match counted
  // on top of it, the teardown, the write that teardown is the first moment
  // for, and the reload after it.
  hexsave::Save before;
  before.wins = 4;
  before.losses = 2;
  before.hasHistory = true;
  before.lastWon = false;
  before.level = hex::Level::Hard;
  for (int i = 0; i < hex::kCellBytes; ++i) before.lastCells[i] = 0x11;

  char card[hexsave::kMaxLineBytes];
  CHECK(hexsave::pack(before, card, sizeof(card)) > 0);

  // recordResult() for a match this device won, in memory.
  hexsave::Record counted;
  counted.wins = before.wins + 1;
  counted.losses = before.losses;
  counted.hasHistory = true;
  counted.lastWon = true;

  // The teardown reloads the card, which is the step that would clobber it.
  hexsave::Save reloaded;
  CHECK(hexsave::unpack(card, reloaded));
  CHECK(reloaded.wins == before.wins);
  hexsave::Record onCard;
  onCard.wins = reloaded.wins;
  onCard.losses = reloaded.losses;
  onCard.hasHistory = reloaded.hasHistory;
  onCard.lastWon = reloaded.lastWon;

  const hexsave::Record kept = hexsave::recordAfterLink(counted, onCard);
  CHECK(kept.wins == before.wins + 1);
  CHECK(kept.losses == before.losses);
  CHECK(kept.lastWon);

  // And it reaches the card, exactly once. Counting it twice is the other half
  // of the same bug and would read as a device that wins every nearby game
  // twice over.
  hexsave::Save after = reloaded;
  after.wins = kept.wins;
  after.losses = kept.losses;
  after.hasHistory = kept.hasHistory;
  after.lastWon = kept.lastWon;
  char written[hexsave::kMaxLineBytes];
  CHECK(hexsave::pack(after, written, sizeof(written)) > 0);

  hexsave::Save resumed;
  CHECK(hexsave::unpack(written, resumed));
  CHECK(resumed.wins == before.wins + 1);
  CHECK(resumed.losses == before.losses);
  CHECK(resumed.lastWon);
  // The settings came off the card and were not clobbered in the other
  // direction: a teardown that kept the whole in-memory Save would have put the
  // match's board and seat back on the front door as a resumable solo game.
  CHECK(resumed.level == hex::Level::Hard);
  CHECK(!resumed.inProgress);

  // A match nobody counted -- the opponent left before a stone went down --
  // keeps what the card has rather than writing a blank record over it.
  hexsave::Record nothing;
  nothing.wins = before.wins;
  nothing.losses = before.losses;
  nothing.hasHistory = before.hasHistory;
  nothing.lastWon = before.lastWon;
  const hexsave::Record quiet = hexsave::recordAfterLink(nothing, onCard);
  CHECK(quiet.wins == before.wins);
  CHECK(quiet.losses == before.losses);

  // And a card somehow further along than the memory is not rolled back.
  hexsave::Record ahead = onCard;
  ahead.wins += 3;
  const hexsave::Record newer = hexsave::recordAfterLink(nothing, ahead);
  CHECK(newer.wins == ahead.wins);
}

void testAnImpossiblePositionCostsTheResumeAndNotTheRecord() {
  hexsave::Save save = aSaveWithAGameInIt();
  save.game.toMove = 9;  // not a colour
  char line[hexsave::kMaxLineBytes];
  CHECK(hexsave::pack(save, line, sizeof(line)) > 0);
  hexsave::Save back;
  CHECK(hexsave::unpack(line, back));
  CHECK(back.wins == save.wins);
  CHECK(back.level == save.level);
  CHECK(!back.inProgress);
}

// --- the brain -------------------------------------------------------------

std::unique_ptr<hexbrain::Pool> makePool() { return std::unique_ptr<hexbrain::Pool>(new hexbrain::Pool()); }

void testTheStateFitsAPacket() {
  // Play<> static_asserts this already; the margin is what makes sending the
  // whole state rather than the move affordable, so it is worth stating where
  // somebody changing the struct will read it.
  CHECK(sizeof(hex::Game) <= 192);
  CHECK(__is_trivially_copyable(hex::Game));
}

void testTheSameSeedReturnsTheSameMove() {
  auto pool = makePool();
  hex::Game game;
  hex::reset(game);
  hex::play(game, hex::cellAt(5, 5));
  hex::play(game, hex::cellAt(4, 6));

  for (const hex::Level level : {hex::Level::Easy, hex::Level::Normal}) {
    uint32_t first = 777u;
    uint32_t second = 777u;
    const int a = hexbrain::chooseMove(game, level, first, *pool);
    const int b = hexbrain::chooseMove(game, level, second, *pool);
    CHECK(a == b);
    // And the seed itself advanced the same way, so a SERIES of moves is
    // reproducible rather than only the first one.
    CHECK(first == second);
  }

  // HARD's branch, which the two levels above never reach: RAVE reads the
  // finished board and UCT exploration is off, so the whole selection is a
  // different expression and repeating it is a different claim. Through
  // chooseMoveWith at a budget a suite can afford -- HARD's own thirty thousand
  // simulations are seconds a test should not spend to ask this.
  const hexbrain::Settings amaf{400, 0, true, true};
  uint32_t first = 31337u;
  uint32_t second = 31337u;
  const int a = hexbrain::chooseMoveWith(game, amaf, first, *pool);
  const int b = hexbrain::chooseMoveWith(game, amaf, second, *pool);
  CHECK(a == b);
  CHECK(first == second);
  CHECK(hex::legal(game, a));
}

// A clock the test controls, so "it stopped when it was told" is a fact rather
// than a stopwatch reading.
uint32_t gFakeMs = 0;
uint32_t gFakeReadings = 0;
uint32_t gFakeStep = 400;
uint32_t fakeClock() {
  // The step is set to just over half the level's budget, so the budget
  // genuinely runs out on the second chunk. A one millisecond step does not:
  // the search finishes its whole simulation count in a handful of readings and
  // the branch this test exists to exercise is never taken -- the test then
  // passes with the budget check deleted.
  ++gFakeReadings;
  gFakeMs += gFakeStep;
  return gFakeMs;
}

void testTheClockStopsTheSearchWhateverTheSimulationCountSays() {
  // The count alone is not a budget. The same simulations are a fraction of a
  // second on a laptop and seconds on the device, and which one you get is a
  // property of the machine -- so the search runs against a clock the caller
  // lends, and this proves the clock is actually consulted.
  //
  // Every other brain test in this file passes no clock at all, which is what
  // keeps them deterministic and is also why deleting the budget break left the
  // whole suite green.
  auto pool = makePool();
  hex::Game game;
  hex::reset(game);
  CHECK(hex::play(game, hex::cellAt(5, 5)));

  for (int i = 0; i < 3; ++i) {
    const hex::Level level = static_cast<hex::Level>(i);
    const hexbrain::Settings settings = hexbrain::settingsFor(level);
    CHECK(settings.budgetMs > 0);
    // The ceiling the budgets exist for. Held below it with room, because the
    // chunk that is running when the clock expires still has to finish.
    CHECK(settings.budgetMs <= 4500);

    // The same position twice: once with all the time in the world, once with
    // this clock. The comparison is what makes the assertion able to fail --
    // "fewer than the count" is also true of a search that simply ran out of
    // board, and that is not what is being measured.
    uint32_t seed = 9090u + static_cast<uint32_t>(i);
    const int unhurried = hexbrain::chooseMove(game, level, seed, *pool, nullptr);
    const int unhurriedSims = hexbrain::lastSimulations();
    CHECK(hex::legal(game, unhurried));
    CHECK(unhurriedSims > 64);

    gFakeMs = 0;
    gFakeReadings = 0;
    gFakeStep = settings.budgetMs / 2 + 1;
    seed = 9090u + static_cast<uint32_t>(i);
    const int move = hexbrain::chooseMove(game, level, seed, *pool, fakeClock);
    // Still a legal cell. A search stopped mid-thought that answered with
    // nothing, or with a cell already holding a stone, is worse than a slow one.
    CHECK(hex::legal(game, move));
    // The clock was read, the budget ran out on it, and the search that was
    // stopped did strictly less work than the one that was not.
    CHECK(gFakeReadings > 1);
    CHECK(gFakeMs >= settings.budgetMs);
    CHECK(hexbrain::lastSimulations() < unhurriedSims);
    CHECK(hexbrain::lastSimulations() < static_cast<int>(settings.simulations));
    // And it says how long it believed it spent, which is what the device log
    // prints beside the count.
    CHECK(hexbrain::lastMs() >= settings.budgetMs);
  }

  // With no clock at all it is bounded by the count alone, and nothing is read.
  gFakeMs = 0;
  gFakeReadings = 0;
  uint32_t seed = 4242u;
  const int move = hexbrain::chooseMove(game, hex::Level::Easy, seed, *pool);
  CHECK(hex::legal(game, move));
  CHECK(gFakeReadings == 0);
  CHECK(hexbrain::lastMs() == 0);
}

void testEveryLevelIsADifferentPlayer() {
  // Three levels made of THREE knobs, which is the claim HexBrain.h makes and
  // the reason the Elo numbers in it are quoted: the budget rises, the playout
  // policy gains the bridge, and the selection gains RAVE. Asserted here
  // because all three could collapse into one value with every other test in
  // this file still green -- a level ladder nothing compares is three names for
  // one player.
  const hexbrain::Settings easy = hexbrain::settingsFor(hex::Level::Easy);
  const hexbrain::Settings normal = hexbrain::settingsFor(hex::Level::Normal);
  const hexbrain::Settings hard = hexbrain::settingsFor(hex::Level::Hard);

  CHECK(easy.simulations < normal.simulations);
  CHECK(normal.simulations < hard.simulations);
  // The clock ladder rises with it, or a level meant to think harder is cut off
  // before it can.
  CHECK(easy.budgetMs < normal.budgetMs);
  CHECK(normal.budgetMs < hard.budgetMs);
  // Every one of them under the ceiling, HARD included -- it is the only one
  // that can reach it.
  CHECK(easy.budgetMs <= 4500);
  CHECK(normal.budgetMs <= 4500);
  CHECK(hard.budgetMs <= 4500);

  // And the policies, where the Elo actually came from. EASY is plain UCT with
  // plain playouts; the bridge arrives at NORMAL; RAVE arrives at HARD and
  // never without the bridge under it, which is the pairing the measurement was
  // made with.
  CHECK(!easy.bridge);
  CHECK(!easy.amaf);
  CHECK(normal.bridge);
  CHECK(!normal.amaf);
  CHECK(hard.bridge);
  CHECK(hard.amaf);

  // An out-of-range level is somebody else's bug and must still be playable.
  const hexbrain::Settings fallback = hexbrain::settingsFor(hex::Level::Count_);
  CHECK(fallback.simulations > 0);
  CHECK(fallback.budgetMs <= 4500);
}

void testTheHandRolledLogarithmIsTheRealOne() {
  // std::log carries no correctly-rounded guarantee, so UCT's logarithm is
  // computed in integer fixed point -- which means the one thing standing
  // between the search and a broken exploration term is this comparison. Return
  // 0.0 from naturalLog and every other assertion in this suite stays green:
  // the search still returns legal moves, still repeats on a seed, and still
  // beats the level below it, because the exploration term merely stops
  // exploring.
  CHECK(hexbrain::naturalLogForTest(0) == 0.0);
  CHECK(hexbrain::naturalLogForTest(1) == 0.0);

  // Across the visit counts a real search reaches: a node's parent has between
  // two and the whole simulation count.
  double worst = 0.0;
  for (uint32_t n = 2; n <= 40000; n = n < 200 ? n + 1 : n + 137) {
    const double got = hexbrain::naturalLogForTest(n);
    const double want = std::log(static_cast<double>(n));
    const double error = got > want ? got - want : want - got;
    if (error > worst) worst = error;
    CHECK(error < 1e-4);
    // Monotone, or the exploration term would prefer a parent it had visited
    // less. The fixed-point squaring loop is exactly where that could go wrong.
    CHECK(got > hexbrain::naturalLogForTest(n - 1) - 1e-9);
  }
  std::printf("  naturalLog: worst error against std::log %.3g\n", worst);
}

void testEveryMoveTheBrainOffersIsLegal() {
  auto pool = makePool();
  uint32_t seed = 4242u;
  const hexbrain::Settings quick{40, 0, true, true};
  for (int trial = 0; trial < 40; ++trial) {
    hex::Game game;
    hex::reset(game);
    int guard = 0;
    while (!hex::over(game) && guard++ <= hex::kCells) {
      const int move = hexbrain::chooseMoveWith(game, quick, seed, *pool);
      CHECK(move != hex::kNoCell);
      CHECK(hex::legal(game, move));
      CHECK(hex::play(game, move));
    }
    CHECK(hex::over(game));
  }
}

void testABudgetOfNothingStillAnswersWithALegalCell() {
  // The I/O matrix's "brain out of time": no playout ever ran, and what comes
  // back still has to be a cell somebody can play. Never an illegal one, and
  // never a corner picked by index order.
  auto pool = makePool();
  uint32_t seed = 5u;
  hex::Game game;
  hex::reset(game);
  const hexbrain::Settings none{0, 0, false, false};
  const int move = hexbrain::chooseMoveWith(game, none, seed, *pool);
  CHECK(hex::legal(game, move));
  CHECK(hexbrain::lastSimulations() == 0);
  CHECK(move == hex::cellAt(hex::kSize / 2, hex::kSize / 2));
}

void testEveryLevelTakesTheWinAndBlocksTheLoss() {
  auto pool = makePool();
  // A column of Black with one hole, and it is White to move: the hole is
  // White's only move that matters and Black's only move that wins. A level
  // that walks past either does not read as easy, it reads as broken.
  hex::Game position;
  hex::reset(position);
  for (int row = 0; row < hex::kSize; ++row) {
    if (row == 4) continue;
    position.toMove = hex::kBlack;
    hex::play(position, hex::cellAt(row, 5));
  }

  for (const hex::Level level : {hex::Level::Easy, hex::Level::Normal, hex::Level::Hard}) {
    uint32_t seed = 31337u;
    hex::Game taking = position;
    taking.toMove = hex::kBlack;
    CHECK(hexbrain::chooseMove(taking, level, seed, *pool) == hex::cellAt(4, 5));
    // The root check answers before a single playout, so this costs nothing
    // even at HARD's budget.
    CHECK(hexbrain::lastSimulations() == 0);

    hex::Game blocking = position;
    blocking.toMove = hex::kWhite;
    CHECK(hexbrain::chooseMove(blocking, level, seed, *pool) == hex::cellAt(4, 5));
  }
}

// One whole game between two settings, both sides drawing from the same seed as
// it advances, so a series is reproducible move for move from its first number.
uint8_t playOneGame(const hexbrain::Settings& black, const hexbrain::Settings& white, uint32_t seed,
                    hexbrain::Pool& pool) {
  hex::Game game;
  hex::reset(game);
  int guard = 0;
  while (!hex::over(game) && guard++ <= hex::kCells) {
    const hexbrain::Settings& settings = game.toMove == hex::kBlack ? black : white;
    const int move = hexbrain::chooseMoveWith(game, settings, seed, pool);
    if (move == hex::kNoCell) break;
    if (!hex::play(game, move)) break;
  }
  return game.winner;
}

void testTheLevelsBeatEachOtherInOrder() {
  // The shipped budgets are 1.5k, 8k and 30k simulations, which is minutes of
  // laptop time a suite cannot spend. These keep the RATIO and the policies --
  // which is what the levels actually differ by, per the Elo measurement in
  // HexBrain.h -- at a fortieth of the compute.
  auto pool = makePool();
  const hexbrain::Settings easy{50, 0, false, false};
  const hexbrain::Settings normal{200, 0, true, false};
  const hexbrain::Settings hard{800, 0, true, true};

  struct Pairing {
    const char* name;
    const hexbrain::Settings& strong;
    const hexbrain::Settings& weak;
  };
  const Pairing pairings[] = {
      {"hard beats normal", hard, normal},
      {"normal beats easy", normal, easy},
      {"hard beats easy", hard, easy},
  };
  for (const Pairing& pairing : pairings) {
    int strongWins = 0;
    // Colours alternate. Black moves first and there is no swap rule, so a
    // series played from one seat would measure the first-player advantage
    // rather than the difference between the two players.
    for (int i = 0; i < 8; ++i) {
      const bool strongIsBlack = (i % 2) == 0;
      const uint8_t won =
          playOneGame(strongIsBlack ? pairing.strong : pairing.weak, strongIsBlack ? pairing.weak : pairing.strong,
                      1000u + static_cast<uint32_t>(i) * 7919u, *pool);
      if (won == (strongIsBlack ? hex::kBlack : hex::kWhite)) ++strongWins;
    }
    check(strongWins > 4, pairing.name, __LINE__);
    std::printf("  %s: %d of 8\n", pairing.name, strongWins);
  }
}

void testAPlayoutAlwaysFillsTheBoardAndNamesAWinner() {
  uint32_t seed = 999u;
  for (const bool bridge : {false, true}) {
    for (int trial = 0; trial < 200; ++trial) {
      uint8_t board[hex::kCells];
      for (int cell = 0; cell < hex::kCells; ++cell) board[cell] = hex::kEmpty;
      // A few stones down first, so the playout starts from a real position
      // rather than always from empty.
      const int seeded = static_cast<int>(nextRandom() % 20u);
      for (int i = 0; i < seeded; ++i) {
        board[nextRandom() % hex::kCells] = (i % 2) ? hex::kWhite : hex::kBlack;
      }
      const uint8_t won = hexbrain::playoutForTest(board, hex::kBlack, seed, bridge);
      for (int cell = 0; cell < hex::kCells; ++cell) CHECK(board[cell] != hex::kEmpty);
      CHECK(won == hex::kBlack || won == hex::kWhite);
      CHECK(floodConnects(board, won));
      CHECK(!floodConnects(board, hex::other(won)));
    }
  }
}

// --- the flow --------------------------------------------------------------

void testBackLeavesTheAppOnlyFromTheFrontDoor() {
  CHECK(hex::leavesApp(hex::Screen::Menu));
  CHECK(!hex::leavesApp(hex::Screen::Settings));
  CHECK(!hex::leavesApp(hex::Screen::Board));
  CHECK(!hex::leavesApp(hex::Screen::Result));
  CHECK(hex::back(hex::Screen::Settings) == hex::Screen::Menu);
  CHECK(hex::back(hex::Screen::Board) == hex::Screen::Menu);
  CHECK(hex::back(hex::Screen::Result) == hex::Screen::Menu);
  CHECK(hex::back(hex::Screen::Menu) == hex::Screen::Menu);

  CHECK(hex::nextLevel(hex::Level::Easy) == hex::Level::Normal);
  CHECK(hex::nextLevel(hex::Level::Normal) == hex::Level::Hard);
  CHECK(hex::nextLevel(hex::Level::Hard) == hex::Level::Easy);
  CHECK(std::strcmp(hex::levelName(hex::Level::Easy), "EASY") == 0);
  CHECK(std::strcmp(hex::levelName(hex::Level::Normal), "NORMAL") == 0);
  CHECK(std::strcmp(hex::levelName(hex::Level::Hard), "HARD") == 0);
}

}  // namespace

int main() {
  testAResetBoardIsTheSameBYTESWhateverWasThereBefore();
  testTheEmptyBoardIsEmptyAndBlackMovesFirst();
  testAStonePlacedStaysAndTheTurnPasses();
  testTheNeighbourhoodIsSymmetricAndSixWide();
  testAColumnOfBlackJoinsTopToBottom();
  testARowOfWhiteJoinsLeftToRight();
  testTheUnionFindAndAFloodFillAlwaysAgree();
  testAFullBoardHasExactlyOneWinnerWhateverIsOnIt();
  testAWinningChainIsAPathBetweenTheWinnersTwoEdges();
  testAnImmediateWinIsVisibleBeforeItIsPlayed();
  testTheBridgeTableIsTheRealPattern();

  testASaveComesBackTheWayItWentIn();
  testAShortLineKeepsWhatItReachedAndDefaultsTheRest();
  testAFileFromAnotherFormatIsRefusedRatherThanRead();
  testAnImpossiblePositionCostsTheResumeAndNotTheRecord();
  testAMatchCountedInMemorySurvivesTheLinkTeardown();

  testTheStateFitsAPacket();
  testTheSameSeedReturnsTheSameMove();
  testTheHandRolledLogarithmIsTheRealOne();
  testEveryLevelIsADifferentPlayer();
  testTheClockStopsTheSearchWhateverTheSimulationCountSays();
  testEveryMoveTheBrainOffersIsLegal();
  testABudgetOfNothingStillAnswersWithALegalCell();
  testEveryLevelTakesTheWinAndBlocksTheLoss();
  testAPlayoutAlwaysFillsTheBoardAndNamesAWinner();
  testTheLevelsBeatEachOtherInOrder();

  testBackLeavesTheAppOnlyFromTheFrontDoor();

  std::printf("test_hex: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
