#!/usr/bin/env python3
"""Near-duplicate detector for the forehead word lists.

    ./tools_local/forehead/dupes.py            # report every category
    ./tools_local/forehead/dupes.py food       # one category

An EXACT duplicate inside a list was already refused. That check was never the
problem. What shipped was five kinds of pair the deck treats as two cards and a
room treats as one answer:

  plural     GRAPE / GRAPES          -- the room says "grape", both are right
  order      DOG BARKING / BARKING DOG
  subset     POPCORN / POPCORN POPPING
  article    LION KING / THE LION KING
  spelling   DONUT / DOUGHNUT, YOGURT / YOGHURT

Every one of these was MANUFACTURED by the revision, not inherited: curate.py's
ADD step tested `if entry not in have`, an exact string match, so it added the
variant of a word that was already there and the generator's exact-duplicate
check waved it through.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
WORDS = REPO / "tools_local/forehead/words"

# Pairs that differ only by one of these are the same answer in a shouting room.
SPELLINGS = [
    ("DONUT", "DOUGHNUT"),
    ("YOGURT", "YOGHURT"),
    ("OMELET", "OMELETTE"),
    ("GRAY", "GREY"),
    ("MOM", "MUM"),
]
STOPWORDS = {"THE", "A", "AN", "OF", "AND"}


def key_plural(entry):
    """GRAPES -> GRAPE, so a singular and its plural collide."""
    if entry.endswith("IES") and len(entry) > 4:
        return entry[:-3] + "Y"
    if entry.endswith("ES") and len(entry) > 4 and entry[-3] in "SXZHO":
        return entry[:-2]
    if entry.endswith("S") and not entry.endswith("SS") and len(entry) > 3:
        return entry[:-1]
    return entry


def bag(entry):
    """The words that carry meaning, order and articles thrown away.

    Punctuation goes too: MRS DOUBTFIRE and MRS. DOUBTFIRE are one film, and the
    exact-match check that let them both in could not see it.
    """
    words = (re.sub(r"[.'-]", "", w) for w in entry.split())
    return frozenset(key_plural(w) for w in words if w and w not in STOPWORDS)


def find(entries):
    """Every (a, b, why) pair that is really one answer."""
    out = []
    by_bag = {}
    for e in entries:
        b = bag(e)
        if not b:
            continue
        for other in by_bag.get(b, []):
            if len(e.split()) == len(other.split()):
                why = "plural" if key_plural(e) == key_plural(other) else "order"
            else:
                why = "article"
            out.append((other, e, why))
        by_bag.setdefault(b, []).append(e)

    # Subsets: one entry's whole meaning sitting inside another's.
    bags = [(e, bag(e)) for e in entries]
    for i, (a, ba) in enumerate(bags):
        for b, bb in bags[i + 1 :]:
            if ba == bb or not ba or not bb:
                continue
            if ba < bb or bb < ba:
                out.append((a, b, "subset"))

    for x, y in SPELLINGS:
        if x in entries and y in entries:
            out.append((x, y, "spelling"))
    return out


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    total = 0
    for path in sorted(WORDS.glob("*.txt")):
        if only and path.stem != only:
            continue
        entries = [
            line.strip()
            for line in path.read_text().splitlines()
            if line.strip() and not line.startswith("#")
        ]
        pairs = find(entries)
        if not pairs:
            continue
        total += len(pairs)
        print(f"{path.stem} ({len(pairs)})")
        for a, b, why in sorted(pairs, key=lambda p: p[2]):
            print(f"  {why:9} {a!r} / {b!r}")
    print(f"\n{total} pairs")


if __name__ == "__main__":
    main()
