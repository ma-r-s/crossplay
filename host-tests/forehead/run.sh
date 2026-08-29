#!/bin/sh
# Builds and runs the FOREHEAD rules and word-list tests. No device and no
# PlatformIO: ForeheadCore is freestanding C++17 and the generated word table is
# plain data, so the whole rulebook AND every entry are checked on a laptop.
#
#   host-tests/forehead/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-forehead-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/forehead
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_forehead.cpp $SRC/ForeheadCore.cpp -o "$BUILD_DIR/test_forehead"
"$BUILD_DIR/test_forehead"
