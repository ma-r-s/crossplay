#!/bin/sh
# Which boots may light the frontlight.
#
#   host-tests/wakepolicy/run.sh
#
# Two halves, and neither is worth anything alone:
#
#   test_wakepolicy  walks the policy itself, in both directions, so "never
#                   light anything" is as red as "always light it".
#   the source check below wires that policy to the only place it can matter.
#                   A pure function nobody calls compiles, passes and ships the
#                   bug; the reported symptom came from main.cpp handing the
#                   restore flag straight to Frontlight.begin(), which is
#                   exactly the line a later edit would put back.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wakepolicy-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++20 -O1 -Wall -Wextra -Werror \
  test_wakepolicy.cpp -o "$BUILD_DIR/test_wakepolicy"
"$BUILD_DIR/test_wakepolicy"

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
check("util/WakePolicy.h" in src, "main.cpp includes util/WakePolicy.h")
call = line_of(r"wakepolicy::restoreFrontlight\s*\(")
check(call is not None, "main.cpp calls wakepolicy::restoreFrontlight()")

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

# 5. THE CLASSIFICATION ITSELF. Everything above tests how the answer is USED
#    and none of it tests that the question is ever asked. A cold reviewer
#    deleted the two lines below from a copy of main.cpp and this suite stayed
#    green on all nine checks with the reported bug fully restored, because a
#    policy that is never handed Boot::Unattended returns the old answer for
#    every boot. These are the checks that would have caught that.
unattended = [i for i, line in enumerate(lines) if re.search(r"bootKind\s*=\s*wakepolicy::Boot::Unattended", line)]
check(len(unattended) >= 1, "main.cpp classifies some boot as Boot::Unattended (found %d)" % len(unattended))
timer_test = any(re.search(r"WakeupReason::Timer", lines[i - 1]) or re.search(r"WakeupReason::Timer", lines[i])
                 for i in unattended)
check(timer_test, "one of those classifications is driven by the Timer wake reason")

# 6. The unattended boot draws nothing. presentsUi() is the other half of the
#    same rule and the same complaint: a refresh that found a picture used to
#    boot the splash and the reader to put it on the glass.
ui_call = line_of(r"if\s*\(\s*!\s*wakepolicy::presentsUi\s*\(")
check(ui_call is not None, "main.cpp guards the boot on !wakepolicy::presentsUi()")
boot_activity = line_of(r"activityManager\.goToBoot\(\)")
check(boot_activity is not None, "main.cpp still has a splash to skip")
if ui_call is not None and boot_activity is not None:
    check(ui_call < boot_activity, "the presentsUi() exit runs BEFORE anything can draw the splash")
enter_sleep = [i for i, line in enumerate(lines)
               if re.search(r"enterDeepSleep\([^)]*/\*unattended=\*/\s*true", line)]
check(len(enter_sleep) == 1, "the unattended boot leaves through enterDeepSleep(..., unattended) (found %d)"
      % len(enter_sleep))

# 7. savedLight is built from the real settings, not from constants. A literal
#    here would make the policy correct and the inputs a lie.
for field, source in (("lightOn", "SETTINGS.frontlightOn"),
                      ("restoreOnWake", "SETTINGS.frontlightRestoreOnWake"),
                      ("silentRebootLightOn", "silentRebootLightOn")):
    check(re.search(r"\.%s\s*=\s*%s\b" % (field, re.escape(source)), src) is not None,
          "savedLight.%s comes from %s" % (field, source))

# 8. startDeepSleepArmed is [[noreturn]], which is what makes "a boot that
#    sleeps again cannot reach the light" a compiler-enforced fact rather than
#    a paragraph. Every sleeping branch ends in it with a `break;` after.
check(re.search(r"CROSSPLAY_SLEEP_NORETURN\s+static void startDeepSleepArmed", src) is not None,
      "startDeepSleepArmed carries CROSSPLAY_SLEEP_NORETURN, so the unreachability is compiler-enforced")

for what in failures:
    print("  FAIL %s" % what)
print("wakepolicy-source: %d checks, %d failed" % (checks, len(failures)))
sys.exit(1 if failures else 0)
PY
