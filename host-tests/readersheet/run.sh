#!/bin/sh
# The reader's toolbar panel sizes its bottom sheet for N rows, then hands the
# list a rowGap it never set. This asks the REAL SDK what gap it resolves on a
# touch device and compares it with the one the sheet reserved. No device, no
# PlatformIO: FreeInkUI is freestanding C++17.
#
#   host-tests/readersheet/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-readersheet-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-comment \
  -I"$SDK/include" -I"$ICONS/include" \
  "$SDK/src/FreeInkUI.cpp" \
  test_readersheet.cpp -o "$BUILD_DIR/test_readersheet"
"$BUILD_DIR/test_readersheet"
