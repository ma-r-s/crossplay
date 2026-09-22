#pragma once

#include <cstdint>
#include <string>

#include "LiveCore.h"

// What Live remembers between one wake and the next.
//
// All of it is on the card, because none of it survives a deep sleep any other
// way: the token that makes this reader itself, the ETag that lets a wake cost
// a few hundred bytes instead of 48KB, the cadence the website last asked for,
// and whether the user has Live turned on at all.
//
// The toggle used to be a bool in the Activity, which meant "on" lasted exactly
// as long as the app was open.

namespace live {

// /sleep.bmp, the slot SleepActivity looks at FIRST.
//
// Live writes the same file the wallpaper picker writes, deliberately: the
// alternative is a second sleep-screen slot that shadows the first one, which
// is card #354 with the arrow pointing the other way. WallpapersCore.h:179
// lists five ways a write to it never reaches the glass, and the one that bites
// by default is SETTINGS.sleepScreen == DARK.
//
// So Live FORCES the setting, exactly as tapping a wallpaper does, through the
// same wallpapers::choiceForSetWallpaper() the picker uses. Turning Live on IS
// a sleep-screen choice -- it is the only thing the feature does -- and a
// toggle that left the device on DARK would be a switch that says "your phone
// is on your sleep screen" over a black panel. The user is told what it
// replaced in the same sentence, the way the picker tells them.
constexpr const char* kSleepImage = "/sleep.bmp";
// Written first, size-checked, then renamed over the live one. Opening
// /sleep.bmp directly truncates it, so a wake that failed halfway would leave a
// partial BMP where yesterday's message was -- a blank or a torn panel, which
// is the one failure state this feature is not allowed to have.
constexpr const char* kSleepImagePart = "/.crosspoint/live-sleep.part";
constexpr const char* kStatePath = "/.crosspoint/live.json";

struct State {
  std::string deviceToken;  // empty means never paired
  std::string fridgeId;
  std::string etag;  // unquoted; empty means "I have nothing, send me whatever you have"
  bool on = false;
  uint32_t intervalSeconds = kDefaultIntervalSeconds;
  // The last X-Cadence, or 0 when the service has never sent one. Kept apart
  // from the interval because under a clock-time schedule they differ, and
  // resolved only through Schedule::cadence.
  uint32_t cadenceSeconds = 0;
  int64_t lastAttemptEpoch = 0;
  int consecutiveFailures = 0;
  // Purely for the screen: the last thing that happened, so a user who opens
  // Live can be told something truer than a spinner.
  int64_t lastSuccessEpoch = 0;

  bool paired() const { return !deviceToken.empty(); }
  Schedule schedule() const {
    Schedule s;
    s.on = on;
    s.paired = paired();
    s.lastAttemptEpoch = lastAttemptEpoch;
    s.intervalSeconds = intervalSeconds;
    s.cadenceSeconds = cadenceSeconds;
    s.consecutiveFailures = consecutiveFailures;
    return s;
  }
};

// Returns false when there is nothing on the card, which is indistinguishable
// from a device that was never paired and is treated as exactly that.
bool load(State& out);

// Write-beside-and-rename, the way StudySync does it and for the reason it
// learned: opening the real path truncates it first, so a power cut mid-write
// left an unparseable file that reads exactly like a device that was never
// paired -- and walked the user through pairing again for no reason they could
// see.
bool save(const State& state);

// Forget the pairing but keep the toggle's position, so a 401 does not silently
// turn a feature off as well as disconnecting it. The user is shown the pairing
// screen; what they had asked for is still what they asked for.
void forgetPairing(State& state);

}  // namespace live
