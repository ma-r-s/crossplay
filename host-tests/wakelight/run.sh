#!/bin/sh
# Which boots may light the frontlight.
#
#   host-tests/wakelight/run.sh
#
# Two halves, and neither is worth anything alone:
#
#   test_wakelight  walks the policy itself, in both directions, so "never
#                   light anything" is as red as "always light it".
#   the source check below wires that policy to the only place it can matter.
#                   A pure function nobody calls compiles, passes and ships the
#                   bug; the reported symptom came from main.cpp handing the
#                   restore flag straight to Frontlight.begin(), which is
#                   exactly the line a later edit would put back.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wakelight-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O1 -Wall -Wextra -Werror \
  test_wakelight.cpp -o "$BUILD_DIR/test_wakelight"
"$BUILD_DIR/test_wakelight"

# The wiring. Everything asserted here is about src/main.cpp's boot path, which
# no host suite can link: it pulls in the display, the SD card, the radio and
# the whole activity stack. So it is read rather than run -- but read
# structurally (line positions and the actual argument text), not by looking for
# a word that a comment could satisfy.
python3 - ../../src/main.cpp <<'PY'
import re
import sys

src = open(sys.argv[1]).read()
lines = src.splitlines()
checks = 0
failures = []


def check(ok, what):
    global checks
    checks += 1
    if not ok:
        failures.append(what)


def line_of(pattern):
    """Index of the first line matching pattern, or None."""
    rx = re.compile(pattern)
    for i, line in enumerate(lines):
        if rx.search(line):
            return i
    return None


# 1. The policy is the thing main.cpp actually asks. A header nobody includes
#    is a tested opinion with no effect on the device.
check("util/WakeLightPolicy.h" in src, "main.cpp includes util/WakeLightPolicy.h")
call = line_of(r"wakelight::restoreFrontlight\s*\(")
check(call is not None, "main.cpp calls wakelight::restoreFrontlight()")

# 2. Frontlight.begin() must not carry the restore decision any more. This is
#    the literal line the bug was: begin(brightness, warmth, restoreLightOn)
#    ran BEFORE the wake switch, so a timer wake was lit before anything had
#    looked at why the device was awake. Its third argument is now a literal
#    false and the light is turned on, if at all, further down.
begins = re.findall(r"Frontlight\.begin\(([^;]*)\)\s*;", src)
check(len(begins) == 1, "main.cpp brings the frontlight up in exactly one place (found %d)" % len(begins))
for args in begins:
    third = [a.strip() for a in args.split(",")][-1]
    check(
        re.fullmatch(r"(/\*\s*on\s*=\s*\*/\s*)?false", third) is not None,
        "Frontlight.begin()'s on argument is a literal false, not a restored setting (got %r)" % third,
    )

# 3. The decision happens AFTER the wake switch. That ordering is the half of
#    the rule no policy function can express: every branch of that switch which
#    decides to sleep again leaves through startDeepSleepArmed(), which does not
#    return, so a boot that shows nobody a UI never reaches the light at all.
#    Move the call back above the switch and the ghost power-button wake and the
#    USB-power cold boot light the panel again, silently, with this suite green.
switch_line = line_of(r"switch\s*\(\s*wakeupReason\s*\)")
check(switch_line is not None, "main.cpp still routes the boot on wakeupReason")
timer_case = line_of(r"case HalGPIO::WakeupReason::Timer")
check(timer_case is not None, "main.cpp still has a Timer wake case to be unattended about")
if call is not None and timer_case is not None:
    check(call > timer_case, "the frontlight decision runs after the wake switch, not before it")

# 4. Nothing else in main.cpp turns the light on. toggleFrontlight() is a user
#    gesture and reads Frontlight.isOn() for its argument; a second literal
#    setOn(true) would be a second boot-time path, which is how this codebase
#    fixes one twin and ships the other.
lit = [i for i, line in enumerate(lines) if re.search(r"Frontlight\.setOn\(\s*true\s*\)", line)]
check(len(lit) == 1, "main.cpp has exactly one unconditional Frontlight.setOn(true) (found %d)" % len(lit))
if lit and call is not None:
    check(0 <= lit[0] - call <= 3, "that setOn(true) is the body of the restoreFrontlight() check")

for what in failures:
    print("  FAIL %s" % what)
print("wakelight-source: %d checks, %d failed" % (checks, len(failures)))
sys.exit(1 if failures else 0)
PY
