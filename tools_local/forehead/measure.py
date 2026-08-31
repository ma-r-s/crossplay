#!/usr/bin/env python3
"""Measure strings in a Toybox cut, in real panel pixels.

    ./tools_local/forehead/measure.py 20 'RESET EVERYTHING' 'ROUND'

Every overflow this app has had came from counting CHARACTERS. "HOLD IT ON YOUR
FOREHEAD" and "EASY ONES FOR SMALL ONES" are both 24 characters and differ by 69
pixels, so a character cap either rejects strings that fit or passes strings that
do not -- and on this panel the second is invisible: the truncation glyph is
U+2026, Jersey has no U+2026, and a missing glyph draws as nothing at all. The
sentence simply stops.

Shares the generator's reader rather than repeating it, so there is one
definition of "how wide is this" in the app.
"""
import sys

from gen_forehead_words import pixels

if __name__ == "__main__":
    size = int(sys.argv[1])
    for arg in sys.argv[2:]:
        print(f"{round(pixels(arg, size)):5}px  {arg!r}")
