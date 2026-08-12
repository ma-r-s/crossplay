#!/bin/sh
# Builds and runs the xkcd app's freestanding tests. No device and no
# PlatformIO needed: XkcdCore is freestanding C++17.
#
#   host-tests/xkcd/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name -- two worktrees sharing one
# build dir means one tree can run, and pass, a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-xkcd-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/xkcd

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/XkcdCore.cpp" test_core.cpp -o "$BUILD_DIR/test_core"

"$BUILD_DIR/test_core"

# The layout rule lives in the pack builder, in Python, so it is tested where
# it lives rather than ported to C++ -- a second implementation of a rule this
# fiddly would drift, which is the same reason the pack and the device share
# one ditherer.
./test_layout.py
