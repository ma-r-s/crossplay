#!/bin/sh
# Builds and runs the workout-rule tests. WorkoutsCore is freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-workouts-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/workouts
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_workouts.cpp $SRC/WorkoutsCore.cpp -o "$BUILD_DIR/test_workouts"
"$BUILD_DIR/test_workouts"
