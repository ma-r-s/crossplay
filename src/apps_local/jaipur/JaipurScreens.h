#pragma once

// The Jaipur screens, as freestanding builders over a model.
//
// No GfxRenderer, no Activity, no storage: a model in, a drawn frame out, which
// is what lets host-tests/ui/ assert what was drawn and what was made tappable.
// The market, the hand and the token piles are not here -- they are the app's
// own surface, and the activity draws them into the rect each builder returns.

#include "../ui/ToyboxScreen.h"

namespace jaipurui {

namespace fui = freeink::ui;

// Semantic actions. Every input path resolves to one of these, so a control
// cannot behave differently depending on which input reached it. The 200s
// belong to the shared link screens; these stay below them.
enum : fui::ActionId {
  ActionStartRow = 1,
  ActionCommit = 2,    // the capsule: take, exchange or sell, whatever it means
  ActionClear = 3,     // drop the current selection
  ActionContinue = 4,  // round over -> next round
  ActionPlayAgain = 5,
};

// The front door.
enum class StartRow : uint8_t { Continue, NewGame, PlayNearby, HowToPlay, Count };

struct StartModel {
  bool hasSavedGame = false;
  // The game you would be going back to: "ROUND 2, SEALS 1-0".
  const char* continueDetail = "";
  int played = 0;
  int won = 0;
  int selected = 0;
};

int startRows(const StartModel& model);
StartRow startRowAt(const StartModel& model, int visibleIndex);
const char* startRowLabel(StartRow row);

// Draws the menu and returns the rect left for the app's own artwork.
fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model);

// Playing. The capsule is the trigger and it names the one action the current
// selection means: TAKE DIAMOND, EXCHANGE 3, SELL 3 CLOTH -> 11. It is dithered
// and inert when that action is not legal, so the control never appears and
// disappears as you tap.
struct BoardModel {
  const char* status = "";
  // What just happened, under the rule: "THEY SOLD 3 SPICE FOR 11".
  const char* report = "";
  // True when the capsule is a live control rather than a readout.
  bool canCommit = false;
  // Something is selected, so there is something to clear.
  bool canClear = false;
  bool roundOver = false;
  bool gameOver = false;
  // Who you are playing, or null against the computer.
  const char* theirName = nullptr;
};

fui::Rect buildBoardChrome(toybox::Screen& screen, const BoardModel& model);

// The end of a round: both scores broken down, the seal that was won, and the
// one control that deals the next one.
struct RoundModel {
  int round = 1;
  int yourScore = 0;
  int theirScore = 0;
  // Goods tokens are face up all round, so both totals are public. Bonus tokens
  // are face down: you know your own, and of theirs you only ever knew the
  // count until this screen turned them over.
  int yourGoods = 0;
  int theirGoods = 0;
  int yourBonus = 0;
  int theirBonus = 0;
  int yourBonusCount = 0;
  int theirBonusCount = 0;
  int yourCamels = 0;
  int theirCamels = 0;
  // -1 when the camels tied and nobody took the token.
  int camelTokenSeat = -1;
  int yourSeals = 0;
  int theirSeals = 0;
  bool youWonRound = false;
  bool drawnRound = false;
  bool matchOver = false;
  const char* theirName = nullptr;
};

fui::Rect buildRoundOver(toybox::Screen& screen, const RoundModel& model);

}  // namespace jaipurui
