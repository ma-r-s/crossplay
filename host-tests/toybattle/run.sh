#!/bin/sh
# Builds and runs the Toy Battle rules and opponent tests. No device and no
# PlatformIO: ToyBattleCore and ToyBattleBrain are freestanding C++17, which is
# what lets the whole rulebook and the opponent be checked without a panel.
#
#   host-tests/toybattle/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/toybattle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/toybattle
CXXFLAGS="-std=c++17 -Wall -Wextra -Werror -O2 -I$SRC"

c++ $CXXFLAGS $SRC/ToyBattleCore.cpp test_toybattle.cpp -o "$BUILD_DIR/test_toybattle"
"$BUILD_DIR/test_toybattle"

c++ $CXXFLAGS $SRC/ToyBattleCore.cpp $SRC/ToyBattleBrain.cpp test_brain.cpp -o "$BUILD_DIR/test_brain"
"$BUILD_DIR/test_brain"

# The terrain editor's checker is the only thing between a mistraced board and
# the firmware, so it is tested here rather than trusted.
python3 ../../tools_local/terrain-editor/selftest.py

# And the board in the tree must still be the board the fixture describes: if
# somebody hand-edits the generated terrain, this is where it shows up.
python3 ../../tools_local/terrain-editor/to_cpp.py --check \
  ../../tools_local/terrain-editor/boards/castle-field.json
