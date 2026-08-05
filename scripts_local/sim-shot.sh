#!/bin/bash
# Drive the X4 Pro simulator with a scripted input sequence and capture
# screenshots. Headless: needs no desktop-control permissions, so it works as a
# regression check in CI or from an agent.
#
#   ./scripts/sim-shot.sh '<input-script>' '<screenshot-script>' [out-dir]
#
# Input actions are '<ms>:<action>' joined by ';':
#   BACK ENTER LEFT RIGHT UP DOWN POWER SLEEP HOME QUIT   (append :<hold-ms>)
#   TAP:<x>,<y>[,<hold-ms>]
#   SWIPE:<x1>,<y1>,<x2>,<y2>[,<duration-ms>]
# Coordinates are logical pixels; the X4 Pro renders 480x800 portrait.
#
# Screenshots are '<ms>:<path>' joined by ';'. BMPs are auto-converted to PNG.
#
# Example — open the Apps hub by touch and photograph it:
#   ./scripts/sim-shot.sh '1800:TAP:120,635;3600:QUIT' '3000:./qa-artifacts/apps.bmp'
set -euo pipefail
# Agent test runs get their own SD card, so scripted taps and reset settings
# never touch the game Mario has in progress in dev.sh.
export CROSSPOINT_SIM_SD="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)/fs_agent"
source "$(dirname "${BASH_SOURCE[0]}")/lib-sim.sh"

INPUT_SCRIPT="${1:?usage: sim-shot.sh '<input-script>' '<screenshot-script>' [out-dir]}"
SHOT_SCRIPT="${2:-}"
OUT_DIR="${3:-$REPO/qa-artifacts}"

seed_fs
build
mkdir -p "$OUT_DIR"

cd "$REPO"
LOG="$OUT_DIR/sim.log"
CROSSPOINT_SIM_INPUT_SCRIPT="$INPUT_SCRIPT" \
CROSSPOINT_SIM_SCREENSHOTS="$SHOT_SCRIPT" \
  "$BIN" > "$LOG" 2>&1 || true

# The default trace is deliberately terse, but it used to hide LOG_INF and
# LOG_DBG entirely, which made a debug probe added mid-session look like the
# code never ran. SIM_LOG_GREP widens it; SIM_LOG_GREP=. shows everything.
echo "activity trace:"
grep -E "${SIM_LOG_GREP:-Entering activity|\[ERR\]}" "$LOG" | sed 's/^/  /' || echo "  (none)"
echo "screenshots:"
bmp_to_png "$OUT_DIR"
echo "full log: $LOG"
