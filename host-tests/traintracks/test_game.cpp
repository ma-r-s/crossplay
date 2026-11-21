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
          if (state == kTrack && g.puzzle.solution[i] && !g.puzzle.given[i] && shapes[i] != g.puzzle.solution[i]) {
            bad = true;
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

// CHECK judges what is drawn, whoever chose it. A lone square on the route
// draws a guessed straight: wrong where the answer bends, right where it runs
// straight, and an empty square beside it is never wrong.
void testCheckJudgesAGuessedPiece() {
  const uint8_t path[] = {static_cast<uint8_t>(cellIndex(2, 0)), static_cast<uint8_t>(cellIndex(2, 1)),
                          static_cast<uint8_t>(cellIndex(1, 1)), static_cast<uint8_t>(cellIndex(1, 2)),
                          static_cast<uint8_t>(cellIndex(1, 3)), static_cast<uint8_t>(cellIndex(1, 4)),
                          static_cast<uint8_t>(cellIndex(1, 5))};
  Puzzle p;
  CHECK(buildFromPath(p, 6, path, 7, kWest, kEast));
  Board b;
  resetBoard(b, p);
  uint8_t shapes[kMaxCells];
  uint8_t wrong[kMaxCells];
  const int bend = cellIndex(1, 1);  // the answer turns here
  b.cell[bend] = kTrack;
  inferShapes(b, p, shapes);
  CHECK(shapes[bend] != p.solution[bend]);
  CHECK(checkBoard(p, b, wrong) == 1 && wrong[bend] == 1);
  b.cell[bend] = kEmpty;
  const int run = cellIndex(1, 3);  // and runs straight here
  b.cell[run] = kTrack;
  inferShapes(b, p, shapes);
  CHECK(checkBoard(p, b, wrong) == (shapes[run] == p.solution[run] ? 0 : 1));
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
        CHECK(h.state == kTrack);  // never a cross
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
        // Track a hint lays is the answer's piece, set.
        if (h.state == kTrack) CHECK(g.board.pin[h.cell] == g.puzzle.solution[h.cell]);
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
      if (bad < 0) continue;
      g.board.cell[bad] = kTrack;
      const Hint h = findHint(g.puzzle, g.board, scratch);
      CHECK(h.kind == HintKind::Fixed);
      CHECK(h.cell == bad);
      // Taken away, not crossed: a hint never lays a cross.
      CHECK(h.state == kEmpty);
      CHECK(applyHint(g, h));
      CHECK(g.board.cell[bad] == kEmpty);
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
  CHECK(last >= 0);
  if (last < 0) return;
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
      for (int k = 0; k < 90; ++k) {
        tapCell(s.game.board, s.game.puzzle, rng.below(n), rng.below(n));
        settle(s.game.board, s.game.puzzle);
      }
      undo(s.game.board);
      undo(s.game.board);
      settle(s.game.board, s.game.puzzle);

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
      // The board draws exactly as it did, so a line is not rerouted by a reload.
      uint8_t before[kMaxCells];
      uint8_t after[kMaxCells];
      inferShapes(s.game.board, s.game.puzzle, before);
      inferShapes(back.game.board, back.game.puzzle, after);
      CHECK(std::memcmp(before, after, sizeof(before)) == 0);
      CHECK(std::memcmp(back.game.board.shape, s.game.board.shape, sizeof(before)) == 0);
      // Redo still works after a reload.
      CHECK(redo(back.game.board) >= 0);
    }
  }
}

// A version 2 save, written before shapes were kept, still loads: same marks,
// shapes inferred afresh.
void testAVersion2SaveLoads() {
  SaveState s;
  s.hasGame = true;
  s.game = newGame(Level::Medium, 320);
  Rng rng{320};
  const int n = s.game.puzzle.size;
  for (int k = 0; k < 30; ++k) tapCell(s.game.board, s.game.puzzle, rng.below(n), rng.below(n));
  settle(s.game.board, s.game.puzzle);
  char buffer[kSaveBytes];
  CHECK(writeSave(s, buffer, kSaveBytes) > 0);
  // Back to version 2: the version digit, and each square's third character.
  std::string text(buffer);
  const size_t header = text.find('\n');
  std::string old = "2" + text.substr(1, header);
  for (int k = 0; k < n * n; ++k) old += text.substr(header + 1 + 3 * k, 2);
  old += text.substr(header + 1 + 3 * n * n);
  SaveState back;
  CHECK(readSave(old.c_str(), back, work));
  CHECK(sameBoard(back.game.board, s.game.board));
  CHECK(std::memcmp(back.game.board.pin, s.game.board.pin, sizeof(s.game.board.pin)) == 0);
  for (int i = 0; i < kMaxCells; ++i) CHECK(back.game.board.shape[i] == 0);
}

// The route's squares from the start gate to the end gate, in order.
int routeOrder(const Puzzle& p, int* out) {
  int row = p.start.row;
  int col = p.start.col;
  int from = p.start.side;
  int len = 0;
  while (inside(p, row, col) && len < kMaxCells) {
    const int i = cellIndex(row, col);
    out[len++] = i;
    int to = -1;
    for (int d = 0; d < 4; ++d) {
      if ((p.solution[i] & dirBit(d)) && d != from) to = d;
    }
    if (to < 0 || (row == p.end.row && col == p.end.col && to == p.end.side)) break;
    row += kDRow[to];
    col += kDCol[to];
    from = opposite(to);
  }
  return len;
}

bool joined(const uint8_t* shapes, const int i, const int d) {
  const int j = cellIndex(rowOf(i) + kDRow[d], colOf(i) + kDCol[d]);
  return (shapes[i] & dirBit(d)) && (shapes[j] & dirBit(opposite(d)));
}

// Track laid next to a line already drawn from the start gate never reroutes
// it: every join the line had is still drawn.
void testALineIsNotRerouted() {
  int probes = 0;
  for (const Level level : kLevels3) {
    for (uint32_t seed = 500; seed < 540; ++seed) {
      Game g = newGame(level, seed);
      const Puzzle& p = g.puzzle;
      int route[kMaxCells];
      const int len = routeOrder(p, route);
      bool laid[kMaxCells] = {};
      for (int k = 0; k < len / 2; ++k) {
        setCell(g.board, p, rowOf(route[k]), colOf(route[k]), kTrack);
        settle(g.board, p);
        laid[route[k]] = true;
      }
      uint8_t before[kMaxCells];
      inferShapes(g.board, p, before);
      for (int e = 0; e < kMaxCells; ++e) {
        if (!inside(p, rowOf(e), colOf(e)) || g.board.cell[e] != kEmpty) continue;
        bool besideLine = false;
        for (int d = 0; d < 4; ++d) {
          const int r2 = rowOf(e) + kDRow[d];
          const int c2 = colOf(e) + kDCol[d];
          if (inside(p, r2, c2) && laid[cellIndex(r2, c2)]) besideLine = true;
        }
        if (!besideLine) continue;
        CHECK(setCell(g.board, p, rowOf(e), colOf(e), kTrack));
        uint8_t after[kMaxCells];
        inferShapes(g.board, p, after);
        for (int i = 0; i < kMaxCells; ++i) {
          if (!laid[i]) continue;
          for (const int d : {kEast, kSouth}) {
            if (inside(p, rowOf(i) + kDRow[d], colOf(i) + kDCol[d]) && joined(before, i, d)) {
              CHECK(joined(after, i, d));
            }
          }
        }
        ++probes;
        CHECK(undo(g.board) == e);
      }
    }
  }
  CHECK(probes > 100);
}

// A tap walks a square through all six pieces, those that cannot fit
// included, and then back to empty: here a corner, where only one fits.
void testATapReachesEverySixPieces() {
  for (uint32_t seed = 600; seed < 620; ++seed) {
    Game g = newGame(Level::Easy, seed);
    const Puzzle& p = g.puzzle;
    const int n = p.size;
    int corner = -1;
    for (const int i : {cellIndex(0, 0), cellIndex(0, n - 1), cellIndex(n - 1, 0), cellIndex(n - 1, n - 1)}) {
      if (corner < 0 && !p.given[i] && gateSide(p, rowOf(i), colOf(i)) < 0) corner = i;
    }
    if (corner < 0) continue;
    const int r = rowOf(corner);
    const int c = colOf(corner);
    CHECK(tapCell(g.board, p, r, c));
    uint8_t shapes[kMaxCells];
    inferShapes(g.board, p, shapes);
    uint8_t seen = 0;  // one bit per piece in kPieces
    auto mark = [&](const uint8_t piece) {
      for (int k = 0; k < 6; ++k) {
        if (kPieces[k] == piece) {
          CHECK(!(seen & (1u << k)));
          seen = static_cast<uint8_t>(seen | (1u << k));
        }
      }
    };
    mark(shapes[corner]);
    for (int k = 0; k < 5; ++k) {
      CHECK(tapCell(g.board, p, r, c));
      CHECK(g.board.cell[corner] == kTrack);
      mark(g.board.pin[corner]);
    }
    CHECK(seen == 0x3F);
    CHECK(tapCell(g.board, p, r, c));
    CHECK(g.board.cell[corner] == kEmpty);
  }
}

// The route drawn from gate to gate is a solve, whatever stray track lies
// elsewhere; the solved board then shows the route alone. A line one square
// short is not a solve, stray track or none.
void testStrayTrackDoesNotBlockTheWin() {
  for (const Level level : kLevels3) {
    for (uint32_t seed = 700; seed < 720; ++seed) {
      Game g = newGame(level, seed);
      const Puzzle& p = g.puzzle;
      int stray = -1;
      int lastFree = -1;
      for (int i = 0; i < kMaxCells; ++i) {
        if (!inside(p, rowOf(i), colOf(i))) continue;
        if (!p.solution[i] && stray < 0) stray = i;
        if (p.solution[i] && !p.given[i]) lastFree = i;
      }
      CHECK(stray >= 0 && lastFree >= 0);
      Game shortOne = g;
      for (int i = 0; i < kMaxCells; ++i) {
        if (!p.solution[i] || p.given[i]) continue;
        setCell(g.board, p, rowOf(i), colOf(i), kTrack, p.solution[i]);
        if (i != lastFree) setCell(shortOne.board, p, rowOf(i), colOf(i), kTrack, p.solution[i]);
      }
      setCell(g.board, p, rowOf(stray), colOf(stray), kTrack);
      setCell(shortOne.board, p, rowOf(stray), colOf(stray), kTrack);
      CHECK(!isWin(p, g.board, checker));  // the clues alone are over
      CHECK(isSolved(p, g.board, checker));
      CHECK(!isSolved(p, shortOne.board, checker));
      tidySolved(g.board, p);
      for (int i = 0; i < kMaxCells; ++i) {
        if (!inside(p, rowOf(i), colOf(i))) continue;
        CHECK((g.board.cell[i] == kTrack) == (p.solution[i] != 0));
        CHECK(g.board.shape[i] == p.solution[i]);
      }
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

  // A negative number in the header.
  {
    std::string negative = text;
    const size_t at = negative.find(' ', 2) + 1;  // the third field, hasGame
    negative.insert(at, "-");
    reject(negative, false);
  }

  // A piece that is not one of the six, and a piece on a crossed square.
  {
    std::string bad = text;
    const size_t cells = bad.find('\n') + 1;
    bad[cells] = '1';
    bad[cells + 1] = 'F';
    reject(bad, true);
    bad[cells] = '2';
    bad[cells + 1] = 'A';  // east-west, but on a cross
    reject(bad, true);
  }

  // The same in the history: a move whose piece is not a piece.
  {
    std::string bad = text;
    const size_t history = bad.find('\n', bad.find('\n') + 1) + 1;
    bad[history + 5] = 'F';  // toPin of the first move
    reject(bad, true);
  }

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
  testCheckJudgesAGuessedPiece();
  testHintsAreAlwaysCorrectAndFinish();
  testAHintPutsAWrongMarkRightFirst();
  testAHintUsesWhatThePlayerKnows();
  testFlow();
  testAssistedMeansAnyHelp();
  testOrdinals();
  testTheLastMoveIsThePlayers();
  testSaveRoundTrip();
  testAVersion2SaveLoads();
  testALineIsNotRerouted();
  testATapReachesEverySixPieces();
  testStrayTrackDoesNotBlockTheWin();
  testBadSavesAreDiscarded();
  testASaveCannotUnlockAGiven();
  std::printf("traintracks game: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
