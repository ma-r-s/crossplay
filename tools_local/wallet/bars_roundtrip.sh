#!/bin/sh
# Draws every Cards barcode encoder's output and reads it back with zbar, which
# host-tests/wallet cannot do because it needs zbar installed.
#
#   apt install zbar-tools      # or: brew install zbar
#   ./tools_local/wallet/bars_roundtrip.sh
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/cards-bars-roundtrip"
mkdir -p "$OUT"
SRC="$REPO/src/apps_local/wallet"
"${CXX:-c++}" -std=c++17 -O1 -I"$SRC" "$SRC/WalletBars.cpp" "$REPO/tools_local/wallet/bars_dump.cpp" -o "$OUT/bars_dump"
"$OUT/bars_dump" > "$OUT/cases.tsv"
python3 "$REPO/tools_local/wallet/bars_zbar.py" "$OUT/cases.tsv"
