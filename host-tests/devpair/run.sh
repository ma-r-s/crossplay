#!/bin/sh
# The pairing decision, tested rather than reviewed.
#
# Three consecutive cold-review rounds found three different bugs in this
# function, all of them in the same twenty lines and none of them visible to any
# suite. It guards replacing the device's firmware. The decision is pure now
# (src/DevModePairing.h) precisely so this file can exist.
#
#   host-tests/devpair/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-devpair-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I../../src \
  test_pairing.cpp -o "$BUILD_DIR/test_pairing"
"$BUILD_DIR/test_pairing"
