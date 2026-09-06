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
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-toybox-ui-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
# Icons is freestanding too (a struct and generated arrays), so the screens
# can carry real icons and still be tested with no renderer and no device.
ICONS=../../freeink-sdk/libs/assets/Icons
mkdir -p "$BUILD_DIR"
# -Wno-comment: see host-tests/dungeon/run.sh. -Wno-format-truncation used to
# sit beside it and does not any more: it was hiding seventeen buffers that
# were provably too small for their own formats (card 256). The three files
# from jaipur down are compiled here for that warning alone -- no strict
# suite built them, so the class had nowhere to be caught in them at all.
# (Four files: JaipurScreens, SolitaireCore, SolitaireScreens, YahtzeeScreens.)
#
# The flag is NOT the gate for this class, though -- host-tests/fmtwidth is.
# -Wformat-truncation is a middle-end warning and every suite here compiles
# at -O0, where it reports nothing at all. See host-tests/fmtwidth/check_widths.py.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-comment \
  -I"$SDK/include" -I"$ICONS/include" \
  "$SDK/src/FreeInkUI.cpp" \
  ../../src/apps_local/battleship/BattleshipScreens.cpp \
  ../../src/apps_local/ShelfScreen.cpp \
  ../../src/apps_local/chess/ChessScreens.cpp \
  ../../src/apps_local/checkers/CheckersScreens.cpp \
  ../../src/apps_local/connectfour/ConnectFourScreens.cpp \
  ../../src/apps_local/connections/ConnectionsCore.cpp \
  ../../src/apps_local/connections/ConnectionsScreens.cpp \
  ../../src/apps_local/dungeon/DungeonCore.cpp \
  ../../src/apps_local/dungeon/DungeonScreens.cpp \
  ../../src/apps_local/forehead/ForeheadCore.cpp \
  ../../src/apps_local/forehead/ForeheadScreens.cpp \
  ../../src/apps_local/hackernews/HackerNewsScreens.cpp \
  ../../src/apps_local/instapaper/InstapaperScreens.cpp \
  ../../src/apps_local/insider/InsiderCore.cpp \
  ../../src/apps_local/insider/InsiderScreens.cpp \
  ../../src/apps_local/knucklebones/KnucklebonesScreens.cpp \
  ../../src/apps_local/link/LinkScreens.cpp \
  ../../src/apps_local/minesweeper/MinesweeperScreens.cpp \
  ../../src/apps_local/picross/PicrossCore.cpp \
  ../../src/apps_local/picross/PicrossScreens.cpp \
  ../../src/apps_local/toybattle/ToyBattleScreens.cpp \
  ../../src/apps_local/toybattle/ToyBattleMenus.cpp \
  ../../src/apps_local/toybattle/ToyBattleHowTo.cpp \
  ../../src/apps_local/toybattle/ToyBattleCore.cpp \
  ../../src/apps_local/toybattle/ToyBattleFlow.cpp \
  ../../src/apps_local/trivia/TriviaScreens.cpp \
  ../../src/apps_local/murdle/MurdleCast.cpp \
  ../../src/apps_local/murdle/MurdleCore.cpp \
  ../../src/apps_local/murdle/MurdleScreens.cpp \
  ../../src/apps_local/murdle/MurdleText.cpp \
  ../../src/apps_local/seasalt/SeaSaltScreens.cpp \
  ../../src/apps_local/player/PlayerAvatar.cpp \
  ../../src/apps_local/player/PlayerName.cpp \
  ../../src/apps_local/player/PlayerScreen.cpp \
  ../../src/apps_local/study/StudyScreens.cpp \
  ../../src/apps_local/sudoku/SudokuCore.cpp \
  ../../src/apps_local/wavelength/WavelengthCore.cpp \
  ../../src/apps_local/wavelength/WavelengthScreens.cpp \
  ../../src/apps_local/sudoku/SudokuScreens.cpp \
  ../../src/apps_local/xkcd/XkcdScreens.cpp \
  ../../src/apps_local/wallpapers/WallpapersCore.cpp \
  ../../src/apps_local/wallpapers/WallpapersScreens.cpp \
  ../../src/apps_local/jaipur/JaipurScreens.cpp \
  ../../src/apps_local/solitaire/SolitaireCore.cpp \
  ../../src/apps_local/solitaire/SolitaireScreens.cpp \
  ../../src/apps_local/yahtzee/YahtzeeScreens.cpp \
  test_ui.cpp -o "$BUILD_DIR/test_ui"
"$BUILD_DIR/test_ui"
