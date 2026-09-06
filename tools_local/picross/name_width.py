#!/usr/bin/env python3
"""Will this puzzle name render on the win screen? The one implementation.

    from name_width import judge, fits, load
    verdict = judge("CHRISTMAS TREE", load())

Two things need this answer and they must not disagree by a pixel: the browser
tool Mario types names into (site/picross-names/), and gen_picross.py, which
refuses janko-names.json outright rather than shipping a name the panel cannot
draw. A second copy of a rule that must agree exactly is the drift this fork
keeps paying for, so this module is the rule and the JavaScript is held to it by
host-tests/picrossnames, which runs both over the same corpus and fails on any
disagreement.

WHY NOT A CHARACTER COUNT. `toybox::fittedTitle` does not clip a name that is
too wide, it SHRINKS it to the next cut, and nothing reports that -- so the
failure is silent and the only honest test is a width. A count has to assume the
worst glyph to be safe, and the cost of that is real: nine `W`s and nine `I`s
differ by more than threefold, so a cap that admits the first refuses
"CHRISTMAS TREE", which fits whole with room to spare.

WHY IT IS NOT A SUM OF ADVANCES. The device reports the width of the INK BOX
(`maxX - minX` in EpdFont::getTextDimensions), and two things in that a sum gets
wrong: each advance is rounded to a whole pixel AS IT IS ACCUMULATED, so the
fractions never cancel, and the box ends at the last glyph's right edge rather
than after its advance. Summing floats measured sixteen capital As at 488px
against the device's 493 -- and under-measuring is the direction that says
"this fits" for a name the device sets a cut down.
"""

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
FONTS = REPO / "src/apps_local/ui/fonts"
BAND_FILE = REPO / "tools_local/picross/name_band.txt"

# The cuts toybox::fittedTitle walks for a name set in kDisplayFont, largest
# first, under toybox::toyboxFaces(). Named by the slot they are bound to, so a
# reader can check them against ToyboxTheme.h rather than trusting this list.
CUT_FILES = (("title", "toybox_30"), ("body", "toybox_20"), ("small", "toybox_10"))

# The characters gen_picross.load_names accepts, and the tool with it. Every one
# of them is in U+0020..U+007E, which is all a Toybox cut carries; a codepoint
# outside it draws NOTHING -- a hole in the word, not a box.
ALLOWED = re.compile(r"^[A-Z0-9 '-]*$")


def load_font(cut):
    """-> per-glyph metrics for printable ASCII, straight out of the generated
    header. advanceX is 12.4 fixed point and is kept that way: EpdFont rounds
    each advance to a whole pixel as it accumulates, so anything measuring with
    these has to round in the same units at the same moments."""
    src = (FONTS / f"{cut}.h").read_text()
    start = src.index(f"{cut}Glyphs[] = {{")
    glyphs = [
        tuple(int(x, 0) for x in m)
        for m in re.findall(
            r"\{\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+)\s*\}",
            src[start : src.index("\n};", start)],
        )
    ]
    start = src.index(f"{cut}Intervals[] = {{")
    intervals = [
        tuple(int(x, 0) for x in m)
        for m in re.findall(
            r"\{\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+)\s*\}",
            src[start : src.index("\n};", start)],
        )
    ]
    # Kerning would have to be replicated too, and no toybox cut has any.
    # Refuse rather than measure a cut whose pairs move.
    if "kernLeftCodepoints" in src and "nullptr,  // kernLeftCodepoints" not in src:
        raise SystemExit(f"{cut}: this cut has kerning data and measure() does not replicate kerning")

    advance, width, left = [], [], []
    for cp in range(0x20, 0x7F):
        for first, last, offset in intervals:
            if first <= cp <= last:
                g = glyphs[offset + (cp - first)]
                advance.append(g[2])
                width.append(g[0])
                left.append(g[3])
                break
        else:
            raise SystemExit(f"{cut}: no glyph for U+{cp:04X}, so this cut cannot be measured with")
    return {"cut": cut, "advance": advance, "width": width, "left": left}


def load():
    """The metrics and the band, read from the SAME sources the device is built
    from: the generated font headers, and the width measure_name_band.sh got out
    of the real screen builder.

    Read from the headers rather than from site/picross-names/data.js on
    purpose. data.js is derived from these same files, and pointing a build-time
    gate at a generated site asset would make gen_picross.py depend on somebody
    having run gen_name_tool.py first -- a build that fails, or worse silently
    judges by stale metrics, because a browser tool was not regenerated."""
    band = BAND_FILE.read_text().strip()
    if not band.isdigit() or int(band) <= 0:
        raise SystemExit(f"{BAND_FILE}: no measured band width -- run tools_local/picross/measure_name_band.sh")
    return {"bandWidth": int(band), "fonts": {slot: load_font(cut) for slot, cut in CUT_FILES}}


def measure(text, cut, fonts):
    """-> (ink-box width in pixels, characters the cut has no glyph for).

    EpdFont::getTextBounds restated. Kerning is not replicated and does not need
    to be: no toybox cut ships any, and gen_name_tool.py refuses to generate
    from one that does."""
    font = fonts[cut]
    pen = 0
    min_x = 0
    max_x = 0
    prev_advance = None
    holes = []
    for ch in text:
        cp = ord(ch)
        if cp < 0x20 or cp > 0x7E:
            holes.append(ch)
            # The device flushes the pending advance for a missing glyph and
            # then advances by nothing for it, so a hole costs no width.
            if prev_advance is not None:
                pen += round_half_up(prev_advance / 16)
            prev_advance = 0
            continue
        g = cp - 0x20
        if prev_advance is not None:
            pen += round_half_up(prev_advance / 16)
        left = pen + font["left"][g]
        right = left + font["width"][g]
        min_x = min(min_x, left)
        max_x = max(max_x, right)
        prev_advance = font["advance"][g]
    return max_x - min_x, holes


def round_half_up(value):
    """fp4::toPixel is `(fp + HALF) >> FRAC_BITS`: half rounds UP, always.
    Python's round() is banker's rounding and would disagree on every exact
    half, which is one pixel per glyph on a face full of them."""
    import math

    return math.floor(value + 0.5)


# The cuts toybox::fittedTitle walks for a name set in kDisplayFont, largest
# first, under toybox::toyboxFaces().
CUTS = ("title", "body", "small")


def judge(name, data):
    """-> dict(level, text, cut). level is "ok" | "warn" | "stop".

    "warn" is a name the device will draw at a SMALLER cut than the screen asks
    for. It is not an error and the device handles it, but it is silent, so
    whoever is judging gets told."""
    if not name:
        return {"level": "ok", "text": "", "cut": 0}

    _w, holes = measure(name, "title", data["fonts"])
    if holes:
        seen = []
        for c in holes:
            if c not in seen:
                seen.append(c)
        quoted = ", ".join('"%s"' % c for c in seen)
        return {
            "level": "stop",
            "cut": None,
            "text": f"the display cut cannot draw {quoted}, and a glyph it lacks is a HOLE in the word, not a box",
        }

    if not ALLOWED.match(name):
        bad = []
        for c in name:
            if not ALLOWED.match(c) and c not in bad:
                bad.append(c)
        quoted = ", ".join('"%s"' % c for c in bad)
        return {
            "level": "stop",
            "cut": None,
            "text": f"{quoted} is not allowed in a puzzle name (A-Z, digits, space, hyphen, apostrophe)",
        }

    band = data["bandWidth"]
    for i, cut in enumerate(CUTS):
        width, _holes = measure(name, cut, data["fonts"])
        if width <= band:
            if i == 0:
                return {"level": "ok", "cut": 0, "text": f"{width} of {band}px at the display cut"}
            return {
                "level": "warn",
                "cut": i,
                "text": f"too wide for the display cut; the device will set it {i} cut(s) smaller",
            }
    return {"level": "stop", "cut": None, "text": "too long even at the smallest cut; the device would ellipsize it"}


def fits(name, data=None):
    """True when the device draws this name whole at the cut the screen asks
    for. The strict answer, and the one a gate wants."""
    return judge(name, data if data is not None else load())["level"] == "ok"


if __name__ == "__main__":
    import sys

    data = load()
    if len(sys.argv) > 1:
        for name in sys.argv[1:]:
            v = judge(name.upper(), data)
            print(f"{v['level']:5}  {name.upper()!r}  {v['text']}")
        raise SystemExit(0)

    # No arguments: say what the rule actually permits, which is the question
    # anyone reaching for a character cap is really asking.
    band = data["bandWidth"]
    print(f"name band: {band}px, display cut {data['fonts']['title']['cut']}")
    for ch in ("W", "M", "A", "E", "I", "1"):
        n = 0
        while judge(ch * (n + 1), data)["level"] == "ok":
            n += 1
        print(f"  {ch!r} x {n} is the most that fits at the display cut")
    for sample in ("CHRISTMAS TREE", "COCKTAIL GLASS", "SAILBOAT", "PIANO", "WWWWWWWWWW"):
        v = judge(sample, data)
        print(f"  {sample!r}: {v['level']} -- {v['text']}")
