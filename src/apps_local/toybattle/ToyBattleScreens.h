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
  // A turn is "place a troop" or "draw two", and the second had no way in at
  // all until this existed. The foot of the board is now whichever of these
  // the position actually offers.
  ActionDraw = 2,
  ActionCancel = 3,
  ActionTake = 4,
  ActionSkip = 5,
  ActionBrief = 6,
  ActionAgain = 7,
  ActionDone = 8,
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
  // The question being asked, on its own line under the rule.
  const char* prompt = "";
  // Drawing is only offered when it is legal: a full rack or an empty reserve
  // dims it rather than hiding it.
  bool canDraw = false;
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

// What this terrain's special bases do. The only thing that changes between
// maps, so it is the only thing the briefing has to carry.
struct BriefModel {
  const toybattle::Terrain* board = nullptr;
  bool specialBases = true;
};
void buildBrief(toybox::Screen& screen, const BriefModel& model);

struct ResultModel {
  toybattle::Game game{};
  uint8_t seat = 0;
};
void buildResult(toybox::Screen& screen, const ResultModel& model);

// How the game is played, in one screen. Short on purpose: the marks carry the
// troops and the briefing carries the terrain, so this only has to cover the
// two things neither of those can say.
void buildHowTo(toybox::Screen& screen);

// What a troop does, in a few words, and why a tap was refused. Both feed the
// one line under the title.
const char* troopBlurb(toybattle::Troop kind);
const char* refusalBlurb(toybattle::Refusal why);

}  // namespace tbui
