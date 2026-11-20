#pragma once

// Train Tracks on screen. Freestanding builders over plain models: these know
// FreeInkUI and Toybox tokens and nothing else, so host-tests/ui can build every
// screen against a fake draw target and ask what it drew and what it made
// tappable.

#include "../ui/ToyboxScreen.h"
#include "TrainTracksCore.h"
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
};

const char* levelName(tt::Level level);

struct BoardModel {
  const tt::Puzzle* puzzle = nullptr;
  const tt::Board* board = nullptr;
  tt::Level level = tt::Level::Easy;
  // True while a puzzle is being laid. The grid is empty then and the status
  // line says so, rather than the app looking frozen.
  bool generating = false;
  bool solved = false;
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

void buildBoard(toybox::Screen& screen, const BoardModel& model);

}  // namespace traintracksui
