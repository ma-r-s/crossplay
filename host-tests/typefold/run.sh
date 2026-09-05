#!/bin/sh
# Builds and runs the typography-fold gate.
#
#   host-tests/typefold/run.sh
#
# This suite exists to ask the FONTS, not a list somebody wrote down. It
# compiles the real cuts from src/apps_local/ui/fonts/ together with the real
# EpdFont lookup drawText uses, and takes every row of utf8FoldTypography's
# table to them: is the source codepoint genuinely missing somewhere, and is the
# replacement genuinely present everywhere. A row added later is checked the
# same way without anybody editing this file.
#
# No PlatformIO and no device: EpdFont.cpp and Utf8.cpp are freestanding C++17,
# and the font headers are plain data. If either file ever reaches for Arduino
# or the SD card, this build fails loudly rather than the check quietly becoming
# device-only and therefore never run.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-typefold-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

# -Wno-missing-field-initializers because the font headers are GENERATED data:
# fontconvert.py emits an EpdFontData aggregate that stops at the last field it
# has a value for, and the firmware build does not turn that warning on either.
# Nothing here is hand-written enough for it to catch anything.
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -I../../lib/Utf8 -I../../lib/EpdFont -I../../src/apps_local/ui \
  ../../lib/Utf8/Utf8.cpp \
  ../../lib/EpdFont/EpdFont.cpp \
  test_typefold.cpp -o "$BUILD_DIR/test_typefold"
"$BUILD_DIR/test_typefold"
