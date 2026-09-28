#pragma once

// Hex on screen. Freestanding builders over plain models: no renderer, no
// storage, no Activity, so host-tests/ui can build every one of them against a
// fake target and ask what was drawn and what was made tappable.

#include "../ui/ToyboxScreen.h"
#include "HexCore.h"
#include "HexFlow.h"

namespace hexui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionSettingsRow = 2,
  ActionAgain = 3,
  ActionDone = 4,
};

enum class MenuRow : int { Play = 0, PlayNearby, Settings, Count };
enum class SettingsRow : int { Opponent = 0, Level, PlayAs, Count };

struct MenuModel {
  const char* nearbyName = nullptr;
  int selected = -1;
  // A game is part-played and PLAY will resume it rather than start one.
  bool inProgress = false;

  // The position the front door draws, packed two bits a cell exactly as
  // `hex::Game` holds it: the game IN PROGRESS when there is one, the last
  // game finished when there is not, and null when neither exists. The front
  // door sat empty until the first game was over in Go's first draft, which is
  // backwards -- the thing a player most wants to see from here is the game
  // they are in the middle of.
  const uint8_t* boardCells = nullptr;
  int moveNumber = 0;

  // Whether the last finished game was won, for the caption under the
  // ornament. Only read when `boardCells` is the last game rather than the one
  // in progress, which `inProgress` already says.
  bool lastWon = false;
  int wins = 0;
  int losses = 0;
};

struct SettingsModel {
  int selected = -1;
  hex::Opponent opponent = hex::Opponent::Computer;
  hex::Level level = hex::Level::Normal;
  // Which colour the player takes against the computer. Black moves first and
  // keeps Hex's standard first-player advantage, because there is no swap rule
  // -- so this row is a real choice rather than a preference.
  uint8_t playAs = hex::kBlack;
};

struct BoardModel {
  hex::Game game{};
  // Which colour this seat plays. Black unless the player chose otherwise or
  // the coin toss did.
  //
  // Whose TURN it is is not a field here and must not become one: `game.toMove`
  // already says, in every mode -- the link only reports YourTurn once their
  // move is on your board, so the two cannot disagree, and a second copy of one
  // fact is a copy that eventually does.
  uint8_t seat = hex::kBlack;
  const char* opponentName = nullptr;
  // Two people sharing one device, so "YOUR MOVE" is the wrong words.
  bool sharedDevice = false;
  bool thinking = false;
};

struct ResultModel {
  hex::Game game{};
  uint8_t seat = hex::kBlack;
  const char* opponentName = nullptr;
  bool sharedDevice = false;
  // The cells of the winning connection, one bit each. Carried in rather than
  // recomputed here, so the board the screen marks and the board the rules
  // settled come from one pass.
  uint8_t chain[hex::kMaskBytes] = {};
};

// Where the board sits and how big its hexagons are. One struct, because the
// same drawing serves the playing board, the finished board and the front
// door's miniature at three different scales -- and a second copy of the
// arithmetic is how a miniature ends up disagreeing with the board it is a
// picture of.
//
// `a` is HALF a hexagon's flat top edge and `h` is half the vertical pitch, and
// they are the two numbers the whole layout is built from: a cell's centre is
// at `(2a + 3a*col, h + 2h*row + h*col)` inside the board box, which is `34a`
// by `32h`. Both integers, so the tiling is exact -- a hexagon's vertices land
// on its neighbours' vertices rather than a rounded pixel away from them.
struct Layout {
  int16_t left = 0;
  int16_t top = 0;
  int16_t a = 12;
  int16_t h = 21;
};

// The playing board's layout on this panel, taken from the device rather than
// from 480x800: the two boards this firmware runs on do not have to agree, and
// a hardcoded extent is a board that runs off the second one.
Layout boardLayout(const fui::DeviceContext& device);

// A cell's centre, and the exact inverse. A hundred and twenty one cells
// against a twenty-four slot interaction buffer, so the board is hit-tested
// arithmetically from the geometry that drew it rather than registered cell by
// cell -- the same discipline chess, checkers and go use, for the same reason.
//
// There is no seat argument and there must not be one: **the board is drawn in
// one fixed orientation and never flips for either player.** Hex's two players
// own different pairs of edges, so turning the board to face whoever is to move
// would not merely move the stones, it would move the goal -- and two people
// sharing one device would each be told they were playing top to bottom.
void cellCentre(const Layout& layout, int cell, int16_t& cx, int16_t& cy);
bool cellAt(const Layout& layout, int x, int y, int& cell);
int16_t stoneRadius(const Layout& layout);

// The four controls the rhombus's two notches hold: a seat card in each on the
// board screen, and on the result screen your card plus the two doors.
//
// Exported so host-tests/ui can walk every pixel of them through `cellAt()` and
// demand it answers no. The board is hit-tested from geometry BEFORE the
// interaction table is routed, so a control the rhombus overlaps is a control
// whose taps place a stone instead -- drawn, listed in the table, and
// unreachable, with nothing on the panel to say why. The notch is a TRIANGLE,
// so how far left a control may start depends on how tall it is, which is why
// the two doors are stacked and take different widths.
fui::Rect theirCardRect(const Layout& layout);
fui::Rect yourCardRect(const Layout& layout);
fui::Rect againButtonRect(const Layout& layout);
fui::Rect doneButtonRect(const Layout& layout);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildSettings(toybox::Screen& screen, const SettingsModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

}  // namespace hexui
