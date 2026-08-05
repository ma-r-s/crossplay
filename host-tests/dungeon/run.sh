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
# The screens are linked in too, for one test: the adventurer's guide teaches
# on the tutorial's own board, and the pages that make up that lesson live with
# the drawing code. FreeInkUI is freestanding C++17, so this costs one more
# translation unit and no device.
SDK=../../freeink-sdk/libs
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$SDK/ui/FreeInkUI/include" -I"$SDK/assets/Icons/include" \
  "$SDK/ui/FreeInkUI/src/FreeInkUI.cpp" \
  $SRC/DungeonCore.cpp $SRC/DungeonScreens.cpp \
  test_dungeon.cpp -o "$BUILD_DIR/test_dungeon"
"$BUILD_DIR/test_dungeon"
