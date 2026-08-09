#pragma once

// Connect Four's navigation, and the one hint the board owes the player.
// Freestanding: no renderer, no Activity.

#include <cstdint>

#include "ConnectFourCore.h"

namespace connectfour {

enum class Screen : uint8_t {
  // The top. Back from here leaves the app, and it is the only screen that does.
  Menu,
  HowTo,
  Board,
  Result,
};

// Where Back goes from `screen`.
//
// Exhaustive switch, no default, so a new screen without a decided Back fails
// the build rather than falling through to something plausible.
constexpr Screen back(const Screen screen) {
  switch (screen) {
    case Screen::Menu:
      // The caller leaves the app; returning Menu keeps this total without
      // inventing a state for "gone".
      return Screen::Menu;
    case Screen::HowTo:
      return Screen::Menu;
    case Screen::Board:
      // Abandoning a board returns to the menu, not out of the app: the first
      // Back means stop playing, the second means leave. Back meaning two
      // different things depending on depth is how players get lost.
      return Screen::Menu;
    case Screen::Result:
      return Screen::Menu;
  }
  return Screen::Menu;
}

constexpr bool leavesApp(const Screen screen) { return screen == Screen::Menu; }

// The game's own three states. Derived from the position and the seat, never
// stored, so there is nothing that can disagree with the board.
enum class Phase : uint8_t {
  Yours,
  Theirs,
  Settled,
};

constexpr Phase phaseFor(const bool finished, const bool yourTurn) {
  if (finished) return Phase::Settled;
  return yourTurn ? Phase::Yours : Phase::Theirs;
}

// The screen a game's own state implies, so the activity never decides it twice.
inline Screen screenFor(const Game& game) { return over(game) ? Screen::Result : Screen::Board; }

// Which columns will accept a disc, as a bitmask over the seven.
//
// Unlike Checkers' movable set this is not teaching a rule -- a full column is
// plainly full. It exists because the column is the tap target, and a target
// that silently does nothing is the failure that game already paid for: a
// rejected tap must either do something or be visibly not a target.
inline uint8_t openColumns(const Game& game) {
  uint8_t mask = 0;
  for (int column = 0; column < kColumns; ++column) {
    if (canDrop(game, column)) mask |= static_cast<uint8_t>(1 << column);
  }
  return mask;
}

}  // namespace connectfour
