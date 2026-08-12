#!/bin/sh
# Builds and runs the Solitaire rules tests. No device and no PlatformIO:
# SolitaireCore is freestanding C++17.
#
#   host-tests/solitaire/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-solitaire-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/solitaire
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/SolitaireCore.cpp \
  test_solitaire.cpp -o "$BUILD_DIR/test_solitaire"
"$BUILD_DIR/test_solitaire"
