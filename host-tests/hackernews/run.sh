#!/bin/sh
# Builds and runs the Hacker News tests. No device and no PlatformIO:
# HackerNewsCore and HackerNewsSaved are freestanding C++17.
#
#   host-tests/hackernews/run.sh
#
# Nothing but the standard library is on the include path, which is the point.
# If the decoders, the readability gate or the saved index ever reach for
# HttpDownloader, ArduinoJson or the SD card, this build fails loudly instead of
# the logic quietly becoming device-only and therefore untested.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name. Two worktrees sharing
# one build dir means one tree can run -- and pass -- a binary the other
# built, which is a green suite whose source is not even present.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-hackernews-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  ../../src/apps_local/hackernews/HackerNewsCore.cpp \
  test_hackernews.cpp -o "$BUILD_DIR/test_hackernews"
"$BUILD_DIR/test_hackernews"

# The saved library. Its own binary because it is the piece that has to keep
# working when everything around it does not: no network, no service, and a
# reader should still open the device and find their articles.
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  ../../src/apps_local/hackernews/HackerNewsSaved.cpp \
  test_saved.cpp -o "$BUILD_DIR/test_saved"
"$BUILD_DIR/test_saved"

# The list's rows. Its own binary for the reason the others are: it must build
# with nothing but the standard library, and the bug it exists to prevent -- one
# shelf's titles over another shelf's indices -- lives entirely in plain data.
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  ../../src/apps_local/hackernews/HackerNewsRows.cpp \
  test_rows.cpp -o "$BUILD_DIR/test_rows"
"$BUILD_DIR/test_rows"
