#!/bin/sh
# Builds and runs the Minesweeper rules tests. No device and no PlatformIO:
# MinesweeperCore is freestanding C++17.
#
#   host-tests/minesweeper/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/minesweeper-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/minesweeper
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_minesweeper.cpp -o "$BUILD_DIR/test_minesweeper"
"$BUILD_DIR/test_minesweeper"
