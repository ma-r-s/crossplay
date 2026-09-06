"""Will this Picross name render at full size on the panel?

THE ONE PLACE THAT ANSWERS THAT, and it is imported rather than copied.
`gen_picross.py` is the gate -- a name that does not fit is a hard error on the
whole bank, so this decides what ships -- and `host-tests/picrossnames` re-asks
it of the header that actually shipped.

It used to have a second caller: a naming page that restated measure() in
JavaScript because it could not import Python, pinned to this one by the corpus
below. That page is deleted (the bank arrives titled and there is nothing left
to name), so the corpus now pins this implementation to its own recorded output
-- which is still worth having, because glyph metrics MOVE when a Toybox cut is
regenerated and every accepted name changes width when they do.

WHY A WIDTH AND NOT A CHARACTER COUNT. A character count is the wrong
instrument for a variable-width font, and it is wrong in BOTH directions at
once: nine capital Ws is already over the band, while "CHRISTMAS TREE" is
fourteen characters and fits whole. A cap set safe for the worst string refuses
most good names; a cap set for an average one ships names the panel quietly
shrinks. Measuring answers the question actually being asked -- does this
render at full size -- and neither number has to be guessed.

WHY toybox::fittedTitle MAKES THIS MATTER. It does not clip an overlong name,
it SHRINKS it, walking down the font slots until one fits. Nothing logs that.
So the failure this guards against is not a broken screen, it is 137 reveals
set two-thirds size with nobody told.
"""

import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
FONTS = os.path.join(ROOT, "src", "apps_local", "ui", "fonts")

# The slots toybox::fittedTitle walks for a name set in kDisplayFont, largest
# first. Fitting at RUNGS[0] is what "renders at full size" means.
RUNGS = (
    ("title", "toybox_30"),  # FONT_SLOT_TITLE, the cut the win screen asks for
    ("body", "toybox_20"),  # FONT_SLOT_BODY
    ("small", "toybox_10"),  # FONT_SLOT_SMALL
)

# The width buildWin() gives the name. NOT a literal: measured from the real
# screen builder by tools_local/picross/measure_name_band.sh and left in
# name_band.txt, so moving the win screen's layout is one command away from
# being reflected here rather than a number nobody knows has gone stale.
BAND_FILE = os.path.join(HERE, "name_band.txt")

# What a name may contain. The panel has no fallback box -- a glyph the cut
# lacks draws NOTHING and advances the pen by nothing (see measure()), so a
# name carrying one is a HOLE in the word, and it measures as a comfortable
# fit. Printable ASCII is exactly the range the toybox cuts cover.
FIRST_CP = 0x20
LAST_CP = 0x7E


def band_width():
    """The name band, in pixels. Refuses to guess."""
    try:
        with open(BAND_FILE, encoding="utf-8") as f:
            text = f.read().strip()
    except OSError:
        raise SystemExit(
            "%s is missing. Run tools_local/picross/measure_name_band.sh -- a name "
            "check against a guessed band is worse than no check, because it reports "
            "clean." % os.path.relpath(BAND_FILE, ROOT)
        )
    if not text.isdigit():
        raise SystemExit("%s does not hold a width: %r" % (os.path.relpath(BAND_FILE, ROOT), text))
    return int(text)


def load_font(name):
    """-> {codepoint: {adv, w, left}} for printable ASCII. `adv` is 12.4 fixed point.

    Three numbers per glyph, not one, because the device reports the width of
    the INK BOX and not the sum of the advances. See measure().
    """
    with open(os.path.join(FONTS, "%s.h" % name), encoding="utf-8") as f:
        src = f.read()
    start = src.index("%sGlyphs[] = {" % name)
    glyphs = [
        tuple(int(x, 0) for x in m)
        for m in re.findall(
            r"\{\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+)\s*\}",
            src[start : src.index("\n};", start)],
        )
    ]
    start = src.index("%sIntervals[] = {" % name)
    intervals = [
        tuple(int(x, 0) for x in m)
        for m in re.findall(
            r"\{\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+)\s*\}",
            src[start : src.index("\n};", start)],
        )
    ]
    # Kerning would have to be replicated too, and none of these cuts has any.
    # REFUSE rather than measure a cut whose pairs move: EpdFont folds the kern
    # into the same accumulator as the advance, so ignoring it would make every
    # answer here quietly wrong by a few pixels per pair -- in the unsafe
    # direction for a name that is nearly too wide.
    if "kernLeftCodepoints" in src and "nullptr,  // kernLeftCodepoints" not in src:
        raise SystemExit("%s: this cut has kerning data and measure() does not replicate kerning" % name)

    metrics = {}
    for cp in range(FIRST_CP, LAST_CP + 1):
        for first, last, offset in intervals:
            if first <= cp <= last:
                g = glyphs[offset + (cp - first)]
                metrics[cp] = {"adv": g[2], "w": g[0], "left": g[3]}
                break
    missing = [cp for cp in range(FIRST_CP, LAST_CP + 1) if cp not in metrics]
    if missing:
        raise SystemExit("%s: no glyph for printable ASCII %r" % (name, missing))
    return metrics


def measure(text, metrics):
    """-> (width_px, holes). EpdFont::getTextBounds RESTATED, not approximated.

    The difference is not academic, and it points the unsafe way. What the
    device reports is the width of the INK BOX (maxX - minX in
    getTextDimensions), which is not the sum of the advances, because:

      * each advance is rounded to a whole pixel AS IT IS ACCUMULATED
        (`lastBaseX += fp4::toPixel(prevAdvanceFP)`), so the fractions never
        get to cancel out;
      * the box runs from the first glyph's left side bearing to the last
        glyph's RIGHT EDGE, so the last glyph contributes its bitmap rather
        than its advance.

    Summing float advances put sixteen capital As at 488px against the device's
    493 -- and under-reporting is exactly the direction that says "fits at full
    size" for a name the panel sets a cut down. Measured on hardware by the
    session that built the naming tool (card #391); this is that measurement's
    algorithm, not a second guess at it.

    `holes` is every character the cut has no glyph for. Such a character draws
    NOTHING and advances by nothing, so it is REPORTED rather than measured --
    otherwise a broken string measures as a comfortable fit.
    """
    pen = 0
    # minX and maxX start at ZERO and are only pushed outward, exactly as
    # getTextDimensions initialises them. They are not seeded from the first
    # glyph.
    min_x = 0
    max_x = 0
    prev_adv = None  # 12.4 fixed point, or None before the first glyph
    holes = []
    for char in text:
        cp = ord(char)
        if cp < FIRST_CP or cp > LAST_CP:
            holes.append(char)
            # The device flushes the pending advance when a glyph is missing
            # and then advances by nothing for it, which is why a hole costs no
            # width.
            if prev_adv is not None:
                pen += round_half_up(prev_adv)
            prev_adv = 0
            continue
        g = metrics[cp]
        if prev_adv is not None:
            pen += round_half_up(prev_adv)
        left = pen + g["left"]
        right = left + g["w"]
        min_x = min(min_x, left)
        max_x = max(max_x, right)
        prev_adv = g["adv"]
    return max_x - min_x, holes


def round_half_up(fp4):
    """fp4::toPixel: 12.4 fixed point to whole pixels, halves rounding up.

    Python's round() is banker's rounding (round(0.5) == 0) and JavaScript's
    Math.round is half-up. They disagree on exactly the values a 12.4 advance
    lands on most often, which is how two "identical" implementations differ by
    a pixel per glyph.
    """
    return (fp4 + 8) // 16


class Fit:
    """How a name lands: `rung` is the slot it fits in, or None if none does."""

    def __init__(self, rung, width, holes):
        self.rung = rung
        self.width = width
        self.holes = holes

    @property
    def full_size(self):
        return self.rung == RUNGS[0][0]


def fitter():
    """-> a callable name -> Fit, with the fonts and the band loaded once."""
    band = band_width()
    fonts = [(slot, load_font(cut)) for slot, cut in RUNGS]

    def fit(name):
        first_width = None
        holes = []
        for slot, metrics in fonts:
            width, holes = measure(name, metrics)
            if first_width is None:
                first_width = width
            if width <= band:
                return Fit(slot, first_width, holes)
        return Fit(None, first_width, holes)

    fit.band = band
    fit.fonts = fonts
    return fit


def corpus():
    """Strings with their measured width in every cut, for pinning the tool's
    JavaScript restatement of measure() to this one.

    Chosen to exercise what a sum of advances gets wrong rather than to look
    like names: repeated wide glyphs (where per-glyph rounding accumulates),
    single glyphs and pairs (where the last glyph's bitmap rather than its
    advance ends the box), the space and the punctuation the file allows, and
    the run that was measured on hardware.
    """
    fonts = [(slot, load_font(cut)) for slot, cut in RUNGS]
    words = [
        "A", "W", "I", "AA", "WW", "II", "A A", "W W",
        "A" * 16, "W" * 12, "I" * 20,
        "CHRISTMAS TREE", "BUTTERFLY", "O'CLOCK", "T-REX", "MARIO'S",
        " ", "  ", "!", "~", "0", "00",
    ]
    out = {}
    for slot, metrics in fonts:
        out[slot] = {w: measure(w, metrics)[0] for w in words}
    return out


def write_corpus():
    """Write name_fit_corpus.json: the pin between this and the naming tool's JS.

    The deleted naming page could not import Python, so its logic.js restated
    measure() in JavaScript. Two implementations of a rule that must agree to
    the pixel is the drift this fork keeps paying for, so they are not left to
    agree by inspection: this file is the corpus both check against.

    host-tests/picrossnames asserts these numbers are still what measure()
    computes, so a change here that moves them goes red rather than quietly
    disagreeing with the browser. host-tests/picrossnames asserts logic.js
    reproduces them.
    """
    import json

    path = os.path.join(HERE, "name_fit_corpus.json")
    payload = {
        "_comment": (
            "GENERATED by tools_local/picross/name_fit.py --corpus. The width "
            "EpdFont::getTextBounds reports for each string in each cut the win "
            "screen's fittedTitle walks. It exists so the naming tool's "
            "JavaScript restatement of measure() and the generator's Python one "
            "cannot drift apart: both check against this."
        ),
        "band": band_width(),
        "widths": corpus(),
    }
    with open(path, "w", encoding="utf-8") as f:
        f.write(json.dumps(payload, indent=1, sort_keys=True) + "\n")
    return path


if __name__ == "__main__":
    import sys as _sys

    if "--corpus" in _sys.argv:
        print("wrote %s" % os.path.relpath(write_corpus(), ROOT))
    else:
        _fit = fitter()
        for _arg in _sys.argv[1:]:
            _landed = _fit(_arg)
            print(
                "%-20r %4dpx  %s%s"
                % (
                    _arg,
                    _landed.width,
                    _landed.rung or "TOO WIDE FOR EVERY CUT",
                    "  holes: %r" % _landed.holes if _landed.holes else "",
                )
            )
