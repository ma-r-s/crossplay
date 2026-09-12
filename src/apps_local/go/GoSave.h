#pragma once

// What Go writes down, and how. Freestanding: no storage, no renderer, so the
// round trip is host-tested rather than discovered on a device.
//
// Two things are saved and they have different lifetimes. The RECORD is a
// device's history and outlives every game EXCEPT across a format change, where
// the whole file is refused (see kVersion); the GAME IN PROGRESS is one
// position and is cleared the moment it finishes. Wavelength shipped an
// onExit() that wrote the first and not the second, and a cold tester lost a
// round by pressing Home one key from Back. A field that is never written
// cannot be recovered by fixing when the write happens.

#include <cstdint>

#include "GoCore.h"
#include "GoFlow.h"

namespace gosave {

// Bumped whenever the layout changes, and an older file is REFUSED.
//
// Accepting one would be better and is not possible as this file is written:
// every field after the header is positional, so a v1 line read as v2 takes the
// next number in the missing one's place and shifts a whole board along by one.
// A game that loads and is wrong is worse than one that does not load. What it
// costs is the record, which is two integers.
//
// 2 added `Game::accepted`, which is who has agreed the count. A v1 file has
// one fewer number on the line and is refused rather than misread: the record
// in it is a handful of integers and the game is one position, and neither is
// worth a migration nobody will ever test again.
constexpr int kVersion = 2;

struct Save {
  int wins = 0;
  int losses = 0;

  // The last finished game, for the front door's ornament.
  bool hasHistory = false;
  uint8_t lastPoints[go::kPoints] = {};
  bool lastWon = false;
  int lastMarginHalves = 0;

  // The settings, because a player who chose HARD once meant it.
  go::Opponent opponent = go::Opponent::Computer;
  go::Level level = go::Level::Medium;
  uint8_t playAs = go::kBlack;

  // A game part-played. `inProgress` is false when there is nothing to resume,
  // and `game` is then not read at all.
  bool inProgress = false;
  go::Game game{};
  uint8_t seat = go::kBlack;
};

// Text, space separated, one line. Returns the bytes written, or 0 when the
// buffer could not hold it -- never a truncated line, because a truncated line
// parses as a shorter game rather than as a failure.
int pack(const Save& save, char* out, int capacity);

// Parses what pack() wrote. Returns false and leaves `save` untouched on
// anything it does not understand, so a half-written file costs the ornament
// and not the record.
bool unpack(const char* text, Save& save);

}  // namespace gosave
