#pragma once

// Knucklebones on screen. Freestanding builders over plain models: these know
// about FreeInkUI and Toybox tokens and nothing else, so host-tests/ui can
// build every screen against a fake draw target and ask what it drew and what
// it made tappable. See ToyboxScreen.h for why screens are written this way.
//
// One builder per Screen in KnucklebonesFlow.h, and the activity picks which to
// call. The flow decides where you are; these only draw it.

#include <Icon.h>

#include "../ui/ToyboxScreen.h"
#include "KnucklebonesCore.h"

namespace knuckleui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  // The menu's rows carry their own value, so one action covers all of them.
  ActionMenuRow = 1,
  // A column of your own grid. The value is the column index, which is also
  // what the core's place() takes -- the screen never invents a coordinate the
  // rules do not already use.
  ActionColumn = 2,
  ActionHowToNext = 3,
  ActionAgain = 4,
  ActionDone = 5,
};

// The menu rows, in order. An enum rather than loose indices because the
// activity switches on them, and a switch over an enum is exhaustive.
enum class MenuRow : int { Play = 0, PlayNearby, HowTo, Count };

struct MenuModel {
  // Shown on the PLAY NEARBY row when another device is already visible, so the
  // row can say who rather than saying "nearby" into the void. Null when nobody
  // is there yet.
  const char* nearbyName = nullptr;
  int selected = -1;

  // The board the last finished match ended on, and the running tally.
  //
  // This is the menu's ornament, and it is ornament in the only sense this fork
  // allows: made of the app's own material, carrying the app's own data. A
  // screenshot of it differs between two devices, which is the test. Before it,
  // this front door was three rows and 60% white -- the anti-pattern the design
  // language names by name.
  bool hasHistory = false;
  knucklebones::Grid lastYours{};
  knucklebones::Grid lastTheirs{};
  int wins = 0;
  int losses = 0;
  int draws = 0;
};

struct HowToModel {
  int page = 0;
  int pageCount = 1;
};

struct BoardModel {
  // Both grids, indexed the way the core indexes them. `you` is the seat this
  // device is playing, so the screen never has to know whether that is seat 0
  // or seat 1 -- the activity resolves it once.
  knucklebones::Grid yours{};
  knucklebones::Grid theirs{};
  // The die awaiting placement, or knucklebones::kEmpty when there is none.
  uint8_t die = knucklebones::kEmpty;
  // Whether this device may place right now. Drawn either way: you watch the
  // opponent play, because a board that goes blank on their turn makes a slow
  // panel feel broken.
  bool yourTurn = true;
  // Who the other seat is. A name when it is another device, null for the
  // built-in opponent -- and the difference is only ever a label.
  const char* opponentName = nullptr;
  // Set while a remote opponent is thinking, so the board can say so rather
  // than looking frozen on a panel that takes half a second to repaint.
  bool waiting = false;
};

struct ResultModel {
  int yourScore = 0;
  int theirScore = 0;
  const char* opponentName = nullptr;
};

// The columns of your own grid, as tap targets. Split out from the builder so a
// test can check that the target a player aims at is the column the rules will
// receive, rather than trusting that two pieces of geometry agree.
fui::Rect columnRect(const fui::DeviceContext& device, int column, bool yours);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildBoard(toybox::Screen& screen, const BoardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

int howToPages();

}  // namespace knuckleui
