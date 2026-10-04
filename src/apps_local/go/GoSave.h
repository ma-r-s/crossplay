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
// The GAME is validated only when `inProgress` says there is one; see unpack().
//
// 5 widened the ko and the last move to two bytes for nineteen by nineteen,
// which also moved kPass and kNoPoint, and grew every array to 361 points.
// 4 added the board size, in three places: the setting, the size of the game in
// progress, and the size of the last finished position the front door draws.
// 3 added the handicap, which became a setting of its own.
// 2 added `Game::accepted`, which is who has agreed the count. A v1 file has
// one fewer number on the line and is refused rather than misread: the record
// in it is a handful of integers and the game is one position, and neither is
// worth a migration nobody will ever test again.
constexpr int kVersion = 5;

// The longest line pack() can write, worst case, with room to spare: 361
// last-game points at two characters, 91 board bytes and 46 dead-mask bytes at
// up to four, and the rest. The suite packs the worst case against it, and the
// activity sizes its read and write buffers by it.
constexpr int kMaxLine = 2048;

struct Save {
  int wins = 0;
  int losses = 0;

  // The last finished game, for the front door's ornament. One byte a point
  // here rather than the game's packed pair of bits: this is a picture, not a
  // position, and nothing plays on it.
  bool hasHistory = false;
  uint8_t lastPoints[go::kMaxPoints] = {};
  uint8_t lastSize = go::kSmallSize;
  bool lastWon = false;
  int lastMarginHalves = 0;

  // The settings, because a player who chose HARD once meant it.
  go::Opponent opponent = go::Opponent::Computer;
  go::Level level = go::Level::Medium;
  uint8_t playAs = go::kBlack;
  int handicap = 0;
  // The board the NEXT new game is played on. Separate from `game.size`, which
  // is the board the game in progress is already on: changing the setting must
  // not reinterpret a position under way.
  int boardSize = go::kSmallSize;

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
