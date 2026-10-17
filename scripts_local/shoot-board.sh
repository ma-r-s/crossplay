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
# This script used to write the selection into shelf.cfg as a literal row index
# ("0 14 0" for Toy Battle, item 14 of 15) and tap its way in. kGames has grown
# twice since -- PICROSS and GO -- so row 14 became SUDOKU, and on 2026-09-20
# the recipe photographed a Sudoku board while still calling the file
# toybattle.png. The menu canary below is the only reason anybody noticed.
#
# The index is now never written down. CROSSPLAY_AUTOSTART names the item by
# TITLE and Shelf::findItemByTitle resolves it against the live kGames, so
# reordering the shelf cannot silently point this recipe at another game.
#
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
source "$REPO/scripts_local/lib-sim.sh"
DEST="${1:-$REPO/site/assets/shots/toybattle.png}"

# The agent's own card, the one sim-shot.sh drives. Never Mario's fs_mario.
CARD="$REPO/fs_agent/.crosspoint"
mkdir -p "$CARD"
rm -f "$CARD/toybattle.sav"

# Autostart lands on the Toy Battle menu, so the only taps are inside the app.
#   240,614  PLAY    first menu row; it is NOT 609, which is where PLAY sits
#                    when a save adds a CONTINUE row above it
#   240,746  START   on the setup screen
CROSSPLAY_AUTOSTART="TOY BATTLE" ./scripts_local/sim-shot.sh \
  "3000:TAP:240,614;4500:TAP:240,746;8000:QUIT" \
  "2500:./qa-artifacts/site-menu.bmp;7500:./qa-artifacts/site-board.bmp" \
  2>&1 | grep -E "FAILED|error:|\.png" | sed 's/^/  /'

[ -f "$REPO/qa-artifacts/site-board.png" ] || { echo "no shot produced"; exit 1; }
write_site_shot "$REPO/qa-artifacts/site-board.png" "$DEST"
echo
echo "qa-artifacts/site-menu.png is the Toy Battle menu on the way through. It is"
echo "captured on purpose: if a tap ever lands somewhere else, that shot says so"
echo "immediately, instead of the board shot quietly being of another game."
echo
echo "Now LOOK at the result, and re-read the card's alt text in site/index.html"
echo "against it. That text called four bases 'squared off' for months while they"
echo "were round and wearing a badge; a fresh shot with stale prose beside it is"
echo "the same defect in a different file."
