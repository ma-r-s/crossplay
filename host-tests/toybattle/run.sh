#!/bin/sh
# Builds and runs the Toy Battle rules and opponent tests. No device and no
# PlatformIO: ToyBattleCore and ToyBattleBrain are freestanding C++17, which is
# what lets the whole rulebook and the opponent be checked without a panel.
#
#   host-tests/toybattle/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-toybattle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/toybattle
CXXFLAGS="-std=c++17 -Wall -Wextra -Werror -O2 -I$SRC"

"${CXX:-c++}" $CXXFLAGS $SRC/ToyBattleCore.cpp test_toybattle.cpp -o "$BUILD_DIR/test_toybattle"
"$BUILD_DIR/test_toybattle"

"${CXX:-c++}" $CXXFLAGS $SRC/ToyBattleCore.cpp $SRC/ToyBattleBrain.cpp test_brain.cpp -o "$BUILD_DIR/test_brain"
"$BUILD_DIR/test_brain"

"${CXX:-c++}" $CXXFLAGS $SRC/ToyBattleCore.cpp $SRC/ToyBattleFlow.cpp test_flow.cpp -o "$BUILD_DIR/test_flow"
"$BUILD_DIR/test_flow"

# The terrain editor's checker is the only thing between a mistraced board and
# the firmware, so it is tested here rather than trusted.
python3 ../../tools_local/terrain-editor/selftest.py

# And the board in the tree must still be the board the fixture describes: if
# somebody hand-edits the generated terrain, this is where it shows up.
for board in ../../tools_local/terrain-editor/boards/*.json; do
  python3 ../../tools_local/terrain-editor/to_cpp.py --check "$board"
done

# The six instruments beside this file: COMPILED, never run.
#
# branching, exploit, montecarlo, tournament and tune are measurements, and
# each one's header says so and says why it is not a test -- they take minutes
# and assert almost nothing, because their job is to decide which opponent is
# strongest rather than to defend one that shipped. mksave writes a save
# parked at a UI state no tap script can reach, for looking at the Cursed
# Cemetery prompt. All six drive about 1,400 lines against ToyBattleCore and
# ToyBattleBrain, and until 2026-09-05 no gate anywhere compiled any of them
# while CI enforced their formatting: a rename in the core would have left
# them broken and silent, and the next person to reach for one would find out
# by not being able to build it.
#
# Compiling is the whole of what they need. -fsyntax-only, all six in under a
# second, at the same -Werror the tests use.
echo "instruments (compile only: measurements, not tests)"
for tool in branching exploit montecarlo tournament tune; do
  "${CXX:-c++}" $CXXFLAGS -fsyntax-only "$tool.cpp"
done
"${CXX:-c++}" $CXXFLAGS -fsyntax-only mksave.cpp
echo "  branching exploit montecarlo tournament tune mksave: compile"
