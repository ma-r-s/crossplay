#pragma once

// WAVELENGTH's save file, as bytes.
//
// Freestanding, like the rules beside it, so host-tests/wavelength/ can prove
// a round survives being written and read back without a card, a renderer or
// an Arduino. That matters more here than in most of the fork: what this file
// carries is a game in progress, and the only way to find out it was carrying
// it wrongly used to be a table losing a round.
//
// WHY A ROUND IN FLIGHT IS IN HERE AT ALL. Deep sleep on this chip is a chip
// reset, and the Home key destroys the activity without asking. Before this
// existed, both threw away the round, the hidden number and the session score,
// and a cold tester found it by pressing Home one key away from Back. Anything
// the app is not willing to lose has to be on the card BEFORE the event, not
// written on the way out of it -- see chess, which saves on the completed move
// rather than trusting onExit().
//
// THE HIDDEN NUMBER IS IN HERE, IN THE CLEAR. It has to be: re-drawing it on
// resume would break the clue already spoken out loud, and would hand the
// clue-giver the re-deal the pause screen exists to prevent. What protects it
// is where it lives. `/.crosspoint/` is a dot directory, and both File
// Transfer and WebDAV refuse to list or serve those unless the owner turns on
// Show Hidden Files in Settings; so reading this file needs either the card in
// a reader or a deliberate settings change made on the device everyone at the
// table is looking at. Both are louder than the cheat they buy, and both are
// equally good for reading the firmware. Obfuscating the byte would be
// theatre, since the mask would sit in the same file and this source is
// public. The route that DID need engineering is the in-app one, and it is
// closed in WavelengthActivity: a resume never lands on a screen that draws
// the number when the game says it is hidden.

#include <cstddef>
#include <cstdint>

#include "WavelengthCore.h"

namespace wavelength {

// Version 1 held the all-time record and the seen deck and nothing else, so a
// card written by any build up to v1.12.4 still loads: v2 appends, and unpack()
// leaves the session blank when the bytes stop at 1's payload.
inline constexpr uint8_t kSaveVersionLegacy = 1;
inline constexpr uint8_t kSaveVersion = 2;

// version byte + record (16) + deck (kSeenWords * 4) + the session block (21).
inline constexpr size_t kLegacyBytes = 1 + 16 + kSeenWords * 4;
inline constexpr size_t kSaveBytes = kLegacyBytes + 21;

// Everything the app must remember to be closed and reopened without a table
// noticing. `screen` and `resumeScreen` are OPAQUE here: the activity owns
// what a screen number means, and this layer only has to carry it back
// unchanged. Zero is the front door, which is also "no round in flight".
struct Saved {
  Record record;
  uint32_t seen[kSeenWords] = {};

  Session session;
  bool sessionStarted = false;
  uint16_t abandoned = 0;  // rounds walked out of, this session

  uint8_t screen = 0;
  uint8_t resumeScreen = 0;
  int16_t spectrum = -1;
  int16_t choice[2] = {-1, -1};
  uint8_t dealt = 0;
  uint8_t target = 0;
  uint8_t guess = 0;
  uint8_t lastPoints = 0;
  bool hasPeeked = false;
  bool practiceRound = true;
  bool callWasRight = false;
  bool abandonedRound = false;

  // Forget the round without forgetting the evening. Used when a session is
  // deliberately ended, because a round that outlives the session that
  // contained it is its own bug: it would resume into a board whose score has
  // been cleared out from under it.
  void clearRound();
};

// Writes kSaveBytes and returns how many it wrote, or 0 if `cap` is short.
size_t pack(const Saved& in, uint8_t* out, size_t cap);

// Reads either version. False means the bytes are not a save file this build
// understands, and the caller starts fresh rather than guessing.
//
// A round whose numbers are outside the rules is DROPPED rather than resumed:
// a truncated or half-written file must not be able to put the game on a
// screen describing a slot that does not exist.
bool unpack(const uint8_t* in, size_t len, Saved& out);

}  // namespace wavelength
