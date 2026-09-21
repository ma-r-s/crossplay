#pragma once

#include <cstdint>

// Measures what a deep sleep actually costs, using the only instrument this
// board has that keeps counting while the SoC is dead: the fuel gauge.
//
// The chip cannot measure itself asleep, and Espressif's own guidance for a
// deep-sleep floor is an ammeter in series with the module. The CW2017 sits on
// I2C with its own supply and carries the state of charge to 1/256 of a
// percent (BatteryMonitor::readSocQ8), which on this cell is about 0.043 mAh
// per step against 11 mAh for the integer register everything else reads. That
// is the difference between measuring a floor overnight and not measuring it
// at all.
//
// Two calls, one on each side of a sleep. Nothing else in the firmware depends
// on them, and both are silent no-ops when the gauge will not answer.
//
// DEVELOPER MODE MUST BE OFF for a reading to mean anything: a device in
// Developer Mode never deep-sleeps, so the mark is written and the wake
// measures an idle device instead of a sleeping one.

namespace powerprobe {

// The arithmetic, pulled out of the logging so it can be tested on the host.
//
// A drop of `dropQ8` sixteenths-of-a-sixteenth of a percent over `elapsed`
// seconds on a `cellMilliampHours` cell, expressed as microamps. 64-bit
// because the numerator passes 2^31 for any drop worth logging: one whole
// percent of a 1100mAh cell over an hour is 11mA, and the product on the way
// there is 2.8e8 before the divide.
//
// Returns 0 for a non-positive drop or a non-positive span rather than
// dividing by zero or reporting a negative current: charging is not a floor
// measurement, and the caller says so in words instead.
inline int64_t microampsFrom(const int32_t dropQ8, const int32_t elapsedSeconds, const int32_t cellMilliampHours) {
  if (dropQ8 <= 0 || elapsedSeconds <= 0) return 0;
  // drop_percent/100 * cell_mAh * 3600 * 1000 / elapsed, with the Q8 divisor
  // folded into the 25600.
  //
  // int64_t EXPLICITLY, not long. The product passes 2^31 long before the
  // divide -- one percent of an 1100mAh cell is 2.8e8 on the way through --
  // and `long` is 64-bit on the host and 32-bit on the ESP32. Written as
  // `long` this computes correctly in every host test and wraps on the device,
  // reporting a small current for a large one, which is the failure that reads
  // as good news. host-tests/powerprobe pins the hazard rather than the type.
  return static_cast<int64_t>(dropQ8) * cellMilliampHours * 3600LL * 1000LL /
         (25600LL * static_cast<int64_t>(elapsedSeconds));
}

// Stamps {epoch, socQ8} on the card so the next boot can price the sleep.
// Called on the way into deep sleep, after the sleep screen is painted.
void beforeSleep();

// Reads what beforeSleep() left, appends one line to the log and clears the
// mark. A mark with no plausible elapsed time is discarded rather than logged.
void afterBoot();

}  // namespace powerprobe
