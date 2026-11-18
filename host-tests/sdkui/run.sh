#!/bin/sh
# The SDK's own FreeInkUI suite, run against the SDK this tree pins.
#
#   host-tests/sdkui/run.sh
#
# The SDK carries 300,000-odd checks of its own components (lists, keyboard,
# header, text) and nothing here ran them: a list change pinned for 1.16.0
# left two of them red, and the gate stayed green because it never looked.
# Compiled here rather than through the SDK's run.sh, whose build directory
# is one path for every tree on this Mac.
set -e
cd "$(dirname "$0")"
UI=../../freeink-sdk/libs/ui/FreeInkUI
[ -f "$UI/test/host/test_freeinkui.cpp" ] || {
  echo "FAIL sdkui: $UI/test/host/test_freeinkui.cpp is missing (is freeink-sdk checked out?)"
  exit 1
}
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-sdkui-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -pthread -Wall -Wextra -Werror -I"$UI/include" "$UI/src/FreeInkUI.cpp" \
  "$UI/test/host/test_freeinkui.cpp" -o "$BUILD_DIR/test_freeinkui"
"$BUILD_DIR/test_freeinkui"
