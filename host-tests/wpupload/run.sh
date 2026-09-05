#!/bin/bash
# The browser wallpaper converter's pure half, run under node with no browser.
# site/wallpapers/convert.js is DOM-free on purpose so its BMP bytes and its
# image-type gate can be pinned here; the canvas fitting is verified in a
# browser instead. Node, not bun, to match the site suite's other JS checks and
# because GitHub's ubuntu-latest ships it.
#
#   host-tests/wpupload/run.sh
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
node "$HERE/test_convert.mjs"
