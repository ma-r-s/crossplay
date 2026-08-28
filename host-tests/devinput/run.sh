#!/bin/sh
# Builds and runs the shared input-vocabulary tests.
#
# lib/DevInput/DevInputCommands.cpp exists so the serial bridge and Developer
# Mode's /api/dev/input cannot drift apart. Nothing enforced that: it is pure
# string parsing behind four injector calls, and it shipped with a hold of -1
# accepted, which wedged the device until a power cycle because a synthetic
# contact releases on `elapsed >= holdMs` and an unsigned -1 never arrives.
#
# The injector and the board are stubbed; what is under test is the parsing,
# the validation, and the replies -- all of which are the contract both
# transports quote back to their callers.
#
#   host-tests/devinput/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-devinput-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  -DCROSSPOINT_DEV_SERIAL_BRIDGE=1 \
  -Istubs -I../../lib/DevInput \
  ../../lib/DevInput/DevInputCommands.cpp test_commands.cpp -o "$BUILD_DIR/test_commands"
"$BUILD_DIR/test_commands"
