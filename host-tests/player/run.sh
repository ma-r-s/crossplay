#!/bin/sh
# Builds and runs the player-name tests. Freestanding: the composing half takes
# no storage and no Arduino, which is the half worth testing -- the load/save
# half is four lines of Storage calls behind a platform guard.
#
#   host-tests/player/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name. Two worktrees sharing
# one build dir means one tree can run -- and pass -- a binary the other
# built, which is a green suite whose source is not even present.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-player-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 ../../src/apps_local/player/PlayerName.cpp \
  test_name.cpp -o "$BUILD_DIR/test_name"
"$BUILD_DIR/test_name"
