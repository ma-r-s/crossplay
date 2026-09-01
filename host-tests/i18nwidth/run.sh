#!/bin/bash
# Every string drawn on a path that cannot wrap must fit the panel.
#
# `renderer.drawCenteredText` draws ONE line and does not wrap. Nothing gated
# i18n widths -- host-tests/brand reads the translation files for brand names and
# key parity and never measures anything -- so a string one word too long runs
# off the panel with every suite green. On 2026-08-31 that accounted for three
# near-misses in a single night, and one string was already over.
#
# Counting characters does not answer it: the face is proportional, so "iiiii"
# and "WWWWW" differ by a factor of three. A character budget shipped AS the gate
# would be worse than no gate, because it licenses the next long string with
# confidence. This reads real advance widths out of the generated font headers.
#
# The key list is DERIVED FROM CALL SITES rather than declared, so it cannot go
# stale the way a hand-maintained list would, and it measures only the strings
# that genuinely cannot wrap.
#
#   host-tests/i18nwidth/run.sh
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$HERE/measure_screens.py" "$@"
