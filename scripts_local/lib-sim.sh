#!/bin/bash
# Shared setup for the X4 Pro desktop simulator.
# Sourced by dev.sh, sim.sh and sim-shot.sh; not meant to be run directly.
set -uo pipefail

# Resolve through the symlink: the live copies at the workspace root are
# symlinks to these, so BASH_SOURCE may be either path.
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
ENV_NAME="simulator_x4_pro"
BIN="$REPO/.pio/build/$ENV_NAME/program"
BUILD_LOG="/tmp/xteink-build.log"
BUILD_LOCK="/tmp/xteink-build.lock"
export PATH="$HOME/.local/bin:$PATH"

# Two simulators run at once: Mario's (dev.sh, fs_mario/) and the agent's
# (sim-shot.sh, fs_agent/). CROSSPOINT_SIM_SD gives each its own SD card, so
# scripted taps and factory-reset settings never disturb a game in progress.
# Callers set it; this is only the fallback for a bare sim.sh.
: "${CROSSPOINT_SIM_SD:=$REPO/fs_}"

# Just the card's shape. CrossMux needed an English settings file seeded here
# because it is a Chinese-first fork whose simulator defaulted to zh_CN, with a
# langSku="cn" field that had to match or the loader reset the language back.
# CrossPoint boots English, so that whole workaround is gone -- verified by
# booting with an empty card.
seed_fs() {
  local root="${1:-$CROSSPOINT_SIM_SD}"
  mkdir -p "$root/books" "$root/.crosspoint"
  # An empty card is not the normal state, and Home looks different in it: with
  # no recent books there are no cover tiles above the menu, so every row index
  # shifts. A shelf-row offset bug shipped because every scripted run here used
  # an empty card and the bug only appears once a book has been opened.
  if [ -z "$(ls -A "$root/books" 2>/dev/null)" ]; then
    echo "note: $root/books is empty -- Home will render its no-books layout."
    echo "      Drop an .epub in there to exercise the recent-books row indices."
  fi
}

# PlatformIO does not tolerate two concurrent builds of the same env, and the
# watcher and the agent both build. Serialise them with an atomic mkdir lock.
build_locked() {
  local waited=0
  while ! mkdir "$BUILD_LOCK" 2>/dev/null; do
    sleep 1
    waited=$((waited + 1))
    if [ "$waited" -gt 300 ]; then
      echo "build lock held for 5 minutes; removing stale $BUILD_LOCK" >&2
      rm -rf "$BUILD_LOCK"
    fi
  done
  ( cd "$REPO" && pio run -e "$ENV_NAME" ) >"$BUILD_LOG" 2>&1
  local status=$?
  rmdir "$BUILD_LOCK" 2>/dev/null
  return $status
}

build() {
  echo "building $ENV_NAME ..."
  build_locked || {
    echo "build failed; see $BUILD_LOG" >&2
    tail -5 "$BUILD_LOG" >&2
    exit 1
  }
}

# Converts every .bmp the simulator wrote into a .png alongside it. The
# simulator emits 32-bit BMPs that macOS `sips` refuses to read.
bmp_to_png() {
  local dir="$1"
  compgen -G "$dir/*.bmp" >/dev/null || return 0
  uv run --quiet --with pillow python - "$dir" <<'PY'
import pathlib, sys
from PIL import Image
for bmp in sorted(pathlib.Path(sys.argv[1]).glob("*.bmp")):
    png = bmp.with_suffix(".png")
    Image.open(bmp).convert("RGB").save(png)
    print(f"  {png.name}")
PY
}
