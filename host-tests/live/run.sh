#!/bin/sh
# Live's arithmetic on a laptop: the interval clamp, the capped backoff, the
# clock plausibility floor and the one wake rule.
#
#   host-tests/live/run.sh
#
# Nothing here touches a card, a radio or a panel, which is the point: the
# failure the backoff exists to prevent is a battery three weeks out, and the
# due-now rule only misbehaves on the sleep AFTER the one you were watching.
# Neither is observable by looking at a device.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-live-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -O1 -Wall -Wextra -Werror \
  ../../src/apps_local/live/LiveCore.cpp \
  test_live.cpp -o "$BUILD_DIR/test_live"
"$BUILD_DIR/test_live"
