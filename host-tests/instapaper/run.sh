#!/bin/sh
# Builds and runs the read-later tests. No device and no PlatformIO:
# InstapaperIndex is freestanding C++17.
#
#   host-tests/instapaper/run.sh
#
# Nothing but the standard library is on the include path, which is the point.
# If the index format or the merge ever reaches for HalStorage, ArduinoJson or
# the network, this build fails loudly instead of the logic quietly becoming
# device-only and therefore untested.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-instapaper-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../lib/Utf8 \
  ../../lib/Utf8/Utf8.cpp \
  ../../src/apps_local/instapaper/InstapaperIndex.cpp \
  test_index.cpp -o "$BUILD_DIR/test_index"
"$BUILD_DIR/test_index"
