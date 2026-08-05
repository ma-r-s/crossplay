#pragma once

// Chess opponent: alpha-beta search over ChessCore.
//
// Freestanding like ChessCore, so the whole thing is host-testable. No heap, no
// clock, no RTOS. Search is bounded by a node budget rather than a wall-clock
// deadline: node counts are deterministic, so a test can assert on them and the
// same budget behaves identically on the C3 and the S3.

#include <cstdint>

#include "ChessCore.h"

namespace chess {

// Scores are centipawns from the side-to-move's point of view.
constexpr int kMateScore = 30000;
// Beyond any plausible material score, so "mate in n" always beats material.
constexpr int kMateThreshold = kMateScore - 1000;

// Each ply costs a ~1KB MoveList in SearchBuffers, and this move generator is
// slow enough that depth 4 is already a second or more on a 160MHz C3, so
// there is nothing to gain from a deeper cap.
constexpr int kMaxSearchDepth = 4;

// One MoveList per ply, ~1KB each. Far too big for a stack frame on the C3, so
// the caller owns it: the activity holds one as a member. Handing the search a
// pre-allocated buffer is also what keeps the engine allocation-free.
struct SearchBuffers {
  MoveList plies[kMaxSearchDepth + 1];
};

struct SearchResult {
  Move best{};
  int score = 0;
  uint32_t nodes = 0;
  // True when the node budget stopped the search early. The move is still
  // legal and still the best found so far, just less considered.
  bool budgetExhausted = false;
  bool hasMove = false;
};

// Static evaluation in centipawns, positive when the side to move is better.
int evaluate(const Position& position);

// Searches to `depth` and returns the best move for the side to move.
// `nodeBudget` caps the work; pass 0 for no cap. Returns hasMove == false only
// when the position has no legal moves (checkmate or stalemate).
SearchResult search(Position& position, int depth, SearchBuffers& buffers, uint32_t nodeBudget = 0);

}  // namespace chess
