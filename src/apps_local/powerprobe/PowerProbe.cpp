#include "PowerProbe.h"

#include <BatteryMonitor.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace powerprobe {
namespace {

constexpr const char* kMark = "/.crosspoint/powerprobe.mark";
constexpr const char* kLog = "/.crosspoint/powerprobe.log";

// Nominal cell, from docs/building-apps.md. The log carries the raw drop and
// the elapsed seconds beside the derived current precisely so this number can
// be wrong without invalidating the reading.
constexpr long kCellMilliampHours = 1100;

// Below this the gauge's own quantisation dominates: one 1/256 step over a few
// minutes prices out at an absurd current. Short sleeps are discarded rather
// than logged as noise.
constexpr long kMinElapsedSeconds = 600;

// A sleep longer than this is almost certainly a clock that was reset rather
// than a fortnight on the shelf, and averaging over it would hide the floor.
constexpr long kMaxElapsedSeconds = 14L * 24 * 3600;

bool readSoc(uint16_t& socQ8) {
#if defined(SIMULATOR)
  // The simulator links its own BatteryMonitor, which has neither this
  // constructor nor readSocQ8 -- and there is no cell behind it to read. A
  // laptop cannot measure a deep sleep it does not take, so the probe reports
  // "no reading" there rather than being stubbed into inventing one: every
  // caller already handles that answer, and a fabricated microamp figure is
  // the one output of this file that would be worse than none.
  (void)socQ8;
  return false;
#else
  // A function-local static, the same shape HalPowerManager uses for its own
  // reads: the default constructor resolves the board's gauge, and there is no
  // singleton accessor on this class.
  static const BatteryMonitor battery;
  return battery.readSocQ8(socQ8);
#endif
}

}  // namespace

void beforeSleep() {
  uint16_t socQ8 = 0;
  if (!readSoc(socQ8)) {
    // No reading means no measurement. Clear any stale mark so the next boot
    // does not price this sleep against a reading from two sleeps ago.
    Storage.remove(kMark);
    return;
  }
  char line[64];
  std::snprintf(line, sizeof(line), "%ld %u\n", static_cast<long>(std::time(nullptr)), static_cast<unsigned>(socQ8));
  if (!Storage.writeFile(kMark, String(line))) LOG_ERR("PWRPROBE", "could not stamp the mark");
}

void afterBoot() {
  if (!Storage.exists(kMark)) return;

  char buf[64] = {0};
  const size_t read = Storage.readFileToBuffer(kMark, buf, sizeof(buf) - 1);
  Storage.remove(kMark);  // one mark prices exactly one sleep, however it goes
  if (read == 0) return;

  long thenEpoch = 0;
  unsigned thenSocQ8 = 0;
  if (std::sscanf(buf, "%ld %u", &thenEpoch, &thenSocQ8) != 2) return;

  uint16_t nowSocQ8 = 0;
  if (!readSoc(nowSocQ8)) return;

  const long elapsed = static_cast<long>(std::time(nullptr)) - thenEpoch;
  if (elapsed < kMinElapsedSeconds || elapsed > kMaxElapsedSeconds) return;

  // Charging, or a gauge that re-modelled upward, gives a negative drop. That
  // is not a floor measurement, so it is logged as such rather than as a
  // nonsense current.
  const long dropQ8 = static_cast<long>(thenSocQ8) - static_cast<long>(nowSocQ8);

  char line[160];
  if (dropQ8 <= 0) {
    std::snprintf(line, sizeof(line), "elapsed=%lds dropQ8=%ld (charged or re-modelled, no reading)\n", elapsed,
                  dropQ8);
  } else {
    // uA = drop_percent/100 * cell_mAh * 3600 * 1000 / elapsed, with the Q8
    // divisor folded in. 64-bit because the numerator passes 2^31 for any
    // drop worth logging.
    const long long microamps = microampsFrom(dropQ8, elapsed, kCellMilliampHours);
    std::snprintf(line, sizeof(line), "elapsed=%lds dropQ8=%ld cell=%ldmAh -> %llduA\n", elapsed, dropQ8,
                  kCellMilliampHours, microamps);
    LOG_INF("PWRPROBE", "slept %lds, drew about %lluuA", elapsed, static_cast<unsigned long long>(microamps));
  }

  HalFile file;
  if (Storage.openFileForAppend("PWRPROBE", kLog, file)) file.print(line);
}

}  // namespace powerprobe
