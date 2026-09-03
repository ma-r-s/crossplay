#pragma once

// How many times the panel has finished showing something.
//
// This exists because of a gap nothing else in the fork can see. Building a
// screen and SHOWING it are two different moments separated by a very long
// time: GfxRenderer::displayBuffer() blocks for 0.3-2s while the waveform
// runs (HalDisplay::HALF_REFRESH is 1720ms). Every app publishes its new hit
// table BEFORE that call, so for up to a full refresh the firmware routes
// taps against a screen the user cannot see yet, and a finger still resting
// where the previous screen's button was lands on whatever the new screen put
// underneath it.
//
// A counter rather than a timestamp on purpose. "Has the panel shown anything
// since this table was built" is the actual question, and a count answers it
// exactly, with no threshold to tune and nothing to get wrong when a refresh
// runs long. Wavelength's kSettleMs = 1100 is the same idea with a guess in
// place of the fact, and it is the only other thing in the tree that tries.
//
// Freestanding by design -- no Arduino, no renderer, nothing. The toybox
// layer includes it by relative path so the screen builders stay host-testable
// with only the SDK on the include path (host-tests/ui/run.sh).
//
// Not atomic. The render task writes it and the loop task reads it, and on
// this target a 32-bit aligned load or store is single-instruction anyway. A
// torn read cannot happen; a stale read can, and the consequence of a stale
// read is one extra dropped tap on a screen the user could not see, which is
// the behaviour this file exists to produce.

#include <stdint.h>

namespace paintclock {

inline uint32_t& counter() {
  static uint32_t painted = 0;
  return painted;
}

// The panel has finished showing the framebuffer. Called by GfxRenderer at the
// end of every paint, so every path is covered -- including the apps that take
// their own RenderLock and paint from loop() rather than through
// ActivityManager's render dispatch.
inline void notePainted() { ++counter(); }

// How many paints have completed. Zero means the panel has never shown
// anything, which on a device is only true before the boot splash and in the
// host tests, where nothing paints at all.
inline uint32_t painted() { return counter(); }

}  // namespace paintclock
