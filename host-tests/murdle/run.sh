#!/bin/sh
# Builds and runs the Murdle puzzle tests. No device and no PlatformIO:
# MurdleCore is freestanding C++17, and so are EpdFont and the generated cuts,
# which is what lets the refusal notice be measured in the real face here.
#
#   host-tests/murdle/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-murdle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/murdle
# The real tile cut and the real EpdFont lookup, so the notice-band check
# measures the face the device draws with rather than a character count. Both
# are freestanding C++17 (host-tests/typefold compiles them the same way);
# -Wno-missing-field-initializers is for the GENERATED font header, whose
# aggregate stops at the last field fontconvert.py had a value for.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -O2 \
  -I../../lib/Utf8 -I../../lib/EpdFont -I../../src/apps_local/ui \
  ../../lib/Utf8/Utf8.cpp ../../lib/EpdFont/EpdFont.cpp \
  $SRC/MurdleCore.cpp $SRC/MurdleCast.cpp $SRC/MurdleText.cpp \
  test_murdle.cpp -o "$BUILD_DIR/test_murdle"
"$BUILD_DIR/test_murdle" "$@"
