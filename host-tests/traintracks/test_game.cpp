// Train Tracks, one game in progress: CHECK, HINT, the front door's offer and
// the save file, checked without a panel.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "TrainTracksCore.h"
#include "TrainTracksFlow.h"
#include "TrainTracksGame.h"
#include "TrainTracksGenerator.h"
#include "TrainTracksSave.h"
#include "TrainTracksSolver.h"

using namespace traintracks;

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

GenWork work;
Solver scratch;
Solver checker;

constexpr Level kLevels3[] = {Level::Easy, Level::Medium, Level::Hard};

Game newGame(const Level level, const uint32_t seed) {
  Game g;
  Generated made;
  const bool ok = generate(level, seed, 2000, made, work);
  CHECK(ok);
  g.level = level;
  g.seed = made.seed;
  g.attempt = made.attempt;
  g.puzzle = made.puzzle;
  resetBoard(g.board, g.puzzle);
  return g;
}

// CHECK flags every mark that contradicts the answer, and on the route every
// square drawn in a shape the answer does not have: a chosen piece that is
// not the answer's, or a join to track the answer does not make. Never an
// empty square, and never an open end that only points at one.
void testCheckFlagsWrongMarksAndShapes() {
  for (const Level level : kLevels3) {
    for (uint32_t seed = 1; seed <= 40; ++seed) {
      Game g = newGame(level, seed);
      Rng rng{seed * 31u};
      const int n = g.puzzle.size;
      for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
          const int i = cellIndex(r, c);
          if (g.puzzle.given[i]) continue;
          g.board.cell[i] = static_cast<uint8_t>(rng.below(3));
          if (g.board.cell[i] == kTrack && rng.below(3) == 0) {
            uint8_t options[6];
            const int count = piecesFor(g.board, g.puzzle, i, options);
            if (count > 0) g.board.pin[i] = options[rng.below(count)];
          }
        }
      }
      uint8_t shapes[kMaxCells];
      inferShapes(g.board, g.puzzle, shapes);
      // Worked out here independently of isWrongShape.
      int expected = 0;
      bool flaggedRight = true;
      uint8_t wrong[kMaxCells];
      const int count = checkBoard(g.puzzle, g.board, wrong);
      for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
          const int i = cellIndex(r, c);
          const uint8_t state = g.board.cell[i];
          bool bad = (state == kTrack && !g.puzzle.solution[i]) || (state == kCross && g.puzzle.solution[i]);
          if (state == kTrack && g.puzzle.solution[i] && !g.puzzle.given[i]) {
            if (g.board.pin[i] && g.board.pin[i] != g.puzzle.solution[i]) bad = true;
            for (int d = 0; d < 4; ++d) {
              if (!(shapes[i] & dirBit(d))) continue;
              const int r2 = r + kDRow[d];
              const int c2 = c + kDCol[d];
              if (!inside(g.puzzle, r2, c2)) continue;
              const int j = cellIndex(r2, c2);
              const bool joined = g.board.cell[j] == kTrack && (shapes[j] & dirBit(opposite(d)));
              if (joined && !(g.puzzle.solution[i] & dirBit(d))) bad = true;
            }
          }
          expected += bad;
          if ((wrong[i] != 0) != bad) flaggedRight = false;
          if (state == kEmpty) CHECK(wrong[i] == 0);
        }
      }
      CHECK(count == expected);
      CHECK(flaggedRight);
    }
  }
}

// The answer, drawn as the answer, is never wrong; one chosen piece turned
// away from it is, and only that square.
void testCheckJudgesAChosenPiece() {
  Game g = newGame(Level::Medium, 9);
  for (int i = 0; i < kMaxCells; ++i) {
    if (!inside(g.puzzle, rowOf(i), colOf(i)) || g.puzzle.given[i]) continue;
    g.board.cell[i] = g.puzzle.solution[i] ? kTrack : kCross;
    g.board.pin[i] = g.puzzle.solution[i];
  }
  uint8_t wrong[kMaxCells];
  CHECK(checkBoard(g.puzzle, g.board, wrong) == 0);
  int target = -1;
  for (int i = 0; i < kMaxCells && target < 0; ++i) {
    if (g.board.pin[i] && !g.puzzle.given[i]) target = i;
  }
  CHECK(target >= 0);
  uint8_t other = 0;
  for (const uint8_t piece : kPieces) {
    if (piece != g.puzzle.solution[target] && other == 0) other = piece;
  }
  g.board.pin[target] = other;
  CHECK(checkBoard(g.puzzle, g.board, wrong) >= 1);
  CHECK(wrong[target] == 1);
  // HINT puts it right before anything else.
  const Hint h = findHint(g.puzzle, g.board, scratch);
  CHECK(h.kind == HintKind::Fixed && h.cell == target && h.state == kTrack && h.pin == g.puzzle.solution[target]);
  CHECK(applyHint(g, h));
  CHECK(checkBoard(g.puzzle, g.board, wrong) == 0);
}

// A route still being laid: right squares, guessed shapes whose open ends
// point at empty squares. Nothing there is a claim, so nothing is wrong.
void testAnOpenEndIsNotWrong() {
  const uint8_t path[] = {static_cast<uint8_t>(cellIndex(2, 0)), static_cast<uint8_t>(cellIndex(2, 1)),
                          static_cast<uint8_t>(cellIndex(1, 1)), static_cast<uint8_t>(cellIndex(1, 2)),
                          static_cast<uint8_t>(cellIndex(1, 3)), static_cast<uint8_t>(cellIndex(1, 4)),
                          static_cast<uint8_t>(cellIndex(1, 5))};
  Puzzle p;
  CHECK(buildFromPath(p, 6, path, 7, kWest, kEast));
  Board b;
  resetBoard(b, p);
  b.cell[cellIndex(1, 1)] = kTrack;  // alone: a guessed straight, both ends open
  uint8_t wrong[kMaxCells];
  CHECK(checkBoard(p, b, wrong) == 0);
}

// Hints only ever say true things. Taken one after another from an empty
// board they finish every puzzle, never touch a given, and never need more
// hints than there are squares.
void testHintsAreAlwaysCorrectAndFinish() {
  for (const Level level : kLevels3) {
    for (uint32_t seed = 100; seed < 160; ++seed) {
      Game g = newGame(level, seed);
      const int n = g.puzzle.size;
      int taken = 0;
      bool won = false;
      bool firstTierOk = true;
      for (; taken <= 2 * n * n && !won; ++taken) {
        const Hint h = findHint(g.puzzle, g.board, scratch);
        CHECK(h.cell >= 0);
        if (h.cell < 0) break;
        CHECK(!g.puzzle.given[h.cell]);
        CHECK((h.state == kTrack) == (g.puzzle.solution[h.cell] != 0));
        if (h.kind == HintKind::Fixed) {
          // Only ever a shape here: hints place right squares, and a guessed
          // join between two of them can still be one the answer lacks.
          CHECK(h.state == kTrack && h.pin == g.puzzle.solution[h.cell]);
        } else {
          CHECK(h.tier >= 1 && h.tier <= spec(level).tier);
        }
        if (taken == 0 && h.tier > spec(level).tier) firstTierOk = false;
        CHECK(applyHint(g, h));
        won = isWin(g.puzzle, g.board, checker);
      }
      CHECK(won);
      CHECK(firstTierOk);
    }
  }
}

// A wrong mark is the first thing a hint deals with, since any deduction past
// it would rest on something false.
void testAHintPutsAWrongMarkRightFirst() {
  for (const Level level : kLevels3) {
    for (uint32_t seed = 200; seed < 220; ++seed) {
      Game g = newGame(level, seed);
      int bad = -1;
      for (int i = 0; i < kMaxCells && bad < 0; ++i) {
        if (inside(g.puzzle, rowOf(i), colOf(i)) && !g.puzzle.solution[i]) bad = i;
      }
      CHECK(bad >= 0);
      g.board.cell[bad] = kTrack;
      const Hint h = findHint(g.puzzle, g.board, scratch);
      CHECK(h.kind == HintKind::Fixed);
      CHECK(h.cell == bad);
      CHECK(h.state == kCross);
      CHECK(applyHint(g, h));
      CHECK(g.board.cell[bad] == kCross);
      // And it is an ordinary move: undo takes it back.
      CHECK(undo(g.board) == bad);
      CHECK(g.board.cell[bad] == kTrack);
    }
  }
}

// A hint reasons forward from the player's own correct marks, so on a board
// that is one square from done it names that square.
void testAHintUsesWhatThePlayerKnows() {
  Game g = newGame(Level::Hard, 7);
  const int n = g.puzzle.size;
  int last = -1;
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      const int i = cellIndex(r, c);
      if (g.puzzle.given[i]) continue;
      g.board.cell[i] = g.puzzle.solution[i] ? kTrack : kCross;
      g.board.pin[i] = g.puzzle.solution[i];  // shapes right too, so only deduction is left
      if (g.puzzle.solution[i]) last = i;
    }
  }
  g.board.cell[last] = kEmpty;
  g.board.pin[last] = 0;
  const Hint h = findHint(g.puzzle, g.board, scratch);
  CHECK(h.cell == last);
  CHECK(h.state == kTrack);
  CHECK(h.tier == 1);
}

// The last move is the player's: a hint that would solve the puzzle is found
// but refused, and asking leaves the board and its history untouched.
void testTheLastMoveIsThePlayers() {
  Game g = newGame(Level::Medium, 21);
  int last = -1;
  for (int i = 0; i < kMaxCells; ++i) {
    if (!inside(g.puzzle, rowOf(i), colOf(i)) || g.puzzle.given[i]) continue;
    setCell(g.board, g.puzzle, rowOf(i), colOf(i), g.puzzle.solution[i] ? kTrack : kCross, g.puzzle.solution[i]);
    if (g.puzzle.solution[i]) last = i;
  }
  CHECK(undo(g.board) >= 0);  // take back one move, so one is left
  const Board before = g.board;
  const Hint h = findHint(g.puzzle, g.board, scratch);
  CHECK(h.cell >= 0);
  CHECK(hintSolves(g, h, scratch));
  // Untouched, including the redo that undo just made available.
  CHECK(std::memcmp(before.cell, g.board.cell, sizeof(before.cell)) == 0);
  CHECK(std::memcmp(before.pin, g.board.pin, sizeof(before.pin)) == 0);
  CHECK(before.history.first == g.board.history.first && before.history.count == g.board.history.count &&
        before.history.cursor == g.board.history.cursor);
  CHECK(std::memcmp(before.history.moves, g.board.history.moves, sizeof(before.history.moves)) == 0);
  CHECK(canRedo(g.board));
  // Two moves from the end, the hint is allowed.
  CHECK(undo(g.board) >= 0);
  const Hint early = findHint(g.puzzle, g.board, scratch);
  CHECK(!hintSolves(g, early, scratch));
  (void)last;
}

// The front door is the unfinished game if there is one, and otherwise the
// new-puzzle screen; Back walks toward it and leaves from it.
void testFlow() {
  CHECK(frontDoor(true) == Screen::Menu);
  CHECK(frontDoor(false) == Screen::NewGame);
  CHECK(leavesApp(Screen::Menu, true));
  CHECK(!leavesApp(Screen::NewGame, true));
  CHECK(leavesApp(Screen::NewGame, false));
  CHECK(!leavesApp(Screen::Board, false));
  CHECK(!leavesApp(Screen::HowTo, false));
  CHECK(back(Screen::NewGame, true) == Screen::Menu);
  CHECK(back(Screen::Board, true) == Screen::Menu);
  CHECK(back(Screen::Board, false) == Screen::NewGame);
  CHECK(back(Screen::HowTo, true) == Screen::NewGame);
  CHECK(back(Screen::HowTo, false) == Screen::NewGame);
  // Every screen reaches a door that leaves within a few steps, either way.
  for (const bool unfinished : {true, false}) {
    for (const Screen start : {Screen::Menu, Screen::HowTo, Screen::NewGame, Screen::Board}) {
      Screen at = start;
      int steps = 0;
      while (!leavesApp(at, unfinished) && steps < 4) {
        at = back(at, unfinished);
        ++steps;
      }
      CHECK(leavesApp(at, unfinished));
    }
  }
}

// CHECK's flags stay until their own square changes, so one CHECK serves for
// as many moves as it takes to put them right.
void testFlagsStayUntilTheirSquareChanges() {
  Game g = newGame(Level::Medium, 33);
  // Two wrong marks: track where there is none, far apart.
  int a = -1;
  int b = -1;
  for (int i = 0; i < kMaxCells; ++i) {
    if (!inside(g.puzzle, rowOf(i), colOf(i)) || g.puzzle.solution[i] || g.puzzle.given[i]) continue;
    if (a < 0) {
      a = i;
    } else if (b < 0 && (rowOf(i) > rowOf(a) + 2 || colOf(i) > colOf(a) + 2)) {
      b = i;
    }
  }
  CHECK(a >= 0 && b >= 0);
  setCell(g.board, g.puzzle, rowOf(a), colOf(a), kTrack);
  setCell(g.board, g.puzzle, rowOf(b), colOf(b), kTrack);
  CheckFlags flags{};
  CHECK(checkAndStamp(g.puzzle, g.board, flags) >= 2);
  CHECK(flags.wrong[a] && flags.wrong[b]);
  // A move on a square nowhere near them: both stay.
  int far = -1;
  for (int i = 0; i < kMaxCells && far < 0; ++i) {
    if (!inside(g.puzzle, rowOf(i), colOf(i)) || g.puzzle.given[i] || g.board.cell[i] != kEmpty) continue;
    const bool nearA = std::abs(rowOf(i) - rowOf(a)) + std::abs(colOf(i) - colOf(a)) <= 1;
    const bool nearB = std::abs(rowOf(i) - rowOf(b)) + std::abs(colOf(i) - colOf(b)) <= 1;
    if (!nearA && !nearB) far = i;
  }
  CHECK(far >= 0);
  holdCell(g.board, g.puzzle, rowOf(far), colOf(far));
  keepFlags(g.puzzle, g.board, flags);
  CHECK(flags.wrong[a] && flags.wrong[b]);
  // Clearing one takes that flag and only that flag.
  holdCell(g.board, g.puzzle, rowOf(a), colOf(a));
  const int left = keepFlags(g.puzzle, g.board, flags);
  CHECK(!flags.wrong[a] && flags.wrong[b]);
  CHECK(left >= 1);
  // A guessed piece re-bending because a neighbour changed counts as change.
  Board& board = g.board;
  int nb = -1;
  for (int d = 0; d < 4 && nb < 0; ++d) {
    const int r2 = rowOf(b) + kDRow[d];
    const int c2 = colOf(b) + kDCol[d];
    if (inside(g.puzzle, r2, c2) && board.cell[cellIndex(r2, c2)] == kEmpty && !g.puzzle.given[cellIndex(r2, c2)]) {
      nb = cellIndex(r2, c2);
    }
  }
  CHECK(nb >= 0);
  uint8_t before[kMaxCells];
  inferShapes(board, g.puzzle, before);
  tapCell(board, g.puzzle, rowOf(nb), colOf(nb));
  uint8_t after[kMaxCells];
  inferShapes(board, g.puzzle, after);
  keepFlags(g.puzzle, board, flags);
  CHECK((flags.wrong[b] != 0) == (before[b] == after[b]));
}

void testOrdinals() {
  CHECK(std::string("1") + ordinalSuffix(1) == "1ST");
  CHECK(std::string(ordinalSuffix(2)) == "ND");
  CHECK(std::string(ordinalSuffix(3)) == "RD");
  CHECK(std::string(ordinalSuffix(4)) == "TH");
  for (const unsigned n : {11u, 12u, 13u, 111u, 112u, 113u}) CHECK(std::string(ordinalSuffix(n)) == "TH");
  CHECK(std::string(ordinalSuffix(21)) == "ST");
  CHECK(std::string(ordinalSuffix(22)) == "ND");
  CHECK(std::string(ordinalSuffix(103)) == "RD");
  CHECK(std::string(ordinalSuffix(0)) == "TH");
}

void testAssistedMeansAnyHelp() {
  Game g;
  CHECK(!g.assisted());
  g.hints = 1;
  CHECK(g.assisted());
  g.hints = 0;
  g.checks = 1;
  CHECK(g.assisted());
}

bool sameBoard(const Board& a, const Board& b) {
  if (std::memcmp(a.cell, b.cell, sizeof(a.cell)) != 0) return false;
  if (a.history.first != b.history.first || a.history.count != b.history.count ||
      a.history.cursor != b.history.cursor) {
    return false;
  }
  for (int k = 0; k < kHistoryDepth; ++k) {
    const Move& x = a.history.moves[k];
    const Move& y = b.history.moves[k];
    if (x.cell != y.cell || x.from != y.from || x.to != y.to) return false;
  }
  return true;
}

// Played, written, read back: the same game, history and all, including a
// history that has wrapped its ring and a redo branch.
void testSaveRoundTrip() {
  for (const Level level : kLevels3) {
    for (uint32_t seed = 300; seed < 310; ++seed) {
      SaveState s;
      s.menuLevel = Level::Hard;
      s.hasGame = true;
      s.onBoard = (seed & 1) != 0;
      s.game = newGame(level, seed);
      s.game.elapsedMs = 3723456u + seed;
      s.game.hints = static_cast<uint16_t>(seed % 7);
      s.game.checks = static_cast<uint16_t>(seed % 3 == 0 ? 65535 : seed % 5);
      Rng rng{seed};
      const int n = s.game.puzzle.size;
      for (int k = 0; k < 90; ++k) tapCell(s.game.board, s.game.puzzle, rng.below(n), rng.below(n));
      undo(s.game.board);
      undo(s.game.board);

      char buffer[kSaveBytes];
      const int used = writeSave(s, buffer, kSaveBytes);
      CHECK(used > 0 && used < kSaveBytes);
      SaveState back;
      CHECK(readSave(buffer, back, work));
      CHECK(back.menuLevel == s.menuLevel);
      CHECK(back.hasGame);
      CHECK(back.onBoard == s.onBoard);
      CHECK(back.game.level == level);
      CHECK(back.game.seed == s.game.seed);
      CHECK(back.game.attempt == s.game.attempt);
      CHECK(back.game.elapsedMs == s.game.elapsedMs);
      CHECK(back.game.hints == s.game.hints);
      CHECK(back.game.checks == s.game.checks);
      CHECK(std::memcmp(&back.game.puzzle, &s.game.puzzle, sizeof(Puzzle)) == 0);
      CHECK(sameBoard(back.game.board, s.game.board));
      // Redo still works after a reload.
      CHECK(redo(back.game.board) >= 0);
    }
  }
}

// Every way a save can be bad opens a clean front door: never a crash, never
// a game laid over the wrong puzzle. The difficulty survives a bad game.
void testBadSavesAreDiscarded() {
  SaveState s;
  s.menuLevel = Level::Medium;
  s.hasGame = true;
  s.game = newGame(Level::Easy, 400);
  char good[kSaveBytes];
  CHECK(writeSave(s, good, kSaveBytes) > 0);
  const std::string text(good);

  auto reject = [](const std::string& bad, const bool keepsLevel) {
    SaveState out;
    out.menuLevel = Level::Hard;
    const bool ok = readSave(bad.c_str(), out, work);
    CHECK(!ok);
    CHECK(!out.hasGame);
    CHECK(out.menuLevel == (keepsLevel ? Level::Medium : Level::Hard));
  };

  reject("", false);
  reject("garbage", false);
  reject("9" + text.substr(1), false);  // another version
  reject(text.substr(0, 40), true);     // truncated mid-board
  reject(text.substr(0, text.size() - 30), true);

  // The hash of a puzzle the generator no longer builds.
  std::string tampered = text;
  {
    int field = 0;
    size_t at = 0;
    while (field < 7) {
      at = tampered.find(' ', at) + 1;
      ++field;
    }
    tampered[at] = tampered[at] == '1' ? '2' : '1';
  }
  reject(tampered, true);

  // A square state that is not one.
  std::string square = text;
  square[square.find('\n') + 1] = '7';
  reject(square, true);

  // No game: the difficulty alone, and that is a good save.
  SaveState bare;
  bare.menuLevel = Level::Hard;
  char buffer[kSaveBytes];
  CHECK(writeSave(bare, buffer, kSaveBytes) > 0);
  SaveState back;
  CHECK(readSave(buffer, back, work));
  CHECK(!back.hasGame);
  CHECK(!back.onBoard);
  CHECK(back.menuLevel == Level::Hard);
}

// A given is track whatever the file says, and history that names a given is
// thrown away rather than allowed to unlock it.
void testASaveCannotUnlockAGiven() {
  SaveState s;
  s.hasGame = true;
  s.game = newGame(Level::Easy, 500);
  int given = -1;
  for (int i = 0; i < kMaxCells && given < 0; ++i) {
    if (s.game.puzzle.given[i]) given = i;
  }
  CHECK(given >= 0);
  s.game.board.cell[given] = kEmpty;
  s.game.board.history.moves[0] = Move{static_cast<uint8_t>(given), kTrack, kEmpty, 0, 0};
  s.game.board.history.count = 1;
  s.game.board.history.cursor = 1;
  char buffer[kSaveBytes];
  CHECK(writeSave(s, buffer, kSaveBytes) > 0);
  SaveState back;
  CHECK(readSave(buffer, back, work));
  CHECK(back.game.board.cell[given] == kTrack);
  CHECK(back.game.board.history.count == 0);
  CHECK(undo(back.game.board) == -1);
}

}  // namespace

int main() {
  testCheckFlagsWrongMarksAndShapes();
  testCheckJudgesAChosenPiece();
  testAnOpenEndIsNotWrong();
  testHintsAreAlwaysCorrectAndFinish();
  testAHintPutsAWrongMarkRightFirst();
  testAHintUsesWhatThePlayerKnows();
  testFlow();
  testAssistedMeansAnyHelp();
  testOrdinals();
  testFlagsStayUntilTheirSquareChanges();
  testTheLastMoveIsThePlayers();
  testSaveRoundTrip();
  testBadSavesAreDiscarded();
  testASaveCannotUnlockAGiven();
  std::printf("traintracks game: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
