#!/bin/sh
# Every Live network call takes the radio first.
#
#   host-tests/liveradio/run.sh
#
# This is a SOURCE check, not a behaviour one, because the behaviour needs a
# device with its radio down and there is no host equivalent. What it catches
# is exactly what shipped in v1.13.10: pairStart, pairJoin, pairPoll,
# listSenders and revokeSender went straight to the transport, so pressing the
# tile on a reader whose radio was off made a TLS request with no network under
# it and the device panicked on a null semaphore. checkNow was the only one
# that joined, which is why the bug hid: the feature worked perfectly for
# anybody whose reader happened to be online already.
#
# A transport does not bring a radio up. Any new live:: call that talks to the
# service needs a live::engine::RadioLease above it, and this fails if one
# appears without.
set -e
cd "$(dirname "$0")/../.."
SRC=src/apps_local/wallpapers/WallpapersActivity.cpp
fails=0
checks=0

# Every function in LiveBridge.h that reaches the service. Read from the header
# rather than listed here, so a sixth call cannot be added without this seeing
# it (a list written into a test stops matching the day somebody adds one).
calls=$(grep -oE '^[a-z]+ [a-zA-Z]+\(' src/apps_local/live/LiveBridge.h \
        | sed 's/^[a-z]* //; s/($//; s/(//' | sort -u)

for fn in $calls; do
  grep -n "live::$fn(" "$SRC" 2>/dev/null | while IFS=: read -r line _; do
    start=$((line - 12)); [ "$start" -lt 1 ] && start=1
    if sed -n "${start},${line}p" "$SRC" | grep -q "RadioLease"; then
      :
    else
      echo "FAIL $SRC:$line  live::$fn is called with no RadioLease above it"
      echo "     a Live request with the radio down panics the device"
      exit 1
    fi
  done || fails=$((fails + 1))
  checks=$((checks + 1))
done

echo "$checks checks, $fails failed"
[ "$fails" -eq 0 ]
