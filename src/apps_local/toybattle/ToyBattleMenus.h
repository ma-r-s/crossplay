#pragma once

// The shell's screens: the front door, the setup, the map list and the rules.
// Free functions over plain models, drawing only through FreeInkUI, so they
// build in host-tests/ui/ against a fake target with no renderer and no device.
//
// Three treatments of each are built at once and photographed side by side,
// because an option described in prose gets judged on the prose. Two of the
// three get deleted once one is chosen -- they are not a setting.

#include "../ui/ToyboxScreen.h"
#include "ToyBattleCore.h"
#include "ToyBattleFlow.h"

namespace tbui {
namespace fui = freeink::ui;

// Which of the three treatments to draw. Temporary by construction: when Mario
// picks one, this enum and two thirds of the file go with it.
enum class Look : uint8_t { FrontDoor = 0, Slab, Briefing };
constexpr int kLookCount = 3;
const char* lookName(Look look);

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
  Look look = Look::FrontDoor;
};

int shellRowCount(const MenuModel& model);
ShellRow shellRowAt(const MenuModel& model, int visibleIndex);
void buildMenu(toybox::Screen& screen, const MenuModel& model);

// --- setup ------------------------------------------------------------------

enum class SetupRow : uint8_t { Map = 0, Opponent, Bases, Count };

struct SetupModel {
  toybattle::Options options{};
  int selected = -1;
  // Against a person there is no opponent to choose, so the row is not there.
  bool forLink = false;
  Look look = Look::FrontDoor;
};

int setupRowCount(const SetupModel& model);
SetupRow setupRowAt(const SetupModel& model, int visibleIndex);
void buildSetup(toybox::Screen& screen, const SetupModel& model);

// --- the map list -----------------------------------------------------------

struct MapPickModel {
  int selected = 0;
  int topRow = 0;
  Look look = Look::FrontDoor;
};

void buildMapPick(toybox::Screen& screen, const MapPickModel& model);

// --- the rules --------------------------------------------------------------

struct HowToModel {
  int page = 0;
  Look look = Look::FrontDoor;
};

int howToPages(Look look);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);

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
