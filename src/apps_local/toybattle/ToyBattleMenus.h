#pragma once

// The shell's screens: the front door, the setup, the map list and the rules.
// Free functions over plain models, drawing only through FreeInkUI, so they
// build in host-tests/ui/ against a fake target with no renderer and no device.
//
// Three treatments of each were built at once and photographed side by side,
// because an option described in prose gets judged on the prose. Mario picked
// the front door on 2026-08-11 and the other two were deleted the same day --
// they were never a setting, and the renders are in qa-artifacts/ if the
// question ever comes back.

#include "../ui/ToyboxScreen.h"
#include "ToyBattleCore.h"
#include "ToyBattleFlow.h"

namespace tbui {
namespace fui = freeink::ui;

enum : fui::ActionId {
  // The 100s, so nothing here can collide with the board's ids in the 1..8
  // range or with the link layer's reserved 200s.
  ActionShellRow = 100,
  ActionStart = 101,
  ActionMapRow = 102,
  ActionSetupRow = 103,
  ActionPageNext = 104,
  ActionPagePrev = 105,
  ActionOpenMaps = 106,
  ActionPickSkill = 107,
  ActionPickBases = 108,
  ActionPickSide = 109,
};

// --- the front door ---------------------------------------------------------

// Continue only exists when there is something to continue, rather than
// existing and doing nothing. The rows below it shift up, so the list is a set
// of rows and not a set of holes.
enum class ShellRow : uint8_t { Continue = 0, Play, Nearby, HowTo, Count };

struct MenuModel {
  int selected = 0;
  bool hasSave = false;
  // What you would be going back to, read out rather than implied: the map and
  // the medals so far. A CONTINUE that does not say what it continues is a
  // button you have to press to find out.
  const char* saveDetail = "";
  int played = 0;
  int won = 0;
  toybattle::Options options{};
  // The position behind the glass: the saved game if there is one, otherwise
  // the chosen map, empty. Never null.
  const toybattle::Game* preview = nullptr;
};

int shellRowCount(const MenuModel& model);
ShellRow shellRowAt(const MenuModel& model, int visibleIndex);
void buildMenu(toybox::Screen& screen, const MenuModel& model);

// --- setup ------------------------------------------------------------------

enum class SetupRow : uint8_t { Map = 0, Opponent, Side, Bases, Count };

struct SetupModel {
  toybattle::Options options{};
  int selected = -1;
  // Against a person there is no opponent to choose, so the row is not there.
  bool forLink = false;
};

int setupRowCount(const SetupModel& model);
SetupRow setupRowAt(const SetupModel& model, int visibleIndex);
void buildSetup(toybox::Screen& screen, const SetupModel& model);

// --- the map list -----------------------------------------------------------

struct MapPickModel {
  // Which page, not which row. Nothing on this screen is selected: a tap picks
  // a map outright, so a highlight would be a cursor -- and this device has no
  // cursor. Pointing is fingers, stepping is buttons.
  int page = 0;
};

// How many maps a page holds, and how many pages that makes. Free functions so
// the activity can clamp its page without knowing the layout.
int mapsPerPage();
int mapPages();

void buildMapPick(toybox::Screen& screen, const MapPickModel& model);

// --- the rules --------------------------------------------------------------

// One position, advanced a move at a time. Chosen by Mario on 2026-08-12 over
// two other treatments built and photographed beside it; the renders are in
// qa-artifacts/ if the question ever comes back. See ToyBattleHowTo.cpp for
// what the deck it replaces got wrong.
struct HowToModel {
  int page = 0;
};

int howToPages();
void buildHowTo(toybox::Screen& screen, const HowToModel& model);

// The deck's own graph and positions, so host-tests can check that every troop
// drawn on a page is one that could be standing there. See ToyBattleHowTo.cpp.
int howToNodeCount();
int howToLinkCount();
void howToLinkAt(int i, int& a, int& b);
int howToOwnerAt(int page, int node);
bool howToIsHq(int node);
int howToHqSeat(int node);
bool howToCutOff(int page, int node);

// --- shared material --------------------------------------------------------

// The real board, small. Every picture in the shell is this function, because
// the alternative is an illustration of a board -- a second thing to learn
// before you can learn the game. `spotlight` is a mask over slots to ring;
// zero rings nothing. `game` may be null for an empty board.
void miniBoard(toybox::Screen& screen, const fui::Rect& box, const toybattle::Terrain& terrain,
               const toybattle::Game* game, uint64_t spotlight);

// What a difficulty and a setting read as on a row.
const char* skillName(toybattle::Skill skill);
const char* skillBlurb(toybattle::Skill skill);

}  // namespace tbui
