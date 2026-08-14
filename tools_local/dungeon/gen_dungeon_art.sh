#!/bin/bash
# Regenerate src/apps_local/dungeon/DungeonArt.h from tools_local/dungeon/dungeon_art.txt.
#
#   brew install librsvg          # rsvg-convert, the only external dependency
#   ./tools_local/dungeon/gen_dungeon_art.sh
#
# The output is committed, because regenerating needs librsvg and a checkout
# should build without it. Edit dungeon_art.txt, run this, commit both.
#
# 36px rather than the shelf's 32: these sit in a 52px board cell rather than a
# 62px list row, and a cell also has to hold its own outline.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"
cd "$REPO"
uv run --quiet --with pillow python freeink-sdk/libs/assets/Icons/tools/gen_icons.py \
  --manifest tools_local/dungeon/dungeon_art.txt \
  --svgdir freeink-sdk/libs/assets/Icons/lucide/icons \
  --sizes 32,36 \
  --out src/apps_local/dungeon/DungeonArt.h
echo "wrote src/apps_local/dungeon/DungeonArt.h"
