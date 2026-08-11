#!/bin/bash
# Photograph one shell treatment end to end. The look is a compile-time constant
# in ToyBattleActivity.h, so this rewrites it, rebuilds and drives the app.
#
#   scripts_local/shoot-looks.sh FrontDoor '240,609' '240,749'
#     $1 look   $2 tap for PLAY   $3 tap for HOW TO PLAY
set -euo pipefail
cd "$(dirname "$0")/.."
LOOK="${1:?look}"
PLAY="${2:?play tap}"
HOWTO="${3:?howto tap}"
NEXT="${4:-355,758}"
perl -0pi -e "s/kLook = tbui::Look::\w+;/kLook = tbui::Look::$LOOK;/" src/apps_local/toybattle/ToyBattleActivity.h
L=$(echo "$LOOK" | tr '[:upper:]' '[:lower:]')
./scripts_local/sim-shot.sh \
  "2000:TAP:120,635;3500:TAP:262,687;5000:TAP:240,480;6400:TAP:$PLAY;7800:TAP:240,140;9200:BACK;9600:BACK;10000:TAP:$HOWTO;11400:TAP:$NEXT;13200:QUIT" \
  "6200:./qa-artifacts/$L-menu.bmp;7600:./qa-artifacts/$L-setup.bmp;9000:./qa-artifacts/$L-maps.bmp;11200:./qa-artifacts/$L-howto1.bmp;12600:./qa-artifacts/$L-howto2.bmp" \
  2>&1 | grep -E "FAILED|error:|\.png" | sed 's/^/  /'
