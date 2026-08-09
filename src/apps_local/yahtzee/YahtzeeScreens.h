#pragma once

// Yahtzee on screen. Freestanding builders over plain models.

#include "../ui/ToyboxScreen.h"
#include "YahtzeeCore.h"
#include "YahtzeeFlow.h"

namespace yzui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionMenuRow = 1,
  ActionHowToNext = 2,
  ActionAgain = 3,
  ActionDone = 4,
  ActionRoll = 5,
};

enum class MenuRow : int { Play = 0, PlayNearby, HowTo, Count };

struct MenuModel {
  const char* nearbyName = nullptr;
  int selected = -1;
};

struct HowToModel {
  int page = 0;
};

struct CardModel {
  yahtzee::Game game{};
  // Which boxes a tap can take, from the rules rather than recomputed here.
  // Zero before the first roll, zero on their turn, and exactly one under a
  // live Joker.
  uint16_t takeable = 0;
  // A Joker is forcing the hand: one box is legal and twelve free ones are not.
  // Without saying so the card looks like it has simply stopped offering
  // anything, which is what a dead screen looks like.
  bool joker = false;
  // Which seat this device plays.
  uint8_t seat = 0;
  bool yourTurn = true;
  const char* opponentName = nullptr;
};

struct ResultModel {
  int yourTotal = 0;
  int theirTotal = 0;
  // The card, so the result screen can say WHERE the game was won rather than
  // only who won it.
  yahtzee::Card yours{};
  yahtzee::Card theirs{};
  const char* opponentName = nullptr;
};

// A die's rect, and the inverse a tap goes through. Five dice and thirteen
// rows is eighteen targets against a twenty-four slot interaction buffer, and
// the ROLL capsule makes nineteen. That fits, but the table is still hit-tested
// arithmetically: a scorecard is a table, and a table whose rows are registered
// controls one at a time is a table that silently loses its last row the day a
// fourteenth category is invented.
fui::Rect dieRect(const fui::DeviceContext& device, int index);
int dieAt(const fui::DeviceContext& device, int x, int y);

// A category row's rect, and its inverse. `row` is the Category index; the
// bonus and total lines are not categories and are not tappable.
fui::Rect rowRect(const fui::DeviceContext& device, int category);
int categoryAt(const fui::DeviceContext& device, int x, int y);

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildCard(toybox::Screen& screen, const CardModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);

int howToPages();

}  // namespace yzui
