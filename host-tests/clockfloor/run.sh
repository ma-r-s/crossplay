#!/bin/sh
# Builds and runs the clock-floor tests. ClockFloor is freestanding C++17.
#
#   host-tests/clockfloor/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-clockfloor-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/ClockFloor.cpp test_clockfloor.cpp \
  -o "$BUILD_DIR/test_clockfloor"
"$BUILD_DIR/test_clockfloor"
