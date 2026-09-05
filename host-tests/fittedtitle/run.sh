#!/bin/sh
# Every title in the fork, measured in the cut that will really draw it.
#
#   host-tests/fittedtitle/run.sh
#
# Two things make this different from host-tests/ui, which already builds these
# same screen files:
#
#   * the DrawTarget measures with the REAL cuts (EpdFont over
#     src/apps_local/ui/fonts) and reproduces GfxRenderer's truncation, so it
#     can see a title being cut. host-tests/ui's target answers a flat ten
#     pixels a character and records whatever string it is handed, which is
#     right for layout tests and blind to this entirely.
#
#   * the corpora are COMPLETE and DERIVED, never sampled and never copied:
#     corpus.py reads the linkplay titles out of the game Activities and the
#     comic titles out of the pack on the card, and the test walks Forehead's,
#     Toy Battle's and the dungeon's own tables and every date Connections can
#     format. A corpus written into a test is a corpus that stops matching the
#     app the day somebody adds a game.
#
# No PlatformIO and no device: the screen builders are freestanding C++17 and
# EpdFont/Utf8 are too. If a builder ever reaches for GfxRenderer or the card,
# this build fails loudly rather than the check quietly becoming device-only.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-fittedtitle-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
APPS=../../src/apps_local

python3 ./corpus.py "$BUILD_DIR/corpus.generated.h"

# Three warnings silenced, all of them GCC-only noise on code this suite only
# compiles in order to drive it. -Wno-missing-field-initializers: the font
# headers are GENERATED aggregates that stop at the last field fontconvert.py
# has a value for, exactly as host-tests/typefold explains. -Wno-comment and
# -Wno-format-truncation: the same two host-tests/ui silences, for the same
# screen files -- without them GCC fails this build on a snprintf in
# ConnectionsScreens.cpp that has nothing to do with fitting a title.
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-comment -Wno-format-truncation \
  -Wno-missing-field-initializers \
  -I"$BUILD_DIR" -I. \
  -I"$SDK/include" -I"$ICONS/include" \
  -I../../lib/Utf8 -I../../lib/EpdFont \
  -I"$APPS/ui" -I"$APPS/connections" -I"$APPS/dungeon" -I"$APPS/forehead" \
  -I"$APPS/link" -I"$APPS/toybattle" -I"$APPS/xkcd" -I"$APPS/player" \
  -I"$APPS/hackernews" \
  "$SDK/src/FreeInkUI.cpp" \
  ../../lib/Utf8/Utf8.cpp \
  ../../lib/EpdFont/EpdFont.cpp \
  "$APPS/connections/ConnectionsCore.cpp" \
  "$APPS/connections/ConnectionsScreens.cpp" \
  "$APPS/dungeon/DungeonCore.cpp" \
  "$APPS/dungeon/DungeonScreens.cpp" \
  "$APPS/forehead/ForeheadCore.cpp" \
  "$APPS/hackernews/HackerNewsScreens.cpp" \
  "$APPS/forehead/ForeheadScreens.cpp" \
  "$APPS/link/LinkScreens.cpp" \
  "$APPS/player/PlayerAvatar.cpp" \
  "$APPS/player/PlayerName.cpp" \
  "$APPS/toybattle/ToyBattleCore.cpp" \
  "$APPS/toybattle/ToyBattleFlow.cpp" \
  "$APPS/toybattle/ToyBattleHowTo.cpp" \
  "$APPS/toybattle/ToyBattleMenus.cpp" \
  "$APPS/toybattle/ToyBattleScreens.cpp" \
  "$APPS/xkcd/XkcdScreens.cpp" \
  test_fittedtitle.cpp -o "$BUILD_DIR/test_fittedtitle"
"$BUILD_DIR/test_fittedtitle"
