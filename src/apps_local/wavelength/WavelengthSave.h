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
// leaves the session blank when the bytes stop at 1's payload. v3 appends the
// boot the save was written in, and a v2 card simply arrives with none -- which
// resumeFor() reads as "cannot know", which is the honest answer for a file
// written by a build that never recorded it.
//
// EVERY VERSION'S LENGTH IS NAMED, not just the current one. The check that
// decides whether a session block is present used to compare against "the whole
// file", so appending these four bytes to kSaveBytes would have made every v2
// card look truncated and silently thrown away the session it was carrying:
// this file's own upgrade turning into the data loss it exists to prevent.
inline constexpr uint8_t kSaveVersionLegacy = 1;
inline constexpr uint8_t kSaveVersionSession = 2;
inline constexpr uint8_t kSaveVersion = 3;

// version byte + record (16) + deck (kSeenWords * 4), then the session block
// (21), then the boot id and the clock stamp (4 each).
inline constexpr size_t kLegacyBytes = 1 + 16 + kSeenWords * 4;
inline constexpr size_t kSessionBytes = kLegacyBytes + 21;
inline constexpr size_t kSaveBytes = kSessionBytes + 8;

// Below this, time(nullptr) is not a date, it is a device that has never had
// its clock set. The RTC is a fitted part on some boards and absent on others,
// it is only ever set by an NTP sync over Wi-Fi, and a flat coin cell puts it
// back to the 1970 epoch. 2023-11-14, the same floor Study and Instapaper use.
inline constexpr uint32_t kClockFloor = 1700000000u;

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

  // Which run of the chip wrote this file. Deep sleep is a chip reset and the
  // device has no clock worth asking, so "was this written by the machine I am
  // running on right now" is the one continuity question this fork can actually
  // answer -- see resumeFor(). Zero means the writer did not record one, which
  // is every v1 and v2 card.
  uint32_t bootId = 0;

  // When it was written, in unix seconds, or 0 when the device could not say.
  // It DECIDES NOTHING -- see resumeFor(), which never reads it. It is here so
  // the screen that asks can put a number on the question: LEFT 6 DAYS AGO
  // answers "is this our game?" outright, where ROUND 2, 11 POINTS does not.
  // On a device with no RTC, or one that has never joined a network, it is 0
  // and the line is simply absent.
  uint32_t savedAt = 0;

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

// What a load should do with the evening this file is carrying.
//
// WHY THIS EXISTS. The save was built so that Home, or the device sleeping
// mid-argument, does not cost the table its round, its hidden number and its
// score. It had no notion of going stale, so days later a completely different
// group opened the app and was dropped into round 2 of somebody else's game --
// a clue they never heard, a score they did not earn, and nothing on the panel
// saying that was what had happened. It is a party game: a different group is
// the normal case, not the edge case.
//
// WHY THE BOOT AND NOT THE CLOCK. Elapsed time is the obvious axis and this
// device cannot measure it. Wake is a chip reset, so millis() restarts; there
// is no battery-backed wall clock, and what date the device thinks it is is not
// a thing an app may rely on. A boot is not a duration, but it is the thing
// actually being asked about: within one run of the chip the device has not
// been away, so it is the same room and the same people, and any resume is
// safe. Across a reset -- deep sleep, the power key, a flat battery, two weeks
// in a drawer -- the answer is genuinely unknown, and the only party at the
// table who knows it is the table.
//
// SO A STALE SESSION IS NOT DISCARDED AND NOT RESUMED. It is offered. Guessing
// wrong in one direction drops a group into a stranger's game; guessing wrong
// in the other destroys a round somebody meant to come back to. Asking is wrong
// in neither, and it costs one screen that only appears when there is something
// to ask about.
enum class Resume : uint8_t {
  // Nothing to carry on: no session, no round. The front door, with no question
  // in front of it -- the ask must never appear over an empty evening.
  Nothing,
  // Written by this same run of the chip. Certainly the same table, so resume
  // exactly as before: Home and back costs nothing.
  Carry,
  // Written before this boot, or by a build that did not record one. The table
  // decides.
  Ask,
};

// `bootId` is this run of the chip, and must not be zero -- zero is reserved for
// "the card does not say", so a caller passing it gets Ask rather than a match
// against every older save on earth.
Resume resumeFor(const Saved& saved, uint32_t bootId);

// How long ago the save was written, in minutes, or -1 when the clock cannot
// say. Three ways it cannot: the save carries no stamp (any older build, or a
// device whose clock was unset when it wrote), this device's own clock is unset
// now, or the answer comes out negative because the clock moved backwards
// between the two -- which really happens, since an NTP sync can correct a
// device that was hours out.
//
// Never an input to the resume decision. A screen that cannot say how long ago
// still asks the question; it just asks it with one fact fewer.
int minutesSince(uint32_t savedAt, uint32_t now);

}  // namespace wavelength
