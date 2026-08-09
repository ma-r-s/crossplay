#!/bin/sh
# Builds and runs the Knucklebones rules tests. No device and no PlatformIO:
# KnucklebonesCore is freestanding C++17, which is what lets the whole rulebook
# be checked without a panel.
#
#   host-tests/knucklebones/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/knucklebones-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/knucklebones
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_knucklebones.cpp -o "$BUILD_DIR/test_knucklebones"
"$BUILD_DIR/test_knucklebones"
