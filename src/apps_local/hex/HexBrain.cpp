#include "HexBrain.h"

#include <cmath>
#include <cstring>

namespace hexbrain {
namespace {

uint32_t nextRandom(uint32_t& seed) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
uint32_t below(uint32_t& seed, const uint32_t limit) { return limit == 0 ? 0 : nextRandom(seed) % limit; }

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
      if (next == hex::kNoCell || board[next] != hex::kBlack || seen[next]) continue;
      seen[next] = true;
      stack[top++] = static_cast<uint8_t>(next);
    }
  }
  return false;
}

// Fill every empty cell; with `bridge`, a stone played into one carrier of an
// opponent's bridge is answered in the other before the shuffle continues.
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
    return blackConnects(board) ? hex::kBlack : hex::kWhite;
  }
  uint8_t saved[3][hex::kCells];
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
      if (savedCount[them] >= hex::kCells) break;
      saved[them][savedCount[them]++] = pattern.partner;
    }
    colour = them;
  }
  return blackConnects(board) ? hex::kBlack : hex::kWhite;
}

// The move a search that ran out of everything still owes: the empty cell
// nearest the centre, never the first in index order, which is a corner.
int centreMost(const hex::Game& game) {
  int best = hex::kNoCell;
  int bestDistance = 0;
  for (int cell = 0; cell < hex::kCells; ++cell) {
    if (game.at(cell) != hex::kEmpty) continue;
    const int dr = hex::rowOf(cell) - hex::kSize / 2;
    const int dc = hex::colOf(cell) - hex::kSize / 2;
    const int distance = dr * dr + dc * dc;
    if (best == hex::kNoCell || distance < bestDistance) {
      best = cell;
      bestDistance = distance;
    }
  }
  return best;
}

}  // namespace

uint32_t Search::allocate(const uint32_t count) {
  if (used_ + count > capacity_) return kLeaf;
  const uint32_t first = used_;
  used_ += count;
  return first;
}

void Search::expand(const uint32_t node, const uint8_t board[hex::kCells]) {
  uint32_t empties = 0;
  for (int cell = 0; cell < hex::kCells; ++cell) empties += board[cell] == hex::kEmpty ? 1u : 0u;
  if (empties == 0) return;
  const uint32_t first = allocate(empties);
  if (first == kLeaf) return;
  uint32_t k = first;
  for (int cell = 0; cell < hex::kCells; ++cell) {
    if (board[cell] != hex::kEmpty) continue;
    Node& child = pool_[k++];
    child = Node{};
    child.firstChild = kLeaf;
    child.move = static_cast<uint8_t>(cell);
  }
  pool_[node].firstChild = first;
  pool_[node].childCount = static_cast<uint8_t>(empties);
}

// RAVE-guided, no exploration term: alpha = rf / (rf + visits) moves a child
// from its all-moves-as-first score to its own as it gains visits. A child with
// neither is tried first (first-play urgency of 1).
uint32_t Search::select(const Node& parent, const float raveFactor) const {
  uint32_t best = parent.firstChild;
  float bestValue = -1.0f;
  for (uint32_t i = 0; i < parent.childCount; ++i) {
    const Node& child = pool_[parent.firstChild + i];
    float value = 1.0f;
    if (child.visits > 0 || child.raveVisits > 0) {
      const float alpha = raveFactor / (raveFactor + static_cast<float>(child.visits));
      value = 0.0f;
      if (child.raveVisits > 0)
        value += alpha * static_cast<float>(child.raveWins) / static_cast<float>(child.raveVisits);
      if (child.visits > 0) value += (1.0f - alpha) * static_cast<float>(child.wins) / static_cast<float>(child.visits);
    }
    if (value > bestValue) {
      bestValue = value;
      best = parent.firstChild + i;
    }
  }
  return best;
}

// Carry the subtree for the position two plies on -- our last move and the
// reply -- to the front of the pool. Copied breadth-first into the free tail
// (the copy is its own queue), then moved down in one memmove. False when the
// path is not in the tree or the tail cannot hold the subtree.
bool Search::reroot(const hex::Game& game) {
  if (game.toMove != rootToMove_) return false;
  int ours = hex::kNoCell;
  int theirs = hex::kNoCell;
  for (int cell = 0; cell < hex::kCells; ++cell) {
    const uint8_t now = game.at(cell);
    if (now == rootCells_[cell]) continue;
    if (rootCells_[cell] != hex::kEmpty) return false;
    if (now == rootToMove_) {
      if (ours != hex::kNoCell) return false;
      ours = cell;
    } else {
      if (theirs != hex::kNoCell) return false;
      theirs = cell;
    }
  }
  if (ours == hex::kNoCell || theirs == hex::kNoCell) return false;

  const Node& root = pool_[root_];
  if (root.firstChild == kLeaf) return false;
  uint32_t mid = kLeaf;
  for (uint32_t i = 0; i < root.childCount; ++i) {
    if (pool_[root.firstChild + i].move == ours) mid = root.firstChild + i;
  }
  if (mid == kLeaf || pool_[mid].firstChild == kLeaf) return false;
  uint32_t next = kLeaf;
  for (uint32_t i = 0; i < pool_[mid].childCount; ++i) {
    if (pool_[pool_[mid].firstChild + i].move == theirs) next = pool_[mid].firstChild + i;
  }
  if (next == kLeaf) return false;

  const uint32_t base = used_;
  if (base + 1 > capacity_) return false;
  pool_[base] = pool_[next];
  uint32_t end = base + 1;
  for (uint32_t q = base; q < end; ++q) {
    Node& copy = pool_[q];
    if (copy.firstChild == kLeaf) continue;
    if (end + copy.childCount > capacity_) return false;
    std::memcpy(&pool_[end], &pool_[copy.firstChild], copy.childCount * sizeof(Node));
    copy.firstChild = end;
    end += copy.childCount;
  }
  const uint32_t size = end - base;
  std::memmove(pool_, pool_ + base, size * sizeof(Node));
  for (uint32_t i = 0; i < size; ++i) {
    if (pool_[i].firstChild != kLeaf) pool_[i].firstChild -= base;
  }
  used_ = size;
  root_ = 0;
  stats_.reused = size;
  return true;
}

// The three levels, placed on one scale by matches of this engine against
// itself (docs/apps/hex.md has the ladder). Evenly spaced, about 620 Elo apart:
// EASY a little above the first version's EASY, NORMAL a little below its
// HARD, and HARD whatever the chip can search in four and a half seconds.
// EASY and NORMAL cost a fraction of a second: they are weaker because they
// see less and choose more loosely, not because they burn the same time on a
// worse algorithm.
Settings settingsFor(const hex::Level level) {
  Settings s;
  switch (level) {
    case hex::Level::Easy:
      s.simulations = 100;
      s.softmaxZ = 1.0f;
      return s;
    case hex::Level::Normal:
      s.simulations = 200;
      return s;
    case hex::Level::Hard:
    case hex::Level::Count_:
      break;
  }
  s.simulations = 20000;
  s.budgetMs = 4500;
  return s;
}

int Search::choose(const hex::Game& game, const Settings& settings, uint32_t& seed, const Clock clock) {
  stats_ = Stats{};
  if (hex::over(game)) return hex::kNoCell;
  const uint8_t me = game.toMove;
  const uint8_t them = hex::other(me);

  // A win on the board, or the one cell that stops theirs: no search needed.
  for (int cell = 0; cell < hex::kCells; ++cell) {
    if (game.at(cell) == hex::kEmpty && hex::winsImmediately(game, cell, me)) {
      reset();
      return cell;
    }
  }
  for (int cell = 0; cell < hex::kCells; ++cell) {
    if (game.at(cell) == hex::kEmpty && hex::winsImmediately(game, cell, them)) {
      reset();
      return cell;
    }
  }

  if (pool_ == nullptr || capacity_ < hex::kCells + 1) return centreMost(game);
  if (!(settings.reuseTree && haveRoot_ && reroot(game))) {
    used_ = 0;
    root_ = allocate(1);
    pool_[root_] = Node{};
    pool_[root_].firstChild = kLeaf;
    pool_[root_].move = hex::kNoCell;
  }
  for (int cell = 0; cell < hex::kCells; ++cell) rootCells_[cell] = game.at(cell);
  rootToMove_ = me;
  haveRoot_ = true;
  if (pool_[root_].firstChild == kLeaf) expand(root_, rootCells_);
  if (pool_[root_].firstChild == kLeaf) return centreMost(game);

  const uint32_t began = clock != nullptr ? clock() : 0;
  uint8_t board[hex::kCells];
  uint32_t path[hex::kCells + 2];
  uint32_t sims = 0;
  for (; sims < settings.simulations; ++sims) {
    if ((sims & 31) == 0 && sims > 0) {
      uint32_t remaining = settings.simulations - sims;
      if (clock != nullptr && settings.budgetMs > 0) {
        const uint32_t elapsed = clock() - began;
        if (elapsed >= settings.budgetMs) break;
        if (elapsed > 0) {
          const uint64_t byTime = static_cast<uint64_t>(sims) * (settings.budgetMs - elapsed) / elapsed;
          if (byTime < remaining) remaining = static_cast<uint32_t>(byTime);
        }
      }
      if (settings.earlyStop) {
        uint32_t first = 0;
        uint32_t second = 0;
        const Node& root = pool_[root_];
        for (uint32_t i = 0; i < root.childCount; ++i) {
          const uint32_t v = pool_[root.firstChild + i].visits;
          if (v > first) {
            second = first;
            first = v;
          } else if (v > second) {
            second = v;
          }
        }
        if (first - second > remaining) {
          stats_.stoppedEarly = true;
          break;
        }
      }
    }

    std::memcpy(board, rootCells_, sizeof(board));
    uint8_t colour = me;
    int depth = 0;
    uint32_t node = root_;
    path[depth++] = node;
    for (;;) {
      if (pool_[node].firstChild == kLeaf) {
        if (pool_[node].visits < settings.expandVisits) break;
        expand(node, board);
        if (pool_[node].firstChild == kLeaf) break;
      }
      const uint32_t child = select(pool_[node], settings.raveFactor);
      board[pool_[child].move] = colour;
      colour = hex::other(colour);
      node = child;
      path[depth++] = node;
    }

    const uint8_t won = playout(board, colour, seed, settings.bridge);

    for (int i = 0; i < depth; ++i) {
      Node& visited = pool_[path[i]];
      ++visited.visits;
      if (i > 0) {
        const uint8_t mover = (i % 2) == 1 ? me : them;
        if (won == mover) ++visited.wins;
      }
      if (visited.firstChild == kLeaf) continue;
      const uint8_t toPlay = (i % 2) == 0 ? me : them;
      const bool toPlayWon = won == toPlay;
      Node* child = &pool_[visited.firstChild];
      for (uint32_t k = 0; k < visited.childCount; ++k, ++child) {
        if (board[child->move] != toPlay) continue;
        ++child->raveVisits;
        if (toPlayWon) ++child->raveWins;
      }
    }
  }
  stats_.simulations = sims;
  stats_.ms = clock != nullptr ? clock() - began : 0;
  stats_.nodes = used_;

  const Node& root = pool_[root_];
  uint32_t best = root.firstChild;
  for (uint32_t i = 0; i < root.childCount; ++i) {
    if (pool_[root.firstChild + i].visits > pool_[best].visits) best = root.firstChild + i;
  }
  // No simulation reached the root's children (a budget of nothing, or a clock
  // that was already out): the centre, not whichever child is first in index
  // order, which is a corner.
  if (pool_[best].visits == 0) return centreMost(game);
  if (settings.softmaxZ > 0.0f) {
    const float floor = settings.ratio * static_cast<float>(pool_[best].visits);
    float total = 0.0f;
    for (uint32_t i = 0; i < root.childCount; ++i) {
      const Node& c = pool_[root.firstChild + i];
      if (static_cast<float>(c.visits) >= floor && c.visits > 0)
        total += std::pow(static_cast<float>(c.visits), settings.softmaxZ);
    }
    float pick = total * static_cast<float>(nextRandom(seed) & 0xFFFFFF) / 16777216.0f;
    for (uint32_t i = 0; i < root.childCount; ++i) {
      const Node& c = pool_[root.firstChild + i];
      if (static_cast<float>(c.visits) < floor || c.visits == 0) continue;
      pick -= std::pow(static_cast<float>(c.visits), settings.softmaxZ);
      if (pick <= 0.0f) {
        best = root.firstChild + i;
        break;
      }
    }
  }
  return pool_[best].move;
}

}  // namespace hexbrain

namespace hexbrain {

uint8_t playoutForTest(uint8_t board[hex::kCells], const uint8_t toMove, uint32_t& seed, const bool bridge) {
  return playout(board, toMove, seed, bridge);
}

uint8_t winnerOfFilledForTest(const uint8_t board[hex::kCells]) {
  return blackConnects(board) ? hex::kBlack : hex::kWhite;
}

}  // namespace hexbrain
