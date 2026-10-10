#!/bin/sh
# Builds and runs the card-file tests. WalletCore is freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-wallet-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/wallet
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_wallet.cpp $SRC/WalletCore.cpp $SRC/WalletBars.cpp -o "$BUILD_DIR/test_wallet"
"$BUILD_DIR/test_wallet"
