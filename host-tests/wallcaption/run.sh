#!/bin/sh
# Walks every built-in wallpaper name and the "+ Add" tile through the picker's
# real geometry and the real toybox cuts, and fails if the selection marker can
# touch the artwork or a caption in any grid position.
#
#   host-tests/wallcaption/run.sh
#
# Links lib/EpdFont and the generated font headers on purpose: host-tests/ui
# measures ten pixels a character, which cannot see a caption overflow a 165px
# cell in a real face.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wallcaption-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
SRC=../../src/apps_local/wallpapers
# ONCE PER LIVE ARRANGEMENT. WALLPAPERS_LIVE_SCREEN is a compile-time choice, so
# a single build walks one of the three and reports clean about the other two --
# and the three do not share a layout, only a model. When the winner is picked
# and the losing two are deleted, this loop goes back to being one build.
for screen in 1 2 3; do
  "${CXX:-c++}" -std=c++17 -O1 -Wall -Wextra -Werror -Wno-comment -Wno-missing-field-initializers \
    -DWALLPAPERS_LIVE_SCREEN="$screen" \
    -I"$SDK/include" -I"$ICONS/include" -I../../lib/EpdFont -I../../lib/Utf8 \
    "$SDK/src/FreeInkUI.cpp" \
    $SRC/WallpapersCore.cpp $SRC/WallpapersScreens.cpp \
    ../../lib/EpdFont/EpdFont.cpp ../../lib/EpdFont/EpdFontFamily.cpp ../../lib/Utf8/Utf8.cpp \
    test_wallcaption.cpp -o "$BUILD_DIR/test_wallcaption-$screen"
  echo "wallcaption: Live arrangement $screen"
  "$BUILD_DIR/test_wallcaption-$screen"
done
