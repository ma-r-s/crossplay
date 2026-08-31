#!/bin/bash
# Regenerate the WAVELENGTH shot the site card uses.
#
#   scripts_local/shoot-wavelength.sh [destination.png]
#     default destination: site/assets/shots/wavelength.png
#
# Written with the shot rather than after it, because shoot-board.sh records
# that every earlier image on the site was captured by hand once with its recipe
# thrown away, and not one of them can be reproduced today.
#
# THE WHOLE DIFFICULTY IS THE STARTING STATE. The DIAL is the screen worth
# showing: it is the one the table stares at, and it is the only one carrying
# the strip, the marker, the number and both end words at once. Reaching it
# takes five taps, and each has to land after the previous screen has painted.
#
#   CROSSPLAY_AUTOSTART   skips the shelf and opens the app.
#   fs_agent              the agent's own card, never Mario's fs_mario.
#
# WHAT IS NOT DETERMINISTIC: which spectrum is dealt. The RNG is seeded from
# millis(), so the pair in the shot changes every run, and so does the guess the
# taps land on. The alt text in site/index.html names the pair, so if you
# regenerate this, either update the alt text or re-run until it matches. Do not
# describe a picture you have not looked at.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$(pwd)"
DEST="${1:-$REPO/site/assets/shots/wavelength.png}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# A record with some history, so the front door is not the empty state nobody
# sees. Only the front door reads it, but the app loads it at entry either way.
mkdir -p "$REPO/fs_agent/.crosspoint"
uv run --quiet python - "$REPO/fs_agent/.crosspoint/wavelength.sav" <<'PY'
import struct, sys
deck = [0] * 20
for i in range(41):
    deck[i // 32] |= 1 << (i % 32)
blob = struct.pack('<B', 1) + struct.pack('<8H', 41, 118, 9, 12, 7, 8, 5, 34) + struct.pack('<20I', *deck)
open(sys.argv[1], 'wb').write(blob)
PY

# START, I HAVE IT, pick the first card, hold to peek, I HAVE MY CLUE, put it
# down. Then two steps up the strip so the marker is off centre, because a
# marker sitting exactly in the middle reads as a default rather than a choice.
CROSSPLAY_AUTOSTART=WAVELENGTH "$REPO/scripts_local/sim-shot.sh" \
  '2400:TAP:240,560;4000:TAP:240,750;5600:TAP:240,240;7200:TAP:240,687,1500;9600:TAP:240,755;11200:TAP:240,750;12800:TAP:140,200;14000:TAP:140,200;16000:QUIT' \
  '15200:'"$WORK"'/dial.bmp' "$WORK" >/dev/null

[ -f "$WORK/dial.png" ] || { echo "no shot produced; see $WORK" >&2; exit 1; }
mkdir -p "$(dirname "$DEST")"
cp "$WORK/dial.png" "$DEST"
echo "wrote $DEST"
echo "LOOK AT IT before updating the alt text: the spectrum is randomly dealt."
