// The dwell that decides when a verdict screen may take itself away.
//
// Written from the failure rather than from the class: Wavelength's result
// screen was drawn correctly and four cold testers never saw it, because the
// timer measured wall time from the moment the code decided to draw -- through
// a panel refresh, and through the reader's own thumb sitting on top of the
// message. Every check passed. The only witness that failed was a person with
// a hand on the device.
//
// So the cases below are about the hand and the refresh, not about arithmetic.

#include <cstdio>
#include <string>

#include "../../src/util/DismissDwell.h"

namespace {
int checks = 0, failed = 0;

void expect(const bool ok, const std::string& what) {
  ++checks;
  if (!ok) {
    ++failed;
    std::printf("  FAIL: %s\n", what.c_str());
  }
}

constexpr uint32_t kHold = 5000;

// Runs the dwell forward one millisecond at a time, which is the only way to
// see WHEN it fires rather than merely that it eventually does.
struct Run {
  DismissDwell dwell;
  uint32_t now = 0;

  // Advances `ms` with the given conditions held throughout; returns the first
  // instant expired() was true, or 0 for never.
  uint32_t advance(const uint32_t ms, const bool revealed, const bool touched) {
    for (uint32_t i = 0; i < ms; ++i) {
      if (dwell.expired(now, revealed, touched, kHold)) return now;
      ++now;
    }
    return 0;
  }
};

void aScreenThePanelHasNotShownNeverExpires() {
  // The refresh half of the Wavelength failure: 0.3-2s of panel time in which
  // the screen exists and nobody can see it. A dwell that counted that would
  // be handing back most of its own window.
  Run run;
  expect(run.advance(kHold * 4, false, false) == 0, "an unrevealed screen never dismisses itself");
}

void aScreenUnderAThumbNeverExpires() {
  // The hand half. A reader whose finger is on the glass has not finished with
  // the screen, and no amount of elapsed time says otherwise.
  Run run;
  expect(run.advance(kHold * 4, true, true) == 0, "a screen held under a finger never dismisses itself");
}

void aShownUntouchedScreenExpiresAtTheHold() {
  Run run;
  const uint32_t at = run.advance(kHold * 2, true, false);
  // Not merely "eventually": the first tick that could count is the one that
  // starts the clock, so the fire lands exactly one hold later.
  expect(at == kHold, "fires at exactly the hold  (fired at " + std::to_string(at) + ")");
}

void aTouchPartWayThroughRestartsTheDwell() {
  // THE case. A reader reaches for the screen with one second to go: resuming
  // from where the clock stopped would fire that second after they lift off,
  // which is the exact window the Wavelength result screen died in. It must
  // start over.
  Run run;
  expect(run.advance(kHold - 1000, true, false) == 0, "nothing fires before the hold");
  expect(run.advance(500, true, true) == 0, "nothing fires while touched");
  const uint32_t before = run.now;
  const uint32_t at = run.advance(kHold * 2, true, false);
  expect(at - before == kHold,
         "a full hold is served after the finger lifts  (served " + std::to_string(at - before) + "ms)");
}

void losingTheRevealAlsoRestarts() {
  // The same rule for the other condition, because a screen that stops being
  // shown has stopped being read.
  Run run;
  expect(run.advance(kHold - 500, true, false) == 0, "nothing fires before the hold");
  expect(run.advance(10, false, false) == 0, "nothing fires while unrevealed");
  const uint32_t before = run.now;
  expect(run.advance(kHold * 2, true, false) - before == kHold, "a full hold is served after the screen returns");
}

void armResetsAPartialDwell() {
  Run run;
  run.advance(kHold - 100, true, false);
  run.dwell.arm();
  const uint32_t before = run.now;
  expect(run.advance(kHold * 2, true, false) - before == kHold, "arm() throws away the time already served");
}

void theClockSurvivesTheMillisRollover() {
  // millis() wraps at 49.7 days. Absolute-stamp comparisons make the screen
  // either stick forever or vanish instantly across that boundary; unsigned
  // differences do neither.
  Run run;
  run.now = 0xFFFFFFFFu - 2000u;
  const uint32_t start = run.now;
  const uint32_t at = run.advance(kHold * 2, true, false);
  expect(static_cast<uint32_t>(at - start) == kHold, "the hold is honoured across the wrap");
}

void countingReportsWhetherTheScreenWillLeaveOnItsOwn() {
  Run run;
  run.dwell.expired(0, true, true, kHold);
  expect(!run.dwell.counting(), "not counting while touched");
  run.dwell.expired(1, true, false, kHold);
  expect(run.dwell.counting(), "counting once shown and untouched");
}
}  // namespace

int main() {
  aScreenThePanelHasNotShownNeverExpires();
  aScreenUnderAThumbNeverExpires();
  aShownUntouchedScreenExpiresAtTheHold();
  aTouchPartWayThroughRestartsTheDwell();
  losingTheRevealAlsoRestarts();
  armResetsAPartialDwell();
  theClockSurvivesTheMillisRollover();
  countingReportsWhetherTheScreenWillLeaveOnItsOwn();
  std::printf("opdssaved: %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
