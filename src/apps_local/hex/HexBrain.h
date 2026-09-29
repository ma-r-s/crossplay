#pragma once

// The opponent. Monte Carlo tree search over `hex::Game`, freestanding and
// deterministic: no heap inside a search, no clock of its own, no Arduino, and
// randomness only through the `uint32_t& seed` the caller lends. The same
// position, settings, seed and tree return the same move on the device, in the
// simulator and in the host suite.
//
// **Why Monte Carlo.** A full Hex board always has exactly one winner, so a
// playout needs no legality check and no scoring pass: fill every empty cell,
// then ask who connected. That is the whole evaluation.
//
// **What the search is, and why each part is there** (docs/apps/hex.md has the
// measurements; each was played against morat, an open-source Hex engine, at
// equal simulations):
//
//   - RAVE-guided selection among ALL of a node's children. A node grows its
//     children together, once it has been visited `expandVisits` times, and
//     its all-moves-as-first statistics pick among them from the first visit:
//     a bad move is never played just so it can be measured. The first version
//     of this engine tried every child once, in random order, before the
//     statistics had any say; the same engine with this change won 40 of 40
//     against it at a quarter of its simulations.
//   - No exploration term. alpha = rf / (rf + visits) moves a child from its
//     all-moves-as-first score to its own; with exploration off (MoHex's own
//     finding) there is no log and no square root in the hot loop, which
//     matters on a chip with a single-precision FPU and no double hardware.
//   - Bridge-aware playouts: a stone played into one carrier of an opponent's
//     bridge is answered in the other. Worth more than anything but RAVE.
//   - Tree reuse. After our move and their reply, the subtree for the position
//     that arose is kept rather than grown again.
//   - An early stop: the search ends as soon as the second-best move at the
//     root cannot catch the best in what is left of the budget. The move is the
//     one the full search would have played; the time is not spent.
//
// **Difficulty is the budget and the final choice, not a weaker algorithm.**
// Every level runs the same search. EASY and NORMAL spend fewer simulations
// and choose among the root's good moves by visits^z (Wu et al., AAAI 2019),
// never among moves the search barely looked at, so a weaker level makes the
// mistakes of a player who saw less rather than random ones.
//
// **The node pool belongs to the caller.** It is megabytes on the device and
// lives in PSRAM; the activity takes it in onEnter() and frees it in onExit().
// When it fills, the tree stops growing and the search continues on the tree it
// has.

#include <cstdint>

#include "HexCore.h"
#include "HexFlow.h"

namespace hexbrain {

// A millisecond clock, lent by the caller. Null means "bounded by simulations
// alone", which is what the host tests pass so their results do not depend on
// the machine running them.
using Clock = uint32_t (*)();

struct Node {
  uint32_t visits;
  // Wins for the player who moved INTO this node, which is the frame in which
  // siblings compare.
  uint32_t wins;
  uint32_t raveVisits;
  uint32_t raveWins;
  // The first of `childCount` contiguous children, or kLeaf.
  uint32_t firstChild;
  uint8_t move;
  uint8_t childCount;
  uint16_t reserved;
};
constexpr uint32_t kLeaf = 0xFFFFFFFFu;

// About four nodes a simulation at expandVisits 10, measured on the laptop, so
// two megabytes holds a twenty-thousand-simulation tree.
constexpr uint32_t kPoolNodes = 2u * 1024u * 1024u / sizeof(Node);

struct Settings {
  uint32_t simulations = 8000;
  // The wall clock a move may take, whichever binds first. 0: no clock.
  uint32_t budgetMs = 0;
  uint16_t expandVisits = 10;
  float raveFactor = 500.0f;
  bool bridge = true;
  bool reuseTree = true;
  bool earlyStop = true;
  // The final choice. 0: the most visited child. Above 0: child i with
  // probability visits_i^z, among children with at least `ratio` of the most
  // visited one's visits.
  float softmaxZ = 0.0f;
  float ratio = 0.1f;
};

Settings settingsFor(hex::Level level);

// What the last search actually did. The device log prints it after every
// move, so a level's budget is answerable to a measurement.
struct Stats {
  uint32_t simulations = 0;
  uint32_t ms = 0;
  uint32_t nodes = 0;
  uint32_t reused = 0;
  bool stoppedEarly = false;
};

class Search {
 public:
  Search() = default;
  Search(Node* pool, uint32_t capacity) : pool_(pool), capacity_(capacity) {}

  // The move for `game.toMove`: always legal, kNoCell only when there is none.
  // A win on the board is taken and a win for them blocked before any search.
  // With no pool, the empty cell nearest the centre.
  int choose(const hex::Game& game, const Settings& settings, uint32_t& seed, Clock clock = nullptr);

  // Forget the tree: a new game, a loaded one, a match, a different level.
  void reset() {
    used_ = 0;
    haveRoot_ = false;
  }

  const Stats& stats() const { return stats_; }

 private:
  bool reroot(const hex::Game& game);
  uint32_t allocate(uint32_t count);
  void expand(uint32_t node, const uint8_t board[hex::kCells]);
  uint32_t select(const Node& parent, float raveFactor) const;

  Node* pool_ = nullptr;
  uint32_t capacity_ = 0;
  uint32_t used_ = 0;
  uint32_t root_ = 0;
  bool haveRoot_ = false;
  uint8_t rootCells_[hex::kCells] = {};
  uint8_t rootToMove_ = hex::kBlack;
  Stats stats_;
};

// Exposed for the suite. One playout from `board` with `toMove` to play,
// filling every empty cell; returns the colour that owns the finished board.
uint8_t playoutForTest(uint8_t board[hex::kCells], uint8_t toMove, uint32_t& seed, bool bridge);

// Exposed for the suite: the child of `parent` (whose children live in `pool`)
// that the search would descend into.
uint32_t selectForTest(const Node* pool, const Node& parent, float raveFactor);

// Exposed for the suite: who owns a finished board. The Hex theorem says this
// is never "nobody", and the suite asserts it over random fills.
uint8_t winnerOfFilledForTest(const uint8_t board[hex::kCells]);

}  // namespace hexbrain
