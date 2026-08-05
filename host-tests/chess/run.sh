#!/bin/sh
# Builds and runs the chess rules tests. No device and no PlatformIO needed:
# ChessCore is freestanding C++17.
#
#   host-tests/chess/run.sh
#
# -O2 matters here: the deeper perft cases are slow in an unoptimised build.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name. Two worktrees sharing
# one build dir means one tree can run -- and pass -- a binary the other
# built, which is a green suite whose source is not even present.
BUILD_DIR="${TMPDIR:-/tmp}/chess-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
c++ -std=c++17 -O2 -Wall -Wextra -Werror ../../src/apps_local/chess/ChessCore.cpp ../../src/apps_local/chess/ChessEngine.cpp test_chess.cpp -o "$BUILD_DIR/test_chess"
"$BUILD_DIR/test_chess"
