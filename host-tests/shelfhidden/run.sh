#!/bin/sh
# Builds and runs the shelf-hidden.cfg format tests. No device and no
# PlatformIO: ShelfHidden is freestanding C++17, which is what lets the file
# deciding WHICH GAMES EXIST for a person be checked without a card.
#
#   host-tests/shelfhidden/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-shelfhidden-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_shelfhidden.cpp $SRC/ShelfHidden.cpp -o "$BUILD_DIR/test_shelfhidden"
"$BUILD_DIR/test_shelfhidden"
