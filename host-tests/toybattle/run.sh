#!/bin/sh
# Builds and runs the Toy Battle rules tests. No device and no PlatformIO:
# ToyBattleCore is freestanding C++17, which is what lets the whole rulebook be
# checked without a panel.
#
#   host-tests/toybattle/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/toybattle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/toybattle
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC $SRC/ToyBattleCore.cpp \
  test_toybattle.cpp -o "$BUILD_DIR/test_toybattle"
"$BUILD_DIR/test_toybattle"
