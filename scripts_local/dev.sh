#!/bin/bash
# Mario's simulator. Leave this running.
#
#   ./scripts/dev.sh                # the integration tree (firmware-next, xteink)
#   ./scripts/dev.sh battleship     # just what wt/battleship is building
#   ./scripts/dev.sh --list         # what there is to watch
#
# It watches one tree's sources and, whenever they change, rebuilds and restarts
# the simulator so the window in front of you is always that code. If a build
# fails it says so and leaves the previous build running, so you are never left
# staring at a dead window.
#
# **Name a tree and you see only that work.** Every chat now develops in its own
# worktree under wt/, so watching one means a different chat saving a file
# cannot restart your simulator mid-move, and the binary you are holding is one
# feature rather than a mixture of three half-finished ones. With no name you
# get firmware-next, which is everything that has actually been merged.
#
# Your SD card (fs_mario/ at the workspace root, not inside any tree) follows
# you whichever tree you watch, so your saves and settings survive switching.
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

list_trees() {
  echo "trees you can watch:"
  printf "  %-16s %s\n" "(no argument)" "firmware-next -- the integration tree"
  local d
  for d in "$WORKSPACE"/wt/*/; do
    [ -d "$d" ] || continue
    local n branch
    n="$(basename "$d")"
    branch="$(git -C "$d" branch --show-current 2>/dev/null || echo '?')"
    printf "  %-16s %s\n" "$n" "$branch"
  done
}

case "${1:-}" in
  --list | -l)
    list_trees
    exit 0
    ;;
  "") TARGET="$WORKSPACE/firmware-next" ;;
  -*)
    echo "usage: dev.sh [<worktree-name>|--list]" >&2
    exit 2
    ;;
  *)
    TARGET="$WORKSPACE/wt/$1"
    if [ ! -d "$TARGET" ]; then
      echo "error: no worktree named '$1'" >&2
      echo >&2
      list_trees >&2
      echo >&2
      echo "make one with: ./scripts/wt.sh new $1" >&2
      exit 2
    fi
    ;;
esac

# Re-point the library at the tree being watched. Everything derived -- the
# binary, the build log, the per-tree build lock -- moves with it, so watching a
# chat's tree contends only with that chat and with nobody else.
set_repo "$TARGET"

# Deliberately NOT inside the tree: switching which tree you watch must not
# switch which saves and settings you have.
export CROSSPOINT_SIM_SD="$WORKSPACE/fs_mario"
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
echo "watching  $REPO"
echo "branch    $(git -C "$REPO" branch --show-current 2>/dev/null || echo '?')"
echo "SD card   $CROSSPOINT_SIM_SD"
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
