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

// There is no tool mode, and the absence is the design.
//
// The first version had one -- DIG and FLAG as a split capsule -- built on the
// belief that the device offers games a single gesture. That belief was wrong.
// `MappedInputManager::isScreenTouchHeld` and `swallowCurrentTouch` are public,
// and the second exists precisely so a long press can fire while the finger is
// still down without the release also registering as a tap. The SDK's own
// `InputLongPress` is defined, routed and host-tested for exactly this shape of
// problem.
//
// So: tap to dig, hold to flag. That removes the mode, removes a control that
// spent the largest solid black on the screen and inverted it on every toggle,
// and removes the worst part -- an indicator six hundred pixels away from where
// the tap lands, in a game where one wrong tap ends the run.

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

// A settled game does NOT imply the Result screen. The board stays, mines
// bared, wearing the verdict as its capsule; Result is a door the player
// takes from there. The first version navigated away on the next tick, so
// the finished minefield -- the thing you want to look at -- flashed for
// under a repaint and vanished, and Mario called the ending anticlimactic.
// (Two helpers that codified the old rule, screenFor and boardAccepts, were
// defined here and never called from anywhere; they are gone.)

}  // namespace minesweeper
