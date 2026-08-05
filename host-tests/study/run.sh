#!/bin/sh
# Builds and runs the study app's freestanding tests. No device and no
# PlatformIO needed: StudyFsrs and StudyDeck are freestanding C++17.
#
#   host-tests/study/run.sh [path/to/converted/deck]
#
# The deck tests parse a deck produced by tools_local/study/anki_to_deck.py, so
# what the converter writes is checked against what the firmware reads. They
# skip with a note if no deck has been converted yet.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name -- two worktrees sharing one
# build dir means one tree can run, and pass, a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/study-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/study

c++ -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/StudyFsrs.cpp" test_fsrs.cpp -o "$BUILD_DIR/test_fsrs"
c++ -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/StudyDeck.cpp" "$SRC/StudyFsrs.cpp" test_deck.cpp -o "$BUILD_DIR/test_deck"
c++ -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/StudyScheduler.cpp" "$SRC/StudyDeck.cpp" "$SRC/StudyFsrs.cpp" test_scheduler.cpp \
  -o "$BUILD_DIR/test_scheduler"

"$BUILD_DIR/test_fsrs"
"$BUILD_DIR/test_deck" "$@"
"$BUILD_DIR/test_scheduler"
