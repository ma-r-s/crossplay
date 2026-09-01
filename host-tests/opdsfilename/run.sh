#!/bin/sh
# Builds and runs the OPDS download-filename tests. No device and no
# PlatformIO: OpdsFilename is freestanding, which is what lets the name a
# downloaded book lands under be checked without a card or a catalogue.
#
#   host-tests/opdsfilename/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-opdsfilename-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/util
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC -I../../lib/Utf8 \
  test_opdsfilename.cpp $SRC/OpdsFilename.cpp $SRC/StringUtils.cpp ../../lib/Utf8/Utf8.cpp -o "$BUILD_DIR/test_opdsfilename"
"$BUILD_DIR/test_opdsfilename"
