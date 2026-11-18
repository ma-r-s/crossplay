// The build-date clock floor every fork clock decision goes through.
//
// The case it exists for, from the desk on 2026-11-17: a reTerminal Sticky
// whose never-set RTC read 2026-02-06. Every floor the fork had (2021,
// 2023-11-14, 2025-01-01, or the build YEAR) accepted that date, so a Wi-Fi
// join never corrected it and Study, Connections and Wordle all worked from
// February.

#include <cstdio>
#include <cstring>

#include "../../src/apps_local/ClockFloor.h"

namespace {

int checks = 0;
int failures = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL: %s\n", what);
  }
}

constexpr int64_t kFeb6_2026 = 1770379200;   // 2026-02-06 12:00 UTC, the Sticky's reading
constexpr int64_t kNov18_2026 = 1794960000;  // 2026-11-18 00:00 UTC

void testParsesTheCompilersShape() {
  check(clockfloor::epochOfBuildDate("Nov 18 2026") == kNov18_2026, "Nov 18 2026 is midnight UTC that day");
  check(clockfloor::epochOfBuildDate("Jan  1 1970") == 0, "the epoch is day zero");
  check(clockfloor::epochOfBuildDate("Feb  6 2026") == kFeb6_2026 - 12 * 3600, "a space-padded day reads");
  check(clockfloor::epochOfBuildDate("Mar  1 2024") - clockfloor::epochOfBuildDate("Feb 28 2024") == 2 * 86400,
        "a leap year's February has its 29th");
  check(clockfloor::epochOfBuildDate("Dec 31 2026") + 86400 == clockfloor::epochOfBuildDate("Jan  1 2027"),
        "a year turns over");
  // Not the shape: no floor rather than a wrong one.
  check(clockfloor::epochOfBuildDate("2026-11-18") == 0, "an ISO date is not __DATE__");
  check(clockfloor::epochOfBuildDate("Foo 18 2026") == 0, "an unknown month");
  check(clockfloor::epochOfBuildDate("Nov 18 20x6") == 0, "a bad year");
  check(clockfloor::epochOfBuildDate(nullptr) == 0, "nothing");
}

void testTheFloorIsTheDayBeforeTheBuild() {
  // A build on Nov 18 accepts the 17th: __DATE__ is the build machine's local
  // date, and a device in UTC can be a day either side of it.
  check(clockfloor::floorFor("Nov 18 2026") == kNov18_2026 - 86400, "the floor is the day before the build");
  check(clockfloor::floorFor("garbage") == 1700000000, "an unreadable build date keeps the old floor");
}

void testTheStickysFebruaryIsNotToday() {
  // The whole report. A build from November cannot be running in February.
  const int64_t floor = clockfloor::floorFor("Nov 18 2026");
  check(kFeb6_2026 < floor, "2026-02-06 is below a Nov 18 2026 build's floor");
  check(kNov18_2026 + 9 * 3600 >= floor, "the build day itself is above it");
}

void testThisBuildsFloor() {
  // The firmware's own: what trusted() compares against is this file's
  // compile day, so it must reject the Sticky's February (any build after
  // 2026-02-07 does) and accept a moment of the day it was built.
  const int64_t today = clockfloor::epochOfBuildDate(__DATE__);
  check(today > 0, "__DATE__ parses");
  check(clockfloor::buildFloor() == today - 86400 || clockfloor::buildFloor() == today,
        "buildFloor() is the day before the build (a compile across midnight may see the next day)");
  check(clockfloor::trusted(today + 12 * 3600), "noon on the build day is trusted");
  check(!clockfloor::trusted(kFeb6_2026), "the Sticky's 2026-02-06 is not");
  check(!clockfloor::trusted(0), "nor is the epoch");
}

}  // namespace

int main() {
  std::printf("ClockFloor\n");
  testParsesTheCompilersShape();
  testTheFloorIsTheDayBeforeTheBuild();
  testTheStickysFebruaryIsNotToday();
  testThisBuildsFloor();
  std::printf("%s %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}
