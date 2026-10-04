#pragma once

// Go's navigation, its settings, and the one piece of state that lives between
// two taps. Freestanding.
//
// Two machines, as Checkers has: `Screen` is the shell and the only thing Back
// navigates, `Phase` is the game. The third thing here is the AIM -- the point
// a finger has chosen but not yet committed -- which belongs to neither.
//
// **A stone goes down in two taps, not one.** Go is played on intersections at
// a 49px pitch on nine lines and 33px on thirteen, which is at or under a
// fingertip, and a stone cannot be taken back in a match. So the first tap aims
// and the second commits, and tapping a different point moves the aim rather
// than playing there. It costs one tap on a move you were sure of and saves a
// game on the one you were not -- and it is what makes the larger board
// offerable at all. Checkers already reads this way (pick, then place) and
// chess has read this way since it was written, so it is also the gesture this
// device has taught.

#include <cstdint>

#include "GoCore.h"

namespace go {

enum class Screen : uint8_t {
  // The top. Back from here leaves the app, and it is the only screen that does.
  Menu,
  // Everything configurable, off the front door. Five value rows is a busy
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

// The three boards, in the order the setting steps through them. A setting
// rather than a constant, and it takes effect on the next NEW game: changing the
// board under a game in progress would have to either discard it or
// reinterpret its stones, and both are worse than waiting.
constexpr int nextBoardSize(const int size) {
  return size == kSmallSize ? kLargeSize : (size == kLargeSize ? kFullSize : kSmallSize);
}

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

// Whether tapping ACCEPT ends the game, given who is sitting opposite.
//
// Against the MACHINE it ends only if the machine agrees with the marking. This
// is the standard stone-removal flow every Go program uses: both sides mark,
// both sides accept, and a disagreement resumes play so the board settles it
// rather than the louder party. Before this, one tap recorded agreement for
// both colours, so the count was whatever the player said it was and a game
// could be won or lost to order.
//
// Two people sharing one device settle it between themselves: they are sitting
// together looking at the same screen, and neither can cheat the other.
//
// A nearby match is not this function's business -- there, both seats accept in
// their own time and `Game::accepted` carries it across the wire.
constexpr bool acceptEndsTheGame(const Opponent opponent, const bool machineAgrees) {
  return opponent == Opponent::Human || machineAgrees;
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
  if (point < 0 || point >= game.points()) return Caution::None;
  if (isEye(game, point, colour)) return Caution::FillsOwnEye;
  if (libertiesAfter(game, point, colour) == 1) return Caution::SelfAtari;
  return Caution::None;
}

// Whether this seat has any legal move that is not filling one of its own eyes.
// When it has not, passing is the only sensible act and the board says so
// rather than leaving the player hunting for a point that is not there.
inline bool hasUsefulMove(const Game& game, const uint8_t colour) {
  const int points = game.points();
  for (int point = 0; point < points; ++point) {
    if (legal(game, point, colour) && !isEye(game, point, colour)) return true;
  }
  return false;
}

// How many empty points still belong to NOBODY and can be played.
//
// Under area scoring this is the whole of "is there anything left worth
// playing": a point already surrounded by one colour counts for them whether or
// not a stone sits on it, so taking it gains nothing, while a point belonging to
// nobody is worth one to whoever takes it.
//
// It is one function because two things need the same answer and must never
// give different ones. The engine passes only when its lead exceeds this count,
// and the board tells the player this many points are still free when their own
// pass did not end the game. A screen saying "nothing left" beside an opponent
// that keeps playing is the fault this exists to prevent.
inline int freePoints(const Game& game, const uint8_t colour) {
  uint8_t owner[kMaxPoints];
  territory(game, owner);
  int free = 0;
  const int points = game.points();
  for (int point = 0; point < points; ++point) {
    if (game.at(point) != kEmpty) continue;
    if (owner[point] != kEmpty) continue;
    if (!legal(game, point, colour)) continue;
    ++free;
  }
  return free;
}

// Whether the board's one spoken line should explain a pass that was played
// through, or leave the row to something more urgent.
//
// Ranked below the three things that answer a more pressing question -- why the
// board came back on its own, that the machine is still thinking, and what is
// wrong with the point under a finger -- and above "YOUR MOVE". It lives here
// rather than inside the screen builder because the Go board has no coverage in
// the screen suite, and a precedence nobody can assert is a precedence that
// drifts. The `thinking` term is not decoration: without it the explanation
// replaces THINKING from the SECOND pass onward, since the flag is still set
// from the pass before while the next search runs.
constexpr bool explainsPlayedOn(const bool itPlayedOn, const int freePoints, const bool disagreed, const bool thinking,
                                const Caution caution) {
  return itPlayedOn && freePoints > 0 && !disagreed && !thinking && caution == Caution::None;
}

}  // namespace go
