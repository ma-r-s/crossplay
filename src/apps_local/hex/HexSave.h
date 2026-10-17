#pragma once

// What Hex writes down, and how. Freestanding: no storage, no renderer, so the
// round trip is host-tested rather than discovered on a device.
//
// Three things are saved and they have different lifetimes. The RECORD and the
// SETTINGS outlive every game; the LAST FINISHED position is the front door's
// ornament; the GAME IN PROGRESS is one position and is cleared the moment it
// ends. Wavelength shipped an onExit() that wrote the first and not the last,
// and a cold tester lost a round by pressing Home one key from Back -- a field
// that is never written cannot be recovered by fixing when the write happens.
//
// **A short line is accepted, a wrong version is not.** The two failures are
// not the same shape. A file written by a build with fewer fields is missing a
// TAIL, so everything it does carry is still in the place this build looks for
// it and the rest can take its default -- which is how a player who set HARD
// once keeps it across the release that added the ornament. A file with a
// different version number is a file whose fields may have MOVED, and reading
// one of those as far as it goes does not fail at the missing number: it reads
// the next one in its place and shifts a whole board along by one. That is a
// game that loads and is wrong, which is worse than one that does not load.
//
// So `unpack` walks forward through three checkpoints -- settings, ornament,
// game -- and commits whatever it reached. Nothing before a checkpoint is ever
// half-applied: the whole thing is parsed into a local and copied over at the
// end.

#include <cstdint>

#include "HexCore.h"
#include "HexFlow.h"

namespace hexsave {

// Bumped whenever a field is INSERTED or its meaning changes. Appending to the
// end does not need it, which is the whole point of the checkpoints above.
constexpr int kVersion = 1;

struct Save {
  int wins = 0;
  int losses = 0;

  // The settings, because a player who chose HARD once meant it.
  hex::Opponent opponent = hex::Opponent::Computer;
  hex::Level level = hex::Level::Normal;
  uint8_t playAs = hex::kBlack;

  // The last finished game, for the front door's ornament. The packed board
  // only: nothing plays on it, so it needs no union-find and no turn.
  bool hasHistory = false;
  uint8_t lastCells[hex::kCellBytes] = {};
  bool lastWon = false;

  // A game part-played. `inProgress` is false when there is nothing to resume,
  // and `game` is then not read at all.
  bool inProgress = false;
  hex::Game game{};
  uint8_t seat = hex::kBlack;
};

// What a packed line needs, with room: the version and five settings, two flags
// and 31 ornament bytes, then the game -- 31 packed cells, 125 union-find
// parents and five more fields -- each written as up to three digits and a
// space, plus the newline.
//
// Stated HERE rather than spelled at each buffer, and that is not tidiness. A
// caller whose buffer is too small gets `pack()` returning 0 and, on the way
// back in, a line that stops early -- which this format is built to ACCEPT.
// So the failure of a buffer that drifted below the real length is not an
// error anybody sees: it is a save that quietly stops carrying the game.
constexpr int kMaxLineBytes = 1400;

// The tally and the ornament, as the four facts a link teardown has to decide
// about. Split out of `Save` as a type of its own because that decision is the
// one piece of this file's behaviour that is neither packing nor parsing, and
// it is the piece that was wrong.
struct Record {
  int wins = 0;
  int losses = 0;
  bool hasHistory = false;
  bool lastWon = false;
};

// The record to hold after a match ends and the solo game is restored.
//
// `counted` is what this device has in MEMORY: `onMatchEnded()` counted the
// match there and could not write it, because writing is refused for the whole
// length of a match -- the position on screen is the shared game and the file
// is what the solo game resumes from. `onCard` is what the reload at teardown
// just brought back, which is the tally as it stood BEFORE the match.
//
// Whichever has seen more games wins. Without this the reload takes the
// pre-match tally straight back over the counted one three lines later: no
// crash, no log, just a record that never moves -- the same silence five games
// in this fork shipped by counting in `gameLoop()` instead of `onMatchEnded()`,
// one door further along.
Record recordAfterLink(const Record& counted, const Record& onCard);

// Text, space separated, one line. Returns the bytes written, or 0 when the
// buffer could not hold it -- never a truncated line, because a truncated line
// parses as a shorter save rather than as a failure, and this file's whole
// tolerance rule is built on a short line being honest.
//
// `capacity` should be kMaxLineBytes; anything smaller is a buffer that may
// silently cost the game in progress.
int pack(const Save& save, char* out, int capacity);

// Parses what pack() wrote, including a line written by a build with fewer
// fields. Returns false and leaves `save` untouched only when the version does
// not match or the very first numbers are missing.
bool unpack(const char* text, Save& save);

}  // namespace hexsave
