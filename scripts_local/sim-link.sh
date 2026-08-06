#!/bin/bash
# Run TWO X4 Pro simulators at once, so local multiplayer can be tested the only
# way that means anything: two devices in the same room finding each other.
#
#   ./scripts/sim-link.sh                                        # two windows to play with
#   ./scripts/sim-link.sh '<input-script>' '<shots>' [out-dir]   # scripted, headless
#
# With no arguments it opens two windows side by side and waits. Play one
# against the other with the mouse; close either window (or Ctrl-C) to stop
# both. That is the point of the whole exercise, so it is the default.
#
# Both devices run the same input script by default, which is the usual case:
# they are doing the same thing to meet each other. Set SIM_LINK_INPUT_B and
# SIM_LINK_SHOTS_B for an asymmetric test, which is how you check what one
# device shows when the other one walks away.
#
# Script syntax is sim-shot.sh's. Screenshot paths get -a and -b inserted before
# the extension, so '9000:./qa-artifacts/link.bmp' writes link-a.bmp and
# link-b.bmp.
#
# Each device gets its own SD card (fs_link_a, fs_link_b) so their saves and
# settings cannot interfere -- and so neither disturbs Mario's dev.sh game in
# fs_mario or the single-device runs in fs_agent. SIM_LINK_FRESH=1 wipes both
# first, which you want whenever a stale save would change where the taps land.
#
# Example -- both devices open Chess and look for each other:
#   ./scripts/sim-link.sh \
#     '1800:TAP:120,635;3600:TAP:120,411;5400:TAP:120,144;20000:QUIT' \
#     '12000:./qa-artifacts/chess.bmp'
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib-sim.sh"
require_same_tree
export CROSSPOINT_SIM_SD="$REPO/fs_link_a"

INPUT_A="${1:-}"
SHOTS_A="${2:-}"
OUT_DIR="${3:-$REPO/qa-artifacts}"
INPUT_B="${SIM_LINK_INPUT_B:-$INPUT_A}"
SHOTS_B="${SIM_LINK_SHOTS_B:-$SHOTS_A}"

SD_A="$REPO/fs_link_a"
SD_B="$REPO/fs_link_b"

# Inserts a tag before each screenshot path's extension, so the two devices
# cannot race to write the same file. This is the whole reason sim-shot.sh could
# not simply be run twice.
tag_shots() {
  local script="$1" tag="$2" out="" entry when path base ext
  [ -z "$script" ] && return 0
  local IFS=';'
  for entry in $script; do
    [ -z "$entry" ] && continue
    when="${entry%%:*}"
    path="${entry#*:}"
    base="${path%.*}"
    ext="${path##*.}"
    out="${out:+$out;}${when}:${base}${tag}.${ext}"
  done
  printf '%s' "$out"
}

if [ "${SIM_LINK_FRESH:-0}" = "1" ]; then
  rm -rf "$SD_A" "$SD_B"
fi
seed_fs "$SD_A"
seed_fs "$SD_B"
build
mkdir -p "$OUT_DIR"

cd "$REPO"
LOG_A="$OUT_DIR/sim-link-a.log"
LOG_B="$OUT_DIR/sim-link-b.log"

# Launched together rather than one after the other: discovery is the thing
# under test, and a device that starts looking ten seconds late is not the same
# test at all.
CROSSPOINT_SIM_SD="$SD_A" \
CROSSPOINT_SIM_INPUT_SCRIPT="$INPUT_A" \
CROSSPOINT_SIM_SCREENSHOTS="$(tag_shots "$SHOTS_A" -a)" \
  "$BIN" > "$LOG_A" 2>&1 &
PID_A=$!
CROSSPOINT_SIM_SD="$SD_B" \
CROSSPOINT_SIM_INPUT_SCRIPT="$INPUT_B" \
CROSSPOINT_SIM_SCREENSHOTS="$(tag_shots "$SHOTS_B" -b)" \
  "$BIN" > "$LOG_B" 2>&1 &
PID_B=$!

if [ -z "$INPUT_A" ]; then
  # Interactive. Closing one window ends the session rather than leaving an
  # orphan looking for a partner that is never coming back.
  echo "two simulators up. Close either window or press Ctrl-C to stop both."
  echo "  chess multiplayer: gear -> OPPONENT until it says NEARBY -> back to board"
  trap 'kill "$PID_A" "$PID_B" 2>/dev/null' EXIT INT TERM
  # Not `wait -n`: macOS ships bash 3.2, where that does not exist.
  while kill -0 "$PID_A" 2>/dev/null && kill -0 "$PID_B" 2>/dev/null; do sleep 1; done
  exit 0
fi

wait "$PID_A" || true
wait "$PID_B" || true

for tag in a b; do
  log="$OUT_DIR/sim-link-$tag.log"
  echo "device $tag:"
  # Wider than sim-shot.sh's default by design: on a two-device run the thing
  # you almost always want to see is whether they found each other, and that is
  # a [LINK] line rather than an activity change.
  grep -E "${SIM_LOG_GREP:-Entering activity|\[LINK\]|\[ERR\]}" "$log" | sed 's/^/  /' || echo "  (none)"
done
echo "screenshots:"
bmp_to_png "$OUT_DIR"
echo "logs: $LOG_A $LOG_B"
