#!/bin/sh
# Builds and runs the Reading Stats tests, then holds the reader seam to one call.
#
#   host-tests/readingstats/run.sh
#
# ReadingStatsCore is freestanding C++17 plus lib/Utf8 (the title cleaner folds
# typography through it), so the record format, the day rule, the streak, the
# week, the yearly list and the torn-file rule all run here with no card.
#
# The second half is a source check, because the seam is a promise about an
# upstream file: src/activities/reader/ReaderActivity.cpp is CrossPoint's, and
# every line the fork adds there is carried by hand through every sync. Reading
# Stats may add exactly one include and one call (and the comment marking them)
# and nothing else, so the file with those lines removed must be byte for byte
# the file upstream ships at the merge base.
set -e
cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-readingstats-tests-$(echo "$ROOT" | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/readingstats

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I$SRC -I../../lib/Utf8 \
  test_readingstats.cpp $SRC/ReadingStatsCore.cpp ../../lib/Utf8/Utf8.cpp \
  -o "$BUILD_DIR/test_readingstats"
"$BUILD_DIR/test_readingstats"

# Titles measured in the real system face: the screens and the fitter, built
# against FreeInkUI and EpdFont with fittedtitle's RealTarget bound to the
# three ubuntu cuts. -Wno-missing-field-initializers for the generated font
# headers, as in host-tests/typefold; -Wno-comment for the SDK's icon header,
# whose ASCII-art comment GCC reads as a multi-line one (host-tests/ui does
# the same). -Wformat-truncation stays ON: it found a real 8-byte buffer here.
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -Wno-comment \
  -I$SRC -I../../lib/Utf8 -I../../lib/EpdFont -I"$SDK/include" -I"$ICONS/include" \
  test_titlefit.cpp $SRC/ReadingStatsCore.cpp $SRC/ReadingStatsScreens.cpp $SRC/ReadingStatsTitles.cpp \
  "$SDK/src/FreeInkUI.cpp" ../../lib/Utf8/Utf8.cpp ../../lib/EpdFont/EpdFont.cpp \
  -o "$BUILD_DIR/test_titlefit"
"$BUILD_DIR/test_titlefit"

# ---- The seam: one call in the reader, nothing more -----------------------
cd "$ROOT"
BASE=""
if git rev-parse --verify --quiet crosspoint/develop >/dev/null 2>&1; then
  BASE=$(git merge-base HEAD crosspoint/develop 2>/dev/null || true)
fi
python3 - "$BASE" <<'PY'
import os
import re
import subprocess
import sys

base = sys.argv[1]
reader = "src/activities/reader/ReaderActivity.cpp"
text = open(reader).read()
lines = text.split("\n")
checks = 0
failed = 0


def check(ok, what):
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print("FAIL readingstats-seam  " + what)


INCLUDE = '#include "apps_local/readingstats/ReadingStatsLog.h"  // fork-local seam'
CALL = re.compile(r"^  readingstats::recordSession\(bookPath, getBookTitle\(\), readerSession, getScreenshotInfo\(\)\.endProgressPercent\);$")
COMMENT = re.compile(r"^  // fork-local seam: Reading Stats")

mentions = [i for i, l in enumerate(lines) if "readingstats" in l.lower() or "reading stats" in l.lower()]
includes = [i for i, l in enumerate(lines) if l == INCLUDE]
calls = [i for i, l in enumerate(lines) if CALL.match(l)]
comments = [i for i, l in enumerate(lines) if COMMENT.match(l)]
check(len(includes) == 1, "%s includes ReadingStatsLog.h %d times, not once" % (reader, len(includes)))
check(len(calls) == 1, "%s calls readingstats::recordSession %d times, not once" % (reader, len(calls)))
check(len(comments) == 1, "%s marks the call %d times, not once" % (reader, len(comments)))
check(set(mentions) <= set(includes + calls + comments),
      "%s mentions Reading Stats on lines other than the seam: %s" % (reader, [m + 1 for m in mentions]))

# Placed inside flushReaderSession, as its first statement, so it runs before
# the early return that drops every session no plugin subscribes to.
if calls and comments:
    call = calls[0]
    opener = None
    for i in range(call, -1, -1):
        if lines[i].startswith("void ReaderActivity::flushReaderSession() {"):
            opener = i
            break
        if lines[i].startswith("}"):
            break
    check(opener is not None, "the call is not inside ReaderActivity::flushReaderSession()")
    if opener is not None:
        check(comments[0] == opener + 1 and call == opener + 2,
              "the call is not the first statement of flushReaderSession(): it must run before the early return")

# Nothing else under src/activities knows Reading Stats exists.
others = subprocess.run(["git", "grep", "-l", "-i", "readingstats", "--", "src/activities"],
                        capture_output=True, text=True).stdout.split()
check(others == [reader] or (not others and not calls),
      "Reading Stats reaches into upstream's activities beyond the seam: %s" % others)

# Against upstream: the file minus the seam is upstream's file.
if base:
    shown = subprocess.run(["git", "show", "%s:%s" % (base, reader)], capture_output=True, text=True)
    if shown.returncode == 0:
        seam = set(includes + calls + comments)
        ours = "\n".join(l for i, l in enumerate(lines) if i not in seam)
        check(ours == shown.stdout,
              "%s differs from crosspoint/develop's beyond the seam: diff it against %s" % (reader, base[:9]))
    else:
        print("  SKIP readingstats-seam: %s is not at the merge base %s" % (reader, base[:9]))
else:
    print("  SKIP readingstats-seam: no crosspoint/develop ref, so the upstream comparison did not run")

print("%s  readingstats-seam: %d checks, %d failed" % ("FAIL" if failed else "ok  ", checks, failed))
sys.exit(1 if failed else 0)
PY
