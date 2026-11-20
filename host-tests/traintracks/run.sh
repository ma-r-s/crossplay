#!/bin/sh
# Builds and runs the Train Tracks tests. No device and no PlatformIO: the
# core, solver and generator are freestanding C++17.
#
#   host-tests/traintracks/run.sh
#
# test_traintracks   rules, win check, undo, shape inference, solver tiers,
#                    all held to a brute force on small grids (~2s)
# test_generator     1000 puzzles per level, determinism, the fallback bank
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-traintracks-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/traintracks
# Named literally, not looped over: check.sh greps this file for each test
# source's name to prove nothing in the directory goes unrun.
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$SRC" test_traintracks.cpp -o "$BUILD_DIR/test_traintracks"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$SRC" test_generator.cpp -o "$BUILD_DIR/test_generator"
"$BUILD_DIR/test_traintracks"
"$BUILD_DIR/test_generator"
