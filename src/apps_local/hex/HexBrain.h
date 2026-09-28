#pragma once

// The opponent. UCT/MCTS over `hex::Game`, freestanding and deterministic: no
// heap, no clock of its own, no Arduino, and randomness only through the
// `uint32_t& seed` the caller lends. The same position, level and seed returns
// the same move on the device, in the simulator and in the host suite.
//
// **Why Monte Carlo and not a hand-written evaluation.** Hex has no material,
// no mobility and no king safety -- the only thing a position is worth is
// whether the connection can be completed, and every attempt to spell that out
// as a heuristic ends up re-deriving the search. It also has a property no
// other game on this shelf has: a FULL board always has exactly one winner, so
// a playout needs no legality check, no terminal test and no scoring pass. Fill
// every empty cell in a random order, then ask who owns the board. That is the
// whole evaluation, and it is why this fits a microcontroller at all.
//
// **Difficulty scales the POLICY, not just the budget.** Cazenave and Saffidine
// measured the bridge pattern in the playout policy at about +105 Elo over
// naive UCT, and AMAF/RAVE on top of it (with UCT exploration switched off) at
// a further +181 Elo -- together roughly what a 250-fold increase in compute
// buys. The gain is affordable here and the compute is not, so:
//
//   EASY    plain UCT, plain playouts, a small budget. Still takes an immediate
//           win and blocks an immediate loss at the root, so it never looks
//           broken -- a machine that walks past a winning move is not "easy",
//           it is wrong.
//   NORMAL  bridge-aware playouts.
//   HARD    bridge playouts plus RAVE, exploration off, the whole budget.
//
// Virtual connections and H-search are the next tier (MoHex territory) and are
// deliberately out of scope: they need a solver and a pattern database, and
// what they buy over this is invisible to anybody who is not already a Hex
// player.
//
// **The node pool belongs to the caller.** Forty-nine kilobytes is not a stack
// object and not something to allocate per move: the Activity takes one in
// onEnter() and frees it in onExit(), and the host tests put one on the heap of
// their own. That also makes the search re-entrant by construction -- it holds
// no state between calls except the two counters below, which are reporting.

#include <cstdint>

#include "HexCore.h"
#include "HexFlow.h"

namespace hexbrain {

// A millisecond clock, lent by the caller. The brain cannot call millis() and
// must not: a null clock means "bounded by simulations alone", which is what
// the host tests pass so their results do not depend on how fast the machine
// running them happens to be.
using Clock = uint32_t (*)();

// What one level asks of the search.
struct Settings {
  // Tree descents, each ending in one playout to a full board.
  uint16_t simulations;
  // The wall clock it may spend, whatever the count says. Whichever binds
  // first wins: the count keeps the tests deterministic (they lend no clock),
  // the clock keeps the device under five seconds a move.
  uint16_t budgetMs;
  // Bridge-aware playouts: when the opponent plays into one of your bridges,
  // answer in the other carrier rather than wherever the shuffle pointed next.
  bool bridge;
  // RAVE/AMAF at selection, with UCT exploration off -- the pairing the Elo
  // measurement was made with. One without the other is not this level.
  bool amaf;
};

Settings settingsFor(hex::Level level);

// One node of the search tree. Children are a linked list rather than an array
// of 121 slots: a node's children are created one per simulation, so a list
// costs two bytes a node against 242 for a dense table that is almost entirely
// empty at these budgets.
struct Node {
  uint32_t visits;
  // Wins for the player who MOVED INTO this node, which is the only frame in
  // which a child's score is comparable with its siblings'.
  uint32_t wins;
  uint32_t raveVisits;
  uint32_t raveWins;
  uint16_t firstChild;
  uint16_t nextSibling;
  uint8_t move;
  uint8_t childCount;
  uint16_t reserved;
};

// Two thousand nodes is a real tree rather than a root table: one node is
// created per simulation, so the search deepens along whatever line it keeps
// coming back to, and once the pool is full the remaining simulations sharpen
// the statistics it already has. Raising it costs 24 bytes a node of the
// activity's allocation and buys very little at these budgets.
constexpr int kPoolNodes = 2048;
constexpr uint16_t kNoNode = 0xFFFFu;

struct Pool {
  Node node[kPoolNodes];
  int used;
};

// The move this seat wants, as a cell index, or `hex::kNoCell` when there is no
// legal move at all. Always legal in `game`.
int chooseMove(const hex::Game& game, hex::Level level, uint32_t& seed, Pool& pool, Clock clock = nullptr);

// The same search with the settings spelled out, which is how the suite plays
// three strengths against each other at budgets a laptop can afford without
// pretending they are the shipped ones.
int chooseMoveWith(const hex::Game& game, const Settings& settings, uint32_t& seed, Pool& pool, Clock clock = nullptr);

// What the last search actually cost: simulations run, and milliseconds spent.
// The budget is a promise; these two are what happened, and the device log
// prints them after every move so the promise is answerable to a measurement.
int lastSimulations();
uint32_t lastMs();

// Exposed for the suite. One playout from `board` with `toMove` to play,
// filling every empty cell; returns the colour that owns the finished board.
// `board` is one byte a cell and is left holding the completed position, which
// is also what the RAVE update reads.
uint8_t playoutForTest(uint8_t board[hex::kCells], uint8_t toMove, uint32_t& seed, bool bridge);

// Exposed for the suite: who owns a finished board. The Hex theorem says this
// is never "nobody", and the suite asserts it over random fills.
uint8_t winnerOfFilledForTest(const uint8_t board[hex::kCells]);

// Exposed for the suite: the hand-rolled natural logarithm the UCT term is
// built on, checked against std::log across the visit counts a real search
// reaches. It exists because std::log carries no correctly-rounded guarantee
// and two libms disagreeing in the last bit would make one seed pick different
// moves on a laptop and on the chip -- and a replacement for a library function
// that nothing compares against is a replacement that can quietly return zero
// and leave every assertion in the suite green.
double naturalLogForTest(uint32_t value);

}  // namespace hexbrain
