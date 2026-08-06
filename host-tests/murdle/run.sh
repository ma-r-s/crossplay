#!/bin/sh
# Builds and runs the Murdle puzzle tests. No device and no PlatformIO:
# MurdleCore is freestanding C++17.
#
#   host-tests/murdle/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/murdle-tests"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/murdle
c++ -std=c++17 -Wall -Wextra -Werror -O2 $SRC/MurdleCore.cpp $SRC/MurdleCast.cpp $SRC/MurdleText.cpp \
  test_murdle.cpp -o "$BUILD_DIR/test_murdle"
"$BUILD_DIR/test_murdle" "$@"
