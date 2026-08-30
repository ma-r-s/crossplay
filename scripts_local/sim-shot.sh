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
source "$(dirname "${BASH_SOURCE[0]}")/lib-sim.sh"
require_same_tree
# Each tree gets its own SD card, so scripted taps and reset settings never
# touch the game Mario has in progress in dev.sh, nor another chat's run.
export CROSSPOINT_SIM_SD="$REPO/fs_agent"

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

# A missing glyph is ALWAYS a layout bug, and it is the one bug this panel
# cannot show you. The SDK truncates overflowing text with U+2026; Jersey has no
# U+2026; a glyph the face does not carry draws as nothing at all. So an
# overflowing line does not arrive as a clipped word or a box character -- the
# sentence just stops, at a plausible-looking place, and the screenshot looks
# fine. FOREHEAD shipped two of these: a settings row reading "RE" and a first
# run whose only sentence ran off the side.
#
# The renderer logs it every single time, on every screen, for computed boxes as
# well as fixed ones, which makes this a better gate than any table of strings
# could be. It was already in the trace above and exited 0, which is the failure
# mode docs warn about in its own right: a printed error that nothing acts on
# reads exactly like a note.
if grep -q "No glyph for codepoint" "$LOG"; then
  echo
  echo "FAILED: text overflowed and was truncated with a glyph the font does not have."
  grep -n "No glyph for codepoint" "$LOG" | sed 's/^/  /' | head -10
  echo "  Measure the offending string: tools_local/forehead/measure.py <cut> '<text>'"
  exit 1
fi
