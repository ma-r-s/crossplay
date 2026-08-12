#!/bin/sh
# Writes a Toy Battle save parked at a hard-to-reach UI state, so a screen that
# needs a specific position can be LOOKED at instead of only asserted.
#
# Built for the Cursed Cemetery bug (2026-08-11): the Exhume prompt needs a
# stocked discard AND a placement on a grave, which no tap script can arrange.
#
#   host-tests/toybattle/mksave.sh fs_agent/.crosspoint/toybattle.sav
#
# Then drive it with the recipe in qa-artifacts/exh-prompt.recipe.sh.
#
# NOTE isWellFormed checks per-kind conservation against the SEED's own shuffle
# -- deliberately, so a state cannot be forged for a different game. A position
# therefore has to be DEALT from that shuffle, not invented, which is why this
# searches seeds rather than assigning troops directly.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/toybattle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/toybattle
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  $SRC/ToyBattleCore.cpp $SRC/ToyBattleFlow.cpp mksave.cpp -o "$BUILD_DIR/mksave"
"$BUILD_DIR/mksave" "$(cd ../.. && pwd)/$1"
