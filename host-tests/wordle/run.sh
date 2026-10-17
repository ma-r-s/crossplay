#!/bin/bash
# Wordle's rules and data formats, checked without a panel.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wordle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/wordle
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_wordle.cpp $SRC/WordleCore.cpp -o "$BUILD_DIR/test_wordle"
"$BUILD_DIR/test_wordle"
