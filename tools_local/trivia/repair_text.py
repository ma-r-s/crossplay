#!/usr/bin/env python3
"""Undo the compass expansion that ate the corpus's initials.

Somewhere upstream a "N./S./E./W. -> North/South/East/West" replacement ran
over text that also contained people's initials, so the pack ships

    U.South Army        for  U.S. Army
    T.South Eliot       for  T.S. Eliot
    P.West Botha        for  P.W. Botha
    A.East Housman      for  A.E. Housman

280 occurrences in 27 distinct forms across the 50,000. Every one is visible
to a player and none of them is a judgement call.

WHY THIS IS A REGEX AND NOT THE MODEL'S `bad` FLAG. The flag looked like it
handled this: handed "This U.South Army general led the Army of Northern
Virginia" it answers bad=True, and that was the evidence used to claim the
class was covered. Measured against the eleven real instances the run had
reached, it fired on ONE. Nine percent recall. A probe built from one
hand-written example agreed with the hypothesis and the population did not,
which is the whole argument for enumerating a class before believing a flag
covers it.

The rule only fires on a single letter followed by a dot and a capitalised
compass word, which is the shape initials make and the shape ordinary prose
does not: "Miami.South Beach" keeps its word because "Miami" is not one letter.
"""
import re

_COMPASS = {"North": "N", "South": "S", "East": "E", "West": "W"}
# <single letter> . <compass word>, and not preceded by another letter, so
# "U.S.South Missouri" repairs its tail without touching the "U.S." in front.
_RE = re.compile(r"(?<![A-Za-z])([A-Z])\.(North|South|East|West)\b")


def repair(s):
    return _RE.sub(lambda m: f"{m.group(1)}.{_COMPASS[m.group(2)]}.", s or "")


if __name__ == "__main__":
    import json, sys
    if len(sys.argv) != 2:
        sys.exit("usage: repair_text.py <corpus.jsonl>   (writes repaired jsonl to stdout)")
    n = 0
    for line in open(sys.argv[1], encoding="utf-8"):
        x = json.loads(line)
        q, a = repair(x["q"]), repair(x["a"])
        if (q, a) != (x["q"], x["a"]):
            n += 1
        x["q"], x["a"] = q, a
        sys.stdout.write(json.dumps(x, ensure_ascii=False, separators=(",", ":")) + "\n")
    print(f"{n:,} questions repaired", file=sys.stderr)
