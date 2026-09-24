#pragma once

// Underhand on screen. Freestanding builders over plain models.
//
// One screen carries the game. The card's text sits at the top, its options
// stack under it, and what is held is a bar of symbols along the bottom. When
// a choice does something worth reading (cards shuffled in, a random loss, a
// reshuffle, foresight) the options give their place to a panel saying so, and
// nothing else on the screen moves.
//
// Every option shows the card's own cost, the same each time the card comes
// back (a punishment's "half of what you hold" shows what that is now). One
// that can be paid only one way is taken by tapping it; where relics stand in
// for part of the cost, a black chip after it shows the whole payment, so no
// relic is spent unseen. One with a choice of
// what pays (a cultist or a prisoner, or a relic for the last food) shows a
// chip per way after the cost, the first black: a tap on a chip pays that
// way, anywhere else on the option the black one. When the chips do not fit,
// it is marked CHOOSE and a tap opens the paying panel on what every way
// pays: the player taps the symbols in the bar for the rest, and PAY lights
// up when they cover it exactly. An option that cannot be taken is dithered,
// shows what it asks, gives and does, and takes no tap; a reason is written
// only where the cost beside the bar does not show it. Tapping the bar of
// symbols, or the line above it, opens How to Play.

#include "../ui/ToyboxScreen.h"
#include "UnderhandView.h"

namespace underhandui {

namespace fui = freeink::ui;
namespace view = underhand::view;

enum : fui::ActionId {
  ActionMain = 1,       // menu: continue the run, or begin one
  ActionNewRun = 2,     // menu: give up the run and start again; end: play again
  ActionOption = 3,     // value: stamp(turn, option): pay it, or choose what pays
  ActionContinue = 4,   // outcome and foresight: carry on
  ActionSeen = 5,       // foresight: value is which card to keep or discard
  ActionMenu = 6,       // end: back to the menu
  ActionPay = 7,        // paying: value stamp(turn, option), pay what is picked
  ActionPick = 8,       // paying: value is a resource, one more of it toward the cost
  ActionCancel = 9,     // paying: back to the options; menu: keep the run
  ActionGiveUp = 10,    // menu: ask before giving up the run
  ActionHelp = 11,      // card: the bar of symbols; menu: how to play
  ActionUnpick = 12,    // paying: value is a resource, one of it taken back
  ActionHelpPage = 13,  // how to play: value is the page to show
  ActionWay = 14,       // card: value stamp(turn, option * kWayStride + way), pay that way at once
  ActionEndRun = 15,    // menu, asked to give up: end the run
};

constexpr int kWayStride = 16;  // ways per option in an ActionWay payload
constexpr int kShownWays = 4;   // at most this many ways are offered on the option itself

constexpr int kHelpPages = 2;

// A tap that pays carries the turn its card was drawn on. Two cards with the
// same buttons in the same places then still build different tables, so the
// tap gate (RevealedInteractions) holds a second tap made while the next card
// is being painted instead of spending it on a card nobody has seen. Six bits
// of turn and a byte of payload (the option); always positive, which touch
// needs.
constexpr int16_t stamp(int turn, int payload) { return static_cast<int16_t>(((turn & 0x3F) << 8) | (payload & 0xFF)); }
constexpr bool stampedFor(int value, int turn) { return value >= 0 && (value >> 8) == (turn & 0x3F); }
constexpr int payloadOf(int value) { return value & 0xFF; }

struct OptionRow {
  const char* text = "";
  // A tap opens the paying panel rather than paying: there is a choice of
  // what pays, or relics would pay for no suspicion lost (guarded).
  bool chooses = false;
  bool guarded = false;
  view::Tokens give;  // the card's own cost
  view::Tokens get;
  char note[112] = {};  // what else it does
  view::Why why;        // when it cannot be taken
  view::OptionState state = view::OptionState::Open;
  // The chips after the cost (view::chips): one per way, or one showing the
  // relics a single way spends. 0 when the cost says it all.
  int ways = 0;
  view::Tokens wayPart[kShownWays];
};

enum class Panel : uint8_t { Options, Outcome, Foresight, Paying };

struct CardModel {
  int turn = 0;         // stamped into every tap that pays
  char title[64] = {};  // in capitals
  const char* flavor = "";
  int deck = 0;  // cards left before the next reshuffle
  int16_t held[underhand::kResources] = {};
  underhand::Odds odds;   // each punishment's chance of striking before the next draw
  underhand::Odds rolls;  // each roll's own chance: which counts invite one at all
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

  // Paying for an option with a choice of what pays: what has been picked
  // from the bar so far, which symbols can still go toward it, whether it
  // pays the cost exactly.
  int payingFor = -1;
  int16_t picked[underhand::kResources] = {};
  bool pickable[underhand::kResources] = {};
  bool pickedExactly = false;
  bool savesLastFood = false;  // one of the ways pays a relic to keep the last food
};

struct MenuModel {
  bool inRun = false;
  bool confirmGiveUp = false;  // asking before the run is thrown away
  bool tutorial = false;       // the next run is the tutorial
  bool saveSetAside = false;   // the save on the card could not be read
  int turn = 0;
  int gods = 0;
  const char* godName[underhand::kMaxGods] = {};
  bool summoned[underhand::kMaxGods] = {};
};

struct EndModel {
  bool won = false;
  bool leaveOnly = false;  // nothing to play: one button, and it leaves
  char headline[48] = {};  // in capitals
  char detail[2][160] = {};
};

// Layout the builders could not make fit. Text too long for its box is
// caught by the renderer's own gate (sim-shot fails on the ellipsis it
// truncates with); this counts what that gate cannot see, such as two rows of
// tokens running into each other. The audit resets and reads it.
int layoutProblems();
const char* lastLayoutProblem();
void resetLayoutProblems();

void buildCard(toybox::Screen& screen, const CardModel& model);
void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildEnd(toybox::Screen& screen, const EndModel& model);
// The symbols, then the rules, a page each. Static.
void buildHelp(toybox::Screen& screen, int page);

}  // namespace underhandui
