#!/bin/sh
# Builds and runs the dungeon rules tests. No device and no PlatformIO:
# DungeonCore is freestanding C++17.
#
#   host-tests/dungeon/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/dungeon-tests-$(basename "$(cd ../.. && pwd)")"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/dungeon
c++ -std=c++17 -Wall -Wextra -Werror $SRC/DungeonCore.cpp \
  test_dungeon.cpp -o "$BUILD_DIR/test_dungeon"
"$BUILD_DIR/test_dungeon"
