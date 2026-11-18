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
# Example: open the Apps hub by touch and photograph it:
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

# Scripted runs open no visible window: a shown one is brought to the front by
# macOS, which pulled Mario off whatever he was looking at every time an agent
# took a screenshot (2026-10-10). The frame still renders and is captured at
# 2x. CROSSPOINT_SIM_SHOW=1 shows the window when you want to watch a run.
if [ -z "${CROSSPOINT_SIM_SHOW:-}" ]; then
  export CROSSPOINT_SIM_HIDDEN=1 SDL_MAC_BACKGROUND_APP=1
fi

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
# Every header in this run's shots: is its back control on its title's line?
# Measured in the picture because the case that prompted it was two pieces of
# code each placing half of one row (Study's count on its own middle of the
# band, the theme's arrow on the row below the battery strip), 12.5px apart
# and photographed by two agents the day it was found without either seeing
# it. tools_local/header_row_check.py says how it reads a header.
HEADER_ROW_FAILED=0
shots=()
IFS=';' read -ra shot_entries <<< "$SHOT_SCRIPT"
for entry in "${shot_entries[@]}"; do
  shot="${entry#*:}"
  shot="${shot%.bmp}.png"
  [ -f "$shot" ] && shots+=("$shot")
done
if [ "${#shots[@]}" -gt 0 ] &&
   ! uv run --quiet --with pillow python "$REPO/tools_local/header_row_check.py" "${shots[@]}"; then
  echo "FAILED: a header's title is off its back control's line (HEADER ROW above)."
  echo "        Words drawn inside the theme's band go on HeaderLayoutRecord::last"
  echo "        (src/components/HeaderLayoutRecord.h), never on a middle worked out by hand."
  HEADER_ROW_FAILED=1
fi

# A missing glyph is ALWAYS a layout bug, and it is the one bug this panel
# cannot show you. The SDK truncates overflowing text with U+2026, and the
# Toybox cuts DO NOT ALL CARRY IT: toybox_10 has the ellipsis, 14/20/30/44/64 do
# not. A glyph the face lacks draws as nothing at all, so at every cut above 10
# an overflowing line does not arrive as a clipped word or a box character --
# the sentence just stops, at a plausible-looking place, and the screenshot
# looks fine. FOREHEAD shipped two of these: a settings row reading "RE" and a
# first run whose only sentence ran off the side.
#
# The corollary is this gate's coverage: an overflow at the 10px cut draws a
# real "..." and logs nothing, so the gate cannot see it -- and does not need
# to, because a human can. Above 10 the gate is the only thing that can.
#
# The renderer logs it every single time, on every screen, for computed boxes as
# well as fixed ones, which makes this a better gate than any table of strings
# could be. It was already in the trace above and exited 0, which is the failure
# mode docs warn about in its own right: a printed error that nothing acts on
# reads exactly like a note.
if grep -q "No glyph for codepoint" "$LOG"; then
  echo
  # WHICH codepoint decides what this means, and the two causes want opposite
  # fixes. 8230 is U+2026, the ellipsis the SDK truncates with -- that is the
  # overflow case. Anything else is a character reaching drawText that the face
  # cannot draw at all, which is usually unsanitised text from outside: a
  # newline (10) out of a network feed, or an accented letter out of a filename.
  # Pointing the second case at measure.py sends the reader hunting a width
  # problem that does not exist.
  if grep -q "No glyph for codepoint 8230" "$LOG"; then
    echo "FAILED: text OVERFLOWED and was truncated with U+2026, which this face"
    echo "        does not carry, so the sentence just stops and looks deliberate."
    grep -n "No glyph for codepoint 8230" "$LOG" | sed "s/^/  /" | head -6
    echo "  Measure it with tools_local/forehead/measure.py <cut> <text>"
    echo "  then shorten the string, drop a cut, or widen the box."
  fi
  if grep "No glyph for codepoint" "$LOG" | grep -qv "codepoint 8230"; then
    echo "FAILED: a character reached drawText that this face cannot draw at all."
    echo "        NOT an overflow. measure.py will not explain this one."
    grep -n "No glyph for codepoint" "$LOG" | grep -v "codepoint 8230" | sed "s/^/  /" | head -6
    echo "  Codepoint 10 is a newline; anything above 126 is outside Jersey ASCII."
    echo "  Both usually mean text from OUTSIDE the firmware -- a network feed, a"
    echo "  filename, an SD card -- reaching the rasteriser unsanitised. Sanitise"
    echo "  it at the boundary it came in through, not at the draw call."
  fi
  exit 1
fi
exit "$HEADER_ROW_FAILED"
