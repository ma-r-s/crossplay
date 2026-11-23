#!/bin/bash
# Regenerate the TRAIN TRACKS shot the site card uses.
#
#   scripts_local/shoot-traintracks.sh [destination.png]
#     default destination: site/assets/shots/traintracks.png
#
# The starting state is a save written by tools_local/traintracks/
# seed_shot_save.cpp from the game's own headers: a Medium puzzle with the
# route laid from the start gate part way, a few crosses, and onBoard set, so
# CROSSPLAY_AUTOSTART opens straight onto the board with no taps. The alt text
# on the card describes that state; reseeding changes both.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$(pwd)"
source "$REPO/scripts_local/lib-sim.sh"
DEST="${1:-$REPO/site/assets/shots/traintracks.png}"

# The agent's own card, the one sim-shot.sh drives. Never Mario's fs_mario.
CARD="$REPO/fs_agent/.crosspoint"
mkdir -p "$CARD"
SEED_BIN="${TMPDIR:-/tmp}/tt-seed-shot-save"
"${CXX:-c++}" -std=c++17 -O1 -I"$REPO/src/apps_local/traintracks" \
  "$REPO/tools_local/traintracks/seed_shot_save.cpp" -o "$SEED_BIN"
"$SEED_BIN" "$CARD/traintracks.sav"

CROSSPLAY_AUTOSTART="TRAIN TRACKS" ./scripts_local/sim-shot.sh \
  "7000:QUIT" \
  "4000:./qa-artifacts/site-traintracks.bmp" \
  2>&1 | grep -E "FAILED|error:|\.png" | sed 's/^/  /'

[ -f "$REPO/qa-artifacts/site-traintracks.png" ] || { echo "no shot produced"; exit 1; }
write_site_shot "$REPO/qa-artifacts/site-traintracks.png" "$DEST"
