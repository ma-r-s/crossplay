#!/bin/sh
# Builds and runs the Battleship rules tests. No device and no PlatformIO:
# BattleshipCore is freestanding C++17, which is also what lets two simulated
# devices play a whole game in host-tests/link/.
#
#   host-tests/battleship/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-battleship-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/battleship
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/BattleshipCore.cpp \
  test_battleship.cpp -o "$BUILD_DIR/test_battleship"
"$BUILD_DIR/test_battleship"
