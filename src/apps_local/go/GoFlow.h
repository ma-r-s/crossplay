#pragma once

// Go's navigation, its settings, and the one piece of state that lives between
// two taps. Freestanding.
//
// Two machines, as Checkers has: `Screen` is the shell and the only thing Back
// navigates, `Phase` is the game. The third thing here is the AIM -- the point
// a finger has chosen but not yet committed -- which belongs to neither.
//
// **A stone goes down in two taps, not one.** Go is played on intersections at
// a 49px pitch, which is under a fingertip, and a stone cannot be taken back in
// a match. So the first tap aims and the second commits, and tapping a
// different point moves the aim rather than playing there. It costs one tap on
// a move you were sure of and saves a game on the one you were not. Checkers
// already reads this way (pick, then place) and chess has read this way since
// it was written, so it is also the gesture this device has taught.

#include <cstdint>

#include "GoCore.h"

namespace go {

enum class Screen : uint8_t {
  // The top. Back from here leaves the app, and it is the only screen that does.
  Menu,
  // Everything configurable, off the front door. Three value rows is a busy
  // front door and a quiet settings screen, not the other way round.
  Settings,
  Board,
  // Both players passed. Dead stones are being agreed before anything is
  // counted. This screen exists because area scoring without it would make the
  // machine fill every neutral point, which reads as an idiot.
  Count,
  Result,
};

enum class Phase : uint8_t { Yours, Theirs, Finished };

// Who the other seat belongs to. Nearby is not here: a link match replaces the
// whole activity's turn source, and an app that stored "nearby" as an opponent
// would have two facts about one thing.
enum class Opponent : uint8_t { Computer, Human };

// Three levels, and they are three different players rather than one player
// given more time. See GoEngine.h.
enum class Level : uint8_t { Easy, Medium, Hard, Count_ };

constexpr Screen back(const Screen screen) {
  switch (screen) {
    case Screen::Menu:
      return Screen::Menu;
    case Screen::Settings:
      return Screen::Menu;
    case Screen::Board:
      return Screen::Menu;
    case Screen::Count:
      // Back out of counting stops the game rather than resuming it. Resuming
      // is a door on the Count screen itself, because "we disagree about what
      // is dead, play it out" is a decision, not an escape.
      return Screen::Menu;
    case Screen::Result:
      return Screen::Menu;
  }
  return Screen::Menu;
}

constexpr bool leavesApp(const Screen screen) { return screen == Screen::Menu; }

// Which screen a game's own stage belongs on. One fact, so the shell and the
// rules cannot disagree about whether the game is being played.
constexpr Screen screenFor(const go::Stage stage) {
  switch (stage) {
    case go::Stage::Playing:
      return Screen::Board;
    case go::Stage::Scoring:
      return Screen::Count;
    case go::Stage::Over:
      return Screen::Result;
  }
  return Screen::Board;
}

constexpr Phase phaseFor(const bool finished, const bool yourTurn) {
  if (finished) return Phase::Finished;
  return yourTurn ? Phase::Yours : Phase::Theirs;
}

constexpr bool acceptsTap(const Phase phase) { return phase == Phase::Yours; }

// No point aimed at.
constexpr int kNothingAimed = -1;

constexpr const char* levelName(const Level level) {
  switch (level) {
    case Level::Easy:
      return "EASY";
    case Level::Medium:
      return "MEDIUM";
    case Level::Hard:
      return "HARD";
    case Level::Count_:
      break;
  }
  return "MEDIUM";
}

constexpr Level nextLevel(const Level level) {
  switch (level) {
    case Level::Easy:
      return Level::Medium;
    case Level::Medium:
      return Level::Hard;
    case Level::Hard:
      return Level::Easy;
    case Level::Count_:
      break;
  }
  return Level::Medium;
}

// What happens if this point is tapped, given what is already aimed at. One
// function so that touch and any other route cannot disagree, and so the
// screen can draw the aim from the same answer the activity acts on.
enum class Tap : uint8_t {
  // Not a legal move: the tap does nothing and the panel does NOT repaint.
  // A repaint that comes back identical is what a bug looks like; see the
  // corner-mark note in Checkers.
  Ignore,
  // Aim here, or move the aim here.
  Aim,
  // The aim was already here: play it.
  Commit,
};

constexpr Tap tapMeaning(const Game& game, const int aimed, const int point, const bool yourTurn,
                         const bool legalHere) {
  if (!yourTurn) return Tap::Ignore;
  if (game.stage != static_cast<uint8_t>(go::Stage::Playing)) return Tap::Ignore;
  if (!legalHere) return Tap::Ignore;
  return point == aimed ? Tap::Commit : Tap::Aim;
}

// Whether a move is worth warning about before it is committed. Legal, and
// almost certainly a mistake: filling one's own eye, or dropping a stone into
// atari for nothing.
//
// This is the whole value of the two-tap placement. Without the pause there is
// nowhere to put the warning, and a beginner's commonest way of losing a group
// they had already won happens in silence.
enum class Caution : uint8_t { None, FillsOwnEye, SelfAtari };

inline Caution cautionFor(const Game& game, const int point, const uint8_t colour) {
  if (point < 0 || point >= kPoints) return Caution::None;
  if (isEye(game, point, colour)) return Caution::FillsOwnEye;
  if (libertiesAfter(game, point, colour) == 1) return Caution::SelfAtari;
  return Caution::None;
}

// Whether this seat has any legal move that is not filling one of its own eyes.
// When it has not, passing is the only sensible act and the board says so
// rather than leaving the player hunting for a point that is not there.
inline bool hasUsefulMove(const Game& game, const uint8_t colour) {
  for (int point = 0; point < kPoints; ++point) {
    if (legal(game, point, colour) && !isEye(game, point, colour)) return true;
  }
  return false;
}

}  // namespace go
