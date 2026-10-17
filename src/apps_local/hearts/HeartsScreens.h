#pragma once

// Hearts' screens, as freestanding builders. See ToyboxScreen.h for why screens
// are written this way: a free function over a plain model, drawing into a
// DrawTarget it was handed, so host-tests/ui/ can run them with a fake target
// and no device.
//
// Landscape, like Solitaire. The renderer does the rotation and tapToLogical()
// maps touches through the same transform, so everything below is plain 800x480
// arithmetic with no rotation in it.

#include "../cards/CardArt.h"
#include "../ui/ToyboxScreen.h"
#include "HeartsCore.h"

namespace heartsui {

namespace fui = freeink::ui;

// The band height, matching Solitaire's. In portrait the standard 76px header
// costs a tenth of the screen; in landscape it would cost a sixth of the
// dimension the table needs.
inline constexpr int16_t kHeaderBand = 56;

enum : fui::ActionId {
  ActionHandCard = 1,  // value is an index into the hand
  ActionButton = 2,    // value is a Button below
};

enum Button : int {
  ButtonConfirm = 0,  // pass these three / play on
  ButtonMenu = 1,
  ButtonHint = 2,
  ButtonHowTo = 3,
  ButtonHowToNext = 4,
};

// One seat, as the board needs to say it.
struct SeatView {
  const char* name = "";
  // One character, for a marker too small to hold a word. Y W N E are all
  // distinct, which is the only reason a single letter works here.
  char initial = '?';
  int total = 0;  // running game score
  int taken = 0;  // points taken this hand
  int cardsLeft = 0;
  bool isTurn = false;
  bool isMe = false;
  bool tookLastTrick = false;
};

// Where every tappable card ended up. The builder fills this while drawing and
// the activity reads it to turn a tap into a card, which is the rule three
// separate bugs in this project came from breaking: hit-testing shares the
// geometry that drew the pixels rather than recomputing it.
struct Layout {
  fui::Rect handCard[hearts::kHandSize] = {};
  int handCount = 0;
  fui::Rect trickCard[hearts::kSeats] = {};
  fui::Rect felt = {};
};

struct BoardModel {
  const hearts::Game* game = nullptr;
  SeatView seats[hearts::kSeats];

  // Which of my cards the rules will accept right now. An illegal card is drawn
  // dithered rather than hidden or silently inert: "why can't I play that" is
  // the first thing a new Hearts player asks, and a dead tap does not answer it.
  bool legal[hearts::kHandSize] = {};
  // Cards picked for the pass, during Phase::Passing.
  bool picked[hearts::kHandSize] = {};
  int pickedCount = 0;

  // One line of plain prose under the table saying what to do or what just
  // happened. This is the whole of the game's teaching, so it is a model field
  // rather than something the board works out: the activity knows why the state
  // changed and the builder does not.
  const char* status = "";
  const char* subStatus = "";
  bool showConfirm = false;
  const char* confirmLabel = "PASS";
  bool confirmEnabled = false;
};

// The table: scores, the trick, and your hand.
void buildBoard(toybox::Screen& screen, const BoardModel& model, Layout& layout);

// What the app opens on, and what Back returns to.
struct MenuModel {
  bool hasSave = false;
  int savedHand = 0;
  // The saved hand has been played out and scored; the game resumes on the
  // score screen, not mid-trick.
  bool savedHandDone = false;
  int bestPlace = 0;  // 1..4, 0 for never finished a game
  int gamesPlayed = 0;
  int gamesWon = 0;
  bool sharp = true;  // which opponent strength is selected
  // NEW GAME has been tapped once and is asking before it discards the save.
  bool confirmingNew = false;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);

// Between hands: what the hand cost, and the running totals.
struct ScoreModel {
  const hearts::Game* game = nullptr;
  SeatView seats[hearts::kSeats];
  bool gameOver = false;
};

void buildScore(toybox::Screen& screen, const ScoreModel& model);

// HOW TO PLAY.
//
// Hearts carries more rules than anything else on this shelf and two of them
// catch every new player: you may not LEAD a heart until one has been
// discarded, and nothing that costs a point may be played on the first trick.
// A game whose legal moves are mostly a list of what you may NOT do has to say
// so somewhere, and the board's dithered cards are only obvious once you have
// been told what dithering means.
struct HowToModel {
  int page = 0;
};

int howToPages();
void buildHowTo(toybox::Screen& screen, const HowToModel& model);

// Every line of the rules screen, so a suite can measure all of them in the
// real cuts rather than a handful somebody retyped into a test. A corpus
// written into a test is a corpus that stops matching the app.
// WHY A TAP WAS REFUSED. In the screens layer rather than the activity so a
// suite that may not touch a renderer can still walk every one of them and
// measure it in the real face. The activity picks which; this owns the words.
enum class Refusal : uint8_t {
  NotYourTurn = 0,
  TwoOfClubsOpens,
  MustFollowSuit,
  NoPointsFirstTrick,
  HeartsNotBroken,
  JustNo,
  Count,
};
const char* refusalText(Refusal why);

// THE STATUS LINE'S WORDS, for the same reason: the suite that measures them
// may not compile an Activity, and a corpus retyped into a test is a corpus
// that stops matching the app the first time somebody rewords a string.
//
// The formatted ones are printf templates. The suite instantiates each with the
// longest seat name and the largest number it can carry, which is the only
// instantiation worth measuring.
enum class Status : uint8_t {
  PickToPass = 0,  // one %s: the direction
  Thinking,        // one %s: a seat
  YourLeadFirst,   // fixed
  YourLead,        // fixed
  FollowSuit,      // one %s: a suit
  TakesIt,         // %s seat, %s verb
  TakesItPoints,   // %s seat, %s verb, %d points, %s plural
  HandOver,        // fixed
  GameOver,        // fixed
  Count,
};
const char* statusTemplate(Status which);

// The longest string any template can produce, rendered into `out`. Exposed so
// the width suite measures the worst case rather than a typical one.
void longestStatus(Status which, char* out, int size);

// The two halves of the board's sub-status, so both can be measured against
// the box they share.
const char* heartsStateText(bool broken);
const char* queenStateText(bool played);

int howToLines(int page);
const char* howToTitle(int page);
const char* howToLine(int page, int line);

}  // namespace heartsui
