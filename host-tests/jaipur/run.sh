#!/bin/sh
# Builds and runs the Jaipur rules and opponent tests. No device and no
# PlatformIO: JaipurCore and JaipurBrain are freestanding C++17, which is what
# lets the whole rulebook be checked without a panel.
#
#   host-tests/jaipur/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/jaipur-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/jaipur
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC $SRC/JaipurCore.cpp $SRC/JaipurBrain.cpp \
  test_jaipur.cpp -o "$BUILD_DIR/test_jaipur"
"$BUILD_DIR/test_jaipur"
