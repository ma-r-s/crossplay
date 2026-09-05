#!/bin/sh
# Builds and runs the Wallpapers picker-core tests. No device and no
# PlatformIO: WallpapersCore is freestanding C++17 (the file-name filter and
# the free-space precondition), which is exactly why it lives apart from the
# Activity's SD I/O -- logic left in an activity is untestable by construction.
#
#   host-tests/wallpapers/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name, so two worktrees cannot run
# one another's binary and report a green suite whose source is not present.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wallpapers-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/wallpapers
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  "$SRC/WallpapersCore.cpp" \
  test_wallpapers.cpp -o "$BUILD_DIR/test_wallpapers"
"$BUILD_DIR/test_wallpapers"
