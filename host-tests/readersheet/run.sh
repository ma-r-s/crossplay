#!/bin/sh
# The reader's toolbar panel sizes its bottom sheet for N rows, then hands the
# list a rowGap it never set. This asks the REAL SDK what gap it resolves on a
# touch device and compares it with the one the sheet reserved. No device, no
# PlatformIO: FreeInkUI is freestanding C++17.
#
#   host-tests/readersheet/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-readersheet-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-comment \
  -I"$SDK/include" -I"$ICONS/include" \
  "$SDK/src/FreeInkUI.cpp" \
  test_readersheet.cpp -o "$BUILD_DIR/test_readersheet"
"$BUILD_DIR/test_readersheet"

# The C++ half proves the ARITHMETIC. It cannot see which gap buildPanel hands
# that arithmetic, so on its own a revert to the raw theme token would leave
# this suite green. This is that missing half: the reader panel must not reach
# for tokens.listRowGap at all. Comments are stripped first, so the explanation
# of the bug is allowed to name it (a detector satisfied by a mention of the
# thing is no detector).
SRC=../../src/activities/reader/ReaderToolbarUi.cpp
if sed 's://.*::' "$SRC" | grep -q "tokens\.listRowGap"; then
  echo "FAIL: ReaderToolbarUi.cpp uses tokens.listRowGap (the RAW theme token)."
  echo "      The list resolves its gap through Screen::resolveListProps(), which"
  echo "      raises it to listTouchRowGap on touch boards. Size the sheet and sync"
  echo "      the nav with the resolved gap. Card #546."
  sed 's://.*::' "$SRC" | grep -n "tokens\.listRowGap"
  exit 1
fi
echo "source guard: reader panel does not use the raw listRowGap  ok"
