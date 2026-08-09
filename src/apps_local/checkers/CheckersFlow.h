#pragma once

// Checkers' navigation and its one piece of input state. Freestanding.
//
// Two machines here, unlike Minesweeper's one: this game has turns, and "whose
// turn" is not a fact the rules can own alone once a link match is involved --
// the link layer decides when their move has actually arrived. A solitaire
// needs only the shell.
//
// The third thing this file owns is the SELECTION, which is neither: picking a
// piece and then a destination is two taps, so there is a state between them
// that belongs to nobody else. Leaving it in the activity is how it ends up
// disagreeing with the board.

#include <cstdint>

#include "CheckersCore.h"

namespace checkers {

enum class Screen : uint8_t {
  // The top. Back from here leaves the app, and it is the only screen that does.
  Menu,
  HowTo,
  Board,
  Result,
};

enum class Phase : uint8_t { Yours, Theirs, Finished };

constexpr Screen back(const Screen screen) {
  switch (screen) {
    case Screen::Menu:
      return Screen::Menu;
    case Screen::HowTo:
      return Screen::Menu;
    case Screen::Board:
      // The first Back stops playing, the second leaves. Back meaning two
      // different things depending on depth is how players get lost.
      return Screen::Menu;
    case Screen::Result:
      return Screen::Menu;
  }
  return Screen::Menu;
}

constexpr bool leavesApp(const Screen screen) { return screen == Screen::Menu; }

constexpr Phase phaseFor(const bool finished, const bool yourTurn) {
  if (finished) return Phase::Finished;
  return yourTurn ? Phase::Yours : Phase::Theirs;
}

constexpr bool acceptsTap(const Phase phase) { return phase == Phase::Yours; }

// No square picked.
constexpr int kNothingPicked = -1;

// Whether `square` is a piece this seat could pick up right now.
//
// Derived from the legal move list rather than from "is it mine", so a piece
// with no move is not selectable at all -- which matters most under the
// mandatory-capture rule, where most of your pieces usually cannot move.
inline bool canPick(const Game& game, const int square) {
  Move list[kMaxMoves];
  const int count = moves(game, list);
  for (int i = 0; i < count; ++i) {
    if (list[i].from == square) return true;
  }
  return false;
}

// The move from `from` to `to`, or false when that is not one. The caller never
// constructs a Move itself, so a destination the rules did not offer cannot be
// expressed at the boundary either.
inline bool moveBetween(const Game& game, const int from, const int to, Move& out) {
  Move list[kMaxMoves];
  const int count = moves(game, list);
  for (int i = 0; i < count; ++i) {
    if (list[i].from != from || list[i].to != to) continue;
    out = list[i];
    return true;
  }
  return false;
}

// Where a picked piece may land. Written into `squares`, returning how many, so
// the screen can mark them without recomputing the rules a second time -- the
// same discipline as hit-testing being derived from the drawing.
// `taken` receives WHAT each landing captures. The rules already computed it;
// dropping it meant a jump drew a mark two squares away with nothing saying
// which enemy disc died, and a chain drew one six squares away with the route
// invisible. In checkers the chain is the game.
inline int destinations(const Game& game, const int from, uint8_t* squares, uint64_t* taken, const int capacity) {
  Move list[kMaxMoves];
  const int count = moves(game, list);
  int found = 0;
  for (int i = 0; i < count && found < capacity; ++i) {
    if (list[i].from != from) continue;
    squares[found] = list[i].to;
    taken[found] = list[i].taken;
    ++found;
  }
  return found;
}

// Whether the side to move is forced to capture. Asked of the move list rather
// than recomputed, so it cannot disagree with what the board will accept.
inline bool captureAvailable(const Game& game) {
  Move list[kMaxMoves];
  const int count = moves(game, list);
  return count > 0 && list[0].taken != 0;
}

// Every square holding a piece with a legal move, as a bitmask.
//
// This is how the compulsory-capture rule becomes visible. Without it a player
// taps a man that cannot move and the screen comes back identical, which is
// what a bug looks like -- and three moves in, seven of twelve men go dead at
// once with nothing announcing it. Marking what CAN move makes the board
// demonstrate the rule instead of describing it.
inline uint64_t movableSquares(const Game& game) {
  Move list[kMaxMoves];
  const int count = moves(game, list);
  uint64_t mask = 0;
  for (int i = 0; i < count; ++i) mask |= static_cast<uint64_t>(1) << list[i].from;
  return mask;
}

}  // namespace checkers
