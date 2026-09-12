#!/bin/sh
# Builds and runs the Go rules tests. GoCore is freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-go-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/go
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_go.cpp $SRC/GoCore.cpp -o "$BUILD_DIR/test_go"
"$BUILD_DIR/test_go"
