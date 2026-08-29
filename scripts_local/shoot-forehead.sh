#!/bin/bash
# Regenerate the FOREHEAD round shot the site card uses.
#
#   scripts_local/shoot-forehead.sh [destination.png]
#     default destination: site/assets/shots/forehead.png
#
# Written at the same time as the shot, because shoot-board.sh records that
# every earlier image on the site was captured by hand once with its recipe
# thrown away, and not one of them can be reproduced today.
#
# THE WHOLE DIFFICULTY IS THE STARTING STATE, and here it is the clock as well
# as the shelf. Three things are written before anything is tapped:
#
#   shelf.cfg       "0 16 0" selects the Games folder and FOREHEAD within it.
#                   The folder derives its PAGE from that selection rather than
#                   storing one, so this also decides which page opens.
#   forehead.sav    written by seed_save.py, which pins the category to ANIMALS
#                   and the round to SIXTY seconds. Without it the round length
#                   is whatever the last person left, and the bar in the shot is
#                   then at a fill nobody can reproduce -- which is exactly how
#                   an earlier attempt at this shot came back showing the
#                   results screen instead of a round.
#   CROSSPLAY_AUTOSTART  skips the shelf entirely and opens the app.
#
# The shot is taken 33 seconds into a 60-second round, so the timer bar is
# HALF SPENT: a full bar and an empty one both look like a bar that does not
# move, and the whole argument for a bar over a numeral is that you can see it
# going. The alt text in site/index.html says "four segments filled and four
# still outlined", so if you change the timing, change the alt text.
#
# WHAT IS STILL NOT DETERMINISTIC: which words are dealt. The seed is millis().
# The alt text names PORCUPINE, so a regenerated shot will differ there and in
# the score, and nowhere else. Update the alt text or re-run until it matches;
# do not pretend the picture is what it is not.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$(pwd)"
DEST="${1:-$REPO/site/assets/shots/forehead.png}"

# The agent's own card, the one sim-shot.sh drives. Never Mario's fs_mario.
CARD="$REPO/fs_agent/.crosspoint"
mkdir -p "$CARD"
python3 tools_local/forehead/seed_save.py "$REPO/fs_agent" >/dev/null
printf '0 16 0' > "$CARD/shelf.cfg"

# 240,150 is the headline block on the front door, which opens the READY card.
# DOWN starts the round; after that every key press is a card, and the six of
# them put a plausible score on the panel by the time the shot is taken.
CROSSPLAY_AUTOSTART=FOREHEAD ./scripts_local/sim-shot.sh \
  "2200:TAP:240,150;3500:DOWN;6000:UP;10000:UP;16000:UP;24000:UP;32000:UP;40000:QUIT" \
  "35000:./qa-artifacts/site-forehead.bmp" \
  2>&1 | grep -E "FAILED|error:|\.png" | sed 's/^/  /'

[ -f "$REPO/qa-artifacts/site-forehead.png" ] || { echo "no shot produced"; exit 1; }
cp "$REPO/qa-artifacts/site-forehead.png" "$DEST"
echo "wrote $DEST"
