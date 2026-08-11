#pragma once

// Toy Battle's screens. Free functions over plain models, drawing only through
// FreeInkUI, so host-tests/ui/ can build them against a fake target with no
// renderer and no device.
//
// The board is hand-drawn into the body rect. It has 17 slots and a rack of 8,
// which is 25 against a 24-slot interaction buffer, so nothing on it is
// registered as a control: both are hit-tested arithmetically from the same
// geometry that drew them.

#include "../ui/ToyboxScreen.h"
#include "ToyBattleCore.h"
#include "ToyBattleFlow.h"

namespace tbui {
namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionCapsule = 2,
};

enum class MenuRow : int { Play = 0, HowTo, Count };

struct MenuModel {
  int selected = -1;
};

struct BoardModel {
  toybattle::Game game{};
  toybattle::Draft draft{};
  uint8_t seat = 0;
  bool yourTurn = true;
  // What the capsule says: the question being asked, or whose turn it is.
  const char* capsule = "";
  bool capsuleLive = false;
};

// --- geometry, shared by the drawing and the hit test -----------------------

// Where slot `slot` sits, in device pixels. Pure on purpose: the drawing, the
// markers and the tap all call this, so a tap lands on the base the player is
// looking at by construction rather than by two sums happening to agree.
fui::Point slotCenter(const fui::DeviceContext& device, const toybattle::Terrain& board, int slot);
int16_t slotRadius();

// The slot under a tap, or -1. Radius-based, because the board is a graph
// rather than a grid and there is no cell to divide into.
int slotAt(const fui::DeviceContext& device, const toybattle::Terrain& board, int x, int y);

// The rack tile for troop `kind`, and the kind under a tap (-1 for none).
fui::Rect rackTile(const fui::DeviceContext& device, int kind);
int rackAt(const fui::DeviceContext& device, int x, int y);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);

}  // namespace tbui
