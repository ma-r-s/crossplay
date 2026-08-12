#!/bin/bash
# Regenerate the Toy Battle board shot the site card uses.
#
#   scripts_local/shoot-board.sh [destination.png]
#     default destination: site/assets/shots/toybattle.png
#
# This exists because every image on the site was captured by hand once and its
# recipe thrown away, so not one of them could be reproduced. The board shot
# nearly cost that on 2026-08-12: badges arrived on Gate and Nullify bases, and
# the committed image survived only because it happens to be Castle Field, which
# carries neither. Had it been La Croisette the site would have shown a board the
# firmware no longer draws, with no way back to the recipe.
#
# THE WHOLE DIFFICULTY IS THE STARTING STATE, and it is why no recipe existed.
# The simulator resumes wherever it was left, and the shelf remembers which item
# was last opened -- ShelfFolderActivity derives its PAGE from that selection
# rather than storing one. So a tap sequence that works on one machine lands on
# a different game on another, which is exactly what happened on the first
# attempt at this file (it opened Minesweeper's how-to). Nothing below is
# navigation from wherever we happen to be; the state is WRITTEN first:
#
#   shelf.cfg      "<last folder> <last item per folder>" -- 0 14 0 selects the
#                  Games folder and Toy Battle within it, which is item 14 of 15
#                  and therefore the last row of the folder's second page.
#   toybattle.sav  deleted. Options live INSIDE the save, so removing it puts
#                  the map back to Castle Field, the opponent to GENERAL, bases
#                  ON and YOU MOVE to FIRST -- and the menu back to three rows,
#                  since CONTINUE only exists when there is something to
#                  continue. Every tap below is measured against that.
#
# WHAT IS STILL NOT DETERMINISTIC: which three troops are dealt. The seed is
# millis(). The card's alt text says "a rack holds three numbered troops" rather
# than which ones, so that is fine here -- but if you are diffing against the
# committed PNG, expect the rack and the two count rows to differ and nothing
# else to.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$(pwd)"
DEST="${1:-$REPO/site/assets/shots/toybattle.png}"

# The agent's own card, the one sim-shot.sh drives. Never Mario's fs_mario.
CARD="$REPO/fs_agent/.crosspoint"
mkdir -p "$CARD"
rm -f "$CARD/toybattle.sav"
printf '0 14 0' > "$CARD/shelf.cfg"

# HOME anchors to the reader's home screen whatever was on screen before.
#   240,635  Games        on Home
#   240,538  TOY BATTLE   last row of the folder page the selection lands on
#   240,614  PLAY         first menu row; it is NOT 609, which is where PLAY
#                         sits when a save adds a CONTINUE row above it
#   240,746  START        on the setup screen
./scripts_local/sim-shot.sh \
  "2500:HOME;4000:TAP:240,635;5500:TAP:240,538;7500:TAP:240,614;9000:TAP:240,746;12500:QUIT" \
  "6800:./qa-artifacts/site-menu.bmp;11700:./qa-artifacts/site-board.bmp" \
  2>&1 | grep -E "FAILED|error:|\.png" | sed 's/^/  /'

[ -f "$REPO/qa-artifacts/site-board.png" ] || { echo "no shot produced"; exit 1; }
cp "$REPO/qa-artifacts/site-board.png" "$DEST"
echo "wrote $DEST"
echo
echo "qa-artifacts/site-menu.png is the Toy Battle menu on the way through. It is"
echo "captured on purpose: if a tap ever lands somewhere else, that shot says so"
echo "immediately, instead of the board shot quietly being of another game."
echo
echo "Now LOOK at the result, and re-read the card's alt text in site/index.html"
echo "against it. That text called four bases 'squared off' for months while they"
echo "were round and wearing a badge; a fresh shot with stale prose beside it is"
echo "the same defect in a different file."
