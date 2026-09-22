#!/bin/sh
# The Live service, driven through its own HTTP surface on a laptop.
#
#   host-tests/fridge/run.sh
#
# Nothing here touches a device, a radio or a panel, and that is the point: the
# three things this covers are all invisible from in front of a reader.
#
# The SHARED HISTORY is state several phones write to at once, and the failure
# that matters is not a 500 -- it is the picked entry being deleted and the
# reader left pointing at a file that is gone, which nobody discovers until the
# fridge in another country shows the wrong thing or nothing at all.
#
# The CLOCK-TIME SCHEDULE is arithmetic no device performs. The reader is
# handed a number of seconds and knows nothing about 07:00, so "does a daily
# alarm work" is a question only this side can answer.
#
# The PENDING CADENCE is a window: between somebody changing the schedule and
# the reader next waking, the two disagree. It has to be ABSENT rather than
# equal when nothing is pending, because the reader draws the line only when
# the key is there, and it has to be the SAME SENTENCE on both surfaces.
set -e
cd "$(dirname "$0")"
FRIDGE_DATA="${TMPDIR:-/tmp}/fridge-host-test-$$" \
  uv run --quiet --with fastapi --with httpx python test_fridge.py
