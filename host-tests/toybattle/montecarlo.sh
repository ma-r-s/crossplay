#!/bin/sh
# Flat Monte Carlo against the heuristic brain. A measurement, not a test.
#   host-tests/toybattle/montecarlo.sh [games] [playouts-per-move]
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/toybattle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/toybattle
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  $SRC/ToyBattleCore.cpp $SRC/ToyBattleBrain.cpp montecarlo.cpp -o "$BUILD_DIR/montecarlo"
"$BUILD_DIR/montecarlo" "$@"
