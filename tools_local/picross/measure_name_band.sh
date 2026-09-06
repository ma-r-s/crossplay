#!/bin/sh
# Measure the Picross win screen's name band and record it.
#
#   tools_local/picross/measure_name_band.sh
#
# Writes the width in pixels to tools_local/picross/name_band.txt, which
# name_fit.py reads and every name check compares against. Run it after any
# change to the win screen's layout, then re-run gen_picross.py: a name that fit
# the old band may not fit the new one, and fittedTitle SHRINKS rather than
# truncates.
#
# host-tests/picrossnames now does this same build on every run and fails if the
# recorded number is not what buildWin currently gives, so a stale name_band.txt
# is a red test rather than a check that reports clean against a band that no
# longer exists.
set -e
cd "$(dirname "$0")"
ROOT=../..
SDK="$ROOT/freeink-sdk/libs/ui/FreeInkUI"
ICONS="$ROOT/freeink-sdk/libs/assets/Icons"
BUILD="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-picross-nameband-$(cd $ROOT && pwd | cksum | cut -d' ' -f1)"
mkdir -p "$BUILD"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I"$SDK/include" -I"$ICONS/include" \
  "$SDK/src/FreeInkUI.cpp" \
  "$ROOT/src/apps_local/picross/PicrossCore.cpp" \
  "$ROOT/src/apps_local/picross/PicrossScreens.cpp" \
  measure_name_band.cpp -o "$BUILD/measure_name_band"
"$BUILD/measure_name_band" > name_band.txt
echo "name band: $(cat name_band.txt)px  (tools_local/picross/name_band.txt)"
