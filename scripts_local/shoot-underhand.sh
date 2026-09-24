#!/bin/bash
# Regenerate the UNDERHAND shot the site card uses.
#
#   scripts_local/shoot-underhand.sh [destination.png]
#     default destination: site/assets/shots/underhand.png
#
# The starting state is a save written by tools_local/underhand/seed.cpp:
# Organ Harvest on turn 9, holding 1 relic, 3 money, 2 cultists, 4 food, 2
# prisoners and 2 suspicion, dealt from the tool's fixed seed. So the shot is
# the same every time: the first two options each show three ways to pay
# joined by OR, the black one the default, and the third costs nothing. If you
# change the card or the hand, change the alt text in site/index.html.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$(pwd)"
source "$REPO/scripts_local/lib-sim.sh"
DEST="${1:-$REPO/site/assets/shots/underhand.png}"

SEED="$(mktemp -d)/underhand-seed"
c++ -std=c++17 -Isrc/apps_local/underhand -Ilib/JsonParser tools_local/underhand/seed.cpp \
  src/apps_local/underhand/UnderhandCards.cpp src/apps_local/underhand/UnderhandEngine.cpp \
  src/apps_local/underhand/UnderhandSave.cpp src/apps_local/underhand/UnderhandView.cpp \
  lib/JsonParser/StreamingJsonParser.cpp -o "$SEED"

# The agent's own card, the one sim-shot.sh drives. Never Mario's fs_mario.
CARD="$REPO/fs_agent/.crosspoint"
mkdir -p "$CARD"
"$SEED" "$CARD/underhand.sav" card=23 held=1,3,2,4,2,2 turn=9 >/dev/null

CROSSPLAY_AUTOSTART=UNDERHAND ./scripts_local/sim-shot.sh \
  "4000:QUIT" \
  "3500:./qa-artifacts/site-underhand.bmp" \
  2>&1 | grep -E "FAILED|error:|\.png" | sed 's/^/  /'
rm -f "$CARD/underhand.sav"

[ -f "$REPO/qa-artifacts/site-underhand.png" ] || { echo "no shot produced"; exit 1; }
write_site_shot "$REPO/qa-artifacts/site-underhand.png" "$DEST"
