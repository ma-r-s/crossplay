#!/bin/sh
# Builds and runs the Hearts rules tests. No device and no PlatformIO:
# HeartsCore is freestanding C++17.
#
#   host-tests/hearts/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-hearts-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/hearts
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/HeartsCore.cpp \
  test_hearts.cpp -o "$BUILD_DIR/test_hearts"
"$BUILD_DIR/test_hearts"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/HeartsCore.cpp $SRC/HeartsBrain.cpp \
  test_brain.cpp -o "$BUILD_DIR/test_brain"
"$BUILD_DIR/test_brain"
