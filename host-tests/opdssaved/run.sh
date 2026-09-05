#!/bin/sh
# Builds and runs the dwell tests for the download's SAVED verdict screen. No
# device and no PlatformIO: DismissDwell is freestanding, which is the whole
# point of it being a separate object at all -- the activity that owns it
# cannot be built on the host, and a rule nobody can watch fail is not a rule.
#
#   host-tests/opdssaved/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-opdssaved-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  test_dismissdwell.cpp -o "$BUILD_DIR/test_dismissdwell"
"$BUILD_DIR/test_dismissdwell"
