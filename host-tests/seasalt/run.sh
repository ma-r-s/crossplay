#!/bin/sh
# Builds and runs the Sea Salt & Paper rules tests. No device and no PlatformIO:
# SeaSaltCore is freestanding C++17, which is what lets the whole rulebook be
# checked without a panel.
#
#   host-tests/seasalt/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-seasalt-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/seasalt
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC $SRC/SeaSaltCore.cpp \
  test_seasalt.cpp -o "$BUILD_DIR/test_seasalt"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC $SRC/SeaSaltCore.cpp $SRC/SeaSaltBrain.cpp \
  test_brain.cpp -o "$BUILD_DIR/test_brain"
"$BUILD_DIR/test_seasalt"
"$BUILD_DIR/test_brain"
