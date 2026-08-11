#!/bin/sh
# Measures how wide the move list actually gets, per board. Deliberately NOT
# part of check.sh: a measurement, like tournament.sh.
#
#   host-tests/toybattle/branching.sh [games-per-condition]
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/toybattle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/toybattle
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  $SRC/ToyBattleCore.cpp $SRC/ToyBattleBrain.cpp branching.cpp -o "$BUILD_DIR/branching"
"$BUILD_DIR/branching" "$@"
