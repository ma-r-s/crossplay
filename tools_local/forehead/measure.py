#!/usr/bin/env python3
"""Measure strings in a Toybox cut, in real panel pixels.

    ./tools_local/forehead/measure.py 20 'RESET EVERYTHING' 'ROUND'

Every overflow this app has had came from counting CHARACTERS. "HOLD IT ON YOUR
FOREHEAD" and "EASY ONES FOR SMALL ONES" are both 24 characters and differ by 69
pixels, so a character cap either rejects strings that fit or passes strings that
do not -- and on this panel the second is invisible: the SDK truncates with U+2026, and
the Toybox cuts DO NOT ALL CARRY IT. toybox_10 has the ellipsis; 14, 20, 30, 44
and 64 do not, and a glyph the face lacks draws as nothing at all. So an
overflow at the 10px cut ends in a visible "...", and at every larger cut the
sentence simply stops at a plausible place and looks deliberate.

Shares the generator's reader rather than repeating it, so there is one
definition of "how wide is this" in the app.
"""
import sys

from gen_forehead_words import pixels

if __name__ == "__main__":
    size = int(sys.argv[1])
    for arg in sys.argv[2:]:
        print(f"{round(pixels(arg, size)):5}px  {arg!r}")
