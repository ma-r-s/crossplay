#!/bin/sh
# Builds and runs the Wappo rules tests, then re-solves every level. No device
# and no PlatformIO: WappoCore is freestanding C++17.
#
#   host-tests/wappo/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wappo-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/wappo
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_wappo.cpp $SRC/WappoCore.cpp -o "$BUILD_DIR/test_wappo"
"$BUILD_DIR/test_wappo"
python3 test_wappo.py
