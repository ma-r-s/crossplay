#include "HexBrain.h"

#include <cmath>

namespace hexbrain {
namespace {

int gLastSimulations = 0;
uint32_t gLastMs = 0;

// xorshift32. Every random number in this file comes through here, from the
// caller's seed, so a search is reproducible by handing it the same one.
uint32_t nextRandom(uint32_t& seed) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

uint32_t below(uint32_t& seed, const uint32_t limit) { return limit == 0 ? 0 : nextRandom(seed) % limit; }

// Natural log, from an integer fixed-point log2 rather than from libm.
//
// Not caution for its own sake: the UCT term decides which child is explored
// next, and two libms that disagree in the last bit would make the SAME seed
// pick different moves on a laptop and on the chip. std::log gives no
// correctly-rounded guarantee; std::sqrt does (IEEE-754 requires it), so the
// square root stays and only the logarithm is replaced.
double naturalLog(const uint32_t value) {
  if (value < 2) return 0.0;
  int msb = 0;
  for (uint32_t v = value; v > 1; v >>= 1) ++msb;
  // Normalised into [2^31, 2^32), i.e. a mantissa in [1, 2).
  uint64_t mantissa = static_cast<uint64_t>(value) << (31 - msb);
  uint32_t fraction = 0;
  uint32_t bit = 1u << 15;
  for (int i = 0; i < 16; ++i) {
    mantissa = (mantissa * mantissa) >> 31;
    if (mantissa >= (1ull << 32)) {
      mantissa >>= 1;
      fraction += bit;
    }
    bit >>= 1;
  }
  const double log2 = static_cast<double>(msb) + static_cast<double>(fraction) / 65536.0;
  return log2 * 0.6931471805599453;
}

// The UCT exploration constant. 0.6 rather than the textbook sqrt(2) because a
// playout here returns a win or a loss and nothing in between, so the scores it
// is added to are already spread across the whole unit interval.
constexpr double kExploration = 0.6;
// Silver's RAVE schedule: beta = sqrt(k / (3n + k)). At k = 1000 a child stops
// leaning on its all-moves-as-first statistics after a few hundred visits,
// which is about where they stop being better than its own.
constexpr double kRaveEquivalence = 1000.0;

// Whether Black owns a finished board, by flood fill from the top row. Cheaper
// than a second union-find and it needs no per-cell parent array: a playout runs
// tens of thousands of times a move and this is the whole of its scoring.
bool blackConnects(const uint8_t board[hex::kCells]) {
  bool seen[hex::kCells] = {};
  uint8_t stack[hex::kCells];
  int top = 0;
  for (int col = 0; col < hex::kSize; ++col) {
    const int cell = hex::cellAt(0, col);
    if (board[cell] != hex::kBlack) continue;
    seen[cell] = true;
    stack[top++] = static_cast<uint8_t>(cell);
  }
  while (top > 0) {
    const int cell = stack[--top];
    if (hex::rowOf(cell) == hex::kSize - 1) return true;
    for (int dir = 0; dir < 6; ++dir) {
      const int next = hex::neighbour(cell, dir);
      if (next == hex::kNoCell) continue;
      if (board[next] != hex::kBlack || seen[next]) continue;
      seen[next] = true;
      stack[top++] = static_cast<uint8_t>(next);
    }
  }
  return false;
}

// Only ever asked of a FULL board, where the Hex theorem makes "not Black"
// exactly "White" -- no draw to represent and no third answer to handle.
uint8_t winnerOfFilled(const uint8_t board[hex::kCells]) { return blackConnects(board) ? hex::kBlack : hex::kWhite; }

// One playout. Fills every empty cell and answers who owns the result.
//
// Without the bridge policy this is one shuffle and one pass: cell i of the
// shuffled order goes to whoever is to move at step i, and no legality check is
// needed because every empty cell is legal and the board can only end full.
//
// With it the pass stops being straight, and that is the consequence to plan
// for rather than a complication to avoid: a bridge response is REACTIVE, so
// the order has to be walked with a queue beside it. When a stone lands in one
// carrier of an opponent's bridge, the other carrier goes on that opponent's
// queue and is played before the queue's owner takes anything else from the
// shuffle. O(1) a move against the precomputed table, and the saved cell is
// re-checked for emptiness when it is popped, because the bridge may have been
// filled by something else in the meantime.
uint8_t playout(uint8_t board[hex::kCells], uint8_t toMove, uint32_t& seed, const bool bridge) {
  uint8_t order[hex::kCells];
  int count = 0;
  for (int cell = 0; cell < hex::kCells; ++cell) {
    if (board[cell] == hex::kEmpty) order[count++] = static_cast<uint8_t>(cell);
  }
  for (int i = count - 1; i > 0; --i) {
    const uint32_t j = below(seed, static_cast<uint32_t>(i + 1));
    const uint8_t swap = order[i];
    order[i] = order[j];
    order[j] = swap;
  }

  if (!bridge) {
    uint8_t colour = toMove;
    for (int i = 0; i < count; ++i) {
      board[order[i]] = colour;
      colour = hex::other(colour);
    }
    return winnerOfFilled(board);
  }

  // One queue a colour, indexed by the colour value itself so there is no
  // second mapping to keep straight.
  //
  // A placement can push up to six saving cells and a turn pops about one, so
  // the queues grow faster than they drain and an unbounded push would run off
  // the end of a 121-entry array inside the hottest loop in the app. The bound
  // is a DROP rather than a bigger array: a bridge response is a hint, the
  // playout is one random sample of many, and the oldest hints in a queue this
  // deep are about bridges the board has long since filled in.
  constexpr int kSaveDepth = hex::kCells;
  uint8_t saved[3][kSaveDepth];
  int savedCount[3] = {0, 0, 0};
  int head = 0;
  uint8_t colour = toMove;
  for (int placed = 0; placed < count; ++placed) {
    int cell = hex::kNoCell;
    while (savedCount[colour] > 0) {
      const uint8_t candidate = saved[colour][--savedCount[colour]];
      if (board[candidate] == hex::kEmpty) {
        cell = candidate;
        break;
      }
    }
    if (cell == hex::kNoCell) {
      while (head < count && board[order[head]] != hex::kEmpty) ++head;
      if (head >= count) break;
      cell = order[head++];
    }
    board[cell] = colour;

    const uint8_t them = hex::other(colour);
    for (int dir = 0; dir < 6; ++dir) {
      const hex::Bridge& pattern = hex::kBridges.at[cell][dir];
      if (pattern.partner == hex::kNoCell) continue;
      if (board[pattern.end[0]] != them || board[pattern.end[1]] != them) continue;
      if (board[pattern.partner] != hex::kEmpty) continue;
      if (savedCount[them] >= kSaveDepth) break;
      saved[them][savedCount[them]++] = pattern.partner;
    }
    colour = hex::other(colour);
  }
  return winnerOfFilled(board);
}

uint16_t allocate(Pool& pool, const uint8_t move) {
  if (pool.used >= kPoolNodes) return kNoNode;
  const uint16_t index = static_cast<uint16_t>(pool.used++);
  Node& node = pool.node[index];
  node.visits = 0;
  node.wins = 0;
  node.raveVisits = 0;
  node.raveWins = 0;
  node.firstChild = kNoNode;
  node.nextSibling = kNoNode;
  node.move = move;
  node.childCount = 0;
  node.reserved = 0;
  return index;
}

// The child worth descending into. Ties break on the lower pool index, which is
// the order the children were created in, so the whole selection is a total
// order and two runs with one seed cannot diverge on a coin toss.
uint16_t bestChild(const Pool& pool, const Node& parent, const bool amaf) {
  const double logParent = naturalLog(parent.visits);
  uint16_t best = kNoNode;
  double bestValue = 0.0;
  for (uint16_t index = parent.firstChild; index != kNoNode; index = pool.node[index].nextSibling) {
    const Node& child = pool.node[index];
    const double visits = static_cast<double>(child.visits);
    double value = static_cast<double>(child.wins) / visits;
    if (amaf && child.raveVisits > 0) {
      const double beta = std::sqrt(kRaveEquivalence / (3.0 * visits + kRaveEquivalence));
      const double rave = static_cast<double>(child.raveWins) / static_cast<double>(child.raveVisits);
      value = (1.0 - beta) * value + beta * rave;
    } else {
      value += kExploration * std::sqrt(logParent / visits);
    }
    if (best == kNoNode || value > bestValue) {
      best = index;
      bestValue = value;
    }
  }
  return best;
}

// A cell nobody has taken and no child of `parent` already covers. Returns
// kNoCell when every empty cell is already a child, which is what ends the
// expansion phase at a node.
int untriedMove(const Pool& pool, const Node& parent, const uint8_t board[hex::kCells], uint32_t& seed) {
  bool taken[hex::kCells] = {};
  for (uint16_t index = parent.firstChild; index != kNoNode; index = pool.node[index].nextSibling) {
    taken[pool.node[index].move] = true;
  }
  uint8_t choices[hex::kCells];
  int count = 0;
  for (int cell = 0; cell < hex::kCells; ++cell) {
    if (board[cell] == hex::kEmpty && !taken[cell]) choices[count++] = static_cast<uint8_t>(cell);
  }
  if (count == 0) return hex::kNoCell;
  return choices[below(seed, static_cast<uint32_t>(count))];
}

}  // namespace

Settings settingsFor(const hex::Level level) {
  switch (level) {
    case hex::Level::Easy:
      return Settings{1500, 800, false, false};
    case hex::Level::Normal:
      return Settings{8000, 2500, true, false};
    case hex::Level::Hard:
      return Settings{30000, 4500, true, true};
    case hex::Level::Count_:
      break;
  }
  return Settings{8000, 2500, true, false};
}

int chooseMove(const hex::Game& game, const hex::Level level, uint32_t& seed, Pool& pool, const Clock clock) {
  return chooseMoveWith(game, settingsFor(level), seed, pool, clock);
}

int chooseMoveWith(const hex::Game& game, const Settings& settings, uint32_t& seed, Pool& pool, const Clock clock) {
  gLastSimulations = 0;
  gLastMs = 0;
  if (hex::over(game)) return hex::kNoCell;

  const uint8_t me = game.toMove;
  const uint8_t them = hex::other(me);

  uint8_t start[hex::kCells];
  int empties = 0;
  int centreMost = hex::kNoCell;
  int centreDistance = 0;
  for (int cell = 0; cell < hex::kCells; ++cell) {
    start[cell] = game.at(cell);
    if (start[cell] != hex::kEmpty) continue;
    ++empties;
    // The fallback a budget of zero returns, so "out of time before a single
    // playout" is still a sensible move rather than the first empty cell in
    // index order -- which on an empty board is the corner.
    const int middle = hex::kSize / 2;
    const int dr = hex::rowOf(cell) - middle;
    const int dc = hex::colOf(cell) - middle;
    const int distance = dr * dr + dc * dc;
    if (centreMost == hex::kNoCell || distance < centreDistance) {
      centreMost = cell;
      centreDistance = distance;
    }
  }
  if (empties == 0) return hex::kNoCell;

  // Two checks before any search, and they are what stops a level from looking
  // broken. A machine that walks past a winning stone, or lets one be played
  // against it when the answer was the single cell in front of it, reads as a
  // bug rather than as an easy opponent -- and at EASY's budget the search
  // genuinely can miss both.
  for (int cell = 0; cell < hex::kCells; ++cell) {
    if (start[cell] == hex::kEmpty && hex::winsImmediately(game, cell, me)) return cell;
  }
  for (int cell = 0; cell < hex::kCells; ++cell) {
    if (start[cell] == hex::kEmpty && hex::winsImmediately(game, cell, them)) return cell;
  }

  pool.used = 0;
  const uint16_t root = allocate(pool, hex::kNoCell);
  if (root == kNoNode) return centreMost;

  const uint32_t began = clock != nullptr ? clock() : 0;
  uint8_t board[hex::kCells];
  uint16_t path[hex::kCells + 1];
  int simulations = 0;
  for (; simulations < static_cast<int>(settings.simulations); ++simulations) {
    // Asked every sixty-four descents rather than every one: the clock is a
    // function call across a task boundary on the device, and the search is
    // stopped BETWEEN simulations either way, so a coarser check costs
    // milliseconds and buys the budget back in cycles.
    if (clock != nullptr && (simulations & 63) == 0 && simulations > 0) {
      if (clock() - began >= settings.budgetMs) break;
    }

    for (int cell = 0; cell < hex::kCells; ++cell) board[cell] = start[cell];
    uint8_t colour = me;
    int depth = 0;
    path[depth++] = root;
    uint16_t node = root;
    // How many cells are still empty at the node being looked at. Kept rather
    // than recounted, because it is what says whether a node has anything left
    // to expand -- and the honest question, "is there an untried move here",
    // costs a walk of the board and of the sibling list. Asking it at every
    // node of every descent was 121 reads a ply; asking it only where the
    // counts disagree asks it once a simulation.
    int remaining = empties;

    for (;;) {
      const Node& current = pool.node[node];
      if (pool.used < kPoolNodes && current.childCount < remaining) {
        const int untried = untriedMove(pool, current, board, seed);
        if (untried != hex::kNoCell) {
          const uint16_t child = allocate(pool, static_cast<uint8_t>(untried));
          Node& parent = pool.node[node];
          pool.node[child].nextSibling = parent.firstChild;
          parent.firstChild = child;
          ++parent.childCount;
          board[untried] = colour;
          colour = hex::other(colour);
          path[depth++] = child;
          break;
        }
      }
      const uint16_t next = bestChild(pool, current, settings.amaf);
      if (next == kNoNode) break;
      board[pool.node[next].move] = colour;
      colour = hex::other(colour);
      path[depth++] = next;
      node = next;
      --remaining;
    }

    const uint8_t won = playout(board, colour, seed, settings.bridge);

    for (int i = 0; i < depth; ++i) {
      Node& visited = pool.node[path[i]];
      ++visited.visits;
      if (i > 0) {
        // path[1] is a move by `me`, path[2] by them, and so on down.
        const uint8_t mover = (i % 2) == 1 ? me : them;
        if (won == mover) ++visited.wins;
      }
      if (!settings.amaf) continue;
      // All-moves-as-first, read off the FINISHED board rather than off a move
      // list. In Hex a playout fills every cell, so "this player played that
      // cell at some point" and "this player owns that cell at the end" are the
      // same statement -- which is what makes AMAF cheap here and is the form
      // the measured gain was made with.
      const uint8_t toPlay = (i % 2) == 0 ? me : them;
      for (uint16_t index = visited.firstChild; index != kNoNode; index = pool.node[index].nextSibling) {
        Node& child = pool.node[index];
        if (board[child.move] != toPlay) continue;
        ++child.raveVisits;
        if (won == toPlay) ++child.raveWins;
      }
    }
  }

  gLastSimulations = simulations;
  gLastMs = clock != nullptr ? clock() - began : 0;

  // The most VISITED child, not the best scoring one. A child with three visits
  // and three wins scores perfectly and has been looked at three times; visits
  // are what the search actually spent its budget deciding.
  uint16_t best = kNoNode;
  uint32_t bestVisits = 0;
  for (uint16_t index = pool.node[root].firstChild; index != kNoNode; index = pool.node[index].nextSibling) {
    const Node& child = pool.node[index];
    if (best == kNoNode || child.visits > bestVisits) {
      best = index;
      bestVisits = child.visits;
    }
  }
  if (best == kNoNode) return centreMost;
  return pool.node[best].move;
}

int lastSimulations() { return gLastSimulations; }
uint32_t lastMs() { return gLastMs; }

uint8_t playoutForTest(uint8_t board[hex::kCells], const uint8_t toMove, uint32_t& seed, const bool bridge) {
  return playout(board, toMove, seed, bridge);
}

uint8_t winnerOfFilledForTest(const uint8_t board[hex::kCells]) { return winnerOfFilled(board); }

double naturalLogForTest(const uint32_t value) { return naturalLog(value); }

}  // namespace hexbrain
