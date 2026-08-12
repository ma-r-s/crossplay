#!/bin/sh
# Builds and runs the ConnectFour rules tests. ConnectFourCore is freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-connectfour-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/connectfour
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_connectfour.cpp -o "$BUILD_DIR/test_connectfour"
"$BUILD_DIR/test_connectfour"
