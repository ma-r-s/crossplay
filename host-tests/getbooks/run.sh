#!/bin/sh
# Builds and runs the Get Books regression tests. No device and no PlatformIO:
# the two pieces under test were written freestanding for exactly this reason
# -- src/components/UiHeaderChrome.h is SDK data, and
# src/components/BlockingFetchInput.h is templated on the input manager so it
# can be driven by a fake here.
#
#   host-tests/getbooks/run.sh
#
# Note what is NOT on the include path: no lib/, no Arduino. If either header
# ever reaches for GfxRenderer, UITheme or the SD card, this build fails loudly
# instead of the check quietly becoming untestable.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-getbooks-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-comment \
  -I"$SDK/include" \
  "$SDK/src/FreeInkUI.cpp" \
  test_getbooks.cpp -o "$BUILD_DIR/test_getbooks"
"$BUILD_DIR/test_getbooks"
