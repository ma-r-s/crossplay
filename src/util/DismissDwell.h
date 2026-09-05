#pragma once

// How long a verdict screen has actually been LOOKED AT, as opposed to how
// long it has existed.
//
// A screen that dismisses itself on a timer measures the wrong thing twice.
// It starts counting when the code decides to show something, which on this
// panel is 0.3-2s before anything appears (HalDisplay::HALF_REFRESH is
// 1720ms), and it keeps counting while the reader's own thumb is sitting on
// top of the message. Wavelength's result screen was drawn correctly and four
// cold testers never saw it, for exactly those two reasons.
//
// So the dwell runs only while both are true: the panel has SHOWN the screen,
// and nobody is touching the glass. A finger arriving mid-dwell restarts it
// rather than shortening it -- a reader whose hand is on the device has not
// finished with the screen, and the worst outcome of guessing wrong is that
// the message stays up until they lift off.
//
// Freestanding on purpose: no Arduino, no renderer, no input manager. `nowMs`
// and the two facts come from the caller, which is what makes the rule
// testable without a device (host-tests/opdssaved). The activity that owns it
// cannot be built on the host at all, and a rule that can only be exercised by
// hand is a rule nobody watches fail.

#include <stdint.h>

// Wrap-safe throughout: every comparison is on an unsigned difference, never
// on two absolute stamps, so millis() rolling over at 49.7 days shortens
// nothing.
class DismissDwell {
 public:
  // Entering the screen. Nothing has been seen yet.
  void arm() {
    running_ = false;
    since_ = 0;
  }

  // True once the screen has been visible and untouched for `holdMs`.
  //
  // `revealed` is "the panel has shown this screen" -- on the device that is
  // the existing reveal gate (UiAppHost::routingReady()), not a second
  // mechanism invented here. `touched` is a live contact on the glass.
  bool expired(const uint32_t nowMs, const bool revealed, const bool touched, const uint32_t holdMs) {
    if (!revealed || touched) {
      // Not "pause": restart. Half a dwell spent under a thumb is not half a
      // dwell spent reading, and resuming from it would hand back the exact
      // window this object exists to close.
      running_ = false;
      return false;
    }
    if (!running_) {
      running_ = true;
      since_ = nowMs;
      return false;
    }
    return static_cast<uint32_t>(nowMs - since_) >= holdMs;
  }

  // For anyone reasoning about a screen that will not go away on its own.
  bool counting() const { return running_; }

 private:
  uint32_t since_ = 0;
  bool running_ = false;
};
