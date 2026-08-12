#!/bin/sh
# Dumps self-play positions with the result of the game they came from, for
# fitting the evaluation instead of guessing it. A measurement, not a test.
#   host-tests/toybattle/tune.sh <games-per-board> [board-index] > positions.tsv
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/toybattle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/toybattle
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  $SRC/ToyBattleCore.cpp $SRC/ToyBattleBrain.cpp tune.cpp -o "$BUILD_DIR/tune"
"$BUILD_DIR/tune" "$@"
