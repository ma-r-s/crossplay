#!/bin/sh
# Builds and runs the Insider rules tests. No device and no PlatformIO:
# InsiderCore is freestanding C++17.
#
#   host-tests/insider/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-insider-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/insider
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/InsiderCore.cpp \
  test_insider.cpp -o "$BUILD_DIR/test_insider"
"$BUILD_DIR/test_insider"
