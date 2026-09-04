#!/bin/sh
# Builds and runs the device-report tests. No device and no PlatformIO:
# DeviceReportCore is freestanding C++17, which is what lets the device id,
# the report header, the state file and the own-host rule be checked without
# a card or a radio. The device glue (DeviceReport.cpp) and the transports
# that call it are what stay unverified until a device talks to a service.
#
#   host-tests/devreport/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-devreport-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/network
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_devreport.cpp $SRC/DeviceReportCore.cpp -o "$BUILD_DIR/test_devreport"
"$BUILD_DIR/test_devreport"
