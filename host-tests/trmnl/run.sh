#!/bin/sh
# Builds and runs the TRMNL tests: the rules (TrmnlCore) and the screens
# (TrmnlScreens, against FreeInkUI with no renderer). Both are freestanding.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-trmnl-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/trmnl
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_trmnl.cpp $SRC/TrmnlCore.cpp -o "$BUILD_DIR/test_trmnl"
"$BUILD_DIR/test_trmnl"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-comment -I"$SDK/include" -I"$ICONS/include" -I$SRC \
  test_trmnl_screens.cpp "$SDK/src/FreeInkUI.cpp" $SRC/TrmnlCore.cpp $SRC/TrmnlScreens.cpp \
  -o "$BUILD_DIR/test_trmnl_screens"
"$BUILD_DIR/test_trmnl_screens"
