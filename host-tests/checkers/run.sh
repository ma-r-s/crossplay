#!/bin/sh
# Builds and runs the Checkers rules tests. CheckersCore is freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/checkers-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/checkers
c++ -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_checkers.cpp -o "$BUILD_DIR/test_checkers"
"$BUILD_DIR/test_checkers"
