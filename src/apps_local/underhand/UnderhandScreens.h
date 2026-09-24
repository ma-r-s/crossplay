#pragma once

// Underhand on screen. Freestanding builders over plain models.
//
// One screen carries the game. The card's text sits at the top, its options
// stack under it, and what is held is a bar of symbols along the bottom. When
// a choice does something worth reading (cards shuffled in, a random loss, a
// reshuffle, foresight) the options give their place to a panel saying so, and
// nothing else on the screen moves.
//
// Taps: an option that can be paid one way is taken by tapping it. One that
// can be paid several ways shows them as chips joined by OR, the first filled
// black: tapping a chip pays that way and tapping the rest of the option pays
// the black one. When they do not all fit, the last chip is MORE, which lists
// every way a page at a time. An option that cannot be taken is dithered,
// still shows what it asks and gives, says why, and takes no tap. Tapping the
// bar of symbols explains them.

#include "../ui/ToyboxScreen.h"
#include "UnderhandView.h"

namespace underhandui {

namespace fui = freeink::ui;
namespace view = underhand::view;

enum : fui::ActionId {
  ActionMain = 1,       // menu: continue the run, or begin one
  ActionNewRun = 2,     // menu: give up the run and start again; end: play again
  ActionOption = 3,     // value: which option, paid its first way
  ActionContinue = 4,   // outcome and foresight: carry on
  ActionSeen = 5,       // foresight: value is which card to keep or discard
  ActionMenu = 6,       // end: back to the menu
  ActionPay = 7,        // value: option * kWayStride + way
  ActionMore = 8,       // value: which option's ways to list
  ActionCancel = 9,     // ways list: back to the options; menu: keep the run
  ActionGiveUp = 10,    // menu: ask before giving up the run
  ActionHelp = 11,      // card: the bar of symbols; menu: how to play
  ActionNextWays = 12,  // ways list: value is the page to show
};

constexpr int kMostWays = 32;          // payments() never finds more
constexpr int kWayStride = kMostWays;  // so option * stride + way is unique
constexpr int kChips = 3;              // ways shown on an option before MORE

struct OptionRow {
  const char* text = "";
  int ways = 0;  // how many exact payments there are
  view::Tokens way[kChips];
  view::Tokens give;  // the cost as asked, shown when it cannot be paid
  view::Tokens get;
  // What else it does when it can be taken; why not when it cannot.
  char note[112] = {};
  view::OptionState state = view::OptionState::Open;
};

enum class Panel : uint8_t { Options, Outcome, Foresight, Ways };

struct CardModel {
  char title[64] = {};  // in capitals
  const char* flavor = "";
  int deck = 0;  // cards left before the next reshuffle
  int16_t held[underhand::kResources] = {};
  view::Danger danger;
  // A danger when there is one; otherwise what the last choice paid and gained.
  char status[112] = {};
  bool statusIsDanger = false;
  view::Tokens lastPaid;
  view::Tokens lastGained;

  Panel panel = Panel::Options;
  int optionCount = 0;
  OptionRow option[underhand::kMaxOptions];

  const char* chose = "";  // the option taken, for the outcome panel
  view::Tokens paid;
  view::Tokens gained;
  view::Tokens lost;          // taken at random
  char outcome[1][160] = {};  // what the choice did to the deck
  int outcomeLines = 0;

  int seenCount = 0;
  const char* seenTitle[3] = {};
  bool discard[3] = {};
  bool mayDiscard = false;

  // The option whose ways are listed, all of them. The screen pages them by
  // the room it has; wayPage wraps.
  int waysFor = -1;
  int wayCount = 0;
  int wayPage = 0;
  view::Tokens listed[kMostWays];
};

struct MenuModel {
  bool inRun = false;
  bool confirmGiveUp = false;  // asking before the run is thrown away
  bool tutorial = false;       // the next run is the tutorial
  int turn = 0;
  int gods = 0;
  const char* godName[underhand::kMaxGods] = {};
  bool summoned[underhand::kMaxGods] = {};
};

struct EndModel {
  bool won = false;
  const char* headline = "";
  char detail[2][160] = {};
};

// Layout the builders could not make fit. Text too long for its box is
// caught by the renderer's own gate (sim-shot fails on the ellipsis it
// truncates with); this counts what that gate cannot see, such as two rows of
// tokens running into each other. The audit resets and reads it.
int layoutProblems();
const char* lastLayoutProblem();
void resetLayoutProblems();
// How many pages the last list of ways drawn had, so the audit can visit each.
int lastWaysPages();

void buildCard(toybox::Screen& screen, const CardModel& model);
void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildEnd(toybox::Screen& screen, const EndModel& model);
// The symbols, the dangers and the tap rules. Static.
void buildHelp(toybox::Screen& screen);

}  // namespace underhandui
