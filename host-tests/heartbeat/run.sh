#!/bin/sh
# Builds and runs the heartbeat tests. No device and no PlatformIO:
# HeartbeatCore is freestanding C++17, which is what lets the device id, the
# event bodies, the state file and the once-a-day rule be checked without a
# card or a radio. The device glue (Heartbeat.cpp) is what stays unverified
# until a device on Wi-Fi shows up on the board.
#
#   host-tests/heartbeat/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-heartbeat-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/network
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_heartbeat.cpp $SRC/HeartbeatCore.cpp -o "$BUILD_DIR/test_heartbeat"
"$BUILD_DIR/test_heartbeat"
