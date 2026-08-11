#pragma once

// Navigation, and the question the board is currently asking. Freestanding: no
// renderer, no Activity, no storage, so both are host-tested with no device.
//
// Two machines, kept apart on purpose. The shell machine is where you are in
// the app; the draft machine is how far through a move you are. If those two
// shared a flag, Back would have to know about turns, which is how "Back went
// to the wrong screen" happens.

#include <cstdint>

#include "ToyBattleBrain.h"
#include "ToyBattleCore.h"

namespace toybattle {

// ---------------------------------------------------------------------------
// The shell
// ---------------------------------------------------------------------------

// What the player is here to do. Two modes rather than three: link is the only
// two-player mode, because a game whose only hidden thing is your own rack does
// not need a pass-the-device screen badly enough to earn one.
enum class Mode : uint8_t { Solo = 0, Link };

// Everything chosen before a game starts. Small, flat and copyable, because in a
// link game the host sends this to the guest and both devices must agree on it
// exactly -- a setting either side got to decide for itself is a desync waiting
// for the first special base.
struct Options {
  // Castle Field, which IS terrain 0. Spelled out rather than written as a
  // bare 0 so it cannot silently become whatever ends up first.
  uint8_t terrain = static_cast<uint8_t>(TerrainId::CastleField);
  Skill skill = Skill::Sergeant;
  bool specialBases = true;
  Mode mode = Mode::Solo;
};

// The shell.
//
//   Menu     the top, and the only way out of the app
//   Setup    map, difficulty, special bases, and the button that starts it
//   MapPick  the terrain list, which needs a screen once there are eight
//   Lobby    link only: looking for a device, and waiting for the host to choose
//   HowTo    the rules, paginated
//   Board    playing
//   Brief    what this terrain's special bases do, read from the board
//   Result   who won
//
// Brief is reachable from the board because that is where the question occurs to
// you, and it is the one screen whose Back is not the menu.
enum class Screen : uint8_t { Menu = 0, Setup, MapPick, Lobby, HowTo, Board, Brief, Result };
constexpr int kScreenCount = 8;

// Where Back goes from `screen`.
//
// Exhaustive switch, no default, so a new screen without a decided Back fails
// the build rather than falling through to something plausible.
//
// Deliberately a function of the screen alone, not of the mode. Leaving Setup or
// the Board in a link game also ends the session, but that is the activity's job
// to carry out, not a second destination: a Back that depended on the mode would
// be the shell machine reading the game machine, which is the coupling the split
// exists to prevent.
constexpr Screen back(const Screen screen) {
  switch (screen) {
    case Screen::Menu:
      // The caller leaves the app; returning Menu keeps this total without
      // inventing a state for "gone".
      return Screen::Menu;
    case Screen::Setup:
      return Screen::Menu;
    case Screen::MapPick:
      return Screen::Setup;
    case Screen::Lobby:
      return Screen::Menu;
    case Screen::HowTo:
      return Screen::Menu;
    case Screen::Board:
      // Abandoning a board returns to the menu, not out of the app: the first
      // Back means stop playing, the second means leave.
      return Screen::Menu;
    case Screen::Brief:
      // Straight back to the game you were reading it for.
      return Screen::Board;
    case Screen::Result:
      return Screen::Menu;
  }
  return Screen::Menu;
}

// Menu is the only screen that leaves the app, and that is a property worth
// stating rather than leaving implied by `back(Menu) == Menu`.
constexpr bool leavesApp(Screen s) { return s == Screen::Menu; }

// ---------------------------------------------------------------------------
// Continuing
// ---------------------------------------------------------------------------
//
// A game in progress, as bytes. `Game` is a POD with an exact-size assert
// because it is already the link layer's wire format, so the save is that
// struct plus what was chosen before it started -- there is no second
// description of a position anywhere in this app, and so no pair of
// descriptions that can disagree.
//
// Solo only. A link game cannot be resumed because the other device is not
// there to resume it, and a save that quietly restarts as a solo game against
// the brain would be a worse outcome than no save.

struct Saved {
  Options options;
  Game game;
  uint8_t seat = 0;
};

// Bytes needed by `encodeSave`. Fixed: there is nothing variable in a position.
constexpr int kSaveBytes = 8 + static_cast<int>(sizeof(Options)) + static_cast<int>(sizeof(Game)) + 1;

// Writes exactly `kSaveBytes` into `out`. Returns the count written.
int encodeSave(const Saved& saved, uint8_t* out);

// Reads one back. Returns false -- and leaves `saved` untouched -- if the bytes
// are the wrong length, the wrong magic, a version this build does not know, a
// terrain index this build does not have, or fail the checksum. A truncated or
// half-written save on an SD card that lost power is the ordinary case here,
// not the exotic one, so every one of those is a return rather than a crash.
bool decodeSave(const uint8_t* in, int length, Saved& saved);

// A position worth offering to continue: still being played, and not a link
// game. Checked on the way in and on the way out, so a finished game cannot be
// written as a save nor offered as one if it somehow was.
bool isResumable(const Saved& saved);

// ---------------------------------------------------------------------------
// Composing a move
// ---------------------------------------------------------------------------

// What the board is waiting for. Derived from the draft and the position, never
// stored: a stored mode and a half-built move are two facts that must agree,
// and they are exactly the two that drift.
enum class Ask : uint8_t {
  Troop,        // pick one off your rack
  Slot,         // pick where it goes
  JumboVictim,  // which adjacent enemy troop Jumbo discards
  DrawOffer,    // Skully or Star: take the draw or decline it
  StealOffer,   // XB-42: shoot into their rack or decline
  ChainOffer,   // Cap'n: place a second troop or decline
  RecallFrom,   // which of your other troops comes home
  ShoveFrom,    // which adjacent enemy troop gets moved
  ShoveTo,      // and where it goes
  ExhumeKind,   // which of your discarded troops comes back
  BaseOffer,    // a special base with nothing to choose: take it or decline
  Ready,        // nothing left to decide
};

// A move under construction. The move itself is the state; `answered` records
// which optional questions have been put to the player, because "declined" and
// "not asked yet" are different things and only one of them may be committed.
struct Draft {
  Move move;
  uint8_t step = 0;  // which link of a Cap'n chain is being composed
  bool slotChosen = false;
  bool effectAnswered = false;
  bool baseAnswered = false;

  void clear() { *this = Draft{}; }
  bool empty() const { return move.stepCount == 0 && !slotChosen; }
};

// The question this draft still has to answer, given the position. Ask::Ready
// means `draft.move` is complete and can be applied.
Ask pending(const Game& game, const Draft& draft);

// --- answering -------------------------------------------------------------
//
// Each returns false and changes nothing if it is not the answer to the
// question actually being asked, so a stray tap cannot half-build a move.

bool answerTroop(const Game& game, Draft& draft, Troop kind);
bool answerSlot(const Game& game, Draft& draft, int slot);
// Yes or no to whichever offer is pending (draw, steal, chain, or a special
// base with no target to choose).
bool answerOffer(const Game& game, Draft& draft, bool take);
// The base or troop kind a targeted question is waiting for.
bool answerTarget(const Game& game, Draft& draft, int slotOrKind);

// --- saying no, and saying why ----------------------------------------------

// Why a tap was refused. The board can always explain itself because the rules
// can name the clause that failed -- the alternative is a screen that ignores
// you and leaves you to guess which of six rules you broke.
enum class Refusal : uint8_t {
  None = 0,      // it was allowed
  NotYours,      // that troop is not on your rack
  Pinned,        // Battlefield has it lying facedown this turn
  NoPath,        // nothing you hold reaches that far
  TooWeak,       // strictly lower strength is the rule
  OwnHq,         // you may never place on your own
  Gated,         // Tropical Pool takes only its printed values
  Nullified,     // Hook's waiver is an effect, and effects do not work there
  NotATarget,    // a question was being asked, and that is not one of its answers
  NothingAsked,  // no question is open, so the tap means nothing
};

// Why `slot` cannot be answered right now. Refusal::None means it can.
Refusal whyNotSlot(const Game& game, const Draft& draft, int slot);
// Why `kind` cannot be picked off the rack.
Refusal whyNotTroop(const Game& game, const Draft& draft, Troop kind);

// --- what the board should light up -----------------------------------------

// The slots a tap could legally land on right now, as a mask over slots. Empty
// when the pending question is not about a slot. The board draws markers from
// this and hit-tests against the same mask, so the two cannot disagree.
uint64_t candidateSlots(const Game& game, const Draft& draft);

// The troop kinds the rack should offer. Bitmask over Troop.
uint8_t candidateTroops(const Game& game, const Draft& draft);

}  // namespace toybattle
