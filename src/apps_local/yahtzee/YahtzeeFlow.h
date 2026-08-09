#pragma once

// Yahtzee's navigation, and the two things the card owes the player.
// Freestanding: no renderer, no Activity.

#include <cstdint>

#include "YahtzeeCore.h"

namespace yahtzee {

enum class Screen : uint8_t {
  // The top. Back from here leaves the app, and it is the only screen that does.
  Menu,
  HowTo,
  Card,
  Result,
};

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
    case Screen::HowTo:
      return Screen::Menu;
    case Screen::Card:
      // Abandoning a game returns to the menu, not out of the app: the first
      // Back means stop playing, the second means leave.
      return Screen::Menu;
    case Screen::Result:
      return Screen::Menu;
  }
  return Screen::Menu;
}

constexpr bool leavesApp(const Screen screen) { return screen == Screen::Menu; }

// The turn's own three states, derived from the game and never stored.
//
// Yahtzee needs this second machine where Minesweeper did not, and the reason
// is worth writing down: "whose turn" is not a rules concept a solitaire has,
// but the state WITHIN a turn is. A turn is not one action, it is up to three
// rolls and then exactly one box, and the difference between them decides what
// every control on the screen does.
enum class Stage : uint8_t {
  // Nothing rolled. The only thing you can do is roll; the dice on screen are
  // the previous player's and mean nothing.
  Fresh,
  // Rolled, with a roll still in hand. You may keep dice and roll again, or
  // stop here and take a box.
  Rolling,
  // Three rolls gone. The dice are final and a box must be taken.
  Spent,
};

constexpr Stage stageFor(const uint8_t rollsUsed) {
  if (rollsUsed == 0) return Stage::Fresh;
  return rollsUsed < kRollsPerTurn ? Stage::Rolling : Stage::Spent;
}

inline Stage stageOf(const Game& game) { return stageFor(game.rollsUsed); }

// The screen a game's own state implies, so the activity never decides it twice.
inline Screen screenFor(const Game& game) { return over(game) ? Screen::Result : Screen::Card; }

// Which boxes a tap can take right now, as a mask over the thirteen.
//
// Zero before the first roll: with no dice there is nothing to score, and a row
// that accepts a tap then would write a number the player never rolled. Under
// the Joker rule it collapses to a single box, which is the one case where a
// player taps a free row and is refused -- so the card has to show it. See
// docs/yahtzee.md.
inline uint16_t takeableBoxes(const Game& game) {
  if (over(game) || game.rollsUsed == 0) return 0;
  const Card& card = game.card[game.turn];
  uint16_t mask = 0;
  for (int i = 0; i < kCategories; ++i) {
    if (canScore(card, game.die, static_cast<Category>(i))) mask |= static_cast<uint16_t>(1u << i);
  }
  return mask;
}

// How far the upper section still has to go for the 35-point bonus, or 0 once
// it is safe. Negative would mean it is already banked, which `bonusEarned`
// answers; this is only ever the shortfall.
//
// It is on the screen because it is the one number in Yahtzee a player has to
// keep in their head, and the card in the box has a printed box for exactly
// this. Deriving it is free; making the player add up six numbers is not.
inline int upperShortfall(const Card& card) {
  const int have = upperTotal(card);
  return have >= kUpperBonusThreshold ? 0 : kUpperBonusThreshold - have;
}

// Whether the bonus is still reachable at all: every unfilled upper box scoring
// its maximum still has to clear 63.
//
// Shown so a player stops chasing it. Without this the shortfall keeps counting
// down toward a number that can no longer be reached, which is worse than not
// showing it.
inline bool upperBonusStillPossible(const Card& card) {
  int best = upperTotal(card);
  for (int i = 0; i < kUpperEnd; ++i) {
    if (card.box[i] == kUnscored) best += (i + 1) * kDice;
  }
  return best >= kUpperBonusThreshold;
}

}  // namespace yahtzee
