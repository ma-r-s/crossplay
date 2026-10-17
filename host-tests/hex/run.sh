#!/bin/sh
# Builds and runs the Hex rules, brain and save tests.
#
# No device and no PlatformIO: HexCore, HexBrain and HexSave are freestanding
# C++17 and were written that way so the whole of the game except the drawing
# runs on a laptop in a second.
#
#   host-tests/hex/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name. Two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built,
# which is a green suite whose source is not even present.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-hex-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/hex

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_hex.cpp \
  $SRC/HexCore.cpp $SRC/HexBrain.cpp $SRC/HexSave.cpp \
  -o "$BUILD_DIR/test_hex"
"$BUILD_DIR/test_hex"
