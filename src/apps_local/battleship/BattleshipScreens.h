#pragma once

// The Battleship screens, as freestanding builders over a model.
//
// No GfxRenderer, no Activity, no storage: a model in, a drawn frame out, which
// is what lets host-tests/ui/ assert what they drew and what they made
// tappable. The grids are not here -- they are the app's own surface and the
// activity draws them into the rect each builder returns, the same split the
// chess board uses.

#include "../ui/ToyboxScreen.h"

namespace bshipui {

namespace fui = freeink::ui;

// Semantic actions. Every input path resolves to one of these, so a control
// cannot behave differently depending on which input reached it. The 200s
// belong to the shared link screens; these stay below them.
enum : fui::ActionId {
  ActionStartRow = 1,
  ActionFire = 2,
  ActionShuffle = 3,
  ActionReady = 4,
  ActionPlayAgain = 5,
};

// The front door. Battleship opens here rather than on a board, so that "tap
// where it says multiplayer" is literally true.
enum class StartRow : uint8_t { Continue, NewGame, PlayNearby, Count };

struct StartModel {
  // No game to continue means no CONTINUE row, rather than one that does
  // nothing.
  bool hasSavedGame = false;
  // Reads out the game you would be returning to: "14 SHOTS, 2 SUNK".
  const char* continueDetail = "";
  int played = 0;
  int won = 0;
  int streak = 0;
  int selected = 0;
};

int startRows(const StartModel& model);
StartRow startRowAt(const StartModel& model, int visibleIndex);
const char* startRowLabel(StartRow row);

// Draws the menu and returns the rect left for the app's own artwork, which is
// the grid of the game you last played. Ornament has to be made of the app's
// own material and carry the app's own data; a picture of a fleet would be
// wallpaper by the third day, and a picture of *your* last game is not.
fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model);

// Setting up. One grid, two buttons, and a line of instruction that changes as
// you learn what the screen does.
struct PlaceModel {
  const char* status = "";
  // False while waiting for the other device, when READY has already been sent
  // and there is nothing left to press.
  bool canEdit = true;
};

fui::Rect buildPlaceChrome(toybox::Screen& screen, const PlaceModel& model);

// Playing. The status capsule is the trigger: it says FIRE when a cell is
// aimed, reports what happened when it is not, and becomes PLAY AGAIN at the
// end. One control, three jobs, and it is the largest thing on the screen that
// is not a grid.
struct BoardModel {
  // What just happened, on its own line under the rule: "C4: HIT", "THEY SANK
  // YOUR CRUISER". Separate from the capsule because one of them reports and
  // the other acts, and a control that changes its meaning as the game narrates
  // itself is a control you stop trusting.
  const char* report = "";
  const char* status = "";
  // A cell is aimed and it is your turn, so the capsule is armed.
  bool canFire = false;
  bool gameOver = false;
};

fui::Rect buildBoardChrome(toybox::Screen& screen, const BoardModel& model);

}  // namespace bshipui
