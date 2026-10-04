#include "GoEngine.h"

#include "GoPatterns.h"

namespace goengine {
namespace {

constexpr int kMaskBytes = go::kMaskBytes;
// The move index that means "pass". The game's own sentinel rather than "one
// past the board": one past a nine by nine board is a real point on a thirteen
// by thirteen one.
constexpr int kPassIndex = go::kPass;

// --- The playout board ------------------------------------------------------
//
// A second, smaller board, and the duplication is deliberate. `go::Game` is the
// game: it carries a superko ring, dead-stone marks, a move number and capture
// tallies, and `go::legal()` copies the whole board to answer one question.
// A playout plays eighty moves and the search plays thousands of playouts, so
// the game's own board is three orders of magnitude too expensive here.
//
// What keeps the two from drifting is not discipline, it is a test: over a
// million random positions are played through BOTH and asserted identical,
// point for point, in host-tests/go, which prints the count it actually
// reached rather than trusting one written here. That is the differential check that makes a second
// implementation safe. See test_go.cpp, testTheFastBoardIsTheSameGame.
struct Fast {
  uint8_t point[go::kMaxPoints];
  // Unpacked, one byte a point: the game packs its board two bits a point to
  // fit a packet, and a playout reads every point of it thousands of times.
  int size;
  int points;
  int16_t komiHalves;
  go::Point ko;
  uint8_t toMove;
  uint8_t passes;
};

void adopt(Fast& fast, const go::Game& game) {
  fast.size = game.size;
  fast.points = game.points();
  for (int i = 0; i < fast.points; ++i) fast.point[i] = game.at(i);
  fast.komiHalves = game.komiHalves;
  fast.ko = game.ko;
  fast.toMove = game.toMove;
  fast.passes = game.passes;
}

// A chain's liberty count, and whether it is exactly `wanted`. Walking stops as
// soon as the answer cannot change, which is what makes the atari test cheap
// enough to run on every neighbour of every move.
int chainLiberties(const Fast& fast, const int start, uint8_t stones[kMaskBytes], const int stopAt) {
  const uint8_t colour = fast.point[start];
  uint8_t counted[kMaskBytes];
  for (int i = 0; i < kMaskBytes; ++i) {
    counted[i] = 0;
    stones[i] = 0;
  }
  go::Point stack[go::kMaxPoints];
  int top = 0;
  stack[top++] = static_cast<go::Point>(start);
  go::mark(stones, start);
  int liberties = 0;
  while (top > 0) {
    const int current = stack[--top];
    go::Point around[4];
    const int count = go::neighbours(fast.size, current, around);
    for (int i = 0; i < count; ++i) {
      const int next = around[i];
      if (fast.point[next] == go::kEmpty) {
        if (!go::marked(counted, next)) {
          go::mark(counted, next);
          if (++liberties > stopAt) return liberties;
        }
        continue;
      }
      if (fast.point[next] != colour || go::marked(stones, next)) continue;
      go::mark(stones, next);
      stack[top++] = static_cast<go::Point>(next);
    }
  }
  return liberties;
}

int captureAround(Fast& fast, const int at, const uint8_t colour, int& lastTaken) {
  go::Point around[4];
  const int count = go::neighbours(fast.size, at, around);
  int taken = 0;
  for (int i = 0; i < count; ++i) {
    const int point = around[i];
    if (fast.point[point] != colour) continue;
    uint8_t stones[kMaskBytes];
    if (chainLiberties(fast, point, stones, 0) != 0) continue;
    for (int p = 0; p < fast.points; ++p) {
      if (!go::marked(stones, p)) continue;
      fast.point[p] = go::kEmpty;
      lastTaken = p;
      ++taken;
    }
  }
  return taken;
}

bool isEye(const Fast& fast, const int point, const uint8_t colour) {
  if (fast.point[point] != go::kEmpty) return false;
  go::Point around[4];
  const int count = go::neighbours(fast.size, point, around);
  for (int i = 0; i < count; ++i) {
    if (fast.point[around[i]] != colour) return false;
  }
  const int row = go::rowOf(fast.size, point);
  const int col = go::colOf(fast.size, point);
  int diagonals = 0;
  int hostile = 0;
  for (int dr = -1; dr <= 1; dr += 2) {
    for (int dc = -1; dc <= 1; dc += 2) {
      if (!go::onBoard(fast.size, row + dr, col + dc)) continue;
      ++diagonals;
      if (fast.point[go::pointAt(fast.size, row + dr, col + dc)] == go::other(colour)) ++hostile;
    }
  }
  return hostile <= (diagonals < 4 ? 0 : 1);
}

// Play, or say it was not legal. Simple ko only: the superko ring is a property
// of the real game and is applied where the real move is chosen, not eighty
// plies into an imagined one.
bool playFast(Fast& fast, const int point) {
  if (point == kPassIndex) {
    fast.ko = go::kNoPoint;
    fast.toMove = go::other(fast.toMove);
    ++fast.passes;
    return true;
  }
  if (fast.point[point] != go::kEmpty) return false;
  if (point == fast.ko) return false;

  const uint8_t colour = fast.toMove;
  // The fast path, and it is most moves: a stone with an empty neighbour has a
  // liberty, so it cannot be suicide and nothing has to be walked.
  bool hasAir = false;
  go::Point around[4];
  const int count = go::neighbours(fast.size, point, around);
  for (int i = 0; i < count; ++i) {
    if (fast.point[around[i]] == go::kEmpty) {
      hasAir = true;
      break;
    }
  }

  fast.point[point] = colour;
  int lastTaken = go::kNoPoint;
  const int taken = captureAround(fast, point, go::other(colour), lastTaken);

  if (!hasAir && taken == 0) {
    uint8_t stones[kMaskBytes];
    if (chainLiberties(fast, point, stones, 0) == 0) {
      fast.point[point] = go::kEmpty;
      return false;
    }
  }

  fast.ko = go::kNoPoint;
  if (taken == 1) {
    uint8_t stones[kMaskBytes];
    const int liberties = chainLiberties(fast, point, stones, 1);
    int size = 0;
    for (int p = 0; p < fast.points; ++p) {
      if (go::marked(stones, p)) ++size;
    }
    if (size == 1 && liberties == 1) fast.ko = static_cast<go::Point>(lastTaken);
  }

  fast.toMove = go::other(colour);
  fast.passes = 0;
  return true;
}

// Who each point belongs to at the end of a position: the stone standing on it,
// or the colour that alone surrounds the empty region it is in.
//
// This is the OWNER MAP, and it is not the same question as "what stone is
// here". The first version of estimateDead asked the second one and was wrong
// in the commonest endgame shape there is: once a dead group is captured during
// a playout, the points it stood on are empty, and an empty point holds no
// stone for anybody. A single dead stone in a corner was never called dead at
// all, and a dead pair was called half dead.
void fastOwner(const Fast& fast, uint8_t owner[go::kMaxPoints]) {
  uint8_t seen[kMaskBytes];
  for (int i = 0; i < kMaskBytes; ++i) seen[i] = 0;
  for (int i = 0; i < fast.points; ++i) owner[i] = fast.point[i];

  go::Point stack[go::kMaxPoints];
  go::Point region[go::kMaxPoints];
  for (int start = 0; start < fast.points; ++start) {
    if (fast.point[start] != go::kEmpty || go::marked(seen, start)) continue;
    int top = 0;
    int found = 0;
    stack[top++] = static_cast<go::Point>(start);
    go::mark(seen, start);
    region[found++] = static_cast<go::Point>(start);
    bool black = false;
    bool white = false;
    while (top > 0) {
      const int current = stack[--top];
      go::Point around[4];
      const int count = go::neighbours(fast.size, current, around);
      for (int i = 0; i < count; ++i) {
        const int next = around[i];
        if (fast.point[next] == go::kBlack) {
          black = true;
          continue;
        }
        if (fast.point[next] == go::kWhite) {
          white = true;
          continue;
        }
        if (go::marked(seen, next)) continue;
        go::mark(seen, next);
        stack[top++] = static_cast<go::Point>(next);
        region[found++] = static_cast<go::Point>(next);
      }
    }
    const uint8_t belongsTo = (black && !white) ? go::kBlack : ((white && !black) ? go::kWhite : go::kEmpty);
    for (int i = 0; i < found; ++i) owner[region[i]] = belongsTo;
  }
}

// Area score from Black's point of view, in half points, komi included. Every
// stone standing at the end of a playout is alive by construction: the playout
// only stops when neither side has a move that is not filling its own eye.
int fastScore(const Fast& fast) {
  uint8_t owner[go::kMaxPoints];
  fastOwner(fast, owner);
  int black = 0;
  int white = 0;
  for (int i = 0; i < fast.points; ++i) {
    if (owner[i] == go::kBlack) ++black;
    if (owner[i] == go::kWhite) ++white;
  }
  return black * 2 - (white * 2 + fast.komiHalves);
}

inline uint32_t nextRandom(uint32_t& seed) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

// The liberty of a chain that has exactly one, or kNoPoint. Walks the chain
// once and stops the moment a second liberty appears.
int soleLiberty(const Fast& fast, const int start) {
  const uint8_t colour = fast.point[start];
  uint8_t seen[kMaskBytes];
  for (int i = 0; i < kMaskBytes; ++i) seen[i] = 0;
  go::Point stack[go::kMaxPoints];
  int top = 0;
  stack[top++] = static_cast<go::Point>(start);
  go::mark(seen, start);
  int liberty = go::kNoPoint;
  while (top > 0) {
    const int current = stack[--top];
    go::Point around[4];
    const int count = go::neighbours(fast.size, current, around);
    for (int i = 0; i < count; ++i) {
      const int next = around[i];
      if (fast.point[next] == go::kEmpty) {
        if (liberty == go::kNoPoint) {
          liberty = next;
          continue;
        }
        if (liberty != next) return go::kNoPoint;  // two liberties: not in atari
        continue;
      }
      if (fast.point[next] != colour || go::marked(seen, next)) continue;
      go::mark(seen, next);
      stack[top++] = static_cast<go::Point>(next);
    }
  }
  return liberty;
}

// Whether the 3x3 shape around `point` is one the MoGo patterns like.
//
// This is the knowledge in the engine, and there is a measurement behind that
// claim: the playout policy is worth about +512 Elo on its own, and a
// policy-guided engine at 500 playouts beats a knowledge-free one at 10,000.
// Without it whole groups die in a playout and nothing notices, which on a
// panel somebody is watching reads as a crash rather than as a loss.
//
// The table carries every rotation, reflection and colour swap already, so
// there is no symmetry code here and none of the eight can be got wrong.
bool matchesPattern(const Fast& fast, const int point, const uint8_t colour) {
  const int row = go::rowOf(fast.size, point);
  const int col = go::colOf(fast.size, point);
  const int classX = col == 0 ? 0 : (col == fast.size - 1 ? 2 : 1);
  const int classY = row == 0 ? 0 : (row == fast.size - 1 ? 2 : 1);
  const int cls = classY * 3 + classX;

  // NW N NE W E SW S SE, skipping whatever is off the board: which neighbours
  // exist is exactly what the class already said.
  static const int8_t kOrder[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
  uint16_t index = 0;
  uint16_t mult = 1;
  for (int i = 0; i < 8; ++i) {
    const int r = row + kOrder[i][0];
    const int c = col + kOrder[i][1];
    if (!go::onBoard(fast.size, r, c)) continue;
    const uint8_t here = fast.point[go::pointAt(fast.size, r, c)];
    const uint16_t value = here == go::kEmpty ? 0 : (here == colour ? 1 : 2);
    index = static_cast<uint16_t>(index + value * mult);
    mult = static_cast<uint16_t>(mult * 3);
  }

  const uint32_t bit = static_cast<uint32_t>(gopatterns::kOffset[cls]) * 8u + index;
  return (gopatterns::kBits[bit / 8] & (1u << (bit % 8))) != 0;
}

// Whether `point` is worth playing at all: legal, and not filling our own eye.
bool sensible(Fast& fast, const int point) {
  if (fast.point[point] != go::kEmpty) return false;
  return !isEye(fast, point, fast.toMove);
}

// One move of a playout.
//
// **The policy is LOCAL, and that is the whole design.** The first version asked
// every chain on the board whether it was in atari, once per move: correct, and
// fourteen times slower than a uniform playout, which at a hundred and thirteen
// Elo a doubling is four ranks handed back to buy one. Go is a local game and an
// atari is caused by the move that was just played, so only the four points
// around `lastMove` can have started one.
//
// This is the Mogo policy reduced to the half that pays for itself here: answer
// an atari the last move created, or take the group that caused it. The 3x3
// shape patterns that go above this are worth another rank and a half and are
// the next thing to add; see docs/apps/go.md.
int playoutMove(Fast& fast, uint32_t& seed, const bool policy, const int lastMove) {
  if (policy && lastMove >= 0 && lastMove < fast.points) {
    // Candidates, in the order Mogo tries them: save what the last move put in
    // atari, else capture what put it there.
    go::Point around[4];
    const int count = go::neighbours(fast.size, lastMove, around);
    int rescue = go::kNoPoint;
    int capture = go::kNoPoint;
    for (int i = 0; i < count; ++i) {
      const int next = around[i];
      if (!go::isStone(fast.point[next])) continue;
      const int liberty = soleLiberty(fast, next);
      if (liberty == go::kNoPoint) continue;
      if (fast.point[next] == fast.toMove) {
        if (rescue == go::kNoPoint) rescue = liberty;
      } else if (capture == go::kNoPoint) {
        capture = liberty;
      }
    }
    // The last move itself can be the chain in atari, which is the shape where
    // a capture is available and nothing around it is.
    if (capture == go::kNoPoint && fast.point[lastMove] != fast.toMove && go::isStone(fast.point[lastMove])) {
      const int liberty = soleLiberty(fast, lastMove);
      if (liberty != go::kNoPoint) capture = liberty;
    }

    const int tries[2] = {rescue, capture};
    for (int which = 0; which < 2; ++which) {
      const int candidate = tries[which];
      if (candidate == go::kNoPoint || !sensible(fast, candidate)) continue;
      Fast trial = fast;
      if (!playFast(trial, candidate)) continue;
      // Not if it walks straight back into atari. Answering an atari with a
      // stone that is itself in atari is a beginner losing two groups instead
      // of one, and in a playout it is noise.
      if (soleLiberty(trial, candidate) != go::kNoPoint) continue;
      fast = trial;
      return candidate;
    }
  }

  // Then the shape patterns, in the eight points around the last move. Mogo's
  // order: answer the tactics first, then play a good local shape, then play
  // anywhere. Local again, for the same reason -- a shape becomes interesting
  // because of the stone that was just put down next to it.
  if (policy && lastMove >= 0 && lastMove < fast.points) {
    const int row = go::rowOf(fast.size, lastMove);
    const int col = go::colOf(fast.size, lastMove);
    go::Point matches[8];
    int found = 0;
    for (int dr = -1; dr <= 1; ++dr) {
      for (int dc = -1; dc <= 1; ++dc) {
        if (dr == 0 && dc == 0) continue;
        if (!go::onBoard(fast.size, row + dr, col + dc)) continue;
        const int point = go::pointAt(fast.size, row + dr, col + dc);
        if (!sensible(fast, point)) continue;
        if (!matchesPattern(fast, point, fast.toMove)) continue;
        matches[found++] = static_cast<go::Point>(point);
      }
    }
    // Chosen at random among the matches rather than by the first one found:
    // walking them in board order would bias every playout towards the
    // top-left, which is a systematic error rather than noise and does not
    // average out over thousands of them.
    while (found > 0) {
      const int which = static_cast<int>(nextRandom(seed) % static_cast<uint32_t>(found));
      const int point = matches[which];
      if (playFast(fast, point)) return point;
      matches[which] = matches[--found];
    }
  }

  const int start = static_cast<int>(nextRandom(seed) % static_cast<uint32_t>(fast.points));
  for (int i = 0; i < fast.points; ++i) {
    const int point = (start + i) % fast.points;
    if (!sensible(fast, point)) continue;
    if (playFast(fast, point)) return point;
  }
  playFast(fast, kPassIndex);
  return kPassIndex;
}

// `played` accumulates which points each colour put a stone on, indexed by
// colour. That is the all-moves-as-first set RAVE is built from, and collecting
// it costs one bit a move.
int runPlayout(Fast fast, uint32_t& seed, const bool policy, int last, uint8_t played[3][kMaskBytes]) {
  // Twice the board is the ceiling every implementation uses. Under simple ko
  // a playout can in principle cycle; the cap ends it and the score of a
  // position that has cycled is close enough for one sample out of thousands.
  const int kMaxMoves = fast.points * 2 + 20;
  for (int move = 0; move < kMaxMoves && fast.passes < 2; ++move) {
    const uint8_t mover = fast.toMove;
    last = playoutMove(fast, seed, policy, last);
    if (played != nullptr && last >= 0 && last < fast.points) go::mark(played[mover], last);
  }
  return fastScore(fast);
}

}  // namespace

int playoutOnce(const go::Game& game, uint32_t& seed, const bool policy) {
  Fast fast;
  adopt(fast, game);
  return runPlayout(fast, seed, policy, -1, nullptr);
}

bool fastPlayForTest(go::Game& game, const int point) {
  Fast fast;
  adopt(fast, game);
  const bool played = playFast(fast, point == go::kPass ? kPassIndex : point);
  for (int i = 0; i < fast.points; ++i) game.put(i, fast.point[i]);
  game.ko = fast.ko;
  game.toMove = fast.toMove;
  game.passes = fast.passes;
  return played;
}

bool passingWins(const go::Game& game, const uint8_t colour) {
  // Counted with every stone alive, which is what Tromp-Taylor does and what the
  // opponent would be agreeing to if it passed and the human passed back.
  go::Game counted = game;
  go::clearMask(counted.dead);
  const go::Score score = go::score(counted);
  return colour == go::kBlack ? score.blackHalves > score.whiteHalves : score.whiteHalves > score.blackHalves;
}

void opinionOnDead(const go::Game& game, uint8_t out[kMaskBytes]) {
  // positionKey hashes the stones and the side to move and nothing else, so it
  // does not move while the players are marking dead stones.
  uint32_t seed = go::positionKey(game);
  if (seed == 0) seed = 0x9E3779B9u;
  estimateDead(game, seed, out);
}

void estimateDead(const go::Game& game, uint32_t& seed, uint8_t out[kMaskBytes]) {
  go::clearMask(out);

  // How often each point ends up BLACK'S at the end of a playout from here --
  // the owner map, not the stones. The difference is the whole correctness of
  // this function: a dead group is captured during the playout, so the points
  // it stood on end EMPTY, and asking which stone is there answers nobody.
  // Asking who the region belongs to answers the captor.
  const int points = game.points();
  int16_t blackness[go::kMaxPoints] = {};
  constexpr int kTrials = 200;
  for (int trial = 0; trial < kTrials; ++trial) {
    Fast fast;
    adopt(fast, game);
    // Both sides pass at the end of a real game, and a playout that inherits
    // those passes stops immediately. Clear them: the question here is what
    // happens if play CONTINUES.
    fast.passes = 0;
    const int kMaxMoves = fast.points * 2 + 20;
    int last = -1;
    for (int move = 0; move < kMaxMoves && fast.passes < 2; ++move) last = playoutMove(fast, seed, true, last);

    uint8_t owner[go::kMaxPoints];
    fastOwner(fast, owner);
    for (int point = 0; point < points; ++point) {
      if (owner[point] == go::kBlack) ++blackness[point];
      if (owner[point] == go::kWhite) --blackness[point];
    }
  }

  // A GROUP lives or dies together, so the verdict is taken for the group and
  // not for the stone. Marking half a dragon dead draws one live stone beside
  // one ghost, which is a board nobody can read and a score nobody agreed.
  //
  // Seventy percent, which is where OGS's autoscorer independently landed.
  // Deliberately not a bare majority: calling a live group dead costs a player
  // a game they won, and calling a dead one live costs them one tap.
  constexpr int16_t kThreshold = kTrials * 7 / 10;
  uint8_t judged[kMaskBytes];
  go::clearMask(judged);
  for (int point = 0; point < points; ++point) {
    const uint8_t here = game.at(point);
    if (!go::isStone(here) || go::marked(judged, point)) continue;

    uint8_t stones[kMaskBytes];
    int size = 0;
    int liberties = 0;
    go::group(game, point, stones, size, liberties);

    int32_t total = 0;
    for (int p = 0; p < points; ++p) {
      if (!go::marked(stones, p)) continue;
      go::mark(judged, p);
      total += blackness[p];
    }
    const int32_t average = size > 0 ? total / size : 0;

    const bool dead = here == go::kBlack ? average <= -kThreshold : average >= kThreshold;
    if (!dead) continue;
    for (int p = 0; p < points; ++p) {
      if (go::marked(stones, p)) go::mark(out, p);
    }
  }
}

}  // namespace goengine
