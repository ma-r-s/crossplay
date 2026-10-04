#include "GoCore.h"

namespace go {
namespace {

// A scratch stack for the flood fills. The whole board is the ceiling for every
// one of them, so nothing here allocates and nothing recurses: a recursive fill
// on a snake filling a nineteen by nineteen board is 361 frames deep on a
// device whose task stacks are measured in kilobytes.
struct Fill {
  Point stack[kMaxPoints];
  int top = 0;
  void push(const int point) { stack[top++] = static_cast<Point>(point); }
  bool empty() const { return top == 0; }
  int pop() { return stack[--top]; }
};

// Lift every stone of `colour` whose group has no liberty, having just placed a
// stone at `at`. Only the four groups touching `at` can have died, which is the
// whole reason a capture is cheap.
//
// Returns how many stones were taken.
int captureAround(Game& game, const int at, const uint8_t colour) {
  Point around[4];
  const int count = neighbours(game.size, at, around);
  int taken = 0;
  for (int i = 0; i < count; ++i) {
    const int point = around[i];
    if (game.at(point) != colour) continue;
    uint8_t stones[kMaskBytes];
    int size = 0;
    int liberties = 0;
    group(game, point, stones, size, liberties);
    if (liberties != 0) continue;
    const int points = game.points();
    for (int p = 0; p < points; ++p) {
      if (marked(stones, p)) game.put(p, kEmpty);
    }
    taken += size;
  }
  return taken;
}

// Whether the position `game` now holds has been held before, within the window
// the ring remembers.
bool repeatsPosition(const Game& game) {
  const uint32_t key = positionKey(game);
  for (int i = 0; i < game.recentCount; ++i) {
    if (game.recent[i] == key) return true;
  }
  return false;
}

void rememberPosition(Game& game) {
  const uint32_t key = positionKey(game);
  if (game.recentCount < kHistory) {
    game.recent[game.recentCount++] = key;
    return;
  }
  for (int i = 1; i < kHistory; ++i) game.recent[i - 1] = game.recent[i];
  game.recent[kHistory - 1] = key;
}

}  // namespace

int neighbours(const int size, const int point, Point out[4]) {
  const int row = rowOf(size, point);
  const int col = colOf(size, point);
  int count = 0;
  if (row > 0) out[count++] = static_cast<Point>(point - size);
  if (row < size - 1) out[count++] = static_cast<Point>(point + size);
  if (col > 0) out[count++] = static_cast<Point>(point - 1);
  if (col < size - 1) out[count++] = static_cast<Point>(point + 1);
  return count;
}

int handicapPoints(const int size, const int handicap, Point out[kMaxHandicap]) {
  if (handicap < 2) return 0;
  const int wanted = handicap > kMaxHandicap ? kMaxHandicap : handicap;
  // The corner star points, then the centre. Two OPPOSITE corners first, then
  // the other two: that is the order every ruleset sets a handicap in, and the
  // first two being opposite is what keeps a two-stone game balanced across the
  // board rather than heavy on one side.
  //
  // Nine takes its stars at the 3-3 points, thirteen and nineteen at the 4-4s,
  // which is where each board's stars are drawn.
  const int near = starLine(size);
  const int far = size - 1 - near;
  const int middle = size / 2;
  const Point order[kMaxHandicap] = {
      static_cast<Point>(pointAt(size, far, near)),      static_cast<Point>(pointAt(size, near, far)),
      static_cast<Point>(pointAt(size, near, near)),     static_cast<Point>(pointAt(size, far, far)),
      static_cast<Point>(pointAt(size, middle, middle)),
  };
  for (int i = 0; i < wanted; ++i) out[i] = order[i];
  return wanted;
}

void reset(Game& game, const int size, const int handicap, const int16_t komiHalves) {
  game.size = static_cast<uint8_t>(isBoardSize(size) ? size : kSmallSize);
  // The whole array, not just this board's points: a nineteen by nineteen game
  // followed by a nine by nine one would otherwise leave stones in the tail,
  // and the save file carries them.
  for (int i = 0; i < kCellBytes; ++i) game.cell[i] = 0;
  clearMask(game.dead);
  for (int i = 0; i < kHistory; ++i) game.recent[i] = 0;
  game.capturedBy[0] = 0;
  game.capturedBy[kBlack] = 0;
  game.capturedBy[kWhite] = 0;
  game.komiHalves = komiHalves;
  game.moveNumber = 0;
  game.toMove = kBlack;
  game.handicap = 0;
  game.ko = kNoPoint;
  game.passes = 0;
  game.lastMove = kNoPoint;
  game.recentCount = 0;
  game.stage = static_cast<uint8_t>(Stage::Playing);
  game.accepted = 0;

  Point stones[kMaxHandicap];
  const int placed = handicapPoints(game.size, handicap, stones);
  if (placed == 0) return;
  for (int i = 0; i < placed; ++i) game.put(stones[i], kBlack);
  game.handicap = static_cast<uint8_t>(placed);
  // The stones are placed, not played: there is no last move, no capture and no
  // move number, and it is White to play. A handicap where Black also moved
  // first would be a stone and a half.
  game.toMove = kWhite;
}

void group(const Game& game, const int point, uint8_t stones[kMaskBytes], int& size, int& liberties) {
  uint8_t scratch[kMaskBytes];
  uint8_t* seen = stones != nullptr ? stones : scratch;
  clearMask(seen);
  size = 0;
  liberties = 0;

  const uint8_t colour = game.at(point);
  if (!isStone(colour)) return;

  // Liberties are counted once each, so an empty point touching three stones of
  // the group is one liberty and not three. Counting them per stone is the
  // oldest bug in every Go implementation.
  uint8_t counted[kMaskBytes];
  clearMask(counted);

  Fill fill;
  fill.push(point);
  mark(seen, point);
  while (!fill.empty()) {
    const int current = fill.pop();
    ++size;
    Point around[4];
    const int count = neighbours(game.size, current, around);
    for (int i = 0; i < count; ++i) {
      const int next = around[i];
      const uint8_t there = game.at(next);
      if (there == kEmpty) {
        if (!marked(counted, next)) {
          mark(counted, next);
          ++liberties;
        }
        continue;
      }
      if (there != colour || marked(seen, next)) continue;
      mark(seen, next);
      fill.push(next);
    }
  }
}

int libertiesAfter(const Game& game, const int point, const uint8_t colour) {
  if (point < 0 || point >= game.points()) return -1;
  if (game.at(point) != kEmpty) return -1;

  Game after = game;
  after.put(point, colour);
  captureAround(after, point, other(colour));

  uint8_t stones[kMaskBytes];
  int size = 0;
  int liberties = 0;
  group(after, point, stones, size, liberties);
  return liberties;
}

bool legal(const Game& game, const int point, const uint8_t colour) {
  if (point == kPass) return true;
  if (point < 0 || point >= game.points()) return false;
  if (game.at(point) != kEmpty) return false;
  // Simple ko: the point that would immediately undo the last single capture.
  // Superko below catches the rest, but this one is checked first because it is
  // the rule a player actually knows and the cheapest to answer.
  if (point == game.ko) return false;

  Game after = game;
  after.put(point, colour);
  captureAround(after, point, other(colour));

  uint8_t stones[kMaskBytes];
  int size = 0;
  int liberties = 0;
  group(after, point, stones, size, liberties);
  // Suicide. Note the order: captures happen first, so a move that fills its
  // own last liberty while taking the surrounding group is perfectly legal.
  if (liberties == 0) return false;

  after.toMove = other(colour);
  return !repeatsPosition(after);
}

bool play(Game& game, const int point) {
  if (game.stage != static_cast<uint8_t>(Stage::Playing)) return false;

  if (point == kPass) {
    rememberPosition(game);
    game.toMove = other(game.toMove);
    game.ko = kNoPoint;
    game.lastMove = kPass;
    ++game.moveNumber;
    if (++game.passes >= 2 || game.moveNumber >= moveLimit(game.size))
      game.stage = static_cast<uint8_t>(Stage::Scoring);
    return true;
  }

  const uint8_t colour = game.toMove;
  if (!legal(game, point, colour)) return false;

  rememberPosition(game);

  game.put(point, colour);
  const int taken = captureAround(game, point, other(colour));
  game.capturedBy[colour] = static_cast<uint16_t>(game.capturedBy[colour] + taken);

  // Simple ko arms only on the shape that can actually repeat: exactly one
  // stone taken by a stone that is now alone with exactly one liberty. Arming
  // it on any capture forbids legal moves, which is a rules bug that looks like
  // the board ignoring a tap.
  game.ko = kNoPoint;
  if (taken == 1) {
    uint8_t stones[kMaskBytes];
    int size = 0;
    int liberties = 0;
    group(game, point, stones, size, liberties);
    if (size == 1 && liberties == 1) {
      Point around[4];
      const int count = neighbours(game.size, point, around);
      for (int i = 0; i < count; ++i) {
        if (game.at(around[i]) == kEmpty) {
          game.ko = around[i];
          break;
        }
      }
    }
  }

  game.toMove = other(colour);
  game.passes = 0;
  game.lastMove = static_cast<Point>(point);
  ++game.moveNumber;
  if (game.moveNumber >= moveLimit(game.size)) game.stage = static_cast<uint8_t>(Stage::Scoring);
  return true;
}

bool isEye(const Game& game, const int point, const uint8_t colour) {
  if (point < 0 || point >= game.points()) return false;
  if (game.at(point) != kEmpty) return false;

  Point around[4];
  const int count = neighbours(game.size, point, around);
  for (int i = 0; i < count; ++i) {
    if (game.at(around[i]) != colour) return false;
  }

  // The diagonal test is what separates an eye from a false eye. On an edge or
  // in a corner the opponent needs only one diagonal to break it; in the middle
  // they need two. Without this a playout happily fills a false eye and the
  // group it was pretending to keep alive dies.
  const int row = game.row(point);
  const int col = game.col(point);
  int diagonals = 0;
  int hostile = 0;
  for (int dr = -1; dr <= 1; dr += 2) {
    for (int dc = -1; dc <= 1; dc += 2) {
      if (!game.on(row + dr, col + dc)) continue;
      ++diagonals;
      if (game.at(game.point(row + dr, col + dc)) == other(colour)) ++hostile;
    }
  }
  const int allowed = diagonals < 4 ? 0 : 1;
  return hostile <= allowed;
}

// FNV-1a over the position AND the side to move, which makes the repetition
// rule SITUATIONAL superko rather than positional: a board the game has held
// before with the other player to move is not forbidden.
//
// That is a real ruleset, not an approximation -- it is what the AGA rules use
// -- and it errs toward permissiveness, so no legal move is ever refused. The
// labelling was wrong in four places before anybody checked which one the code
// implemented.
uint32_t positionKey(const Game& game) {
  uint32_t hash = 2166136261u;
  const int points = game.points();
  for (int i = 0; i < points; ++i) {
    hash ^= game.at(i);
    hash *= 16777619u;
  }
  hash ^= game.toMove;
  hash *= 16777619u;
  return hash;
}

void territory(const Game& game, uint8_t owner[kMaxPoints]) {
  const int points = game.points();
  // Dead stones are lifted before anything is counted: under area scoring a
  // dead stone is worth two points to its captor, one for the point it stands
  // on and one for the stone it will become. Treating it as empty and letting
  // the fill reach it does both at once.
  uint8_t board[kMaxPoints];
  for (int i = 0; i < points; ++i) {
    board[i] = marked(game.dead, i) ? kEmpty : game.at(i);
    owner[i] = board[i];
  }

  uint8_t seen[kMaskBytes];
  clearMask(seen);
  for (int start = 0; start < points; ++start) {
    if (board[start] != kEmpty || marked(seen, start)) continue;

    // One empty region. It belongs to whichever colour is the only one on its
    // border; a region both colours touch is neutral and counts for nobody,
    // which is the whole reason nobody has to fill the dame.
    uint8_t region[kMaskBytes];
    clearMask(region);
    Fill fill;
    fill.push(start);
    mark(seen, start);
    mark(region, start);
    bool touchesBlack = false;
    bool touchesWhite = false;
    while (!fill.empty()) {
      const int current = fill.pop();
      Point around[4];
      const int count = neighbours(game.size, current, around);
      for (int i = 0; i < count; ++i) {
        const int next = around[i];
        if (board[next] == kBlack) {
          touchesBlack = true;
          continue;
        }
        if (board[next] == kWhite) {
          touchesWhite = true;
          continue;
        }
        if (marked(seen, next)) continue;
        mark(seen, next);
        mark(region, next);
        fill.push(next);
      }
    }

    const uint8_t belongsTo = (touchesBlack && !touchesWhite)   ? kBlack
                              : (touchesWhite && !touchesBlack) ? kWhite
                                                                : kEmpty;
    if (belongsTo == kEmpty) continue;
    for (int p = 0; p < points; ++p) {
      if (marked(region, p)) owner[p] = belongsTo;
    }
  }
}

Score score(const Game& game) {
  uint8_t owner[kMaxPoints];
  territory(game, owner);

  Score result{0, 0};
  const int points = game.points();
  for (int i = 0; i < points; ++i) {
    if (owner[i] == kBlack) result.blackHalves = static_cast<int16_t>(result.blackHalves + 2);
    if (owner[i] == kWhite) result.whiteHalves = static_cast<int16_t>(result.whiteHalves + 2);
  }
  result.whiteHalves = static_cast<int16_t>(result.whiteHalves + game.komiHalves);
  return result;
}

Outcome outcome(const Game& game) {
  if (game.stage != static_cast<uint8_t>(Stage::Over)) return Outcome::Running;
  const Score counted = score(game);
  return counted.blackHalves > counted.whiteHalves ? Outcome::BlackWins : Outcome::WhiteWins;
}

int16_t marginHalves(const Game& game) {
  const Score counted = score(game);
  const int16_t difference = static_cast<int16_t>(counted.blackHalves - counted.whiteHalves);
  return difference < 0 ? static_cast<int16_t>(-difference) : difference;
}

}  // namespace go
