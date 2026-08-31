#!/bin/bash
# Regenerate the TRIVIA shot the site card uses.
#
#   scripts_local/shoot-trivia.sh [destination.png]
#     default destination: site/assets/shots/trivia.png
#
# The shot this replaced was made by hand and was wrong in a way nobody could
# have fixed without remaking it: a 2x render cropped to its top-left quadrant,
# so TRIVIA, the clue and the NEXT button were all cut off mid-word, and the
# picture disagreed with its own alt text. sim-shot.sh produces the frame the
# panel actually paints, at panel size, which is the whole reason to use it.
#
# THE STARTING STATE IS THE WHOLE DIFFICULTY, and here it is the pack:
#
#   trivia/pack.dat   ONE clue, written by seed_shot_pack.py -- the clue the
#                     alt text describes. The shipped pack is fifty thousand
#                     built from a dataset this repo does not carry, and which
#                     clue it deals depends on a scheduler; a one-clue pack
#                     makes the picture reproducible, which is the property
#                     that lets the alt text be checked against it.
#   shelf.cfg         "0 17 0" selects the Games folder and TRIVIA within it.
#                     Unused while AUTOSTART is set, written so the card is in
#                     a sane state if the shot is ever taken by hand.
#   CROSSPLAY_AUTOSTART=TRIVIA  opens the app rather than the shelf.
#
# Then two taps: QUIZMASTER on the front door, and REVEAL at the foot of the
# question. The shot is taken with the answer showing, because that is the
# state worth a picture -- an unrevealed clue is a wall of text with a button
# under it, and it is also the state the alt text describes.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$(pwd)"
DEST="${1:-$REPO/site/assets/shots/trivia.png}"

# The agent's own card, the one sim-shot.sh drives. Never Mario's fs_mario.
CARD="$REPO/fs_agent/.crosspoint"
mkdir -p "$CARD"
python3 tools_local/trivia/seed_shot_pack.py "$REPO/fs_agent" >/dev/null
printf '0 17 0' > "$CARD/shelf.cfg"

# 240,117 is the middle of the first list row (QUIZMASTER); 240,750 is the
# middle of the REVEAL button, which drawAction() lays 16px under the footer
# rule at 64px tall. Both were read off a capture rather than computed: the
# first attempt tapped 160 and 715, which are the row boundary and the 35px of
# margin above the button, and the shot came back showing the front door.
CROSSPLAY_AUTOSTART=TRIVIA ./scripts_local/sim-shot.sh \
  "2500:TAP:240,117;5000:TAP:240,750;9000:QUIT" \
  "7000:./qa-artifacts/site-trivia.bmp" \
  2>&1 | grep -E "FAILED|error:|\.png" | sed 's/^/  /'

[ -f "$REPO/qa-artifacts/site-trivia.png" ] || { echo "no shot produced"; exit 1; }
cp "$REPO/qa-artifacts/site-trivia.png" "$DEST"
echo "wrote $DEST"
