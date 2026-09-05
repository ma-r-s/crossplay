#!/bin/sh
# Builds and runs the glyph-cache lifetime tests.
#
#   host-tests/prewarmscope/run.sh
#
# The real FontCacheManager, with SdCardFont faked in the test TU (SdCardFont.cpp
# is not linked: it needs the SD card). FontDecompressor comes along because
# FontCacheManager holds one; the SD-font path is what these tests drive, so no
# font file is needed.
#
# What is NOT on the include path: no src/, no Arduino core, no PlatformIO.
# stubs/ supplies the millisecond clock and the log sink, borrowed from
# host-tests/fontguard. If FontCacheManager ever reaches for the renderer or the
# card, this build fails loudly rather than the lifetime quietly becoming
# device-only and therefore untested.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-prewarmscope-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

LIB=../../lib

# uzlib is vendored third-party C: -Werror belongs on our code, not on theirs.
"${CC:-cc}" -std=c11 -O1 -g -I"$LIB/uzlib/src" \
  -c "$LIB/uzlib/src/tinflate.c" -o "$BUILD_DIR/tinflate.o"
"${CC:-cc}" -std=c11 -O1 -g -c ../fontguard/stubs/uzlib_checksums.c -o "$BUILD_DIR/uzlib_checksums.o"

"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -Werror \
  -Istubs -I"$LIB/GfxRenderer" -I"$LIB/EpdFont" -I"$LIB/InflateReader" -I"$LIB/Utf8" -I"$LIB/uzlib/src" \
  "$LIB/GfxRenderer/FontCacheManager.cpp" \
  "$LIB/EpdFont/FontDecompressor.cpp" \
  "$LIB/EpdFont/EpdFontFamily.cpp" \
  "$LIB/EpdFont/EpdFont.cpp" \
  "$LIB/InflateReader/InflateReader.cpp" \
  "$LIB/Utf8/Utf8.cpp" \
  test_prewarmscope.cpp "$BUILD_DIR/tinflate.o" "$BUILD_DIR/uzlib_checksums.o" -o "$BUILD_DIR/test_prewarmscope"

"$BUILD_DIR/test_prewarmscope"
