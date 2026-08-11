#!/bin/sh
# Competes the opponent policies against each other and prints a tier list.
# Deliberately NOT part of check.sh: it is a measurement that takes minutes, not
# a test that guards a commit.
#
#   host-tests/toybattle/tournament.sh [games-per-pairing-per-condition]
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/toybattle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/toybattle
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  $SRC/ToyBattleCore.cpp $SRC/ToyBattleBrain.cpp tournament.cpp -o "$BUILD_DIR/tournament"
"$BUILD_DIR/tournament" "$@"
