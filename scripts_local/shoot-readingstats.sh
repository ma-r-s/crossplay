#!/bin/bash
# Photograph READING STATS from a seeded history.
#
#   scripts_local/shoot-readingstats.sh <profile> <theme> <out-prefix> [extra-shots]
#
#   profile  real | few | empty   (tools_local/readingstats/seed_stats.cpp)
#   theme    0 classic, 1 lyra, 2 lyra extended, 3 roundedraff, 4 covergrid
#   out      writes <out-prefix>-main.png, and -books.png / -books2.png when
#            the history has a door to the full list
#
# The history is written by a tool compiled against ReadingStatsCore, so the
# card holds what the reader itself would have written, dated from today in
# UTC (the simulator's clock zone). The app is opened with
# CROSSPLAY_AUTOSTART rather than tapped to through the shelf, so a new row on
# the shelf cannot move the shot.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$(pwd)"
PROFILE="${1:?profile: real|few|empty}"
THEME="${2:?theme 0..4}"
OUT="${3:?out prefix}"

CARD="$REPO/fs_agent/.crosspoint"
mkdir -p "$CARD"
SEEDER="${TMPDIR:-/tmp}/readingstats-seed-$(echo "$REPO" | cksum | cut -d' ' -f1)"
c++ -std=c++17 -O1 -Isrc/apps_local/readingstats -Ilib/Utf8 \
  tools_local/readingstats/seed_stats.cpp src/apps_local/readingstats/ReadingStatsCore.cpp lib/Utf8/Utf8.cpp \
  -o "$SEEDER"
"$SEEDER" "$CARD" "$(date -u +%F)" "$PROFILE"

# A fresh settings file per shot, so one theme's state cannot leak into the
# next; the shelf state too, so Back lands somewhere predictable.
rm -f "$CARD/settings.json" "$CARD/shelf.cfg"
printf '{"uiTheme": %d}\n' "$THEME" > "$CARD/settings.json"

# The door is the foot of the body: 784 - 52/2 on the 480x800 panel.
INPUT="3500:TAP:240,758;6500:DOWN;9500:QUIT"
SHOTS="3000:$OUT-main.bmp;6000:$OUT-books.bmp;9000:$OUT-books2.bmp"
CROSSPLAY_AUTOSTART="READING STATS" ./scripts_local/sim-shot.sh "$INPUT" "$SHOTS" "$(dirname "$OUT")"
