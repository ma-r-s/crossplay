#!/bin/sh
# Builds and runs the picross rules tests. No device and no PlatformIO:
# PicrossCore is freestanding C++17.
#
#   host-tests/picross/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-picross-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/picross
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  "$SRC/PicrossCore.cpp" \
  test_picross.cpp -o "$BUILD_DIR/test_picross"
"$BUILD_DIR/test_picross"
