#pragma once

// Knucklebones, the two state machines. Freestanding: no renderer, no Activity.
//
// This file exists because "Back went to the wrong screen" is the bug this
// project keeps writing. It is not a bug you find by reading an activity, it is
// a bug you find by playing, which means you find it late or not at all. So the
// navigation is a table here rather than a pile of `if (showingHowTo)` inside
// the activity, and a host test asserts the properties that make it correct:
// Back is defined for every screen, every screen reaches the top, and exactly
// one screen leaves the app.
//
// Two machines, deliberately separate:
//
//   * `Screen` is the SHELL -- menu, how to play, the board, the result. This
//     is the one Back navigates.
//   * `Phase` is the GAME -- whose placement it is, and whether the match has
//     finished. Back never touches it.
//
// Mixing them is how the bug happens: if "am I in the how-to?" and "is it my
// turn?" live in the same set of flags, then Back has to know about turns and
// the turn logic has to know about the how-to.

#include <cstdint>

namespace knucklebones {

enum class Screen : uint8_t {
  // The top. Back from here leaves the app entirely, and it is the ONLY screen
  // that does; see leavesApp().
  Menu,
  HowTo,
  Board,
  // The result of a finished match. A separate screen from Board rather than a
  // flag on it, because the two accept different input: the board takes a
  // column, this takes an acknowledgement.
  Result,
};

// Every phase of actual play. Kept apart from Screen so that neither has to
// know the other exists.
enum class Phase : uint8_t {
  // This device's placement. The die is already rolled and shown: the roll
  // happens when the turn begins, so a player always sees what they must place
  // before choosing where.
  Yours,
  // The other seat, whether that is the built-in opponent or another device.
  // The screens do not distinguish them, and neither does anything else here.
  Theirs,
  Finished,
};

// Where Back goes from `screen`.
//
// Total by construction: an exhaustive switch with no default, so adding a
// screen without deciding where its Back goes fails the build rather than
// falling through to something plausible. That is the whole point of this
// function existing.
constexpr Screen back(const Screen screen) {
  switch (screen) {
    case Screen::Menu:
      // Handled by the caller, which calls shelf::leave(). Returning Menu keeps
      // this function total without inventing a fifth state for "gone".
      return Screen::Menu;
    case Screen::HowTo:
      return Screen::Menu;
    case Screen::Board:
      // Abandoning a match returns to the menu, not to the shelf. Two taps to
      // leave a game is right: the first is "stop playing", the second is
      // "leave". Going straight out would make Back mean two different things
      // depending on how deep you were.
      return Screen::Menu;
    case Screen::Result:
      return Screen::Menu;
  }
  return Screen::Menu;
}

// True for the one screen whose Back leaves the app. The activity asks this
// rather than comparing against Menu itself, so the rule lives here with the
// table it belongs to.
constexpr bool leavesApp(const Screen screen) { return screen == Screen::Menu; }

// Which phase a game state implies. Derived rather than stored, for the same
// reason the shelf derives its page: two facts that must agree are one fact.
constexpr Phase phaseFor(const bool matchOver, const bool yourTurn) {
  if (matchOver) return Phase::Finished;
  return yourTurn ? Phase::Yours : Phase::Theirs;
}

// Whether a tap on the board can do anything right now. The board is drawn in
// every phase -- you watch the opponent place -- but it only accepts a column
// on your own turn, and never once the match is over.
constexpr bool acceptsPlacement(const Phase phase) { return phase == Phase::Yours; }

}  // namespace knucklebones
