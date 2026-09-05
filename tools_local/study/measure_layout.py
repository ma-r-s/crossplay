#!/usr/bin/env python3
"""How tall is each card's answer side, and how much room is left under it?

Written to answer one question honestly: is there space for the sentence image
that 290 of 301 cards carry, or is that only true of the short cards?

Guessing at it was not good enough. A first pass using estimated line heights
was 70% low, because `drawText` advances by the font's *ascender*
(GfxRenderer::getTextHeight returns exactly that) and the built-in serif's
ascenders are 27, 36 and 41 pixels at 12, 16 and 18 point -- not the ~20 a
glance at the type would suggest. So this reads the real numbers: glyph
advances out of the .cpfont files for the CJK, and out of the generated
builtin-font headers for the Latin, and walks the same wrap the renderer walks.

    .venv-study/bin/python tools_local/study/measure_layout.py \\
        fs_mario/study/mandarin --fonts fs_mario/study/fonts
"""

import argparse
import pathlib
import re
import statistics
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import scripts  # noqa: E402

# The device's layout, from StudyActivity.cpp.
SCREEN_W, SCREEN_H = 480, 800
HEADER, FOOTER, MARGIN = 89, 128, 16
MAX_WIDTH = SCREEN_W - 2 * MARGIN
BODY = SCREEN_H - HEADER - FOOTER

# Gaps drawWrapped's caller inserts between blocks, and the rule.
GAP_AFTER_HEADWORD, GAP_AFTER_READING = 6, 2
RULE_ABOVE, RULE_BELOW = 20, 20
GAP_AFTER_SENTENCE, GAP_AFTER_SENTENCE_READING = 6, 2
TOP_INSET = 8

REPO = pathlib.Path(__file__).resolve().parents[2]
BUILTIN = REPO / "lib/EpdFont/builtinFonts"


def builtin_font(name):
    """Codepoint -> advance in pixels, plus the ascender, from a generated header.

    The header is C, but the two tables are regular enough to read directly:
    an interval list of (start, end, firstGlyphIndex) and a glyph array whose
    third field is a 12.4 fixed-point advance.
    """
    text = (BUILTIN / f"{name}.h").read_text(errors="replace")

    glyphs = []
    block = re.search(r"Glyphs\[\] = \{(.*?)\n\};", text, re.S)
    for row in re.finditer(r"\{\s*(\d+),\s*(\d+),\s*(\d+),", block.group(1)):
        glyphs.append(int(row.group(3)) / 16.0)

    advances = {}
    block = re.search(r"Intervals\[\] = \{(.*?)\n\};", text, re.S)
    for row in re.finditer(
        r"\{\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)", block.group(1)
    ):
        start, end, first = (int(g, 16) for g in row.groups())
        for cp in range(start, end + 1):
            index = first + (cp - start)
            if index < len(glyphs):
                advances[cp] = glyphs[index]

    # The EpdFontData initialiser: bitmap, glyph, intervals, intervalCount,
    # advanceY, ascender, descender, ...
    init = re.search(rf"EpdFontData {name} = \{{(.*?)\n\}};", text, re.S).group(1)
    numbers = re.findall(r"^\s*(-?\d+),\s*$", init, re.M)
    ascender = int(numbers[2])
    return advances, ascender


def cpfont(path):
    data = path.read_bytes()
    (_sid, n_iv, n_gl, _advY, _asc, _desc, _kl, _kr, _klc, _krc, _lig, off) = (
        struct.unpack_from("<B3xIIBhhHHBBBI4x", data, 32)
    )
    intervals = [struct.unpack_from("<III", data, off + i * 12) for i in range(n_iv)]
    base = off + n_iv * 12
    advances = {}
    for start, end, first in intervals:
        for cp in range(start, end + 1):
            gi = first + (cp - start)
            if gi < n_gl:
                w, h, adv, _l, _t, _r, _o = struct.unpack_from(
                    "<BBHhhH2xI", data, base + gi * 16
                )
                if w or h or adv:
                    advances[cp] = adv / 16.0
    return advances, _asc


# The device breaks beside a wide character and at a space; scripts.py owns
# that rule so this measurement and the wrap it mirrors cannot drift.
def is_cjk(ch):
    return scripts.is_wide(ch)


def wrapped_height(text, advances, ascender):
    """Same greedy wrap StudyActivity::drawWrapped does, in whole lines."""
    if not text:
        return 0
    lines, width = 1, 0.0
    for ch in text:
        adv = advances.get(ord(ch), 0.0)
        if width + adv > MAX_WIDTH and width > 0:
            lines += 1
            width = 0.0
        width += adv
    return lines * ascender


def read_deck(path):
    data = path.read_bytes()
    _ver, fields, _flags, count = struct.unpack_from("<HBBI", data, 8)
    index = struct.unpack_from("<%dI" % (count + 1), data, 16)
    for i in range(count):
        off = index[i]
        note = []
        for f in range(fields):
            (ln,) = struct.unpack_from("<H", data, off)
            off += 2
            raw = data[off : off + ln]
            off += ln
            if f == 4:
                raw = raw[:-2]
            note.append(raw.decode("utf-8"))
        yield i, note


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("deck", type=pathlib.Path)
    ap.add_argument("--fonts", type=pathlib.Path, required=True)
    ap.add_argument("--headword-size", type=int, default=50)
    ap.add_argument("--sentence-size", type=int, default=17)
    args = ap.parse_args()

    families = sorted(p.name for p in args.fonts.iterdir() if p.is_dir())
    if not families:
        sys.exit(f"no font families under {args.fonts}")

    serif18, asc18 = builtin_font("notoserif_18_regular")
    serif16, asc16 = builtin_font("notoserif_16_regular")
    serif12, asc12 = builtin_font("notoserif_12_regular")
    print(
        f"built-in serif ascenders (the line advance): 18pt={asc18} 16pt={asc16} 12pt={asc12}"
    )

    heights = []
    worst = []
    for family in families:
        hw, hw_asc = cpfont(
            args.fonts / family / f"{family}_{args.headword_size}.cpfont"
        )
        st, st_asc = cpfont(
            args.fonts / family / f"{family}_{args.sentence_size}.cpfont"
        )
        if family == families[0]:
            print(f"CJK ascenders: headword={hw_asc} sentence={st_asc}\n")
        for i, n in read_deck(args.deck / "deck.dat"):
            h = TOP_INSET
            h += wrapped_height(n[0], hw, hw_asc)
            h += GAP_AFTER_HEADWORD + wrapped_height(n[1], serif18, asc18)
            h += GAP_AFTER_READING + wrapped_height(n[2], serif16, asc16)
            h += wrapped_height(n[3], serif12, asc12)
            if n[4]:
                h += RULE_ABOVE + RULE_BELOW
                h += wrapped_height(n[4], st, st_asc)
                h += GAP_AFTER_SENTENCE + wrapped_height(n[5], serif16, asc16)
                h += GAP_AFTER_SENTENCE_READING + wrapped_height(n[6], serif16, asc16)
            heights.append(h)
            worst.append((h, i, family, n[0], n[2][:40]))

    worst.sort(reverse=True)
    spare = [BODY - h for h in heights]
    print(
        f"body available: {BODY}px  (screen {SCREEN_H} - header {HEADER} - footer {FOOTER})\n"
    )
    print(
        f"answer-side height   min {min(heights)}  median {statistics.median(heights):.0f}  max {max(heights)}"
    )
    print(
        f"spare below content  min {min(spare)}  median {statistics.median(spare):.0f}  max {max(spare)}\n"
    )

    for label, limit in (
        ("overflow the screen", 0),
        ("leave under 80px", 80),
        ("leave under 160px", 160),
    ):
        n = sum(1 for s in spare if s < limit)
        print(f"  card/face combinations that {label:22}: {n:5} of {len(spare)}")

    print("\ntallest cards:")
    for h, i, family, hw, mean in worst[:5]:
        print(f"  {h:4}px ({BODY - h:+4}px spare)  note {i} {hw} in {family}  '{mean}'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
