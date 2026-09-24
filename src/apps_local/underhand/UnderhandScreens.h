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
// still shows what it asks, gives and does, says why, and takes no tap.
// Tapping the bar of symbols, or the line above it, opens How to Play.

#include "../ui/ToyboxScreen.h"
#include "UnderhandView.h"

namespace underhandui {

namespace fui = freeink::ui;
namespace view = underhand::view;

enum : fui::ActionId {
  ActionMain = 1,       // menu: continue the run, or begin one
  ActionNewRun = 2,     // menu: give up the run and start again; end: play again
  ActionOption = 3,     // value: stamp(turn, option), paid its first way
  ActionContinue = 4,   // outcome and foresight: carry on
  ActionSeen = 5,       // foresight: value is which card to keep or discard
  ActionMenu = 6,       // end: back to the menu
  ActionPay = 7,        // value: stamp(turn, option * kWayStride + way)
  ActionMore = 8,       // value: stamp(turn, option), whose ways to list
  ActionCancel = 9,     // ways list: back to the options; menu: keep the run
  ActionGiveUp = 10,    // menu: ask before giving up the run
  ActionHelp = 11,      // card: the bar of symbols; menu: how to play
  ActionNextWays = 12,  // ways list: value is the page to show
  ActionHelpPage = 13,  // how to play: value is the page to show
};

constexpr int kMostWays = view::kMostPayments;
constexpr int kWayStride = kMostWays;  // so option * stride + way is unique
constexpr int kChips = 3;              // ways shown on an option before MORE
constexpr int kHelpPages = 2;

// A tap that pays carries the turn its card was drawn on. Two cards with the
// same buttons in the same places then still build different tables, so the
// tap gate (RevealedInteractions) holds a second tap made while the next card
// is being painted instead of spending it on a card nobody has seen. Six bits
// of turn and a byte of payload; always positive, which touch needs.
constexpr int16_t stamp(int turn, int payload) { return static_cast<int16_t>(((turn & 0x3F) << 8) | (payload & 0xFF)); }
constexpr bool stampedFor(int value, int turn) { return value >= 0 && (value >> 8) == (turn & 0x3F); }
constexpr int payloadOf(int value) { return value & 0xFF; }
static_assert((underhand::kMaxOptions - 1) * kWayStride + kMostWays - 1 <= 0xFF, "a payment must fit its byte");

struct OptionRow {
  const char* text = "";
  int ways = 0;      // how many exact payments there are
  int sensible = 0;  // of them, the ones not spending a relic on suspicion; they come first
  view::Tokens way[kChips];
  view::Tokens give;  // the cost as asked, shown when it cannot be paid
  view::Tokens get;
  // What else it does; when it cannot be taken, then why not. shortNote is
  // only the why, for a closed option whose words need the room.
  char note[112] = {};
  char shortNote[64] = {};
  view::OptionState state = view::OptionState::Open;
};

enum class Panel : uint8_t { Options, Outcome, Foresight, Ways };

struct CardModel {
  int turn = 0;         // stamped into every tap that pays
  char title[64] = {};  // in capitals
  const char* flavor = "";
  int deck = 0;  // cards left before the next reshuffle
  int16_t held[underhand::kResources] = {};
  underhand::Odds odds;  // of each punishment before the next draw
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
// How many pages the last list of ways drawn had, so the audit can visit each.
int lastWaysPages();

void buildCard(toybox::Screen& screen, const CardModel& model);
void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildEnd(toybox::Screen& screen, const EndModel& model);
// The symbols, then the rules, a page each. Static.
void buildHelp(toybox::Screen& screen, int page);

}  // namespace underhandui
