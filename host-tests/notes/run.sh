#!/bin/sh
# Builds and runs the note-format tests. NotesCore is freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-notes-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/notes
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_notes.cpp $SRC/NotesCore.cpp -o "$BUILD_DIR/test_notes"
"$BUILD_DIR/test_notes"

# THE NOTE SLEEP SCREEN IS SAVED AS 8, and stays 8.
#
# Devices store Settings > Sleep Screen as the enum's NUMBER. NOTE shipped as 8,
# so every device that put a note on its sleep screen has 8 on its card. CrossPoint
# 2.0 added COVER_TRANSPARENT as ITS 8; had the merge taken upstream's numbering,
# those devices would wake into Cover + Transparent with no word said. Read from
# the source because CrossPointSettings.h cannot be compiled on a host: NOTE is
# 8, the label table maps 8 to the note's label, and the settings loader's clamp
# (the label count) still admits it.
python3 - ../../src/CrossPointSettings.h ../../src/SettingsList.cpp <<'PY'
import re
import sys

hdr = re.sub(r"//[^\n]*", "", open(sys.argv[1]).read())
lst = re.sub(r"//[^\n]*", "", open(sys.argv[2]).read())
fails = 0
m = re.search(r"enum\s+SLEEP_SCREEN_MODE\s*\{(.*?)\};", hdr, re.S)
values = {}
if m:
    nxt = 0
    for entry in [e.strip() for e in m.group(1).split(",") if e.strip()]:
        name, _, val = entry.partition("=")
        nxt = int(val) if val.strip() else nxt
        values[name.strip()] = nxt
        nxt += 1
if values.get("NOTE") != 8:
    fails += 1
    print("FAIL notes  SLEEP_SCREEN_MODE::NOTE is %s, not 8: every device that chose a note would wake into another mode" % values.get("NOTE"))
if not values or values.get("SLEEP_SCREEN_MODE_COUNT", 0) <= 8:
    fails += 1
    print("FAIL notes  SLEEP_SCREEN_MODE_COUNT does not admit 8, so the loader's clamp would discard a saved note")
if len(set(v for k, v in values.items() if k != "SLEEP_SCREEN_MODE_COUNT")) != len(values) - 1:
    fails += 1
    print("FAIL notes  two sleep screen modes share a number: %s" % values)
if re.search(r"values\[CrossPointSettings::NOTE\]\s*=\s*StrId::STR_SLEEP_NOTE;", lst) is None:
    fails += 1
    print("FAIL notes  SettingsList.cpp no longer labels the NOTE value, so Settings would show it as a blank row")
print("notes-sleep-value: NOTE=%s COVER_TRANSPARENT=%s, %d failed" % (values.get("NOTE"), values.get("COVER_TRANSPARENT"), fails))
sys.exit(1 if fails else 0)
PY
