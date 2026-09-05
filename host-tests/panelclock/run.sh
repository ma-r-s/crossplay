#!/bin/sh
# Builds and runs the panel-time accumulator tests. No device and no
# PlatformIO: PaintClock.h is freestanding by design (see the header), which is
# the only reason the split it computes can be tested at all -- GfxRenderer,
# where it is wired in, needs an Arduino and an SD card.
#
#   host-tests/panelclock/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-panelclock-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../lib/GfxRenderer \
  test_panelclock.cpp -o "$BUILD_DIR/test_panelclock"
"$BUILD_DIR/test_panelclock"
