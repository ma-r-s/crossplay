#!/bin/sh
# The two implementations of "will this Picross name fit", pinned to each other,
# and the naming tool's logic with no browser.
#
#   host-tests/picrossnames/run.sh
#
# THE PIN IS THE POINT. A name is accepted exactly when it renders at FULL SIZE
# in the win screen's band, and two things ask that question: the generator, in
# Python (tools_local/picross/name_fit.py), and the naming page, in JavaScript
# (site/picross-names/logic.js), which cannot import Python and so restates the
# same measurement. A second copy of a rule that must agree to the pixel is the
# drift this fork keeps paying for, so name_fit_corpus.json is the pin: the
# Python half writes it, the check below fails if those numbers are not what
# measure() computes today, and test_logic.js drives the JavaScript against the
# same file.
#
# The Python half used to live in host-tests/picrossprov, which existed to keep
# a per-puzzle CREDIT table honest against the bank. That obligation is gone with
# the janko puzzles and the suite went with it; this measurement is not
# attribution and had no business being parked there.
set -e
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"

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

# site/picross-names/logic.js holds everything in that tool that is not the
# page, precisely so this can drive it. Two confirmed data-loss bugs were found
# by a cold review rather than by a test -- a stale draft surviving a save-file
# merge, and an out-of-range saved position that bricked the tool while the page
# still rendered perfectly -- and both are pinned in test_logic.js.
node test_logic.js
