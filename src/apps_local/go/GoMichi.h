#pragma once

// The bridge between this app's rules and michi-c2's engine.
//
// Two boards again, and for the same reason the last engine had two: `go::Game`
// is the game -- the wire format, the save payload, the thing the screens draw
// -- and michi has its own `Position`, with a bordered array, block ids and
// incremental liberty sets it needs to be fast. Neither wants to be the other.
//
// So this file owns the one conversion, in one direction, and every michi
// symbol stays behind it: nothing else in the app includes a michi header, and
// michi never learns that go::Game exists.

#include <cstdint>

#include "GoCore.h"
#include "GoFlow.h"

namespace gomichi {

// A millisecond clock, lent by the caller. The engine cannot call millis() and
// must not: a null clock means "bounded by simulations alone", which is what
// the host tests pass so their results do not depend on how fast the machine
// running them happens to be.
using Clock = uint32_t (*)();

// What one level asks of the engine.
struct Settings {
  // Simulations a move. michi calls these N_SIMS; each is one tree descent plus
  // one playout to the end of the game.
  uint16_t simulations;
  // The wall clock it may spend, whatever the count says. Whichever binds
  // first wins: the count keeps the tests deterministic (they lend no clock at
  // all), the clock keeps the device under five seconds on every board. The
  // clock is read inside the search's own loop, so this is a real bound rather
  // than an estimate, and it costs at most one simulation of overshoot; see
  // MichiBridge.c.
  uint16_t budgetMs;
};

Settings settingsFor(go::Level level);

// The move this seat wants, as a `go::` point index or `go::kPass`. Always
// legal in `game`.
int chooseMove(const go::Game& game, go::Level level, uint32_t& seed, Clock clock = nullptr);

// What the last search actually cost: simulations run, and milliseconds spent.
// The budget above is a promise; these two are what happened, and the device log
// prints them after every move so the promise is answerable to a measurement.
int lastSimulations();
uint32_t lastMs();

// What the engine's position says its ko, last move and move count are, in this
// app's terms. The ko point, or kNoPoint; the last move, kPass, or kNoPoint.
// Only the suite asks: it is how "the engine was told about the ko" is checked
// directly rather than guessed from the move that came back.
struct Context {
  int ko;
  int lastMove;
  int moveNumber;
};
Context lastContext(int size);

// Drops the search tree. The engine stays up; the next move builds a new one.
// Called when the app closes: the tree is hundreds of kilobytes of PSRAM and
// nothing reads it between games.
void forget();

}  // namespace gomichi
