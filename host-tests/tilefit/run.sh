#!/bin/sh
# Walks every board of the published archive through the real Connections board
# screen and fails on a single shortened line.
#
#   host-tests/tilefit/run.sh
#
# Unlike host-tests/ui, this one links lib/EpdFont and the generated font
# headers: a truncation is a width in a real face, and a draw target that
# answers ten pixels a character cannot see one. The archive rides along as a
# fixture (archive.idx / archive.dat, the app's own pack format) so the suite
# runs on a bare CI runner with no SD card and no download.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-tilefit-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
SRC=../../src/apps_local/connections
"${CXX:-c++}" -std=c++17 -O1 -Wall -Wextra -Werror -Wno-comment -Wno-missing-field-initializers \
  -I"$SDK/include" -I"$ICONS/include" -I../../lib/EpdFont -I../../lib/Utf8 \
  "$SDK/src/FreeInkUI.cpp" \
  $SRC/ConnectionsCore.cpp $SRC/ConnectionsPack.cpp $SRC/ConnectionsScreens.cpp \
  ../../lib/EpdFont/EpdFont.cpp ../../lib/EpdFont/EpdFontFamily.cpp ../../lib/Utf8/Utf8.cpp \
  test_tilefit.cpp -o "$BUILD_DIR/test_tilefit"
"$BUILD_DIR/test_tilefit" .
