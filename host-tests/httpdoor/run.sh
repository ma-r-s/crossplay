#!/bin/bash
# Every request HttpDownloader makes passes through one door: openRequest()
# (the radio check: a TLS connect with WiFi never started panics the device),
# applyCommonClientSetup() (the CrossPlay UA and the device report headers),
# closeRequest() (lastStatus() and the report's delivery) and, where a caller
# can cancel, AbortPoll (a Back that works before the first body byte).
#
# Until the 2026-11-17 sync only runGetSecure had them, and upstream's
# fetchUrl(FetchOptions), postForm and postToFile walked past all of it. This
# reads src/network/HttpDownloader.cpp and fails any function that opens a
# client or calls fetchResumable without them, so the next upstream path that
# builds its own request fails here instead of on a device.
#
#   host-tests/httpdoor/run.sh
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
python3 "$HERE/door.py" "$ROOT/src/network/HttpDownloader.cpp"
