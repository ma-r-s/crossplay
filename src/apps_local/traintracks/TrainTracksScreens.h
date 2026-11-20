#pragma once

// Train Tracks on screen. Freestanding builders over plain models: these know
// FreeInkUI and Toybox tokens and nothing else, so host-tests/ui can build every
// screen against a fake draw target and ask what it drew and what it made
// tappable.

#include "../ui/ToyboxScreen.h"
#include "TrainTracksCore.h"
#include "TrainTracksFlow.h"
#include "TrainTracksGame.h"
#include "TrainTracksGenerator.h"

namespace traintracksui {

namespace fui = freeink::ui;
namespace tt = traintracks;

enum : fui::ActionId {
  ActionUndo = 1,
  ActionRedo = 2,
  ActionCheck = 3,
  ActionHint = 4,
  ActionMenu = 5,
  // CONTINUE: back to the unfinished game.
  ActionPlay = 6,
  // A difficulty picked on the new-puzzle screen, value = the Level.
  ActionLevel = 7,
  ActionHowTo = 8,
  // To the new-puzzle screen, whatever is saved.
  ActionNewGame = 9,
  ActionHowToNext = 10,
  // PLAY PUZZLE: the previewed puzzle replaces the saved game.
  ActionPlayPreview = 11,
};

const char* levelName(tt::Level level);

// "12:04", or "1:02:03" past the hour.
void formatClock(uint32_t ms, char* out, int size);

struct MenuModel {
  // The unfinished game, drawn as the ornament. The front door is only this
  // screen while there is one.
  const tt::Game* game = nullptr;
};

struct NewGameModel {
  tt::Level selected = tt::Level::Easy;
  // The puzzle PLAY PUZZLE will deal, or nullptr while it is being laid.
  const tt::Game* preview = nullptr;
};

struct HowToModel {
  int page = 0;
};

int howToPages();

struct BoardModel {
  const tt::Puzzle* puzzle = nullptr;
  const tt::Board* board = nullptr;
  tt::Level level = tt::Level::Easy;
  bool solved = false;
  uint32_t elapsedMs = 0;
  uint16_t hints = 0;
  uint16_t checks = 0;
  // What CHECK or HINT just said. Cleared by the next move.
  const char* notice = nullptr;
  // One flag per square CHECK found wrong, or nullptr.
  const uint8_t* wrong = nullptr;
  // The square HINT just filled, or -1.
  int hintCell = -1;
};

// Where everything on the board screen sits, from the panel's size and the
// grid's. Drawing and hit-testing both read this one struct, so a tap can never
// land on a cell other than the one drawn under it.
struct Geometry {
  int n;
  int16_t cell;
  int16_t gridX;
  int16_t gridY;
  int16_t rowClueX;   // left edge of the row-clue gutter
  int16_t colClueY;   // top edge of the column-clue band
  int16_t statusY;    // top of the status line
  int16_t controlsY;  // top of the controls
};

Geometry boardGeometry(const fui::DeviceContext& device, int n);
fui::Rect cellRect(const Geometry& g, int row, int col);
// Inverse of cellRect. False outside the grid.
bool cellAt(const Geometry& g, int x, int y, int& row, int& col);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildNewGame(toybox::Screen& screen, const NewGameModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);

}  // namespace traintracksui
