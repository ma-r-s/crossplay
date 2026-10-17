#!/bin/bash
# Shared setup for the X4 Pro desktop simulator.
# Sourced by dev.sh, sim.sh, sim-shot.sh and sim-link.sh; not run directly.
#
# Everything here is derived from the *tree the script lives in*, never from a
# fixed path. Several trees exist at once -- firmware-next/ integrates, and each
# wt/<name>/ is one chat's isolated worktree -- and every one of them builds,
# runs and screenshots independently. A single hardcoded /tmp path is enough to
# make two of them collide, which is exactly how they used to.
set -uo pipefail

# Resolve through the symlink: the workspace-root scripts/ entries are symlinks
# to these, so BASH_SOURCE may be either path.
SCRIPTS_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
ENV_NAME="simulator_x4_pro"

# The workspace is the directory holding every tree. Found by walking up for the
# marker rather than by counting ".." levels, because firmware-next/ sits one
# level down and wt/<name>/ sits two.
find_workspace() {
  local d="$1"
  while [ "$d" != "/" ]; do
    [ -e "$d/.xteink-workspace" ] && { printf '%s' "$d"; return 0; }
    d="$(dirname "$d")"
  done
  # No marker: assume the old single-tree layout so nothing breaks outright.
  printf '%s' "$(dirname "$1")"
}
WORKSPACE="$(find_workspace "$SCRIPTS_DIR")"

# The 7.3GB PlatformIO object cache is shared by every tree. It is content
# addressed, so a fresh worktree's first build is mostly cache hits instead of a
# cold compile -- and one copy per chat would have been 7.3GB each.
export PLATFORMIO_BUILD_CACHE_DIR="$WORKSPACE/.pio-cache"

# Point this library at a tree. Called once at source time for the tree the
# script lives in; dev.sh calls it again to watch a different one.
set_repo() {
  REPO="$(cd "$1" && pwd)"
  BIN="$REPO/.pio/build/$ENV_NAME/program"
  # Per tree, not per machine. Two trees building at once is the normal case now
  # and must not serialise; two builds of the same env in one tree still cannot
  # overlap, which is what the lock is actually for.
  local tag
  tag="$(printf '%s' "$REPO" | shasum | cut -c1-8)"
  BUILD_LOG="${TMPDIR:-/tmp}/xteink-build-$tag.log"
  BUILD_LOCK="${TMPDIR:-/tmp}/xteink-build-$tag.lock"
}
set_repo "$SCRIPTS_DIR/.."

export PATH="$HOME/.local/bin:$PATH"

# Each tree keeps its own agent SD card and its own artifacts; Mario's card
# lives at the workspace root so it follows him whichever tree he watches.
# Callers set this; the fallback is only for a bare sim.sh.
: "${CROSSPOINT_SIM_SD:=$REPO/fs_}"

# The scripts/ symlinks at the workspace root resolve back to firmware-next no
# matter where you call them from. So `./scripts/sim-shot.sh` typed out of habit
# inside wt/battleship/ would build and photograph the *integration* tree, boot
# perfectly, and have every tap land somewhere else -- indistinguishable from
# the feature being broken. That is the same shape as the wrong-binary trap that
# cost a session once, so it gets refused rather than documented.
require_same_tree() {
  local here
  here="$(pwd -P)"
  case "$here/" in
    "$REPO"/*) return 0 ;;
  esac
  cat >&2 <<EOF
error: this script drives $REPO
       but you are in $here

Those are different trees, so it would build, run and screenshot code that is
not the code you are working on. Run the copy that belongs to your own tree:

    cd $here && ./scripts_local/$(basename "$0") ...

(The workspace-root ./scripts/ symlinks resolve to firmware-next and are meant
for the integration tree only.)
EOF
  exit 2
}

# Just the card's shape. CrossPoint boots English, so the langSku workaround the
# fork's Chinese-first earlier base needed here is gone -- verified with an
# empty card.
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

# PlatformIO does not tolerate two concurrent builds of the same env. Serialise
# them with an atomic mkdir lock -- per tree, so other trees are unaffected.
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
  echo "building $ENV_NAME in $(basename "$REPO") ..."
  build_locked || {
    echo "build failed; see $BUILD_LOG" >&2
    tail -5 "$BUILD_LOG" >&2
    exit 1
  }
}

# Writes a capture to a site shot at the panel's own size. THE SIMULATOR
# CAPTURES AT 2x: 960x1600 portrait, 1600x960 landscape. site/index.html
# declares the 1x numbers, so a straight `cp` ships four times the pixels at
# exactly the right aspect ratio -- which looks perfect on the page and is
# invisible to everything except host-tests/site/page_structure.py. trivia,
# wavelength, toybattle and forehead all shipped that way on 2026-09-01.
# LANCZOS rather than NEAREST because it is what the committed shots were made
# with: NEAREST keeps the capture's 12 tones and matches 70% of toybattle.png,
# LANCZOS produces its 190 and matches 83%, the remainder being the rack the
# recipe already documents as nondeterministic.
write_site_shot() {
  local src="$1" dest="$2"
  [ -f "$src" ] || { echo "no capture at $src" >&2; return 1; }
  uv run --quiet --with pillow python - "$src" "$dest" <<'PY'
import sys
from PIL import Image
src, dest = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB")
w, h = im.size
if (w, h) in ((960, 1600), (1600, 960)):
    im = im.resize((w // 2, h // 2), Image.LANCZOS)
elif (w, h) not in ((480, 800), (800, 480)):
    sys.exit(f"unexpected capture size {w}x{h}: the panel is 480x800")
im.save(dest)
print(f"  {dest} {im.width}x{im.height}")
PY
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
