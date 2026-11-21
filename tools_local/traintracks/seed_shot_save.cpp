// Writes the Train Tracks save the site card's shot is taken from: a Medium
// puzzle part way through, as a player leaves it. Built from the game's own
// headers, so the save always matches the firmware it is shot with.
//
//   c++ -std=c++17 -I src/apps_local/traintracks \
//       tools_local/traintracks/seed_shot_save.cpp -o seed && ./seed <card>/.crosspoint/traintracks.sav
//
// The state: the route laid from the start gate, square by square as a player
// would, with automatic pieces; a few crosses where track cannot go; the rest
// of the board open. onBoard is set, so the app opens straight onto it.
// With a second argument "almost", the whole route but one square is laid and
// that square is printed, so a shot can tap it and photograph the solve.

#include <cstdio>
#include <string>

#include "TrainTracksGame.h"
#include "TrainTracksGenerator.h"
#include "TrainTracksSave.h"

using namespace traintracks;

static GenWork work;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: seed_shot_save <path to traintracks.sav>\n");
    return 2;
  }
  SaveState s;
  s.menuLevel = Level::Medium;
  s.hasGame = true;
  s.onBoard = true;
  Game& g = s.game;
  Generated made;
  if (!generate(Level::Medium, 1121, 2000, made, work)) return 1;
  g.level = Level::Medium;
  g.seed = made.seed;
  g.attempt = made.attempt;
  g.puzzle = made.puzzle;
  g.elapsedMs = (4 * 60 + 12) * 1000;
  resetBoard(g.board, g.puzzle);
  const Puzzle& p = g.puzzle;

  // The route in order from the start gate; the first 60% of it is laid.
  int route[kMaxCells];
  int len = 0;
  int row = p.start.row, col = p.start.col, from = p.start.side;
  while (len < kMaxCells) {
    const int i = cellIndex(row, col);
    route[len++] = i;
    int to = -1;
    for (int d = 0; d < 4; ++d) {
      if ((p.solution[i] & dirBit(d)) && d != from) to = d;
    }
    if (row == p.end.row && col == p.end.col) break;
    row += kDRow[to];
    col += kDCol[to];
    from = opposite(to);
  }
  // "almost": every route square but one, for shooting the solved board after
  // one tap; prints that square as "row col".
  const bool almost = argc > 2 && std::string(argv[2]) == "almost";
  int left = -1;
  if (almost) {
    for (int k = len - 2; k > 0 && left < 0; --k) {
      if (!p.given[route[k]]) left = route[k];
    }
  }
  const int laid = almost ? len : len * 6 / 10;
  for (int k = 0; k < laid; ++k) {
    if (route[k] == left) continue;
    setCell(g.board, p, rowOf(route[k]), colOf(route[k]), kTrack);
    settle(g.board, p);
  }
  // Crosses: the first few squares off the route beside the laid line.
  int crosses = 0;
  for (int k = 0; k < laid && crosses < 5; ++k) {
    for (int d = 0; d < 4 && crosses < 5; ++d) {
      const int r = rowOf(route[k]) + kDRow[d];
      const int c = colOf(route[k]) + kDCol[d];
      if (!inside(p, r, c)) continue;
      const int j = cellIndex(r, c);
      if (p.solution[j] || g.board.cell[j] != kEmpty) continue;
      setCell(g.board, p, r, c, kCross);
      ++crosses;
    }
  }
  settle(g.board, p);

  char text[kSaveBytes];
  if (writeSave(s, text, kSaveBytes) <= 0) return 1;
  FILE* f = std::fopen(argv[1], "w");
  if (!f) return 1;
  std::fputs(text, f);
  std::fclose(f);
  if (almost) {
    std::printf("%d %d\n", rowOf(left), colOf(left));
  } else {
    std::printf("laid %d of %d route squares, %d crosses\n", laid, len, crosses);
  }
  return 0;
}
