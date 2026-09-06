#!/bin/sh
# The Picross naming tool's logic, with no browser.
#
#   host-tests/picrossnames/run.sh
#
# site/picross-names/logic.js holds everything in that tool that is not the
# page, precisely so this can drive it. Two confirmed data-loss bugs were found
# by a cold review rather than by a test -- a stale draft surviving a save-file
# merge, and an out-of-range saved position that bricked the tool while the page
# still rendered perfectly -- and both are pinned in test_logic.js.
set -e
cd "$(dirname "$0")"
node test_logic.js
