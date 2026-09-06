#!/bin/bash
# The Picross credit, checked against the puzzles that actually ship.
#
# The designers' names, the rights line and the source URLs used to sit beside
# every puzzle in PicrossPuzzles.h. They do not any more -- 137 source URLs were
# ~34KB of an ~51KB bank, a URL costing more flash than the puzzle it pointed
# at, and no player ever read one. Mario's call.
#
# That makes assets_local/picross/PROVENANCE.md the ONLY place the credit lives,
# on a public repository, for puzzles used by permission and not under any
# licence. A credit file that has quietly stopped matching the shipped bank is
# worse than no file, because it reads as a checked claim.
#
# So the mapping is GENERATED into that file by gen_picross.py, and this
# re-derives it independently -- from janko.txt and from the BITMAPS actually
# emitted into the header -- and fails if the file is not exactly it.
#
# Matching is by bitmap rather than by name, and that is the point of the
# design: the string each puzzle carries is now Mario's NAME for the picture,
# which he is writing by hand and which says nothing about where the picture
# came from. The bitmap is the puzzle.
#
#   host-tests/picrossprov/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

python3 - "$ROOT" <<'PY'
import os
import re
import sys

root = sys.argv[1]
checks = 0
failed = 0


def check(ok, label, detail=""):
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print(f"FAIL picrossprov  {label}" + (f": {detail}" if detail else ""))


def read(rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read()


# --- the source: every candidate, with its origin --------------------------
#
# Parsed with a GRID STATE, not line by line, and that is not a style choice: a
# solid row of a picture is "##########", which starts with a '#' and is
# indistinguishable from a comment unless you already know a grid is open. The
# first version of this file did it flat, silently dropped every solid row, and
# reported all 137 puzzles as untraceable. gen_picross.parse() consumes the grid
# in an inner loop for exactly this reason.
PROV_KEYS = ("author", "license", "source")
source = {}
defaults = {}
pending = {}
lines = read("assets_local/picross/janko.txt").splitlines()
i = 0
while i < len(lines):
    line = lines[i].rstrip()
    if not line.strip() or line.lstrip().startswith("#"):
        i += 1
        continue
    if line.lstrip().startswith("@"):
        text = line.strip()
        sticky = text.startswith("@@")
        key, _, value = text.lstrip("@").partition(" ")
        (defaults if sticky else pending)[key.strip().lower()] = value.strip()
        i += 1
        continue
    name = line.strip()
    prov = dict(defaults)
    prov.update(pending)
    pending = {}
    i += 1
    cells = []
    while i < len(lines) and lines[i].strip() and set(lines[i].strip()) <= {"#", "."}:
        cells.append([1 if ch == "#" else 0 for ch in lines[i].strip()])
        i += 1
    if not cells:
        continue
    size = len(cells)
    bits = tuple(sum(1 << c for c in range(size) if cells[r][c]) for r in range(size))
    source.setdefault((size, bits), []).append((name, prov))

# --- what actually shipped --------------------------------------------------
header = read("src/apps_local/picross/PicrossPuzzles.h")
body = header[header.index("constexpr Puzzle kPuzzles[]") :]
body = body[: body.index("};")]
shipped = []
for entry in re.finditer(r'\{"((?:[^"\\]|\\.)*)",\s*(\d+),\s*\{([^}]*)\}\}', body):
    size = int(entry.group(2))
    rows = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", entry.group(3))]
    shipped.append((entry.group(1), size, tuple(rows[:size])))
check(len(shipped) > 0, "the bank parsed", f"{len(shipped)} puzzles")

# THE ATTRIBUTION REALLY LEFT THE FIRMWARE. Not "the struct was removed" -- the
# STRINGS. A later session re-adding a designer name to the header, in any
# shape, is the thing this catches, and it is the reason the check greps for the
# names rather than for the type that used to hold them.
designers = {p["author"] for entries in source.values() for _n, p in entries if p.get("author")}
check(len(designers) >= 5, "designers recovered from janko.txt", f"{sorted(designers)}")
for who in sorted(designers):
    check(who not in header, "no designer name in the firmware", who)
check("janko.at" not in header, "no source URL in the firmware")

# --- the mapping, re-derived ------------------------------------------------
expected = []
for _name, size, rows in shipped:
    matches = source.get((size, rows), [])
    if len(matches) != 1:
        check(False, "a shipped bitmap matches exactly one source picture",
              f"{len(matches)} matches for a {size}x{size}")
        continue
    src_name, prov = matches[0]
    for key in PROV_KEYS:
        check(bool(prov.get(key)), f"{src_name} records its {key}")
    expected.append((src_name, prov["author"], prov["license"], prov["source"]))
check(len(expected) == len(shipped), "every shipped puzzle traced to its origin",
      f"{len(expected)} of {len(shipped)}")

# --- the file that carries the credit ---------------------------------------
doc = read("assets_local/picross/PROVENANCE.md")
begin, end = "<!-- BEGIN GENERATED CREDITS -->", "<!-- END GENERATED CREDITS -->"
check(begin in doc and end in doc, "PROVENANCE.md still has its generated block")
if begin in doc and end in doc:
    block = doc[doc.index(begin) : doc.index(end)]
    listed = []
    for row in re.finditer(r"^\| `([^`]+)` \| (.+?) \| (.+?) \| <(.+?)> \|$", block, re.M):
        listed.append((row.group(1), row.group(2), row.group(3), row.group(4)))
    check(len(listed) == len(expected), "the table lists every shipped puzzle and no others",
          f"{len(listed)} rows against {len(expected)} puzzles")
    if sorted(listed) != sorted(expected):
        missing = [e for e in expected if e not in listed][:3]
        extra = [l for l in listed if l not in expected][:3]
        check(False, "the table is the shipped bank", f"missing {missing}, extra {extra}")
    else:
        check(True, "the table is the shipped bank")
    # The claim the file makes about itself, checked rather than believed.
    check(f"{len(expected)} puzzles ship" in block, "the table states the count it lists")

# --- the names, against the measurement that decides them -------------------
#
# The generator refuses a name that does not render at full size, but the header
# is a file and a file can be hand-edited. These re-ask the same question of
# what actually shipped, and they re-ask it through name_fit rather than through
# a second copy of the rule.
sys.path.insert(0, os.path.join(root, "tools_local", "picross"))
import name_fit  # noqa: E402

fit = name_fit.fitter()
check(fit.band > 0, "the win screen's name band is measured", f"{fit.band}px")
for name, _size, _rows in shipped:
    if not name:
        continue  # unnamed: the win screen draws no name band at all
    landed = fit(name)
    check(not landed.holes, f"{name!r} is drawable", f"no glyph for {landed.holes}")
    check(landed.full_size, f"{name!r} renders at full size",
          f"{landed.width}px against a {fit.band}px band, so fittedTitle shrinks it")

# The corpus is the pin between this measurement and the naming tool's
# JavaScript restatement of it (site/picross-names/logic.js, driven by
# host-tests/picrossnames). If these numbers move, the two have drifted and one
# of them is now lying to Mario while he types.
corpus_path = os.path.join(root, "tools_local", "picross", "name_fit_corpus.json")
if not os.path.exists(corpus_path):
    check(False, "name_fit_corpus.json exists", "run tools_local/picross/name_fit.py --corpus")
else:
    import json

    with open(corpus_path, encoding="utf-8") as f:
        committed = json.load(f)
    check(committed.get("band") == fit.band, "the corpus records the measured band",
          f"{committed.get('band')} against {fit.band}")
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

print(f"picrossprov: {checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
PY
