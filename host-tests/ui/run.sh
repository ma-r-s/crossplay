#!/bin/sh
# Builds and runs the screen tests. No device and no PlatformIO: FreeInkUI is
# freestanding C++17, and this fork's screen builders were written to stay that
# way so they can be tested here.
#
#   host-tests/ui/run.sh
#
# Note what is NOT on the include path: no src/, no lib/, no Arduino. That is
# deliberate. If a screen builder ever reaches for GfxRenderer, UITheme or the
# SD card, this build fails loudly instead of the screens quietly becoming
# untestable again.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name. Two worktrees sharing
# one build dir means one tree can run -- and pass -- a binary the other
# built, which is a green suite whose source is not even present.
BUILD_DIR="${TMPDIR:-/tmp}/toybox-ui-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
# Icons is freestanding too (a struct and generated arrays), so the screens
# can carry real icons and still be tested with no renderer and no device.
ICONS=../../freeink-sdk/libs/assets/Icons
mkdir -p "$BUILD_DIR"
c++ -std=c++17 -Wall -Wextra -Werror -I"$SDK/include" -I"$ICONS/include" \
  "$SDK/src/FreeInkUI.cpp" \
  ../../src/apps_local/battleship/BattleshipScreens.cpp \
  ../../src/apps_local/ShelfScreen.cpp \
  ../../src/apps_local/chess/ChessScreens.cpp \
  ../../src/apps_local/connections/ConnectionsCore.cpp \
  ../../src/apps_local/connections/ConnectionsScreens.cpp \
  ../../src/apps_local/hackernews/HackerNewsScreens.cpp \
  ../../src/apps_local/link/LinkScreens.cpp \
  ../../src/apps_local/player/PlayerAvatar.cpp \
  ../../src/apps_local/player/PlayerName.cpp \
  ../../src/apps_local/player/PlayerScreen.cpp \
  test_ui.cpp -o "$BUILD_DIR/test_ui"
"$BUILD_DIR/test_ui"
