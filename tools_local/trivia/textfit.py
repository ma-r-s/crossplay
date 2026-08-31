#!/usr/bin/env python3
"""Will this clue fit the box, measured against the real font.

A CHARACTER cap cannot express this. "HOLD IT ON YOUR FOREHEAD" and "EASY ONES
FOR SMALL ONES" are both 24 characters and differ by 69 pixels in the same face,
so a character cap either rejects text that fits or passes text that does not --
and on a 1-bit panel the second is invisible: the line is truncated with U+2026,
Jersey has no glyph for it, and the sentence simply stops mid-thought.

A clue is a sentence, not a label, so what matters is the WRAPPED height against
the box, not a single-line width. Advances come out of the generated font header
as 12.4 fixed point (sixteenths of a pixel); reading them as whole pixels makes
a hand-rolled version come out 16x too wide.
"""
import functools, pathlib, re

_REPO = pathlib.Path(__file__).resolve().parents[2]
# Two font trees. The builtin faces are upstream's; the fork's own cuts (the
# reading serif an app's prose is actually set in) live under apps_local.
FONT_DIRS = (_REPO / "src/apps_local/ui/fonts", _REPO / "lib/EpdFont/builtinFonts")

def _font_path(name):
    for d in FONT_DIRS:
        p = d / f"{name}.h"
        if p.exists():
            return p
    raise FileNotFoundError(f"no font header for {name} in {[str(d) for d in FONT_DIRS]}")

@functools.lru_cache(maxsize=8)
def font(name):
    """Codepoint -> advance in pixels, plus the ascender."""
    text = _font_path(name).read_text(errors="replace")
    glyphs = []
    block = re.search(r"Glyphs\[\] = \{(.*?)\n\};", text, re.S)
    for row in re.finditer(r"\{\s*(\d+),\s*(\d+),\s*(\d+),", block.group(1)):
        glyphs.append(int(row.group(3)) / 16.0)          # 12.4 fixed point
    advances = {}
    block = re.search(r"Intervals\[\] = \{(.*?)\n\};", text, re.S)
    for row in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)",
                           block.group(1)):
        start, end, first = (int(g, 16) for g in row.groups())
        for cp in range(start, end + 1):
            i = first + (cp - start)
            if i < len(glyphs):
                advances[cp] = glyphs[i]
    init = re.search(rf"EpdFontData {name} = \{{(.*?)\n\}};", text, re.S).group(1)
    nums = re.findall(r"^\s*(-?\d+),\s*$", init, re.M)
    # advanceY, ascender, descender -- drawText advances by the ASCENDER
    return advances, int(nums[2])

def text_width(s, advances):
    return sum(advances.get(ord(c), 0.0) for c in s)

def wrapped_lines(text, advances, max_width):
    """Greedy word wrap, hard-breaking a single word too wide for the line."""
    lines, cur = 0, ""
    for word in text.split():
        trial = f"{cur} {word}" if cur else word
        if text_width(trial, advances) <= max_width:
            cur = trial
            continue
        if cur:
            lines += 1
        while text_width(word, advances) > max_width:
            cut = len(word)
            while cut > 1 and text_width(word[:cut], advances) > max_width:
                cut -= 1
            lines += 1
            word = word[cut:]
        cur = word
    if cur:
        lines += 1
    return lines

def fits(text, max_width, max_height, font_name):
    advances, ascender = font(font_name)
    return wrapped_lines(text, advances, max_width) * ascender <= max_height

def height_px(text, max_width, font_name):
    advances, ascender = font(font_name)
    return wrapped_lines(text, advances, max_width) * ascender
