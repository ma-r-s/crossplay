#!/bin/sh
# Builds and runs the FSRS scheduler tests. No device and no PlatformIO needed:
# StudyFsrs is freestanding C++17.
#
#   host-tests/study/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name -- two worktrees sharing one
# build dir means one tree can run, and pass, a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/study-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
c++ -std=c++17 -O2 -Wall -Wextra -Werror \
  ../../src/apps_local/study/StudyFsrs.cpp test_fsrs.cpp -o "$BUILD_DIR/test_fsrs"
"$BUILD_DIR/test_fsrs"
