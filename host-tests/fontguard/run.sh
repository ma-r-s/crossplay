#!/bin/sh
# Can a font make the glyph decoder read past the end of its group array?
#
# FontDecompressor::getGroupIndex() can name a group that does not exist two
# ways -- a not-found sentinel equal to groupCount, and a raw uint16_t taken
# from the font file with nothing checking it. getBitmap() tested for both.
# prewarmCache() did not, and indexed fontData->groups with the result.
#
# Reading the code could not settle whether any font reaches that, so this
# suite builds the fonts by hand and runs the real decoder over them.
#
#   host-tests/fontguard/run.sh
#
# A GUARD PAGE IS THE INSTRUMENT. The fixtures mmap every array a malformed font
# would walk off so it sits flush against a PROT_NONE page: a read of
# groups[groupCount] traps at the offending byte instead of returning whatever
# followed it in memory. Without that, these tests PASS against the unfixed
# decoder -- which is precisely why a read-only review could not settle whether
# the out-of-bounds read was real.
#
# Deliberately NOT AddressSanitizer, which would otherwise be the obvious choice:
# its runtime hangs in its own initialiser (get_dyld_hdr) on this machine's Apple
# clang 17 / Darwin 27 pairing, so the suite never reaches main(). mmap and
# mprotect need no sanitizer runtime, so this behaves the same under CI's GCC.
#
# What is NOT on the include path: no src/, no Arduino core, no PlatformIO. The
# decoder needs <Arduino.h> and <Logging.h> for a millisecond clock and four log
# lines, and stubs/ supplies both. If it ever reaches for the SD card or the
# renderer, this build fails loudly rather than the decoder quietly becoming
# device-only and therefore untested.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-fontguard-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

LIB=../../lib

# tinflate.c is C, and uzlib is vendored third-party: -Werror belongs on our
# code, not on theirs.
"${CC:-cc}" -std=c11 -O1 -g -I"$LIB/uzlib/src" \
  -c "$LIB/uzlib/src/tinflate.c" -o "$BUILD_DIR/tinflate.o"
"${CC:-cc}" -std=c11 -O1 -g -c stubs/uzlib_checksums.c -o "$BUILD_DIR/uzlib_checksums.o"

"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -Werror \
  -Istubs -I"$LIB/EpdFont" -I"$LIB/InflateReader" -I"$LIB/Utf8" -I"$LIB/uzlib/src" \
  "$LIB/EpdFont/FontDecompressor.cpp" \
  "$LIB/InflateReader/InflateReader.cpp" \
  "$LIB/Utf8/Utf8.cpp" \
  test_fontguard.cpp "$BUILD_DIR/tinflate.o" "$BUILD_DIR/uzlib_checksums.o" -o "$BUILD_DIR/test_fontguard"

"$BUILD_DIR/test_fontguard"
