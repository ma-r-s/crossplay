#!/bin/bash
# Mario's simulator. Leave this running.
#
#   ./scripts/dev.sh
#
# It watches the firmware sources and, whenever they change, rebuilds and
# restarts the simulator so the window in front of you is always the current
# code. If a build fails it says so and leaves the previous build running, so
# you are never left staring at a dead window.
#
# It keeps its own SD card (fs_mario/) separate from the one agent test runs use
# (fs_agent/), so scripted taps, screenshot runs and factory-reset settings never
# disturb your game in progress, and your settings never skew a test.
#
# Ctrl-C stops the watcher and the simulator together. Closing just the window
# reopens it; the watcher notices within ~2s.
#
# If the UI ever appears frozen, check qa-artifacts/mario-sim.log before assuming
# a crash. Some upstream activities do blocking network I/O on the main loop:
# Settings > Fonts downloads multi-megabyte CJK fonts synchronously and pins the
# loop for 40s at a time with no repaint and no input. The log says so plainly
# ("New max loop duration: 39864 ms").
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib-sim.sh"

export CROSSPOINT_SIM_SD="$REPO/fs_mario"
seed_fs

SIM_PID=""

stop_sim() {
  [ -z "$SIM_PID" ] && return
  kill "$SIM_PID" 2>/dev/null
  # Wait for it to actually go, so the next launch never overlaps with the old
  # window still on screen.
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    kill -0 "$SIM_PID" 2>/dev/null || break
    sleep 0.2
  done
  kill -9 "$SIM_PID" 2>/dev/null
  SIM_PID=""
}

cleanup() {
  echo
  echo "stopping"
  stop_sim
  exit 0
}
trap cleanup INT TERM

# Hash of everything a human can change that affects the binary.
#
# The generated files MUST be excluded. PlatformIO's pre: scripts rewrite
# lib/I18n/I18n{Keys.h,Strings.h,Strings.cpp} and src/network/html/**.generated.h
# on every build, so watching them means every build triggers the next one and
# the watcher spins forever rebuilding. (It did exactly that the first time.)
source_stamp() {
  find "$REPO/src" "$REPO/lib" "$REPO/assets_local" -type f \
    \( -name '*.cpp' -o -name '*.h' -o -name '*.c' -o -name '*.ttf' -o -name '*.svg' \) \
    -not -name '*.generated.h' \
    -not -name 'I18nKeys.h' -not -name 'I18nStrings.h' -not -name 'I18nStrings.cpp' \
    -exec stat -f '%m %N' {} + 2>/dev/null | sort | shasum | cut -d' ' -f1
}

restart() {
  stop_sim
  # `exec` matters: without it the subshell is the child we record in SIM_PID and
  # the simulator is ITS child, so killing SIM_PID leaves the window orphaned to
  # init and every rebuild leaks another one. exec replaces the subshell with the
  # binary, so SIM_PID is the simulator itself.
  ( cd "$REPO" && exec "$BIN" >"$REPO/qa-artifacts/mario-sim.log" 2>&1 ) &
  SIM_PID=$!
  echo "  running (pid $SIM_PID)"
}

mkdir -p "$REPO/qa-artifacts"
echo "watching $REPO/src and lib for changes"
echo "your SD card: $CROSSPOINT_SIM_SD"
echo

STAMP=""
while true; do
  NOW="$(source_stamp)"
  if [ "$NOW" != "$STAMP" ]; then
    STAMP="$NOW"
    echo "$(date '+%H:%M:%S') building..."
    if build_locked; then
      restart
    else
      echo "  BUILD FAILED, keeping the previous build running"
      echo "  $(tail -3 "$BUILD_LOG" | tr '\n' ' ')"
    fi
  fi
  # If the window went away (you closed it, or the app died), bring it straight
  # back. Without this the only way to get it back was a source change, which
  # meant asking the agent to touch a file. Stop the whole thing with Ctrl-C.
  if [ -n "$SIM_PID" ] && ! kill -0 "$SIM_PID" 2>/dev/null; then
    echo "$(date '+%H:%M:%S') window closed, reopening"
    SIM_PID=""
    restart
  fi
  sleep 2
done
