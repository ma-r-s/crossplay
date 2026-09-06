#!/bin/sh
# "Will this Picross name render at FULL SIZE?" -- the one rule that decides what
# the win screen can reveal, checked three ways.
#
#   host-tests/picrossnames/run.sh
#
# 1. THE BAND IS RE-MEASURED FROM THE REAL SCREEN BUILDER, not read on trust.
#    tools_local/picross/name_band.txt holds 448, and every name check in this
#    repository is a comparison against that number. It was written by a script
#    nobody ran again: move buildWin's layout -- the insets, the takeBottom
#    heights, the margins -- and 448 silently becomes wrong, and every name check
#    goes on reporting clean against a band that no longer exists. So this suite
#    now compiles measure_name_band against the CURRENT PicrossScreens.cpp and
#    fails if its answer is not what the file says. A checked-in measurement that
#    nothing re-derives is a literal, and this fork has paid for those.
#
# 2. EVERY NAME THE BANK ACTUALLY SHIPS is measured against that band. The
#    generator refuses an over-wide name, but a header is a file and a file can
#    be hand-edited or badly merged. This re-asks the question of what shipped,
#    through name_fit rather than a second copy of the rule, and requires the
#    count to be exactly kPuzzleCount -- a suite whose scope is chosen by a regex
#    and never counted is the shape that reports clean while measuring less than
#    it claims.
#
# 3. THE CORPUS PIN. name_fit_corpus.json records what measure() computed when it
#    was written. Glyph metrics move when a Toybox cut is regenerated -- that is
#    a recorded, real failure in this fork, not a hypothetical -- and every
#    accepted name silently changes width when they do. The pin turns that into
#    a red test.
#
# It used to drive a second, JavaScript restatement of this rule as well
# (site/picross-names/logic.js, the naming page). That page is deleted: the bank
# arrives titled and there is nothing left to name. One implementation, still
# pinned to its own recorded output.
#
# The Python half of this lived in host-tests/picrossprov, which existed to keep
# a per-puzzle CREDIT table honest against the bank. That obligation is gone with
# the janko puzzles and the suite went with it; this measurement is not
# attribution and had no business being parked there.
set -e
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"

# --- 1. the band, re-measured from the screen builder that defines it ---------
#
# Built here rather than trusting tools_local/picross/name_band.txt, because that
# file is the input to every other check below and nothing else re-derives it.
SDK="$ROOT/freeink-sdk/libs/ui/FreeInkUI"
ICONS="$ROOT/freeink-sdk/libs/assets/Icons"
BUILD="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-picrossnames-$(cd "$ROOT" && pwd | cksum | cut -d' ' -f1)"
mkdir -p "$BUILD"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -isystem "$SDK/include" -isystem "$ICONS/include" \
  "$SDK/src/FreeInkUI.cpp" \
  "$ROOT/src/apps_local/picross/PicrossCore.cpp" \
  "$ROOT/src/apps_local/picross/PicrossScreens.cpp" \
  "$ROOT/tools_local/picross/measure_name_band.cpp" -o "$BUILD/measure_name_band"
MEASURED="$("$BUILD/measure_name_band")"
RECORDED="$(cat "$ROOT/tools_local/picross/name_band.txt")"
if [ "$MEASURED" != "$RECORDED" ]; then
  echo "FAIL picrossnames  the win screen's name band has MOVED"
  echo "      buildWin gives the name ${MEASURED}px; tools_local/picross/name_band.txt says ${RECORDED}px."
  echo "      Every name in the bank was accepted against the recorded number, so they are all"
  echo "      unchecked until this is resolved. Run tools_local/picross/measure_name_band.sh,"
  echo "      then re-run tools_local/picross/gen_picross.py -- a name that fit the old band may"
  echo "      not fit this one, and fittedTitle SHRINKS rather than truncates."
  exit 1
fi
echo "picrossnames: the name band re-measures to ${MEASURED}px, as recorded"

python3 - "$ROOT" <<'PY'
import json
import os
import sys

root = sys.argv[1]
sys.path.insert(0, os.path.join(root, "tools_local", "picross"))
import name_fit  # noqa: E402

checks = 0
failed = 0


def check(ok, label, detail=""):
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print(f"FAIL picrossnames  {label}" + (f": {detail}" if detail else ""))


fit = name_fit.fitter()
# Measured from the real screen builder into name_band.txt, never guessed: a
# name check against a guessed band is worse than no check, because it reports
# clean.
check(fit.band > 0, "the win screen's name band is measured", f"{fit.band}px")

# EVERY NAME THE BANK ACTUALLY SHIPS, re-asked here.
#
# The generator refuses a name that does not render at full size, but the header
# is a file and a file can be hand-edited or badly merged. This re-asks the same
# question of what actually shipped, through name_fit rather than through a
# second copy of the rule.
#
# It matters more than it used to. While the names were typed by hand, checking
# the file Mario wrote checked everything that could be wrong. The bank now
# arrives titled, so the widest name arrives with the corpus -- and the pack this
# bank was built from carried exactly one over the band.
import re  # noqa: E402

bank = open(os.path.join(root, "src/apps_local/picross/PicrossPuzzles.h"), encoding="utf-8").read()
body = bank.split("constexpr Puzzle kPuzzles[] = {", 1)[1].split("\n};", 1)[0]
names = [m for m, _s, _r in re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*(\d+),\s*\{([^}]*)\}\}', body)]

# THE COUNT IS THE SCOPE, and it is asserted rather than assumed. This suite's
# reach is decided by a regex over a generated file: a new field in Puzzle, a
# changed row shape or a bad merge would silently match fewer rows, and the suite
# would report green having measured a subset of the bank.
#
# The cross-check counts rows by their BITMASK LIST -- every row has exactly one
# ", {0x" -- which is the part of the row the name pattern does not look at. Two
# counts that fail for different reasons; they disagree exactly when the row
# pattern has lost rows the file still has.
expected = body.count(", {0x")
check(
    len(names) == expected,
    "every row in the bank was measured",
    f"matched {len(names)} of {expected} rows -- the regex has lost some",
)
check(len(names) > 0, "the bank parsed", f"{len(names)} puzzles")
for name in names:
    if not name:
        continue  # unnamed: the win screen draws no name band at all
    landed = fit(name)
    check(not landed.holes, f"{name!r} is drawable", f"no glyph for {landed.holes}")
    check(
        landed.full_size,
        f"{name!r} renders at full size",
        f"{landed.width}px against a {fit.band}px band, so fittedTitle would SHRINK it",
    )

# The corpus is the pin between this measurement and the naming tool's
# JavaScript restatement of it. If these numbers move, the two have drifted and
# one of them is now lying about what fits.
corpus_path = os.path.join(root, "tools_local", "picross", "name_fit_corpus.json")
if not os.path.exists(corpus_path):
    check(False, "name_fit_corpus.json exists", "run tools_local/picross/name_fit.py --corpus")
else:
    with open(corpus_path, encoding="utf-8") as f:
        committed = json.load(f)
    check(
        committed.get("band") == fit.band,
        "the corpus records the measured band",
        f"{committed.get('band')} against {fit.band}",
    )
    fresh = name_fit.corpus()
    if committed.get("widths") != fresh:
        moved = [
            f"{rung}/{word}: {committed['widths'][rung][word]} -> {fresh[rung][word]}"
            for rung in fresh
            for word in fresh[rung]
            if committed.get("widths", {}).get(rung, {}).get(word) != fresh[rung][word]
        ][:5]
        check(False, "the committed corpus is what measure() computes today", "; ".join(moved))
    else:
        check(True, "the committed corpus is what measure() computes today")

print(f"picrossnames (fit): {checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
PY
