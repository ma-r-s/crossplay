#!/bin/bash
# The browser build's canned HttpDownloader must still match the real one's
# declarations: nothing else in the gate compiles that stub (only the emulator
# workflow does, after a merge), and the 2026-09-11 sync went green here while
# the first emulator rebuild after it died on exactly that mismatch.
#
#   host-tests/wasmstub/run.sh
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
python3 "$HERE/parity.py" "$ROOT"
