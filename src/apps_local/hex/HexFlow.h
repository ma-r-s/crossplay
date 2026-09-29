#pragma once

// Hex's navigation and its settings. Header-only constexpr, freestanding.
//
// One machine, not two. Go needs a `Phase` beside its `Screen` because a go
// game has a stage the shell cannot see -- playing, counting, over. Hex has
// none: a stone goes down, and either it won or it did not. `Screen` is the
// whole shell and `hex::over()` is the whole game state.
//
// **A stone goes down in ONE tap.** Go aims first and commits second because a
// nine-line board is a 49px pitch and a stone cannot be taken back. Hex's cells
// are about 46px across and, unlike Go's crossings, they tile the panel with no
// gutters between them, so the target a finger gets is the whole cell. A second
// tap would buy accuracy the geometry already has and cost a tap on every move.

#include <cstdint>

#include "HexCore.h"

namespace hex {

enum class Screen : uint8_t {
  // The top. Back from here leaves the app, and it is the only screen that does.
  Menu,
  Settings,
  Board,
  Result,
};

// Who the other seat belongs to. Nearby is NOT here: a link match replaces the
// whole activity's turn source, and an app that stored "nearby" as an opponent
// would have two facts about one thing. Multiplayer is an action on the front
// door, the way chess has it.
enum class Opponent : uint8_t { Computer, Human };

// Three levels, and they are three different players rather than one player
// given more time: the playout policy changes with the budget. See HexBrain.h.
enum class Level : uint8_t { Easy, Normal, Hard, Count_ };

constexpr Screen back(const Screen screen) {
  switch (screen) {
    case Screen::Menu:
      return Screen::Menu;
    case Screen::Settings:
      return Screen::Menu;
    case Screen::Board:
      return Screen::Menu;
    case Screen::Result:
      return Screen::Menu;
  }
  return Screen::Menu;
}

constexpr bool leavesApp(const Screen screen) { return screen == Screen::Menu; }

constexpr const char* levelName(const Level level) {
  switch (level) {
    case Level::Easy:
      return "EASY";
    case Level::Normal:
      return "NORMAL";
    case Level::Hard:
      return "HARD";
    case Level::Count_:
      break;
  }
  return "NORMAL";
}

constexpr Level nextLevel(const Level level) {
  switch (level) {
    case Level::Easy:
      return Level::Normal;
    case Level::Normal:
      return Level::Hard;
    case Level::Hard:
      return Level::Easy;
    case Level::Count_:
      break;
  }
  return Level::Normal;
}

// What a tap on a cell means. One function, so the screen can draw from the
// same answer the activity acts on and neither can invent a third rule.
enum class Tap : uint8_t {
  // Occupied, finished, or not your turn. The tap does nothing AND the panel
  // does not repaint: a refresh that comes back identical is what a bug looks
  // like on e-ink, and it is the whole of this app's no-op behaviour.
  Ignore,
  Place,
};

constexpr Tap tapMeaning(const Game& game, const int cell, const bool yourTurn) {
  if (!yourTurn) return Tap::Ignore;
  if (over(game)) return Tap::Ignore;
  if (!validCell(cell)) return Tap::Ignore;
  return game.at(cell) == kEmpty ? Tap::Place : Tap::Ignore;
}

}  // namespace hex
