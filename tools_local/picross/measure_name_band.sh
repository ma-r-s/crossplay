#!/bin/sh
# Measure the Picross win screen's name band and record it.
#
#   tools_local/picross/measure_name_band.sh
#
# Writes the width in pixels to tools_local/picross/name_band.txt, which
# gen_name_tool.py reads. Run it after any change to the win screen's layout,
# then re-run gen_name_tool.py: the naming tool's "will this fit" answer is
# only as good as this number.
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
