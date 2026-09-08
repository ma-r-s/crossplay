#!/bin/sh
# Builds and runs the study app's freestanding tests. No device and no
# PlatformIO needed: StudyFsrs, StudyScheduler, StudyDeck and StudyStats are
# freestanding C++17.
#
#   host-tests/study/run.sh [path/to/converted/deck]
#
# The deck tests parse a deck produced by tools_local/study/anki_to_deck.py, so
# what the converter writes is checked against what the firmware reads.
#
# With no argument they used to SKIP, and nothing in check.sh or in CI has ever
# passed one, so two of the five binaries below printed
# "PASS (0 checks, 0 failures)" on every run this repo has had. They now get a
# fixture built here: make_fixture.py writes a synthetic Anki collection and
# runs the REAL converter over it, so the round trip happens rather than being
# waited for. Pass a directory to point them at a real converted deck instead.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name -- two worktrees sharing one
# build dir means one tree can run, and pass, a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-study-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/study

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/StudyFsrs.cpp" test_fsrs.cpp -o "$BUILD_DIR/test_fsrs"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/StudyDeck.cpp" "$SRC/StudyFsrs.cpp" test_deck.cpp -o "$BUILD_DIR/test_deck"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/StudyScheduler.cpp" "$SRC/StudyDeck.cpp" "$SRC/StudyFsrs.cpp" test_scheduler.cpp \
  -o "$BUILD_DIR/test_scheduler"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/StudyStats.cpp" "$SRC/StudyDeck.cpp" "$SRC/StudyFsrs.cpp" test_stats.cpp \
  -o "$BUILD_DIR/test_stats"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$SRC/StudyImages.cpp" "$SRC/StudyDeck.cpp" "$SRC/StudyFsrs.cpp" test_images.cpp \
  -o "$BUILD_DIR/test_images"
# StudyText is header-only, and this is the only test that compiles the card
# face's drawing at all: StudyActivity needs Arduino, the HAL and a panel, so
# for as long as the wrap lived inside it nothing could check where a line
# broke or where the cloze underline went.
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  test_text.cpp -o "$BUILD_DIR/test_text"

# The deck under test. An argument wins; otherwise build one. A failure here
# is a failure of the suite: the tests below now REFUSE a missing deck rather
# than skipping past it, so a fixture that cannot be built must be loud at the
# point it cannot be built.
DECK="${1:-}"
if [ -z "$DECK" ]; then
  DECK="$BUILD_DIR/fixture"
  rm -rf "$DECK"
  python3 make_fixture.py --out "$DECK"
fi

# Before the fixture is even needed: it takes no deck, and a wrap that is
# broken makes every card unreadable whatever the deck says.
"$BUILD_DIR/test_text"
"$BUILD_DIR/test_fsrs"
"$BUILD_DIR/test_deck" "$DECK"
"$BUILD_DIR/test_scheduler"
"$BUILD_DIR/test_stats"
"$BUILD_DIR/test_images" "$DECK"
