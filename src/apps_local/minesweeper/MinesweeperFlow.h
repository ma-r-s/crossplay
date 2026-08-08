#pragma once

// Minesweeper's navigation. Freestanding: no renderer, no Activity.
//
// The cycle asks for two state machines, a shell and a game. **This game only
// needs the shell**, and that is worth saying out loud rather than inventing a
// second one to satisfy the shape: Minesweeper's game machine already exists,
// as `minesweeper::Status` in the rules, where it belongs. Restating it here
// would be two facts that must agree, which is the exact failure the two-machine
// rule was written to prevent. A game with turns needs both because "whose turn"
// is not a rules concept the core can own alone; a solitaire does not.
//
// What this file does own is the third machine no one predicted: the TOOL.

#include <cstdint>

#include "MinesweeperCore.h"

namespace minesweeper {

enum class Screen : uint8_t {
  // The top. Back from here leaves the app, and it is the only screen that does.
  Menu,
  HowTo,
  Board,
  Result,
};

// Dig or flag. This is a mode, and modes are usually a smell -- but the input
// layer leaves no alternative and pretending otherwise would be worse.
//
// Every other game here is one activation path: you tap a thing and the thing
// happens. Minesweeper needs two verbs on the same cell, and the device offers
// exactly one gesture to games -- `wasScreenTapped`. There is no long press, no
// second button a thumb can reach while touching the glass, and no right click.
//
// So the mode is real, and the response is to make it impossible to be in the
// wrong one by accident: it is a first-class control on screen, always visible,
// always saying which one is live. A mode you cannot see is the thing that
// makes modes bad.
enum class Tool : uint8_t { Dig, Flag };

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

constexpr Tool other(const Tool tool) { return tool == Tool::Dig ? Tool::Flag : Tool::Dig; }

// Whether a tap on the board can do anything at all right now. The board is
// still drawn when it cannot -- a finished game is the thing you want to look
// at -- but it stops accepting.
inline bool boardAccepts(const Game& game) { return !over(game); }

// The screen a game's own status implies, so the activity never decides this
// twice. Derived from the rules rather than tracked beside them.
inline Screen screenFor(const Game& game) { return over(game) ? Screen::Result : Screen::Board; }

}  // namespace minesweeper
