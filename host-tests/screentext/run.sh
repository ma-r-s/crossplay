#!/bin/sh
# Builds and runs the screen-text tests. No device and no PlatformIO: both
# files under test are freestanding C++17.
#
#   host-tests/screentext/run.sh
#
# Only the standard library is on the include path, which is the point. The
# whitespace helper lives in lib/Utf8 beside utf8ComposeNfc, and for the same
# reason: it exists because of what the device fonts cannot draw. If either file
# ever reaches for Arduino, expat or the SD card, this build fails loudly rather
# than the logic quietly becoming device-only and therefore untested.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-screentext-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../lib/Utf8 -I../../src/util \
  ../../lib/Utf8/Utf8.cpp \
  ../../src/util/BookmarkUtil.cpp \
  test_screentext.cpp -o "$BUILD_DIR/test_screentext"
"$BUILD_DIR/test_screentext"
