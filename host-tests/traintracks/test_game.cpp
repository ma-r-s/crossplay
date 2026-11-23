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
// Where hintSolves tries a hint: a copy of the board, as the activity keeps one.
Board probe;

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
            g.board.pin[i] = kPieces[rng.below(6)];
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

// A cross on the route is the first thing a hint puts right, as track. Track
// laid off the route is never touched: a hint reveals track, it does not take
// any away, and it reasons past the wrong track rather than from it.
void testAHintOnlyEverLaysTrack() {
  for (const Level level : kLevels3) {
    for (uint32_t seed = 200; seed < 220; ++seed) {
      Game g = newGame(level, seed);
      const Puzzle& p = g.puzzle;
      int off = -1;
      int on = -1;
      for (int i = 0; i < kMaxCells; ++i) {
        if (!inside(p, rowOf(i), colOf(i)) || p.given[i]) continue;
        if (!p.solution[i] && off < 0) off = i;
        if (p.solution[i] && on < 0) on = i;
      }
      CHECK(off >= 0 && on >= 0);
      if (off < 0 || on < 0) continue;
      // Wrong track alone: left where it is; the hint is track elsewhere.
      setCell(g.board, p, rowOf(off), colOf(off), kTrack);
      Hint h = findHint(p, g.board, scratch);
      CHECK(h.cell != off && h.state == kTrack && p.solution[h.cell] != 0);
      // A cross on the route as well: that comes first, and becomes track.
      setCell(g.board, p, rowOf(on), colOf(on), kCross);
      h = findHint(p, g.board, scratch);
      CHECK(h.kind == HintKind::Fixed && h.cell == on && h.state == kTrack);
      CHECK(applyHint(g, h));
      CHECK(g.board.cell[on] == kTrack && g.board.pin[on] == p.solution[on]);
      // An ordinary move: undo takes it back.
      CHECK(undo(g.board) == on);
      CHECK(g.board.cell[on] == kCross);
    }
  }
}

// From a board of random marks, right and wrong, hints taken one after
// another only ever add track or turn it to the answer's piece, never take
// track away or lay a cross, and carry the line through to a solve, with the
// wrong track still lying where the player left it until the solve tidies it.
void testHintsFinishAroundWrongTrack() {
  int games = 0;
  for (const Level level : kLevels3) {
    for (uint32_t seed = 1100; seed < 1140; ++seed) {
      Game g = newGame(level, seed);
      const Puzzle& p = g.puzzle;
      Rng rng{seed};
      const int n = p.size;
      for (int k = 0; k < n * 2; ++k) {
        const int r = rng.below(n);
        const int c = rng.below(n);
        if (rng.below(3) == 0) {
          holdCell(g.board, p, r, c);
        } else {
          tapCell(g.board, p, r, c);
        }
        settle(g.board, p);
      }
      bool done = isSolved(p, g.board, checker);
      for (int taken = 0; taken < 2 * n * n && !done; ++taken) {
        const Hint h = findHint(p, g.board, scratch);
        CHECK(h.cell >= 0 && h.state == kTrack && p.solution[h.cell] != 0);
        if (h.cell < 0) break;
        if (hintSolves(g, h, probe, checker)) {
          done = true;  // the last move is the player's
          break;
        }
        CHECK(applyHint(g, h));
        settle(g.board, p);
        CHECK(g.board.cell[h.cell] == kTrack);
        CHECK(g.board.pin[h.cell] == p.solution[h.cell]);
        done = isSolved(p, g.board, checker);
      }
      CHECK(done);
      ++games;
    }
  }
  CHECK(games == 120);
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
  CHECK(hintSolves(g, h, probe, scratch));
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
  CHECK(!hintSolves(g, early, probe, scratch));
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
      checkBoard(s.game.puzzle, s.game.board, s.game.marked);

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
      CHECK(std::memcmp(back.game.marked, s.game.marked, sizeof(s.game.marked)) == 0);
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

// Growing a line out from a station, square by square in random directions,
// its open end points into a row and column that still have room for track
// whenever some empty square that does is open to it, rather than running
// straight on into a line already full.
void testAnOpenEndLooksForRoom() {
  int ends = 0;
  for (const Level level : kLevels3) {
    for (uint32_t seed = 950; seed < 1110; ++seed) {
      Game g = newGame(level, seed);
      const Puzzle& p = g.puzzle;
      Rng rng{seed};
      int at = cellIndex(p.start.row, p.start.col);
      int prev = -1;
      for (int step = 0; step < p.size * 2; ++step) {
        if (!p.given[at] && g.board.cell[at] != kTrack) {
          setCell(g.board, p, rowOf(at), colOf(at), kTrack);
          settle(g.board, p);
        }
        uint8_t shapes[kMaxCells];
        inferShapes(g.board, p, shapes);
        auto roomAt = [&](const int d, bool& empty) {
          const int r2 = rowOf(at) + kDRow[d];
          const int c2 = colOf(at) + kDCol[d];
          empty = inside(p, r2, c2) && g.board.cell[cellIndex(r2, c2)] == kEmpty;
          return empty && rowCount(g.board, p, r2) < p.rowClue[r2] && colCount(g.board, p, c2) < p.colClue[c2];
        };
        // The newest square's open end: not back the way the line came, and
        // only judged when the square is automatic and its other end is a real
        // join (the previous square, or the station).
        if (!p.given[at] && g.board.pin[at] == 0) {
          int back = -1;
          for (int d = 0; d < 4; ++d) {
            const int r2 = rowOf(at) + kDRow[d];
            const int c2 = colOf(at) + kDCol[d];
            if (prev < 0 ? d == p.start.side : (inside(p, r2, c2) && cellIndex(r2, c2) == prev)) back = d;
          }
          if (back >= 0 && (shapes[at] & dirBit(back))) {
            for (int d = 0; d < 4; ++d) {
              if (d == back || !(shapes[at] & dirBit(d))) continue;
              bool empty = false;
              if (roomAt(d, empty) || !empty) continue;
              ++ends;
              for (int e = 0; e < 4; ++e) {
                if (shapes[at] & dirBit(e)) continue;
                bool alt = false;
                CHECK(!roomAt(e, alt));
              }
            }
          }
        }
        // Step on to a random empty neighbour.
        int options[4];
        int count = 0;
        for (int d = 0; d < 4; ++d) {
          const int r2 = rowOf(at) + kDRow[d];
          const int c2 = colOf(at) + kDCol[d];
          if (inside(p, r2, c2) && g.board.cell[cellIndex(r2, c2)] == kEmpty) options[count++] = d;
        }
        if (count == 0) break;
        const int d = options[rng.below(count)];
        prev = at;
        at = cellIndex(rowOf(at) + kDRow[d], colOf(at) + kDCol[d]);
      }
    }
  }
  CHECK(ends > 50);
}

// With the board settling after every tap, as it does in play, a cycle still
// steps through the six pieces in their fixed order and comes back round to
// undecided: the neighbours fitting around a chosen piece never change it.
void testACycleSurvivesSettling() {
  int cycles = 0;
  for (const Level level : kLevels3) {
    for (uint32_t seed = 800; seed < 830; ++seed) {
      Game g = newGame(level, seed);
      const Puzzle& p = g.puzzle;
      int route[kMaxCells];
      const int len = routeOrder(p, route);
      for (int k = 0; k < len; k += 3) {
        setCell(g.board, p, rowOf(route[k]), colOf(route[k]), kTrack);
        settle(g.board, p);
      }
      for (int i = 0; i < kMaxCells; ++i) {
        if (!inside(p, rowOf(i), colOf(i)) || p.given[i]) continue;
        const Board before = g.board;
        const int r = rowOf(i);
        const int c = colOf(i);
        if (g.board.cell[i] != kTrack) {
          tapCell(g.board, p, r, c);
          settle(g.board, p);
        }
        if (g.board.pin[i] != 0) {
          g.board = before;
          continue;
        }
        CHECK(tapCell(g.board, p, r, c));
        settle(g.board, p);
        CHECK(g.board.pin[i] == kPinUnknown);
        for (int k = 0; k < 6; ++k) {
          CHECK(tapCell(g.board, p, r, c));
          settle(g.board, p);
          CHECK(g.board.cell[i] == kTrack && g.board.pin[i] == kPieces[k]);
        }
        CHECK(tapCell(g.board, p, r, c));
        CHECK(g.board.cell[i] == kTrack && g.board.pin[i] == kPinUnknown);
        ++cycles;
        g.board = before;
      }
    }
  }
  CHECK(cycles > 1000);
}

// Turning a square beside a line through its pieces and back to empty leaves
// the line drawn as it was: a chosen piece pointing into the line never pulls
// it apart, so there is nothing to put back.
void testCyclingBesideALineLeavesItWhole() {
  int probes = 0;
  for (const Level level : kLevels3) {
    for (uint32_t seed = 900; seed < 920; ++seed) {
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
        bool beside = false;
        for (int d = 0; d < 4; ++d) {
          const int r2 = rowOf(e) + kDRow[d];
          const int c2 = colOf(e) + kDCol[d];
          if (inside(p, r2, c2) && laid[cellIndex(r2, c2)]) beside = true;
        }
        if (!beside) continue;
        const Board saved = g.board;
        for (int t = 0; t < 10 && !(t > 1 && g.board.pin[e] == kPinUnknown); ++t) {
          tapCell(g.board, p, rowOf(e), colOf(e));
          settle(g.board, p);
          uint8_t now[kMaxCells];
          inferShapes(g.board, p, now);
          for (int i = 0; i < kMaxCells; ++i) {
            if (!laid[i]) continue;
            for (const int d : {kEast, kSouth}) {
              if (inside(p, rowOf(i) + kDRow[d], colOf(i) + kDCol[d]) && joined(before, i, d)) {
                CHECK(joined(now, i, d));
              }
            }
          }
        }
        CHECK(g.board.cell[e] == kTrack && g.board.pin[e] == kPinUnknown);
        ++probes;
        g.board = saved;
      }
    }
  }
  CHECK(probes > 100);
}

// A tap walks a square through all six pieces in the same order everywhere,
// those that cannot fit included, and then back round to undecided, never to
// empty: here a corner, where only one fits, and from a cross.
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
    CHECK(g.board.cell[corner] == kTrack && g.board.pin[corner] == 0);
    for (int round = 0; round < 2; ++round) {
      CHECK(tapCell(g.board, p, r, c));
      CHECK(g.board.cell[corner] == kTrack && g.board.pin[corner] == kPinUnknown);
      for (int k = 0; k < 6; ++k) {
        CHECK(tapCell(g.board, p, r, c));
        CHECK(g.board.cell[corner] == kTrack && g.board.pin[corner] == kPieces[k]);
      }
      // The second round starts from the question mark the first ends on.
      if (round == 0) {
        CHECK(holdCell(g.board, p, r, c) && g.board.cell[corner] == kEmpty);
        CHECK(holdCell(g.board, p, r, c) && g.board.cell[corner] == kCross);
      }
    }
    CHECK(tapCell(g.board, p, r, c));
    CHECK(g.board.cell[corner] == kTrack && g.board.pin[corner] == kPinUnknown);
  }
}

// A cycle is the same whatever happens between taps: a neighbour turned into
// track, another square's cycle, an undo, or a piece set from outside, as
// HINT sets one. The next tap always gives the piece after the one on show.
void testAnInterruptedCycleReachesEveryPieceOnce() {
  int cycles = 0;
  for (uint32_t seed = 100; seed < 160; ++seed) {
    const Game base = newGame(Level::Easy, seed);
    const Puzzle& p = base.puzzle;
    for (int i = 0; i < kMaxCells; ++i) {
      const int r = rowOf(i);
      const int c = colOf(i);
      if (!inside(p, r, c) || p.given[i]) continue;
      Game g = base;
      int neighbour = -1;
      int other = -1;
      for (int d = 0; d < 4 && neighbour < 0; ++d) {
        const int r2 = r + kDRow[d];
        const int c2 = c + kDCol[d];
        if (inside(p, r2, c2) && !p.given[cellIndex(r2, c2)]) neighbour = cellIndex(r2, c2);
      }
      for (int j = kMaxCells - 1; j >= 0 && other < 0; --j) {
        if (inside(p, rowOf(j), colOf(j)) && !p.given[j] && j != i && j != neighbour) other = j;
      }
      if (neighbour < 0 || other < 0) continue;
      CHECK(tapCell(g.board, p, r, c));
      CHECK(tapCell(g.board, p, r, c));
      settle(g.board, p);
      CHECK(g.board.pin[i] == kPinUnknown);
      for (int k = 0; k < 6; ++k) {
        if (k % 3 == 0) {
          tapCell(g.board, p, rowOf(neighbour), colOf(neighbour));
        } else if (k % 3 == 1) {
          tapCell(g.board, p, rowOf(other), colOf(other));
          tapCell(g.board, p, rowOf(other), colOf(other));
        } else {
          // Tap on, then undo back.
          tapCell(g.board, p, r, c);
          undo(g.board);
        }
        settle(g.board, p);
        CHECK(tapCell(g.board, p, r, c));
        settle(g.board, p);
        CHECK(g.board.cell[i] == kTrack && g.board.pin[i] == kPieces[k]);
      }
      CHECK(tapCell(g.board, p, r, c));
      CHECK(g.board.pin[i] == kPinUnknown);

      // A piece set from outside: the tap gives the one after it.
      for (int k = 0; k < 6; ++k) {
        setCell(g.board, p, r, c, kTrack, kPieces[k]);  // no change when already shown
        settle(g.board, p);
        CHECK(tapCell(g.board, p, r, c));
        CHECK(g.board.pin[i] == (k < 5 ? kPieces[k + 1] : kPinUnknown));
      }
      ++cycles;
    }
  }
  CHECK(cycles > 500);
}

// A piece left undecided is still track: it counts toward its line and the
// win, and the neighbours draw just as they would beside an automatic piece.
// CHECK calls it wrong on the route (its piece is not set), HINT sets it, and
// a save and undo both keep it.

void testAnUndecidedPieceIsTrack() {
  int tried = 0;
  for (uint32_t seed = 500; seed < 540; ++seed) {
    Game g = newGame(Level::Medium, seed);
    const Puzzle& p = g.puzzle;
    int target = -1;
    for (int i = 0; i < kMaxCells && target < 0; ++i) {
      if (inside(p, rowOf(i), colOf(i)) && p.solution[i] && !p.given[i]) target = i;
    }
    if (target < 0) continue;
    ++tried;
    const int r = rowOf(target);
    const int c = colOf(target);
    // Lay the route except the target, which is automatic track.
    for (int i = 0; i < kMaxCells; ++i) {
      if (p.solution[i] && !p.given[i] && i != target) setCell(g.board, p, rowOf(i), colOf(i), kTrack);
    }
    CHECK(tapCell(g.board, p, r, c));
    uint8_t automatic[kMaxCells];
    inferShapes(g.board, p, automatic);
    // Tap through to the undecided piece.
    int taps = 0;
    while (g.board.pin[target] != kPinUnknown && taps < 8) {
      CHECK(tapCell(g.board, p, r, c));
      ++taps;
    }
    CHECK(g.board.cell[target] == kTrack && g.board.pin[target] == kPinUnknown);
    CHECK(taps == 1);  // the tap after the automatic piece
    CHECK(rowCount(g.board, p, r) == p.rowClue[r] && colCount(g.board, p, c) == p.colClue[c]);
    uint8_t shapes[kMaxCells];
    inferShapes(g.board, p, shapes);
    for (int i = 0; i < kMaxCells; ++i) {
      if (i != target) CHECK(shapes[i] == automatic[i]);
    }
    // The squares are all right, but the puzzle is not solved until the piece
    // is set: finishing is laying the route, pieces and all.
    CHECK(isWin(p, g.board, checker));
    CHECK(!isSolved(p, g.board, checker));
    uint8_t wrong[kMaxCells];
    CHECK(checkBoard(p, g.board, wrong) == 1 && wrong[target] == 1);
    const Hint h = findHint(p, g.board, scratch);
    CHECK(h.kind == HintKind::Fixed && h.cell == target && h.pin == p.solution[target]);
    // Setting it is the winning move, so HINT will not make it.
    CHECK(hintSolves(g, h, probe, scratch));
    {
      Board set = g.board;
      CHECK(setCell(set, p, r, c, kTrack, p.solution[target]));
      CHECK(isSolved(p, set, checker));
      // Any piece will do for the win, as everywhere else: what was held back
      // was only the question mark.
      for (const uint8_t piece : kPieces) {
        Board other = g.board;
        setCell(other, p, r, c, kTrack, piece);
        CHECK(isSolved(p, other, checker));
      }
    }
    // An undecided piece off the route is stray track and does not hold the
    // win back.
    {
      Board stray = g.board;
      for (int i = 0; i < kMaxCells; ++i) {
        if (p.solution[i] && !p.given[i]) setCell(stray, p, rowOf(i), colOf(i), kTrack, p.solution[i]);
      }
      int off = -1;
      for (int i = 0; i < kMaxCells && off < 0; ++i) {
        if (inside(p, rowOf(i), colOf(i)) && !p.solution[i]) off = i;
      }
      CHECK(off >= 0 && setCell(stray, p, rowOf(off), colOf(off), kTrack, kPinUnknown));
      CHECK(isSolved(p, stray, checker));
    }

    SaveState s;
    s.hasGame = true;
    s.game = g;
    char buffer[kSaveBytes];
    CHECK(writeSave(s, buffer, kSaveBytes) > 0);
    SaveState back;
    CHECK(readSave(buffer, back, work));
    CHECK(back.game.board.pin[target] == kPinUnknown);

    // The next tap chooses a piece; undo brings the undecided piece back.
    CHECK(tapCell(g.board, p, r, c) && g.board.cell[target] == kTrack && g.board.pin[target] != kPinUnknown &&
          g.board.pin[target] != 0);
    CHECK(undo(g.board) == target && g.board.pin[target] == kPinUnknown);
  }
  CHECK(tried > 30);
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

  // A header field wider than what it is read into: seed (field 5) and the
  // clock (field 11) are 32 bits, and 2^32 + 7 must not come back as 7.
  auto withField = [&](const int index, const std::string& value) {
    std::string out = text;
    size_t at = 0;
    for (int field = 0; field < index; ++field) at = out.find(' ', at) + 1;
    const size_t end = out.find_first_of(" \n", at);
    out.replace(at, end - at, value);
    return out;
  };
  reject(withField(5, "4294967303"), true);
  reject(withField(11, "4294967301"), true);
  // SOLVED over a board that is not: field 8 is the solved flag.
  reject(withField(8, "1"), true);

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
    bad[cells + 1] = 'E';  // three connections
    reject(bad, true);
    bad[cells] = '2';
    bad[cells + 1] = 'F';  // undecided, but on a cross
    reject(bad, true);
    bad[cells] = '2';
    bad[cells + 1] = 'A';  // east-west, but on a cross
    reject(bad, true);
  }

  // The same in the history: a move whose piece is not a piece.
  {
    std::string bad = text;
    const size_t history = bad.find('\n', bad.find('\n') + 1) + 1;
    bad[history + 5] = 'E';  // toPin of the first move: three connections
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

// What a save carries beyond the squares has to hold together too: CHECK's
// flags only on real, non-given squares; a shape that is a piece; a history
// that replays against the squares saved with it, or none at all. A version 5
// save, from before the flags were kept, still opens its game.
void testASaveHoldsTogether() {
  SaveState s;
  s.hasGame = true;
  s.game = newGame(Level::Easy, 410);
  const Puzzle& p = s.game.puzzle;
  const int n = p.size;
  int last = -1;
  for (int i = 0; i < kMaxCells && last < 0; ++i) {
    if (inside(p, rowOf(i), colOf(i)) && !p.given[i] && !p.solution[i]) last = i;
  }
  CHECK(last >= 0);
  CHECK(tapCell(s.game.board, p, rowOf(last), colOf(last)));  // stray track
  settle(s.game.board, p);
  CHECK(checkBoard(p, s.game.board, s.game.marked) > 0);
  CHECK(s.game.marked[last]);
  char good[kSaveBytes];
  CHECK(writeSave(s, good, kSaveBytes) > 0);
  const std::string text(good);
  const size_t squares = text.find('\n') + 1;
  const size_t history = text.find('\n', squares) + 1;
  const size_t flags = text.find('\n', history) + 1;
  const int at = 3 * (rowOf(last) * n + colOf(last));  // the square's three characters

  {
    SaveState back;
    CHECK(readSave(text.c_str(), back, work));
    CHECK(back.game.marked[last]);
    CHECK(back.game.board.history.count == 1);
  }
  // Version 5: no flags line. The game opens, unflagged.
  {
    std::string old = "5" + text.substr(1, flags - 1);
    SaveState back;
    CHECK(readSave(old.c_str(), back, work));
    CHECK(back.hasGame);
    CHECK(sameBoard(back.game.board, s.game.board));
    for (const uint8_t flag : back.game.marked) CHECK(flag == 0);
  }
  // A square the history does not lead to: the move laid track there, the
  // file says empty. The game opens, without its history.
  {
    std::string bad = text;
    bad[squares + at] = '0';
    bad[squares + at + 1] = '0';
    bad[squares + at + 2] = '0';
    SaveState back;
    CHECK(readSave(bad.c_str(), back, work));
    CHECK(back.game.board.cell[last] == kEmpty);
    CHECK(back.game.board.history.count == 0);
    CHECK(undo(back.game.board) < 0);
  }
  // A shape that is not a piece.
  {
    std::string bad = text;
    bad[squares + at + 2] = '7';
    SaveState back;
    CHECK(!readSave(bad.c_str(), back, work));
  }
  // A flag past the last square, and a flag on a given.
  {
    std::string bad = text;
    const int digits = (n * n + 3) / 4;
    if ((n * n) % 4 != 0) {
      bad[flags + digits - 1] = 'F';
      SaveState back;
      CHECK(!readSave(bad.c_str(), back, work));
    }
    int given = -1;
    for (int k = 0; k < n * n && given < 0; ++k) {
      if (p.given[cellIndex(k / n, k % n)]) given = k;
    }
    CHECK(given >= 0);
    bad = text;
    bad[flags + given / 4] = hexDigit(hexValue(bad[flags + given / 4]) | (8 >> (given % 4)));
    SaveState back;
    CHECK(!readSave(bad.c_str(), back, work));
  }
  // A clock far past 32 bits.
  {
    std::string bad = text;
    size_t pos = 0;
    for (int field = 0; field < 11; ++field) pos = bad.find(' ', pos) + 1;
    bad.replace(pos, bad.find(' ', pos) - pos, "99999999999");
    SaveState back;
    CHECK(!readSave(bad.c_str(), back, work));
  }
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
  testAHintOnlyEverLaysTrack();
  testHintsFinishAroundWrongTrack();
  testAHintUsesWhatThePlayerKnows();
  testFlow();
  testAssistedMeansAnyHelp();
  testOrdinals();
  testTheLastMoveIsThePlayers();
  testSaveRoundTrip();
  testALineIsNotRerouted();
  testATapReachesEverySixPieces();
  testAnInterruptedCycleReachesEveryPieceOnce();
  testACycleSurvivesSettling();
  testAnOpenEndLooksForRoom();
  testCyclingBesideALineLeavesItWhole();
  testStrayTrackDoesNotBlockTheWin();
  testAnUndecidedPieceIsTrack();
  testBadSavesAreDiscarded();
  testASaveHoldsTogether();
  testASaveCannotUnlockAGiven();
  std::printf("traintracks game: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
