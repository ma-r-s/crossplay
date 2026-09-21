// Which boots may light the frontlight, on a laptop.
//
// The bug this suite exists for, in Mario's words on 2026-09-21: "the backlight
// is turning on on refresh". A Live refresh is a deep-sleep timer wake, a deep
// sleep wake is a full chip reset, and setup() restored the user's light the
// way it does for a person's wake -- so every scheduled check lit a kitchen at
// whatever hour the schedule landed on, and held the device's largest single
// draw up for the length of a radio join.
//
// Every assertion here is a decision nothing else can see. A device that wakes
// correctly and a device that wakes lit take the same time to boot, log the
// same lines, draw the same pixels and pass the same build. The only other
// instrument is a person in a dark room at 4am.
//
// Asserted in BOTH directions on purpose. "Never light anything" fixes the
// reported symptom and breaks the feature, and a suite that only checks the
// off cases would call it green: for every Unattended case that must stay dark
// there is a User case with the SAME saved settings that must come up lit.

#include <cstdio>
#include <initializer_list>

#include "../../src/util/WakeLightPolicy.h"

static int failures = 0;
// Counted because check.sh reads sub-suites with `grep -c "checks, 0 failed"`:
// a suite that only prints "ok" reports zero assertions to the one instrument
// meant to notice a suite that stopped asserting anything.
static int checks = 0;

static void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    std::printf("  FAIL %s\n", what);
    ++failures;
  }
}

using wakelight::Boot;
using wakelight::restoreFrontlight;
using wakelight::Saved;

// The four settings combinations a user can actually be in. silentRebootLightOn
// is not one of them: it is the live state captured at a restart, so it is
// varied separately below.
static constexpr Saved kOffOff = {/*lightOn=*/false, /*restoreOnWake=*/false, /*silentRebootLightOn=*/false};
static constexpr Saved kOffRestore = {/*lightOn=*/false, /*restoreOnWake=*/true, /*silentRebootLightOn=*/false};
static constexpr Saved kOnNoRestore = {/*lightOn=*/true, /*restoreOnWake=*/false, /*silentRebootLightOn=*/false};
static constexpr Saved kOnRestore = {/*lightOn=*/true, /*restoreOnWake=*/true, /*silentRebootLightOn=*/false};

// THE REPORTED BUG. Mario's own hypothesis was that it needed the light on and
// Restore Light on Wake on, and he was right about the combination: that is the
// only one that ever lit, because it is the only one that lights a person's
// wake either. It is asserted first and by name.
static void testTheReportedCase() {
  check(restoreFrontlight(Boot::User, kOnRestore),
        "light on + restore on: a person's wake still comes up lit (the feature)");
  check(!restoreFrontlight(Boot::Unattended, kOnRestore),
        "light on + restore on: a Live refresh stays dark (the bug Mario saw)");
}

// A scheduled wake is dark whatever the user's settings say. A setting chosen
// for the wakes a person asks for is not consent for a wake they never asked
// for, so this is not conditional on anything.
static void testUnattendedIsAlwaysDark() {
  for (const Saved& saved : {kOffOff, kOffRestore, kOnNoRestore, kOnRestore}) {
    check(!restoreFrontlight(Boot::Unattended, saved), "unattended wake: dark for every settings combination");
  }
  // Including one carrying a silent restart's live state, which must not leak
  // into a timer wake even though the two cannot coincide today.
  const Saved carried = {/*lightOn=*/true, /*restoreOnWake=*/true, /*silentRebootLightOn=*/true};
  check(!restoreFrontlight(Boot::Unattended, carried), "unattended wake: dark even with a carried silent-reboot state");
}

// The behaviour that existed before Live and must be unchanged by this fix,
// because it is the behaviour a person reaches for the device expecting.
static void testUserWakeIsUnchanged() {
  check(!restoreFrontlight(Boot::User, kOffOff), "user wake: light off, restore off -> dark");
  check(!restoreFrontlight(Boot::User, kOffRestore), "user wake: light off, restore on -> dark");
  check(!restoreFrontlight(Boot::User, kOnNoRestore), "user wake: light on, restore OFF -> dark (restore is the gate)");
  check(restoreFrontlight(Boot::User, kOnRestore), "user wake: light on, restore on -> lit");
}

// A silent restart replays the LIVE state, not the saved preference, and the
// two legitimately diverge: a wake with Restore Light on Wake off leaves the
// light off while the saved "was on" preference is kept. Re-deriving from
// settings here would light a device the user is holding in the dark, or dark
// a device they are reading by.
static void testSilentRestartReplaysLiveState() {
  const Saved wasLit = {/*lightOn=*/false, /*restoreOnWake=*/false, /*silentRebootLightOn=*/true};
  check(restoreFrontlight(Boot::Silent, wasLit),
        "silent restart: light was live-on -> comes back lit, against both settings");
  const Saved wasDark = {/*lightOn=*/true, /*restoreOnWake=*/true, /*silentRebootLightOn=*/false};
  check(!restoreFrontlight(Boot::Silent, wasDark),
        "silent restart: light was live-off -> stays dark, against both settings");
}

int main() {
  testTheReportedCase();
  testUnattendedIsAlwaysDark();
  testUserWakeIsUnchanged();
  testSilentRestartReplaysLiveState();
  if (failures != 0) {
    std::printf("wakelight: %d checks, %d failed\n", checks, failures);
    return 1;
  }
  std::printf("wakelight: %d checks, 0 failed\n", checks);
  return 0;
}
