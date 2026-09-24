#!/bin/sh
# Builds and runs the Underhand tests. No device and no PlatformIO: the engine,
# its card reader, the save and the view are freestanding. The real cards are
# embedded, so the bots always play them.
#
#   host-tests/underhand/run.sh
#   UNDERHAND_DATA=<folder with cardwip.json and savedatafiletemplate.json> host-tests/underhand/run.sh
#
# The second form also checks the embedded cards against the APK's own files.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-underhand-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/underhand
JSON=../../lib/JsonParser
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC -I$JSON \
  test_underhand.cpp $SRC/UnderhandCards.cpp $SRC/UnderhandEngine.cpp $SRC/UnderhandSave.cpp $SRC/UnderhandView.cpp \
  $JSON/StreamingJsonParser.cpp \
  -o "$BUILD_DIR/test_underhand"
"$BUILD_DIR/test_underhand"
