#!/bin/sh
# Builds and runs the dungeon rules tests. No device and no PlatformIO:
# DungeonCore is freestanding C++17.
#
#   host-tests/dungeon/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-dungeon-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/dungeon
# The screens are linked in too, for one test: the adventurer's guide teaches
# on the tutorial's own board, and the pages that make up that lesson live with
# the drawing code. FreeInkUI is freestanding C++17, so this costs one more
# translation unit and no device.
SDK=../../freeink-sdk/libs
# One GCC-only suppression. It does not show up under Apple clang, which is why
# it only appeared once CI built this on Linux.
#
# -Wno-comment: FreeInkUIIcon.h documents a shell command in a // comment whose
# line ends in a backslash, which GCC reads as a line continuation. The header
# belongs to the pinned freeink-sdk submodule, so silencing it here is cheaper
# than carrying a patch against someone else's tree.
#
# -Wno-format-truncation was here too, on the argument that truncation is the
# intended behaviour. It is not: a tile shows the whole text, no exceptions, and
# a cut string looks the same as a short one on the panel. It hid a guide
# counter that could not hold its own "%d/%d" (card 256). Do not put it back.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-comment \
  -I"$SDK/ui/FreeInkUI/include" -I"$SDK/assets/Icons/include" \
  "$SDK/ui/FreeInkUI/src/FreeInkUI.cpp" \
  $SRC/DungeonCore.cpp $SRC/DungeonScreens.cpp \
  test_dungeon.cpp -o "$BUILD_DIR/test_dungeon"
"$BUILD_DIR/test_dungeon"
