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
// One invariant this rests on, unstated until now and worth failing loudly on.
// The count is of ANY paint, not of a paint of any particular screen, so it
// says "the panel has moved on" rather than "your table is what is showing".
// That is only equivalent because every screen in this fork builds its table
// and paints it in the same function, as adjacent statements. A path that
// builds a table and returns WITHOUT painting would mark it revealed at the
// next unrelated paint -- a pushed panel, a popup drawn over a game. If you
// are adding one, gate it yourself or do not route against it.
//
// Not atomic. The render task writes it and the loop task reads it, and on
// this target a 32-bit aligned load or store is single-instruction anyway. A
// torn read cannot happen; a stale read can, and the consequence of a stale
// read is one extra dropped tap on a screen the user could not see, which is
// the behaviour this file exists to produce.

#include <stdint.h>

#include <atomic>

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

// "Has the panel shown the screen I just built?", as a latch.
//
// Split out of UiAppHost so it can be tested at all: UiAppHost needs a
// GfxRenderer and an Arduino, and cannot be built on the host, while this is
// the entire decision it makes and is freestanding. host-tests/ui exercises
// this object, not a restatement of it.
//
// Two tasks touch it -- the render task arms and stamps, the loop task asks --
// so the two fields are atomic. revealed() latches: once a paint has landed it
// stops asking, so a later unrelated paint cannot re-arm it.
class RevealGate {
 public:
  // A new screen exists in memory. It is not live until the panel shows it.
  void arm() { awaiting_.store(true); }

  // The table for the pending screen was (re)built just now. Called on every
  // build while a reveal is pending, so a render that rebuilds several times
  // before its single paint measures from the LAST build, not the first.
  void markBuilt() {
    if (awaiting_.load()) builtAt_.store(painted());
  }

  // True once a paint has landed since the last markBuilt(), and true forever
  // after until the next arm().
  bool revealed() const {
    if (!awaiting_.load()) return true;
    if (painted() == builtAt_.load()) return false;
    awaiting_.store(false);
    return true;
  }

 private:
  mutable std::atomic<bool> awaiting_{false};
  std::atomic<uint32_t> builtAt_{0};
};

}  // namespace paintclock
