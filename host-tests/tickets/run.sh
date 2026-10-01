#!/bin/sh
# Builds and runs the ticket-format tests. TicketsCore is freestanding C++17.
#
#   host-tests/tickets/run.sh
#
# Only the standard library and lib/Utf8 are on the include path, which is the
# point: if the parser ever reaches for HalStorage, ArduinoJson or the
# renderer, this build fails loudly instead of the format quietly becoming
# device-only and therefore untested.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-tickets-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I../../lib/Utf8 \
  -I../../src/apps_local/tickets \
  ../../lib/Utf8/Utf8.cpp \
  ../../src/apps_local/tickets/TicketsCore.cpp \
  test_tickets.cpp -o "$BUILD_DIR/test_tickets"
"$BUILD_DIR/test_tickets"
