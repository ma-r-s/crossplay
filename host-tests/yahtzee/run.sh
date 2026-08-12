#!/bin/sh
# Builds and runs the Yahtzee rules tests. YahtzeeCore is freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-yahtzee-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/yahtzee
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_yahtzee.cpp -o "$BUILD_DIR/test_yahtzee"
"$BUILD_DIR/test_yahtzee"
