#pragma once

// Wordle's screens: free functions over plain models, so host-tests/ui can
// build them without a device.

#include <Icon.h>

#include "../ui/ToyboxScreen.h"

namespace wordleui {

namespace fui = freeink::ui;

constexpr int kRows = 6;
constexpr int kLetters = 5;

// What a tile or key says about its letter. Empty and Typed are the row being
// written; the other three are a submitted guess.
enum class Mark : uint8_t { Empty, Typed, Absent, Present, Correct };

struct Tile {
  char letter = 0;
  Mark mark = Mark::Empty;
};

struct GameModel {
  const char* title = "WORDLE";
  const char* rightLabel = nullptr;
  Tile tiles[kRows][kLetters];
  Mark keys[26] = {};  // A..Z, Empty until a guess has used the letter
};

void buildGame(toybox::Screen& screen, const GameModel& model);

}  // namespace wordleui
