#!/bin/sh
# The BOOX Picco: its profile against the board CrossPlay's apps already run
# on (test_profile), the one guard every flash path asks (test_updateslot), and
# its build, release and refusal contracts (test_config.py).
#
#   host-tests/picco/run.sh
set -e
cd "$(dirname "$0")"
SDK=../../freeink-sdk/libs/hardware
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-picco-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

# The SDK's own Picco host stubs (Arduino.h and friends) are what let its
# BoardConfig.h compile off the device.
"${CXX:-c++}" -std=c++20 -O1 -Wall -Wextra -Wno-unused-parameter -DFREEINK_DEVICE_PICCO=1 \
  -I"$SDK/InputManager/test/host/picco_stubs" -I"$SDK/BoardConfig/include" \
  test_profile.cpp -o "$BUILD_DIR/test_profile"
"$BUILD_DIR/test_profile"

"${CXX:-c++}" -std=c++20 -O1 -Wall -Wextra -Werror -Istubs -I../../src \
  test_updateslot.cpp -o "$BUILD_DIR/test_updateslot"
"$BUILD_DIR/test_updateslot"

python3 test_config.py
