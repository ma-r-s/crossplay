#pragma once

// Checkers on screen. Freestanding builders over plain models.

#include "../ui/ToyboxScreen.h"
#include "CheckersCore.h"
#include "CheckersFlow.h"

namespace checkui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionHowToNext = 2,
  ActionAgain = 3,
  ActionDone = 4,
};

enum class MenuRow : int { Play = 0, PlayNearby, HowTo, Count };

struct MenuModel {
  const char* nearbyName = nullptr;
  int selected = -1;
};

struct HowToModel {
  int page = 0;
};

struct BoardModel {
  checkers::Game game{};
  // The square a piece has been picked up from, or kNothingPicked. Owned by the
  // flow, not invented here.
  int picked = checkers::kNothingPicked;
  // Where that piece may land, straight from the rules' own move list rather
  // than recomputed -- the same discipline as hit-testing being derived from
  // the drawing.
  uint8_t destinations[checkers::kMaxMoves] = {};
  // What each of those landings captures, carried straight from the rules.
  uint64_t takenMasks[checkers::kMaxMoves] = {};
  int destinationCount = 0;
  // Every piece with a legal move, so a compulsory capture is visible before
  // the player discovers it by tapping men that will not lift.
  uint64_t movable = 0;
  // Which seat this device plays, so the board can be drawn from that side.
  uint8_t seat = checkers::kLight;
  // How many pieces each side still has, so the material strips can show what
  // has been taken. Twelve each at the start.
  int yourPieces = checkers::kMen;
  int theirPieces = checkers::kMen;
  bool yourTurn = true;
  // A capture is available, so every legal move is one.
  bool mustTake = false;
  const char* opponentName = nullptr;
};

struct ResultModel {
  checkers::Outcome outcome = checkers::Outcome::Draw;
  uint8_t seat = checkers::kLight;
  int yourPieces = 0;
  int theirPieces = 0;
  const char* opponentName = nullptr;
};

// A square's rect, and its exact inverse. Sixty-four squares against a
// twenty-four slot interaction buffer, so the board is hit-tested
// arithmetically from the geometry that drew it rather than registered square
// by square. Both take the seat, because the board is drawn from the playing
// side's end and a tap has to mean the same square the player is looking at.
fui::Rect squareRect(const fui::DeviceContext& device, int file, int rank, uint8_t seat);
bool squareAt(const fui::DeviceContext& device, int x, int y, uint8_t seat, int& file, int& rank);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

int howToPages();

}  // namespace checkui
