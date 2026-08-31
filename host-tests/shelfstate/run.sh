#!/bin/sh
# Builds and runs the shelf.cfg format tests. No device and no PlatformIO:
# ShelfState is freestanding C++17, which is what lets the file format that
# survives a sleep be checked without a card.
#
#   host-tests/shelfstate/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-shelfstate-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_shelfstate.cpp $SRC/ShelfState.cpp -o "$BUILD_DIR/test_shelfstate"
"$BUILD_DIR/test_shelfstate"
