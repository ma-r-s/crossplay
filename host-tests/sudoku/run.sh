#!/bin/sh
# Builds and runs the Sudoku rules tests. No device and no PlatformIO:
# SudokuCore is freestanding C++17.
#
#   host-tests/sudoku/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-sudoku-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/sudoku
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_sudoku.cpp $SRC/SudokuCore.cpp -o "$BUILD_DIR/test_sudoku"
"$BUILD_DIR/test_sudoku"
