#!/bin/sh
# Builds and runs the WAVELENGTH rules tests. No device and no PlatformIO:
# WavelengthCore is freestanding C++17, so the whole rulebook is checked on a
# laptop, including the two exhaustive properties (no dominated slot, and a
# perfect round being unbeatable).
#
#   host-tests/wavelength/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wavelength-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/wavelength
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_wavelength.cpp $SRC/WavelengthCore.cpp -o "$BUILD_DIR/test_wavelength"
"$BUILD_DIR/test_wavelength"

uv run --quiet python ../../tools_local/wavelength/check_widths.py
