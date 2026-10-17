#include "HexCore.h"

namespace hex {
namespace {

// Union by index rather than by rank: the smaller node wins, so the four border
// nodes -- which are the largest indices -- never become roots, and a forest
// that crossed the wire has one fewer way to be surprising. The trees stay
// shallow anyway, because every union here is between neighbours on a board
// that only ever grows.
void join(Game& game, const int a, const int b) {
  const int rootA = find(game, a);
  const int rootB = find(game, b);
  if (rootA == rootB) return;
  if (rootA < rootB) {
    game.parent[rootB] = static_cast<uint8_t>(rootA);
  } else {
    game.parent[rootA] = static_cast<uint8_t>(rootB);
  }
}

// Whether `cell` sits on `colour`'s first or second border, and which node that
// is. Black owns the top and bottom rows, White the left and right columns.
int borderNode(const int cell, const uint8_t colour, const bool second) {
  if (colour == kBlack) {
    if (!second && rowOf(cell) == 0) return kTopNode;
    if (second && rowOf(cell) == kSize - 1) return kBottomNode;
    return -1;
  }
  if (!second && colOf(cell) == 0) return kLeftNode;
  if (second && colOf(cell) == kSize - 1) return kRightNode;
  return -1;
}

}  // namespace

void reset(Game& game) {
  for (int i = 0; i < kCellBytes; ++i) game.cell[i] = 0;
  for (int i = 0; i < kNodes; ++i) game.parent[i] = static_cast<uint8_t>(i);
  game.toMove = kBlack;
  game.winner = kEmpty;
  game.lastMove = kNoCell;
  game.reserved = 0;
  game.moveNumber = 0;
}

int find(const Game& game, int node) {
  if (node < 0 || node >= kNodes) return -1;
  // Bounded. The forest is bytes from a packet or a card, and a cycle in it
  // would otherwise hang whichever task asked.
  for (int steps = 0; steps < kNodes; ++steps) {
    const int up = game.parent[node];
    if (up == node) return node;
    // A parent past the end is a forest this build did not write: stop where we
    // are rather than indexing off the array. Only the upper bound is checked,
    // because `parent` is unsigned and a negative one cannot be represented.
    if (up >= kNodes) return node;
    node = up;
  }
  return node;
}

bool connected(const Game& game, const int a, const int b) {
  const int rootA = find(game, a);
  return rootA >= 0 && rootA == find(game, b);
}

bool legal(const Game& game, const int cell) {
  if (!validCell(cell)) return false;
  if (over(game)) return false;
  return game.at(cell) == kEmpty;
}

bool play(Game& game, const int cell) {
  if (!legal(game, cell)) return false;
  const uint8_t colour = game.toMove;
  game.put(cell, colour);

  for (int dir = 0; dir < 6; ++dir) {
    const int next = neighbour(cell, dir);
    if (next == kNoCell) continue;
    if (game.at(next) != colour) continue;
    join(game, cell, next);
  }
  const int first = borderNode(cell, colour, false);
  if (first >= 0) join(game, cell, first);
  const int second = borderNode(cell, colour, true);
  if (second >= 0) join(game, cell, second);

  game.lastMove = static_cast<uint8_t>(cell);
  ++game.moveNumber;
  if (colour == kBlack ? connected(game, kTopNode, kBottomNode) : connected(game, kLeftNode, kRightNode)) {
    game.winner = colour;
    // The turn stays where it is once the game is over. Handing it on would
    // make `toMove` mean "who would have played next", which is a second
    // meaning for one field and the sort of thing a screen reads by accident.
    return true;
  }
  game.toMove = other(colour);
  return true;
}

bool full(const Game& game) {
  for (int cell = 0; cell < kCells; ++cell) {
    if (game.at(cell) == kEmpty) return false;
  }
  return true;
}

bool winsImmediately(const Game& game, const int cell, const uint8_t colour) {
  if (!validCell(cell) || game.at(cell) != kEmpty) return false;
  // A copy rather than a play-and-undo: the union-find has no undo, and a
  // second code path that unpicks it is exactly the sort of thing that would
  // only be wrong in the position nobody tested.
  Game probe = game;
  probe.winner = kEmpty;
  probe.toMove = colour;
  if (!play(probe, cell)) return false;
  return probe.winner == colour;
}

bool winningChain(const Game& game, uint8_t out[kMaskBytes]) {
  for (int i = 0; i < kMaskBytes; ++i) out[i] = 0;
  const uint8_t colour = game.winner;
  if (!isStone(colour)) return false;

  // Breadth-first from every one of the winner's stones on their first border,
  // so what comes back is a SHORTEST connection rather than the whole group.
  uint8_t previous[kCells];
  uint8_t queue[kCells];
  for (int i = 0; i < kCells; ++i) previous[i] = kNoCell;

  int head = 0;
  int tail = 0;
  for (int i = 0; i < kSize; ++i) {
    const int cell = colour == kBlack ? cellAt(0, i) : cellAt(i, 0);
    if (game.at(cell) != colour) continue;
    previous[cell] = static_cast<uint8_t>(cell);
    queue[tail++] = static_cast<uint8_t>(cell);
  }

  int reached = -1;
  while (head < tail && reached < 0) {
    const int cell = queue[head++];
    const bool atFarBorder = colour == kBlack ? rowOf(cell) == kSize - 1 : colOf(cell) == kSize - 1;
    if (atFarBorder) {
      reached = cell;
      break;
    }
    for (int dir = 0; dir < 6; ++dir) {
      const int next = neighbour(cell, dir);
      if (next == kNoCell) continue;
      if (game.at(next) != colour) continue;
      if (previous[next] != kNoCell) continue;
      previous[next] = static_cast<uint8_t>(cell);
      queue[tail++] = static_cast<uint8_t>(next);
    }
  }
  if (reached < 0) return false;

  for (int cell = reached;; cell = previous[cell]) {
    out[cell / 8] = static_cast<uint8_t>(out[cell / 8] | (1u << (cell % 8)));
    if (previous[cell] == cell) break;
  }
  return true;
}

}  // namespace hex
