// The only arithmetic in the power probe, on the host.
//
// It converts a gauge reading into a current, and it is the one part that can
// be silently wrong forever: a probe that reports 40uA instead of 400 looks
// exactly as plausible in a log, and the whole go/no-go for Live rests on
// which side of ~300uA the answer lands.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "PowerProbe.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

namespace {

constexpr int32_t kCell = 1100;  // mAh, the X4 Pro's cell

// One whole percent of an 1100mAh cell is 11mAh. Drawn over an hour that is
// 11mA, which is 11000uA. Worked by hand so the test does not agree with the
// code by repeating it.
void testAWholePercentInAnHour() { CHECK(powerprobe::microampsFrom(256, 3600, kCell) == 11000); }

// The resolution claim this whole approach rests on: ONE step of the fraction
// register is 1/256 of a percent, which on this cell is about 0.043mAh. Over
// an hour that is about 43uA, so a floor near 150uA moves several steps an
// hour and is visible overnight rather than over a fortnight.
void testOneFractionStepIsTensOfMicroamps() {
  const int64_t uA = powerprobe::microampsFrom(1, 3600, kCell);
  CHECK(uA >= 40 && uA <= 46);
}

// A realistic good floor and a realistic bad one, twelve hours apart, must
// land either side of the decision threshold rather than both looking fine.
void testTheGoAndNoGoCasesSeparate() {
  const int32_t twelveHours = 12 * 3600;
  // 150uA for 12h = 1.8mAh = 0.164% = 42 Q8 steps
  CHECK(powerprobe::microampsFrom(42, twelveHours, kCell) < 300);
  // 1.5mA for 12h = 18mAh = 1.64% = 419 Q8 steps
  CHECK(powerprobe::microampsFrom(419, twelveHours, kCell) > 1000);
}

// Charging, a re-modelled gauge, or a clock that did not advance. None of
// these is a floor, and none may come back as a negative or a division fault.
void testNonReadingsAreZeroRatherThanNonsense() {
  CHECK(powerprobe::microampsFrom(0, 3600, kCell) == 0);
  CHECK(powerprobe::microampsFrom(-5, 3600, kCell) == 0);
  CHECK(powerprobe::microampsFrom(100, 0, kCell) == 0);
  CHECK(powerprobe::microampsFrom(100, -60, kCell) == 0);
}

// 100% of an 1100mAh cell over a week is about 6.5mA.
void testALongSleepComputesCorrectly() {
  const int32_t week = 7 * 24 * 3600;
  const int64_t uA = powerprobe::microampsFrom(25600, week, kCell);
  CHECK(uA > 6000 && uA < 7000);
}

// WHY the function says int64_t and not long.
//
// `long` is 64-bit on this host and 32-bit on the ESP32, so an accumulator
// written as `long` computes correctly in every test here and wraps on the
// device. Swapping the type would therefore leave this whole suite green. This
// check pins the HAZARD instead of the spelling: it does the same arithmetic
// in 32 bits and asserts it really does overflow, so the reason the real
// function is 64-bit cannot quietly stop being true.
void testThirtyTwoBitsWouldHaveWrapped() {
  const int32_t dropQ8 = 25600, cell = kCell;
  int32_t narrow = dropQ8;
  narrow *= cell;  // 2.8e7, still fine
  narrow *= 3600;  // 1.0e11 -- wraps
  const int64_t wide = static_cast<int64_t>(dropQ8) * cell * 3600LL;
  CHECK(static_cast<int64_t>(narrow) != wide);
}

}  // namespace

int main() {
  testAWholePercentInAnHour();
  testOneFractionStepIsTensOfMicroamps();
  testTheGoAndNoGoCasesSeparate();
  testNonReadingsAreZeroRatherThanNonsense();
  testALongSleepComputesCorrectly();
  testThirtyTwoBitsWouldHaveWrapped();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
