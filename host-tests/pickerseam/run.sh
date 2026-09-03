#!/bin/sh
# Builds and runs the press/release seam tests, then asserts the wiring.
#
#   host-tests/pickerseam/run.sh
#
# WHY THIS SUITE EXISTS AT ALL. WifiSelectionActivity finishes on the button
# PRESS; the RELEASE arrives ~77ms later at the caller underneath, which never
# saw the press and reads it as its own input. On a device that has never
# joined Wi-Fi that made Hacker News unopenable -- the app puts the picker in
# front of itself and backing out of the picker shut the app, so the saved
# shelf, the half that exists for having no network, needed a network to reach.
# Eleven files launch that picker and act on a release; fifty-five files in
# src/ act on a release at all.
#
# WHY IT IS THE ONLY PLACE THIS IS CHECKED. The simulator cannot see this class
# of bug: it does not compile lib/hal, and its latch clears in beginFrame()
# rather than update(), so the two edges never land in different activities
# there. A green simulator run says nothing about it, in either direction.
#
# TWO REGISTERS, AND THE SECOND ONE IS NEW. The gate is freestanding, so the
# first part builds with nothing but the standard library -- if the state
# machine ever reaches for HalGPIO, SETTINGS or the renderer, this build fails
# loudly instead of the logic quietly becoming device-only and therefore
# untested.
#
# The flip side used to be written here as an admission: the WIRING was not
# checked, only read. That is what let the arm be written at two of the three
# points currentActivity changes -- the missing one is the immediate branch of
# replaceActivity(), which is how Back leaves the crash report, and the release
# went on to open the most recently read book. So the wiring is asserted below
# instead of described: assignments funnelled through one setter, the gate
# consulted for the release edge and nothing else, Power left out of the gated
# list, and both settle paths present. A source scan is a blunt instrument, but
# the alternative here was an enumeration, and the enumeration is what failed.
set -e
cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)
# Keyed to this checkout, not just the suite name. Two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-pickerseam-tests-$(echo "$ROOT" | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../src/util \
  ../../src/util/ButtonReleaseGate.cpp \
  test_gate.cpp -o "$BUILD_DIR/test_gate"

set +e
"$BUILD_DIR/test_gate"
gate_status=$?

# ---- The wiring the gate is useless without --------------------------------
checks=0
failed=0
ok()  { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL pickerseam  $1"; }

AM="$ROOT/src/activities/ActivityManager.cpp"
MIM_C="$ROOT/src/MappedInputManager.cpp"
MIM_H="$ROOT/src/MappedInputManager.h"
for f in "$AM" "$MIM_C" "$MIM_H"; do
  [ -f "$f" ] || { echo "FAIL pickerseam  $f is missing -- the scan below would pass by finding nothing"; exit 1; }
done

# 1. Every install of the activity on top goes through the one setter that
#    arms. Written as "which function is this line in" rather than "does the
#    arm appear near it": the bug was an assignment nobody counted.
stray=$(awk '
  /^[A-Za-z_].*ActivityManager::[A-Za-z_]+\(/ { fn = $0; sub(/^.*ActivityManager::/, "", fn); sub(/\(.*/, "", fn) }
  /currentActivity[ \t]*=[^=]/ { if (fn != "setCurrentActivity") print FNR ": assigned in " fn "()" }
  /currentActivity\.reset\(/  { if (fn != "exitActivity" || $0 !~ /currentActivity\.reset\(\)/) print FNR ": reset in " fn "()" }
' "$AM")
if [ -z "$stray" ]; then
  ok
else
  bad "currentActivity is installed outside setCurrentActivity(), so that install arms nothing:"
  echo "$stray" | sed 's/^/    ActivityManager.cpp:/'
fi

# 2. ...and that setter is what arms. Both halves matter: a setter that stopped
#    arming, or an arm that went back to being scattered over call sites.
if awk '/^void ActivityManager::setCurrentActivity/ { inside = 1 }
        inside && /swallowNextReleaseOfHeldButtons\(\)/ { found = 1 }
        inside && /^}/ { exit }
        END { exit found ? 0 : 1 }' "$AM"; then
  ok
else
  bad "setCurrentActivity() does not arm the release gate"
fi
callers=$(grep -c "swallowNextReleaseOfHeldButtons()" "$AM")
if [ "$callers" = "1" ]; then
  ok
else
  bad "swallowNextReleaseOfHeldButtons() is called $callers times in ActivityManager.cpp; the setter is the only site"
fi

# 3. The gate is asked about the RELEASE edge and nothing else. Gating a press
#    or a level reads as a stuck button -- worse than the bug, and invisible to
#    every frame-by-frame case in test_gate.cpp.
if grep -q 'fn == &HalGPIO::wasReleased && releaseGate.swallowsRelease' "$MIM_C"; then
  ok
else
  bad "readButton() no longer conditions the gate on wasReleased -- presses and levels would be gated too"
fi
consults=$(grep -c "releaseGate.swallowsRelease" "$MIM_C")
if [ "$consults" = "1" ]; then
  ok
else
  bad "releaseGate.swallowsRelease appears $consults times in MappedInputManager.cpp; readButton is the only place it may"
fi

# 4. Power stays outside. Its release is consumed outside the activity stack
#    (sleep, the frontlight double-click window), so an arm on it could swallow
#    a sleep.
gated=$(sed -n '/GATED_BUTTONS\[\] = {/,/};/p' "$MIM_H")
if [ -z "$gated" ]; then
  bad "GATED_BUTTONS is gone from MappedInputManager.h"
elif echo "$gated" | grep -q "BTN_POWER"; then
  bad "GATED_BUTTONS now includes BTN_POWER; an arm on Power can swallow a sleep"
else
  ok
fi

# 5. Both paths to a release read settle. Only ActivityManager::loop() would
#    leave an arm standing for the whole of a blocking download and swallow the
#    Back that cancels it.
grep -q "mappedInput.settleReleaseGate()" "$AM" && ok || bad "ActivityManager::loop() no longer settles the gate"
awk '/void update\(\) const/ { inside = 1 }
     inside && /settleReleaseGate\(\)/ { found = 1 }
     inside && /^  }/ { exit }
     END { exit found ? 0 : 1 }' "$MIM_H" && ok || bad "MappedInputManager::update() no longer settles the gate"

echo "$checks checks, $failed failed"
[ "$gate_status" -eq 0 ] && [ "$failed" -eq 0 ]
