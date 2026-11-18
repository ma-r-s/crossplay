#pragma once

// Can the wall clock be today? Not if it reads earlier than the day this
// firmware was built: nothing runs before it is compiled.
//
// The one floor every fork decision about the clock uses: whether a Wi-Fi join
// resyncs it (WifiSelectionActivity), which day's puzzle Connections and Wordle
// open, whether Study dates a review, whether Instapaper, RSS and Wavelength
// stamp anything, and whether Live trusts its schedule. Each used to carry its
// own floor (2021, 2023-11-14, 2025-01-01, or the build YEAR), and every one of
// them passed a reTerminal Sticky whose never-set RTC read 2026-02-06 on
// 2026-11-17: a plausible date, nine months old, so Study said NOTHING TO
// REVIEW (report #727) and both daily puzzles opened February's.
//
// Freestanding: no Arduino, so host-tests/clockfloor drives it. __DATE__ is read
// in ClockFloor.cpp alone, the one translation unit whose build date is the
// floor. A release bump changes CROSSPOINT_VERSION on every compile line, so a
// release build compiles this file that day rather than reusing a cached copy.

#include <cstdint>

namespace clockfloor {

// __DATE__'s shape, "Nov 18 2026" (day space-padded), to the epoch second of
// that day's 00:00 UTC. 0 for anything that is not that shape.
int64_t epochOfBuildDate(const char* date);

// The earliest instant that can be now: the build day, less one day because
// __DATE__ is the build machine's local date and the device clock is UTC.
int64_t floorFor(const char* buildDate);

// floorFor(__DATE__) as ClockFloor.cpp was compiled.
int64_t buildFloor();

// Whether `now` (epoch seconds) can be today on this firmware.
bool trusted(int64_t now);

}  // namespace clockfloor
