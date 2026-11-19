#!/bin/sh
# The browser build's stand-ins for system libraries (tools_local/wasm/stubs):
# the ones with logic in them are checked here, on the host, because the
# browser build itself only runs on GitHub (crossplay-emulator.yml).
#
#   host-tests/wasmstubs/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wasmstubs-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 test_sha.cpp -o "$BUILD_DIR/test_sha"
"$BUILD_DIR/test_sha"
