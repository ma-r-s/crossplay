#!/bin/sh
# Builds and runs the reveal sweep. Same freestanding build as host-tests/ui:
# no device, no PlatformIO, no src/ or lib/ on the include path.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-revealsweep-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wno-comment -Wno-format-truncation \
  -I"$SDK/include" -I"$ICONS/include" \
  "$SDK/src/FreeInkUI.cpp" \
  ../../src/apps_local/battleship/BattleshipScreens.cpp \
  ../../src/apps_local/link/LinkScreens.cpp \
  ../../src/apps_local/player/PlayerAvatar.cpp \
  ../../src/apps_local/player/PlayerName.cpp \
  ../../src/apps_local/minesweeper/MinesweeperScreens.cpp \
  ../../src/apps_local/sudoku/SudokuCore.cpp \
  ../../src/apps_local/sudoku/SudokuScreens.cpp \
  ../../src/apps_local/wavelength/WavelengthCore.cpp \
  ../../src/apps_local/wavelength/WavelengthScreens.cpp \
  sweep.cpp -o "$BUILD_DIR/sweep"
"$BUILD_DIR/sweep"
