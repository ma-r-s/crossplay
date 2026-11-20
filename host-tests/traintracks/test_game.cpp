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

// CHECK flags exactly the marks that contradict the answer, and an empty
// square never.
void testCheckFlagsExactlyTheWrongMarks() {
  for (const Level level : kLevels3) {
    for (uint32_t seed = 1; seed <= 40; ++seed) {
      Game g = newGame(level, seed);
      Rng rng{seed * 31u};
      const int n = g.puzzle.size;
      int expected = 0;
      for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
          const int i = cellIndex(r, c);
          if (g.puzzle.given[i]) continue;
          const uint8_t state = static_cast<uint8_t>(rng.below(3));
          g.board.cell[i] = state;
          if ((state == kTrack && !g.puzzle.solution[i]) || (state == kCross && g.puzzle.solution[i])) ++expected;
        }
      }
      uint8_t wrong[kMaxCells];
      CHECK(checkBoard(g.puzzle, g.board, wrong) == expected);
      for (int i = 0; i < kMaxCells; ++i) {
        if (g.board.cell[i] == kEmpty || !inside(g.puzzle, rowOf(i), colOf(i))) CHECK(wrong[i] == 0);
      }
    }
  }
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
      for (; taken <= n * n && !won; ++taken) {
        const Hint h = findHint(g.puzzle, g.board, scratch);
        CHECK(h.cell >= 0);
        if (h.cell < 0) break;
        CHECK(!g.puzzle.given[h.cell]);
        CHECK(h.kind != HintKind::Fixed);
        CHECK((h.state == kTrack) == (g.puzzle.solution[h.cell] != 0));
        CHECK(h.tier >= 1 && h.tier <= spec(level).tier);
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
      if (g.puzzle.solution[i]) last = i;
    }
  }
  g.board.cell[last] = kEmpty;
  const Hint h = findHint(g.puzzle, g.board, scratch);
  CHECK(h.cell == last);
  CHECK(h.state == kTrack);
  CHECK(h.tier == 1);
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
  s.game.board.history.moves[0] = Move{static_cast<uint8_t>(given), kTrack, kEmpty};
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
  testCheckFlagsExactlyTheWrongMarks();
  testHintsAreAlwaysCorrectAndFinish();
  testAHintPutsAWrongMarkRightFirst();
  testAHintUsesWhatThePlayerKnows();
  testFlow();
  testAssistedMeansAnyHelp();
  testSaveRoundTrip();
  testBadSavesAreDiscarded();
  testASaveCannotUnlockAGiven();
  std::printf("traintracks game: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
