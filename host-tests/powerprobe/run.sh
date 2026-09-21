#!/bin/sh
# The power probe's arithmetic, on the host.
#
#   host-tests/powerprobe/run.sh
#
# PowerProbe.cpp reaches for the gauge, the card and the logger and cannot
# build here. The conversion from a gauge reading to a current is deliberately
# an inline in the header instead, with no includes of its own, so the one part
# that decides whether Live ships can be checked without a device.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout: two worktrees sharing a build dir means one tree can
# run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-powerprobe-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../src/apps_local/powerprobe \
  test_powerprobe.cpp -o "$BUILD_DIR/test_powerprobe"
"$BUILD_DIR/test_powerprobe"
