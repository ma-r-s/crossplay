#include "GoEngine.h"

#include "GoPatterns.h"

namespace goengine {
namespace {

constexpr int kMaskBytes = (go::kPoints + 7) / 8;
// The move index that means "pass", as the search enumerates candidates.
constexpr int kPassIndex = go::kPoints;
constexpr int kCandidates = go::kPoints + 1;

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
  uint8_t point[go::kPoints];
  int16_t komiHalves;
  uint8_t ko;
  uint8_t toMove;
  uint8_t passes;
};

void adopt(Fast& fast, const go::Game& game) {
  for (int i = 0; i < go::kPoints; ++i) fast.point[i] = game.point[i];
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
  uint8_t stack[go::kPoints];
  int top = 0;
  stack[top++] = static_cast<uint8_t>(start);
  go::mark(stones, start);
  int liberties = 0;
  while (top > 0) {
    const int current = stack[--top];
    uint8_t around[4];
    const int count = go::neighbours(current, around);
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
      stack[top++] = static_cast<uint8_t>(next);
    }
  }
  return liberties;
}

int captureAround(Fast& fast, const int at, const uint8_t colour, int& lastTaken) {
  uint8_t around[4];
  const int count = go::neighbours(at, around);
  int taken = 0;
  for (int i = 0; i < count; ++i) {
    const int point = around[i];
    if (fast.point[point] != colour) continue;
    uint8_t stones[kMaskBytes];
    if (chainLiberties(fast, point, stones, 0) != 0) continue;
    for (int p = 0; p < go::kPoints; ++p) {
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
  uint8_t around[4];
  const int count = go::neighbours(point, around);
  for (int i = 0; i < count; ++i) {
    if (fast.point[around[i]] != colour) return false;
  }
  const int row = go::rowOf(point);
  const int col = go::colOf(point);
  int diagonals = 0;
  int hostile = 0;
  for (int dr = -1; dr <= 1; dr += 2) {
    for (int dc = -1; dc <= 1; dc += 2) {
      if (!go::onBoard(row + dr, col + dc)) continue;
      ++diagonals;
      if (fast.point[go::pointAt(row + dr, col + dc)] == go::other(colour)) ++hostile;
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
  uint8_t around[4];
  const int count = go::neighbours(point, around);
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
    for (int p = 0; p < go::kPoints; ++p) {
      if (go::marked(stones, p)) ++size;
    }
    if (size == 1 && liberties == 1) fast.ko = static_cast<uint8_t>(lastTaken);
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
void fastOwner(const Fast& fast, uint8_t owner[go::kPoints]) {
  uint8_t seen[kMaskBytes];
  for (int i = 0; i < kMaskBytes; ++i) seen[i] = 0;
  for (int i = 0; i < go::kPoints; ++i) owner[i] = fast.point[i];

  uint8_t stack[go::kPoints];
  uint8_t region[go::kPoints];
  for (int start = 0; start < go::kPoints; ++start) {
    if (fast.point[start] != go::kEmpty || go::marked(seen, start)) continue;
    int top = 0;
    int found = 0;
    stack[top++] = static_cast<uint8_t>(start);
    go::mark(seen, start);
    region[found++] = static_cast<uint8_t>(start);
    bool black = false;
    bool white = false;
    while (top > 0) {
      const int current = stack[--top];
      uint8_t around[4];
      const int count = go::neighbours(current, around);
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
        stack[top++] = static_cast<uint8_t>(next);
        region[found++] = static_cast<uint8_t>(next);
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
  uint8_t owner[go::kPoints];
  fastOwner(fast, owner);
  int black = 0;
  int white = 0;
  for (int i = 0; i < go::kPoints; ++i) {
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
  uint8_t stack[go::kPoints];
  int top = 0;
  stack[top++] = static_cast<uint8_t>(start);
  go::mark(seen, start);
  int liberty = go::kNoPoint;
  while (top > 0) {
    const int current = stack[--top];
    uint8_t around[4];
    const int count = go::neighbours(current, around);
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
      stack[top++] = static_cast<uint8_t>(next);
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
  const int row = go::rowOf(point);
  const int col = go::colOf(point);
  const int classX = col == 0 ? 0 : (col == go::kSize - 1 ? 2 : 1);
  const int classY = row == 0 ? 0 : (row == go::kSize - 1 ? 2 : 1);
  const int cls = classY * 3 + classX;

  // NW N NE W E SW S SE, skipping whatever is off the board: which neighbours
  // exist is exactly what the class already said.
  static const int8_t kOrder[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
  uint16_t index = 0;
  uint16_t mult = 1;
  for (int i = 0; i < 8; ++i) {
    const int r = row + kOrder[i][0];
    const int c = col + kOrder[i][1];
    if (!go::onBoard(r, c)) continue;
    const uint8_t here = fast.point[go::pointAt(r, c)];
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
  if (policy && lastMove >= 0 && lastMove < go::kPoints) {
    // Candidates, in the order Mogo tries them: save what the last move put in
    // atari, else capture what put it there.
    uint8_t around[4];
    const int count = go::neighbours(lastMove, around);
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
  if (policy && lastMove >= 0 && lastMove < go::kPoints) {
    const int row = go::rowOf(lastMove);
    const int col = go::colOf(lastMove);
    uint8_t matches[8];
    int found = 0;
    for (int dr = -1; dr <= 1; ++dr) {
      for (int dc = -1; dc <= 1; ++dc) {
        if (dr == 0 && dc == 0) continue;
        if (!go::onBoard(row + dr, col + dc)) continue;
        const int point = go::pointAt(row + dr, col + dc);
        if (!sensible(fast, point)) continue;
        if (!matchesPattern(fast, point, fast.toMove)) continue;
        matches[found++] = static_cast<uint8_t>(point);
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

  const int start = static_cast<int>(nextRandom(seed) % go::kPoints);
  for (int i = 0; i < go::kPoints; ++i) {
    const int point = (start + i) % go::kPoints;
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
  constexpr int kMaxMoves = go::kPoints * 2 + 20;
  for (int move = 0; move < kMaxMoves && fast.passes < 2; ++move) {
    const uint8_t mover = fast.toMove;
    last = playoutMove(fast, seed, policy, last);
    if (played != nullptr && last >= 0 && last < go::kPoints) go::mark(played[mover], last);
  }
  return fastScore(fast);
}

// --- The tree ---------------------------------------------------------------

// One node a playout, at most: a visited leaf grows exactly one child rather
// than all of its children at once. Expanding a node fully would put eighty
// nodes in the pool for one visit and exhaust it in fifty playouts.
//
// Fourteen bytes, and every field is sized against what it can actually hold: a
// child is visited at most once a playout, so nothing here can exceed the
// playout budget.
struct Node {
  int16_t score;      // playouts won by the side that played `move`, minus lost
  int16_t raveScore;  // the same, over every simulation where that move appeared
  uint16_t visits;
  uint16_t raveVisits;
  // How many of `visits` are imagined rather than played. The prior has to go
  // INTO the counts or it cannot influence selection, and it has to come back
  // OUT of them when the move is finally chosen: at three thousand playouts a
  // root child sees about fifty real visits, which is the same order as the
  // prior, so choosing the most-visited child would be choosing the child with
  // the biggest prior.
  uint16_t prior;
  int16_t firstChild;
  int16_t nextSibling;
  uint8_t move;
  uint8_t cursor;  // the next candidate index this node has not tried
};

// Three thousand nodes is 42KB of static SRAM and it is enough BECAUSE of RAVE:
// the whole point of sharing statistics between moves is that a shallow tree
// stops being a problem. Without it, 3,000 playouts over sixty legal moves is
// fifty samples a move, which is noise.
constexpr int kMaxNodes = 3072;
Node gNodes[kMaxNodes];
int gNodeCount = 0;

int newNode(const uint8_t move) {
  if (gNodeCount >= kMaxNodes) return -1;
  const int index = gNodeCount++;
  gNodes[index].score = 0;
  gNodes[index].raveScore = 0;
  gNodes[index].visits = 0;
  gNodes[index].raveVisits = 0;
  gNodes[index].prior = 0;
  gNodes[index].firstChild = -1;
  gNodes[index].nextSibling = -1;
  gNodes[index].move = move;
  gNodes[index].cursor = 0;
  return index;
}

// The next candidate this node has not yet tried, playing it into `fast`.
// Returns the move index, or -1 when there is nothing left.
int expandOne(Node& node, Fast& fast, const bool* blind) {
  while (node.cursor < kCandidates) {
    const int candidate = node.cursor++;
    // `blind` is the root's exclusion mask and is null everywhere else: a
    // beginner misses a move, they do not become unable to imagine the opponent
    // replying there.
    if (blind != nullptr && candidate != kPassIndex && blind[candidate]) continue;
    if (candidate == kPassIndex) {
      // Pass is a real move and has to be in the tree: without it a losing
      // engine fills its own eyes at the end of a lost game rather than
      // accepting it, which is the single most common way a Go program looks
      // broken to a human.
      playFast(fast, kPassIndex);
      return candidate;
    }
    if (fast.point[candidate] != go::kEmpty) continue;
    if (isEye(fast, candidate, fast.toMove)) continue;
    if (playFast(fast, candidate)) return candidate;
  }
  return -1;
}

// What a new child is worth BEFORE anybody has played it out.
//
// This is the second half of the knowledge, and it is worth about as much as
// the playout policy: with three thousand playouts spread over sixty legal
// moves, a child gets fifty samples, and fifty samples cannot tell a good move
// from a bad one. A prior is a head start expressed in the same currency the
// search already speaks -- so many imagined wins out of so many imagined games
// -- so nothing downstream has to know priors exist.
//
// `before` is the position the move was played FROM, so the shape it reads is
// the one the player was looking at.
void seedPrior(Node& node, const Fast& before, const int move, const uint8_t colour, const int lastMove) {
  if (move == kPassIndex) {
    // A pass is worth considering and almost never worth playing, so it gets a
    // small, losing prior rather than none: without one the search wastes its
    // first visits discovering that, every single move.
    node.visits = 6;
    node.score = -6;
    node.raveVisits = 6;
    node.raveScore = -6;
    node.prior = 6;
    return;
  }

  int weight = 0;
  int wins = 0;
  const auto add = [&](const int games, const int won) {
    weight += games;
    wins += won;
  };

  // A local shape the patterns like. The strongest single signal available
  // here, and free: the table has already been consulted for the playouts.
  if (lastMove >= 0 && lastMove < go::kPoints && matchesPattern(before, move, colour)) {
    const int row = go::rowOf(move) - go::rowOf(lastMove);
    const int col = go::colOf(move) - go::colOf(lastMove);
    if (row >= -1 && row <= 1 && col >= -1 && col <= 1) add(16, 16);
  }

  // Taking stones, weighted by how many. Cheap to ask: only the chains touching
  // the move can be captured by it.
  {
    uint8_t around[4];
    const int count = go::neighbours(move, around);
    int taken = 0;
    int rescued = 0;
    for (int i = 0; i < count; ++i) {
      const int next = around[i];
      if (!go::isStone(before.point[next])) continue;
      if (soleLiberty(before, next) != move) continue;
      if (before.point[next] == colour) {
        ++rescued;
      } else {
        ++taken;
      }
    }
    if (taken > 0) add(10 + taken * 4, 10 + taken * 4);
    if (rescued > 0) add(8, 8);
  }

  // Throwing a stone away. Legal, occasionally brilliant, and at this level
  // almost always a mistake -- and the one move that makes a watching human
  // conclude the machine is broken.
  {
    Fast after = before;
    if (playFast(after, move) && soleLiberty(after, move) != go::kNoPoint) add(12, 0);
  }

  // The first line early on. Not wrong, but on a nine by nine it is the last
  // place a game is decided and the search should not spend its samples there.
  const int row = go::rowOf(move);
  const int col = go::colOf(move);
  if (row == 0 || col == 0 || row == go::kSize - 1 || col == go::kSize - 1) add(8, 2);

  if (weight == 0) return;
  node.visits = static_cast<uint16_t>(weight);
  node.score = static_cast<int16_t>(wins * 2 - weight);
  node.raveVisits = node.visits;
  node.raveScore = node.score;
  node.prior = node.visits;
}

uint32_t isqrt(const uint32_t value) {
  uint32_t root = 0;
  uint32_t remainder = value;
  uint32_t place = 1u << 30;
  while (place > remainder) place >>= 2;
  while (place != 0) {
    if (remainder >= root + place) {
      remainder -= root + place;
      root += place << 1;
    }
    root >>= 1;
    place >>= 2;
  }
  return root;
}

// Upper confidence bound blended with the all-moves-as-first estimate, integer
// arithmetic throughout: a device with no FPU pays for every sqrt and the
// search does this once per child per playout.
//
// RAVE is what makes a small tree work. The same move played later in a
// simulation says something about playing it now, so every child learns from
// every simulation its move appeared in rather than only from the ones that
// began with it. The blend leans on RAVE while the real count is small and
// hands over as it grows, on the usual schedule: beta = sqrt(k / (3n + k)).
int selectChild(const int parent) {
  const Node& node = gNodes[parent];
  uint32_t bits = 0;
  for (uint32_t v = node.visits; v > 1; v >>= 1) ++bits;
  const uint32_t lnVisits = bits * 693 / 1000 + 1;

  int best = -1;
  int32_t bestValue = -0x7FFFFFFF;
  for (int child = node.firstChild; child != -1; child = gNodes[child].nextSibling) {
    const Node& kid = gNodes[child];
    if (kid.visits == 0 && kid.raveVisits == 0) return child;

    // Both rates are in thousandths of a win, doubled: -1 to +1 becomes 0 to
    // 2000, so one point of winrate is twenty units and the exploration
    // constant below is in the same currency.
    const int32_t rate = kid.visits > 0 ? (kid.score * 1000) / static_cast<int32_t>(kid.visits) + 1000 : 1000;
    const int32_t raveRate =
        kid.raveVisits > 0 ? (kid.raveScore * 1000) / static_cast<int32_t>(kid.raveVisits) + 1000 : 1000;

    constexpr uint32_t kRaveEquivalence = 1000;
    const uint32_t beta =
        kid.raveVisits == 0 ? 0 : isqrt(kRaveEquivalence * 1000000u / (3u * kid.visits + kRaveEquivalence)) / 1;
    // beta is now sqrt(k / (3n + k)) * 1000, clamped by construction to 1000.
    const int32_t blended = static_cast<int32_t>(
        (static_cast<int64_t>(beta) * raveRate + static_cast<int64_t>(1000 - static_cast<int32_t>(beta)) * rate) /
        1000);

    const uint32_t denominator = kid.visits > 0 ? kid.visits : 1;
    const int32_t bonus = static_cast<int32_t>(1400 * isqrt(lnVisits * 1000 / denominator) / 32);
    const int32_t value = blended + bonus;
    if (value > bestValue) {
      bestValue = value;
      best = child;
    }
  }
  return best;
}

}  // namespace

Settings settingsFor(const go::Level level) {
  switch (level) {
    case go::Level::Easy:
      // Two stones and sixty percent blind. The stones do the arithmetic; the
      // blindness does the character.
      return Settings{1200, 2, 1, 600};
    case go::Level::Medium:
      return Settings{3000, 0, go::kDefaultKomiHalves, 0};
    case go::Level::Hard:
      return Settings{8000, 0, go::kDefaultKomiHalves, 0};
    case go::Level::Count_:
      break;
  }
  return Settings{3000, 0, go::kDefaultKomiHalves, 0};
}

void openingFor(const go::Level level, int& handicap, int16_t& komiHalves) {
  const Settings settings = settingsFor(level);
  handicap = settings.handicap;
  komiHalves = settings.komiHalves;
}

int playoutOnce(const go::Game& game, uint32_t& seed, const bool policy) {
  Fast fast;
  adopt(fast, game);
  return runPlayout(fast, seed, policy, -1, nullptr);
}

bool fastPlayForTest(go::Game& game, const int point) {
  Fast fast;
  adopt(fast, game);
  const bool played = playFast(fast, point == go::kPass ? kPassIndex : point);
  for (int i = 0; i < go::kPoints; ++i) game.point[i] = fast.point[i];
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

namespace {

// Whether this move is one a blinded search must look at anyway.
//
// KaTrain floors its blind bot the same way, and the reason is what "blind"
// must not become: a bot that cannot see a capture in front of it is not a
// beginner, it is broken. A beginner misses things ACROSS the board, which is
// what the random exclusion models, and notices what is under their hand.
bool mustConsider(const go::Game& game, const int point, const uint8_t colour) {
  // Takes two or more stones.
  go::Game after = game;
  if (!go::play(after, point)) return false;
  const int taken = after.capturedBy[colour] - game.capturedBy[colour];
  if (taken >= 2) return true;
  // Saves one of ours that was down to its last liberty.
  uint8_t around[4];
  const int count = go::neighbours(point, around);
  for (int i = 0; i < count; ++i) {
    if (game.point[around[i]] != colour) continue;
    uint8_t stones[kMaskBytes];
    int size = 0;
    int liberties = 0;
    go::group(game, around[i], stones, size, liberties);
    if (liberties == 1) return true;
  }
  return false;
}

}  // namespace

int chooseMove(const go::Game& game, const go::Level level, uint32_t& seed) {
  const Settings settings = settingsFor(level);
  const uint8_t colour = game.toMove;

  // What this turn is allowed to look at. Chosen fresh every move, so the bot is
  // blind to a different part of the board each time -- a beginner who misses a
  // corner and then notices it, rather than one who never looks there.
  bool blind[go::kPoints] = {};
  if (settings.blindPerMille > 0) {
    int legal = 0;
    for (int point = 0; point < go::kPoints; ++point) {
      if (go::legal(game, point, colour) && !go::isEye(game, point, colour)) ++legal;
    }
    // A floor, because blinding nearly everything late in a game leaves one
    // arbitrary point and the bot stops looking like it is playing at all.
    const int keep = legal * (1000 - settings.blindPerMille) / 1000;
    const int wanted = keep < 6 ? 6 : keep;
    if (wanted < legal) {
      for (int point = 0; point < go::kPoints; ++point) {
        if (!go::legal(game, point, colour) || go::isEye(game, point, colour)) continue;
        blind[point] = (nextRandom(seed) % 1000) < settings.blindPerMille;
        if (blind[point] && mustConsider(game, point, colour)) blind[point] = false;
      }
      // The floor has to be ENFORCED, not computed and admired. Each point is
      // blinded independently, so with eight legal moves and six in ten blinded
      // there is a one in sixty chance of blinding all of them -- and a root
      // whose only surviving child is PASS passes a game it is winning. That is
      // exactly the failure this whole mechanism exists to avoid, and it showed
      // up as twenty-three passes in a suite that plays four thousand moves.
      int survivors = 0;
      for (int point = 0; point < go::kPoints; ++point) {
        if (!blind[point] && go::legal(game, point, colour) && !go::isEye(game, point, colour)) ++survivors;
      }
      while (survivors < wanted) {
        const int point = static_cast<int>(nextRandom(seed) % go::kPoints);
        if (!blind[point]) continue;
        if (!go::legal(game, point, colour) || go::isEye(game, point, colour)) continue;
        blind[point] = false;
        ++survivors;
      }
    }
  }

  Fast root;
  adopt(root, game);

  gNodeCount = 0;
  const int rootIndex = newNode(go::kPass);
  if (rootIndex < 0) return go::kPass;

  for (uint16_t playout = 0; playout < settings.playouts; ++playout) {
    Fast fast = root;
    int path[go::kPoints];
    int depth = 0;
    int node = rootIndex;
    int lastMove = game.lastMove < go::kPoints ? game.lastMove : -1;
    path[depth++] = node;

    while (true) {
      if (gNodes[node].cursor < kCandidates) {
        // The position BEFORE the move, so the prior reads the shape the player
        // was actually looking at rather than the one their own stone made.
        const Fast before = fast;
        const uint8_t mover = fast.toMove;
        const int move = expandOne(gNodes[node], fast, node == rootIndex ? blind : nullptr);
        if (move < 0) break;
        const int child = newNode(static_cast<uint8_t>(move));
        if (child < 0) break;
        seedPrior(gNodes[child], before, move, mover, lastMove);
        gNodes[child].nextSibling = gNodes[node].firstChild;
        gNodes[node].firstChild = static_cast<int16_t>(child);
        node = child;
        path[depth++] = node;
        if (move != kPassIndex) lastMove = move;
        break;
      }
      const int child = selectChild(node);
      if (child < 0) break;
      playFast(fast, gNodes[child].move);
      if (gNodes[child].move != kPassIndex) lastMove = gNodes[child].move;
      node = child;
      path[depth++] = node;
      if (depth >= go::kPoints - 1) break;
    }

    uint8_t played[3][kMaskBytes] = {};
    // The moves taken through the TREE belong in the set too. They are moves
    // this simulation played, and leaving them out means a child never learns
    // from a simulation that went through its own sibling.
    for (int i = 1; i < depth; ++i) {
      const uint8_t mover = (i % 2 == 1) ? colour : go::other(colour);
      if (gNodes[path[i]].move < go::kPoints) go::mark(played[mover], gNodes[path[i]].move);
    }

    const int result = runPlayout(fast, seed, true, lastMove, played);
    // `result` is from Black's side. A node's score is kept from the point of
    // view of whoever played its move, which is the opposite of whoever is to
    // move in it -- getting that inversion wrong makes an engine that plays its
    // opponent's best move, and it looks exactly like a weak engine.
    for (int i = 1; i < depth; ++i) {
      Node& visited = gNodes[path[i]];
      ++visited.visits;
      const uint8_t mover = (i % 2 == 1) ? colour : go::other(colour);
      const int forMover = mover == go::kBlack ? result : -result;
      visited.score = static_cast<int16_t>(visited.score + (forMover > 0 ? 1 : (forMover < 0 ? -1 : 0)));
    }
    ++gNodes[rootIndex].visits;

    // All moves as first. In every node the simulation passed through, any
    // child whose move was played LATER by the same side is credited with the
    // same result: the assumption is that a good move is good whenever it is
    // played, which is wrong in detail and right often enough to be worth two
    // hundred and fifty Elo on a tree this shallow.
    for (int i = 0; i < depth; ++i) {
      const uint8_t toMove = (i % 2 == 0) ? colour : go::other(colour);
      const int forMover = toMove == go::kBlack ? result : -result;
      const int16_t delta = static_cast<int16_t>(forMover > 0 ? 1 : (forMover < 0 ? -1 : 0));
      for (int child = gNodes[path[i]].firstChild; child != -1; child = gNodes[child].nextSibling) {
        Node& kid = gNodes[child];
        if (kid.move >= go::kPoints) continue;
        if (!go::marked(played[toMove], kid.move)) continue;
        ++kid.raveVisits;
        kid.raveScore = static_cast<int16_t>(kid.raveScore + delta);
      }
    }
  }

  // The most VISITED child, not the best rate: a move tried twice and won twice
  // is not better than one tried four thousand times and won sixty percent of
  // them, and picking by rate is how a search throws its own work away.
  // Real visits, with the prior taken back out. A move the search actually
  // looked at four hundred times beats one it imagined twenty-four wins for and
  // then looked at thirty.
  const auto realVisits = [](const int child) {
    const Node& kid = gNodes[child];
    return kid.visits > kid.prior ? static_cast<int>(kid.visits - kid.prior) : 0;
  };
  int best = -1;
  int bestVisits = -1;
  for (int child = gNodes[rootIndex].firstChild; child != -1; child = gNodes[child].nextSibling) {
    if (realVisits(child) <= bestVisits) continue;
    bestVisits = realVisits(child);
    best = child;
  }
  if (best < 0) return go::kPass;

  // Medium does not always play its best move. It plays a move it thought hard
  // about, chosen among the ones it thought hardest about, weighted by how hard.
  // That is a club player not concentrating, and it is a different KIND of
  // weakness from Easy's -- which is the whole point of having three levels
  // rather than one dial.
  //
  // A floor of half the top move's visits is what keeps it from ever picking
  // something it barely looked at: sampling a full softmax with no floor is the
  // mistake that turns "not concentrating" into "occasionally insane".
  if (level == go::Level::Medium && bestVisits > 4) {
    const int floorVisits = bestVisits / 2;
    uint32_t total = 0;
    for (int child = gNodes[rootIndex].firstChild; child != -1; child = gNodes[child].nextSibling) {
      if (realVisits(child) >= floorVisits) total += static_cast<uint32_t>(realVisits(child));
    }
    if (total > 0) {
      uint32_t ticket = nextRandom(seed) % total;
      for (int child = gNodes[rootIndex].firstChild; child != -1; child = gNodes[child].nextSibling) {
        if (realVisits(child) < floorVisits) continue;
        if (ticket < static_cast<uint32_t>(realVisits(child))) {
          best = child;
          break;
        }
        ticket -= static_cast<uint32_t>(realVisits(child));
      }
    }
  }

  int chosen = gNodes[best].move;

  // The best move that is neither a pass nor something the real rules refuse.
  // Used by both guards below, because both of them used to fall back to PASS
  // and both were therefore ways to hand over a winning game.
  const auto bestPlayable = [&]() {
    int found = -1;
    int foundVisits = -1;
    for (int child = gNodes[rootIndex].firstChild; child != -1; child = gNodes[child].nextSibling) {
      if (gNodes[child].move == kPassIndex) continue;
      if (realVisits(child) <= foundVisits) continue;
      if (!go::legal(game, gNodes[child].move, colour)) continue;
      foundVisits = realVisits(child);
      found = gNodes[child].move;
    }
    if (found >= 0) return found;
    // The tree may have nothing usable at all, so fall back to the rules
    // themselves rather than to a pass: any legal move that is not our own eye
    // beats resigning a game by accident.
    for (int point = 0; point < go::kPoints; ++point) {
      if (go::legal(game, point, colour) && !go::isEye(game, point, colour)) return point;
    }
    return -1;
  };

  // The Leela Zero pass rule. The search will happily pass out a position that
  // merely LOOKS finished, so passing is only allowed when passing actually
  // wins. Without this the engine hands over won games, which is the loudest
  // "this is broken" signal a Go program can emit.
  if (chosen == kPassIndex && !passingWins(game, colour)) {
    const int alternative = bestPlayable();
    if (alternative >= 0) chosen = alternative;
  }

  // Whatever the search or the dice produced has to survive the real rules: the
  // search knows simple ko and the game knows superko, and the one move a year
  // where they differ must not reach the board.
  if (chosen != kPassIndex && !go::legal(game, chosen, colour)) {
    const int alternative = bestPlayable();
    return alternative >= 0 ? alternative : go::kPass;
  }
  return chosen == kPassIndex ? go::kPass : chosen;
}

void estimateDead(const go::Game& game, uint32_t& seed, uint8_t out[kMaskBytes]) {
  go::clearMask(out);

  // How often each point ends up BLACK'S at the end of a playout from here --
  // the owner map, not the stones. The difference is the whole correctness of
  // this function: a dead group is captured during the playout, so the points
  // it stood on end EMPTY, and asking which stone is there answers nobody.
  // Asking who the region belongs to answers the captor.
  int16_t blackness[go::kPoints] = {};
  constexpr int kTrials = 200;
  for (int trial = 0; trial < kTrials; ++trial) {
    Fast fast;
    adopt(fast, game);
    // Both sides pass at the end of a real game, and a playout that inherits
    // those passes stops immediately. Clear them: the question here is what
    // happens if play CONTINUES.
    fast.passes = 0;
    constexpr int kMaxMoves = go::kPoints * 2 + 20;
    int last = -1;
    for (int move = 0; move < kMaxMoves && fast.passes < 2; ++move) last = playoutMove(fast, seed, true, last);

    uint8_t owner[go::kPoints];
    fastOwner(fast, owner);
    for (int point = 0; point < go::kPoints; ++point) {
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
  for (int point = 0; point < go::kPoints; ++point) {
    const uint8_t here = game.point[point];
    if (!go::isStone(here) || go::marked(judged, point)) continue;

    uint8_t stones[kMaskBytes];
    int size = 0;
    int liberties = 0;
    go::group(game, point, stones, size, liberties);

    int32_t total = 0;
    for (int p = 0; p < go::kPoints; ++p) {
      if (!go::marked(stones, p)) continue;
      go::mark(judged, p);
      total += blackness[p];
    }
    const int32_t average = size > 0 ? total / size : 0;

    const bool dead = here == go::kBlack ? average <= -kThreshold : average >= kThreshold;
    if (!dead) continue;
    for (int p = 0; p < go::kPoints; ++p) {
      if (go::marked(stones, p)) go::mark(out, p);
    }
  }
}

}  // namespace goengine
