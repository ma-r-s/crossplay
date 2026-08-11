#pragma once

// Navigation, and the question the board is currently asking. Freestanding: no
// renderer, no Activity, no storage, so both are host-tested with no device.
//
// Two machines, kept apart on purpose. The shell machine is where you are in
// the app; the draft machine is how far through a move you are. If those two
// shared a flag, Back would have to know about turns, which is how "Back went
// to the wrong screen" happens.

#include <cstdint>

#include "ToyBattleCore.h"

namespace toybattle {

// ---------------------------------------------------------------------------
// The shell
// ---------------------------------------------------------------------------

// Brief is the map briefing: what this terrain's special bases do. It is
// reachable from the board, because that is where the question occurs to you.
enum class Screen : uint8_t { Menu = 0, Setup, HowTo, Board, Brief, Result };
constexpr int kScreenCount = 6;

// Where Back goes from `screen`.
//
// Exhaustive switch, no default, so a new screen without a decided Back fails
// the build rather than falling through to something plausible.
constexpr Screen back(const Screen screen) {
  switch (screen) {
    case Screen::Menu:
      // The caller leaves the app; returning Menu keeps this total without
      // inventing a state for "gone".
      return Screen::Menu;
    case Screen::Setup:
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
