#!/bin/sh
# Builds and runs the Connections rules tests. No device and no PlatformIO:
# ConnectionsCore is freestanding C++17.
#
#   host-tests/connections/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-connections-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/connections
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror $SRC/ConnectionsCore.cpp \
  test_connections.cpp -o "$BUILD_DIR/test_connections"
"$BUILD_DIR/test_connections"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror $SRC/ConnectionsCore.cpp $SRC/ConnectionsPack.cpp $SRC/ConnectionsResults.cpp \
  $SRC/ConnectionsImport.cpp test_pack.cpp -o "$BUILD_DIR/test_pack"
"$BUILD_DIR/test_pack"
