#!/bin/bash
# One-shot interactive launch. For everyday use prefer ./scripts/dev.sh, which
# stays up and rebuilds itself when the code changes.
#
# Build and launch the X4 Pro simulator interactively.
#
#   ./scripts/sim.sh
#
# Controls: arrows = buttons, Return = confirm, Esc = back, P = power,
# S = sleep, H = capacitive Home key, mouse = touch/tap/swipe.
# Drop EPUBs into firmware/fs_/books/ to have something to read.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib-sim.sh"

seed_fs
build
echo "launching (close the window or press Ctrl-C to stop)"
cd "$REPO" && exec "$BIN"
