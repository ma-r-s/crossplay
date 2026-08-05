#!/bin/bash
# Regenerate src/apps_local/insider/InsiderArt.h from tools_local/insider_art.txt.
#
#   brew install librsvg          # rsvg-convert, the only external dependency
#   ./tools_local/gen_insider_art.sh
#
# The output is committed, because regenerating needs librsvg and a checkout
# should build without it. Edit insider_art.txt, run this, commit both.
#
# 96px rather than the shelf's 32: a role card is held up to one face for about
# a second and a half, and the silhouette is what carries it in that time.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
cd "$REPO"
uv run --quiet --with pillow python freeink-sdk/libs/assets/Icons/tools/gen_icons.py \
  --manifest tools_local/insider_art.txt \
  --svgdir freeink-sdk/libs/assets/Icons/lucide/icons \
  --sizes 96 \
  --out src/apps_local/insider/InsiderArt.h
echo "wrote src/apps_local/insider/InsiderArt.h"
