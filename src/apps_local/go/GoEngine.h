#pragma once

// The opponent. Freestanding C++17: no renderer, no Arduino, no heap, no
// exceptions, so the whole thing runs on a laptop and can be played against
// itself a thousand times in a host test.
//
// **Monte Carlo tree search, not alpha-beta.** Go has no usable hand-written
// evaluation function -- the reason computer Go was stuck at beginner level for
// thirty years is that "who is ahead" in a Go position cannot be counted the
// way material can in chess. What works instead is to play the position out at
// random to the end a few thousand times and count who won. That needs no
// knowledge, no opening book and no weights, which is also why it fits in a
// device with fifty kilobytes of flash to spare.
//
// The search runs on a COPY. `GoActivity` hands it one, because the render task
// reads the live board on its own task and a search makes and unmakes thousands
// of positions: chess put half a second of mid-search garbage on the panel that
// way, invisible in the simulator where the search finishes before the repaint.

#include <cstdint>

#include "GoCore.h"
#include "GoFlow.h"

namespace goengine {

// What a level actually is. Three different players, not one player given more
// time, and NOT one player told to blunder.
//
// The measurement behind this shape, from docs/apps/go.md: across the entire
// playout budget this device can reach, strength moves about 113 Elo per
// doubling, so the whole feasible range is three and a half ranks and it bottoms
// out in single-digit kyu. Thinking time alone therefore cannot produce a level
// a beginner can beat. Handicap can: on nine by nine a stone is worth about
// three ranks among single-digit kyu and more below, so two stones is a bigger
// step than every doubling this device can afford put together.
//
// So the ladder is built from HANDICAP AND KOMI, which is measurable and which
// cannot make the opponent look broken, because it never plays a deliberately
// worse move. What makes Easy feel like a weaker player rather than a spotted
// one is BLINDNESS: it searches a random part of the board each turn and misses
// things elsewhere, which is what being a beginner actually is.
struct Settings {
  // Playouts per move. A COUNT rather than a clock, because a count is
  // deterministic and a test can assert on it.
  uint16_t playouts;
  // Stones Black is given before the first move. The player is Black at the
  // easier levels; see `handicapFor`.
  uint8_t handicap;
  // Komi to White, in half points. Always odd, so no game it produces can tie.
  int16_t komiHalves;
  // Per mille of the legal moves the search REFUSES TO LOOK AT this turn, chosen
  // fresh each move. Zero means it considers everything.
  //
  // This is KaTrain's mechanism and it is the opposite of blunder injection:
  // every move it plays is one it thought about, so it never looks insane, but
  // it misses what it did not look at. A beginner does not weigh a capture and
  // decide against it; they do not see it.
  uint16_t blindPerMille;
};

Settings settingsFor(go::Level level);

// A level's opening: how many stones Black gets and what komi White takes. The
// two are one decision, so they are answered by one function and the activity
// cannot set half of it.
void openingFor(go::Level level, int& handicap, int16_t& komiHalves);

// The move this seat wants to play: a point, or go::kPass. Always legal in
// `game`, and always a move the caller may play without further checking.
//
// `seed` is advanced, so a caller that wants a repeatable game keeps it and a
// caller that wants variety does not.
int chooseMove(const go::Game& game, go::Level level, uint32_t& seed);

// Which stones cannot live, as a bit a point, for the counting screen.
//
// Answered by PLAYING THE POSITION OUT rather than by a life-and-death
// analyser: a few hundred playouts from the finished position, and a stone that
// belongs to the other colour at the end of most of them is dead. That reuses
// the machinery that already exists and it is what the strong programs do,
// because a hand-written analyser gets seki and bent-four wrong in ways nobody
// can debug on a device.
//
// It is a SUGGESTION. The players may flip any group, which is what the
// counting screen is for.
void estimateDead(const go::Game& game, uint32_t& seed, uint8_t out[(go::kPoints + 7) / 8]);

// Exposed for the tests: one random playout from `game`, returning the area
// score difference in half points from Black's point of view.
int playoutOnce(const go::Game& game, uint32_t& seed, bool policy);

// Whether passing right now would win for `colour`, counted on the board exactly
// as it stands with every stone alive.
//
// This is the Leela Zero pass rule, and it is what stops the two behaviours that
// make a Go program look broken to a human. It never passes while passing would
// lose, so it does not hand over a won game; and it passes the moment passing
// wins, so it does not fill every neutral point first. Filling them is not
// stupid -- under area scoring a neutral point is worth one -- but a human reads
// it as the machine not knowing the game is over.
bool passingWins(const go::Game& game, uint8_t colour);

// Exposed for ONE test, and it is the test that makes the second board safe.
//
// The search does not use `go::Game`: it plays on a smaller board with no
// history, no dead marks and no tallies, because the game's own board copies
// the whole board to answer one question, and a search asks that question
// times. Two implementations of one rulebook is exactly the shape that drifts,
// so `host-tests/go` plays hundreds of thousands of random positions through
// BOTH and asserts the boards are identical point for point. This is how it
// reaches the second one.
//
// Applies `point` to the fast board built from `game` and writes the resulting
// position back into `game.point`. Returns what the fast board said about
// legality. Nothing but the test calls it.
bool fastPlayForTest(go::Game& game, int point);

}  // namespace goengine
