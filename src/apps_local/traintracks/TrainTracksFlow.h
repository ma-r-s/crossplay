#pragma once

// Train Tracks' navigation, and nothing else. Freestanding.
//
// Four screens and no game machine: whether a game is finished is a fact the
// rules already hold (Game::solved), so the finished board is the play screen
// in its solved state rather than a fifth screen that has to agree with it.

#include <cstdint>

#include "TrainTracksGame.h"

namespace traintracks {

enum class Screen : uint8_t {
  // The front door while a game is unfinished: CONTINUE or NEW PUZZLE.
  Menu,
  HowTo,
  // Pick a difficulty, see the puzzle it dealt, play it. With no unfinished
  // game this IS the front door. The saved game is replaced only by PLAY
  // PUZZLE, so leaving this screen never loses it.
  NewGame,
  Board,
};

// The screen the app opens on, and the one Back reaches last: the game you
// have if it is unfinished, otherwise the new-puzzle screen.
constexpr Screen frontDoor(const bool unfinished) { return unfinished ? Screen::Menu : Screen::NewGame; }

// Where Back goes. Exhaustive and no default, so a new screen without a decided
// Back fails the build.
constexpr Screen back(const Screen screen, const bool unfinished) {
  switch (screen) {
    case Screen::Menu:
      // The caller leaves the app. Should the game have finished under it,
      // the way out still runs through the real front door.
      return frontDoor(unfinished);
    case Screen::HowTo:
      // Only the new-puzzle screen opens it.
      return Screen::NewGame;
    case Screen::NewGame:
      return frontDoor(unfinished);
    case Screen::Board:
      // Stop solving first, leave second. The game is saved either way.
      return frontDoor(unfinished);
  }
  return Screen::Menu;
}

// Back from the front door leaves the app, whichever screen that is.
constexpr bool leavesApp(const Screen screen, const bool unfinished) { return screen == frontDoor(unfinished); }

}  // namespace traintracks
