#!/bin/sh
# Builds and runs the press/release seam tests.
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
# WHAT THIS CANNOT SEE. ButtonReleaseGate is freestanding, so this builds with
# nothing but the standard library -- if the gate ever reaches for HalGPIO,
# SETTINGS or the renderer, this build fails loudly instead of the logic
# quietly becoming device-only and therefore untested.
#
# The flip side, said plainly because a clean list hides an absence: the wiring
# is NOT compiled here. That the three call sites exist -- settle at the top of
# ActivityManager::loop(), settle in MappedInputManager::update(), arm at both
# points currentActivity changes -- and that readButton() is the only place a
# physical index is read, are checked by reading the code, not by this suite.
# The frame loop in test_gate.cpp mirrors their ORDER, so it pins the shape the
# wiring has to have; it cannot tell you the wiring is still there.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name. Two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-pickerseam-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../src/util \
  ../../src/util/ButtonReleaseGate.cpp \
  test_gate.cpp -o "$BUILD_DIR/test_gate"
"$BUILD_DIR/test_gate"
