#!/usr/bin/env python3
"""Everything the panel will draw, measured against the box it has to fit in.

This is the check nothing else can make. A C++ host test cannot parse a font
header, and a screenshot only shows the state somebody photographed -- so a label
a glyph too wide for its key, or a result one digit too long for the display,
crosses its border in a state nobody rendered and the only symptom is that it
looks wrong. Both have already happened here: Ubuntu Bold's "DEL" came within a
pixel and a half of its key's border, Noto Serif's was fifteen pixels WIDER than
its key, and a seven-character result was landing three rungs down the display's
ladder.

It checks the invisible half too: a codepoint the face has no glyph for draws as
NOTHING AT ALL on this renderer, so it is reported rather than silently costing
zero width.

    host-tests/calculator/label_fit.py <table from test_calculator --labels>
"""

import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
FONTS = REPO / "src/apps_local/calculator/fonts"

# Read from CalcFontIds.h rather than copied, so a renumbered font cannot make
# this script quietly measure the wrong face.
IDS = {}
for line in (REPO / "src/apps_local/calculator/CalcFontIds.h").read_text().splitlines():
    m = re.match(r"constexpr int k(\w+)FontId = (0x[0-9A-Fa-f']+);", line.strip())
    if m:
        IDS[int(m.group(2).replace("'", ""), 16)] = m.group(1)

FILES = {
    "Label": "calc_jersey_28",
    "Small": "calc_jersey_20",
    "Number": "calc_jersey_56",
    "Mid": "calc_jersey_44",
    "Tiny": "calc_jersey_34",
    "Finest": "calc_jersey_26",
}
_missing = sorted(set(IDS.values()) - set(FILES))
assert not _missing, f"label_fit.py has no file for: {', '.join(_missing)}"

# A label has to sit inside its key with air on both sides. Ten per cent a side is
# the least that still reads as a key with a glyph in it rather than a glyph with
# a box round it.
SIDE_AIR = 0.10


def load_font(name):
    src = (FONTS / f"{name}.h").read_text()
    start = src.index(f"{name}Glyphs[] = {{")
    glyphs = [
        tuple(int(x, 0) for x in m)
        for m in re.findall(
            r"\{\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),\s*(-?\w+)\s*\}",
            src[start : src.index("\n};", start)],
        )
    ]
    start = src.index(f"{name}Intervals[] = {{")
    intervals = [
        tuple(int(x, 0) for x in m)
        for m in re.findall(
            r"\{\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+)\s*\}",
            src[start : src.index("\n};", start)],
        )
    ]
    return glyphs, intervals


def advance_y(name):
    """EpdFontData's fifth field: the face's own baseline-to-baseline advance."""
    src = (FONTS / f"{name}.h").read_text()
    body = src[src.index(f"static const EpdFontData {name} = {{") :]
    body = body[: body.index("};")]
    fields = [x.strip().rstrip(",") for x in body.splitlines()[1:] if x.strip()]
    return int(fields[4])


def cap_height(font):
    """The ink height of a digit -- what a reader judges the size by."""
    glyphs, intervals = font
    for first, last, offset in intervals:
        if first <= ord("8") <= last:
            return glyphs[offset + (ord("8") - first)][1]
    return 0


def widest_glyph(font, chars):
    return max(measure(c, font)[0] for c in chars)


def measure(text, font):
    """-> (pixels, missing). advanceX is 12.4 fixed point (EpdFontData.h)."""
    glyphs, intervals = font
    total = 0.0
    missing = []
    for ch in text:
        cp = ord(ch)
        for first, last, offset in intervals:
            if first <= cp <= last:
                total += glyphs[offset + (cp - first)][2] / 16.0
                break
        else:
            missing.append(ch)
    return total, missing


CACHE = {}


def font_for(font_id):
    if font_id not in CACHE:
        CACHE[font_id] = load_font(FILES[IDS[font_id]])
    return CACHE[font_id]


def check_cut_metrics():
    """CalcCutMetrics.h against the real font headers.

    Those constants are not decoration: the display's height is DERIVED from
    them, so a regenerated cut that moves a line height by a pixel moves every
    band on the screen and takes the difference out of the key rows. This is what
    makes the claim in that header true rather than hopeful.
    """
    src = (REPO / "src/apps_local/calculator/CalcCutMetrics.h").read_text()
    declared = {
        m[0]: (int(m[1]), int(m[2]))
        for m in re.findall(r"constexpr CutMetrics k(\w+)Cut\{(\d+), (\d+)\}", src)
    }
    checks = failed = 0
    for key, (line_h, cap) in sorted(declared.items()):
        name = FILES.get(key)
        if name is None:
            failed += 1
            print(f"FAIL label_fit  CalcCutMetrics.h declares k{key}Cut and no font is mapped to it")
            continue
        font = load_font(name)
        checks += 2
        if advance_y(name) != line_h:
            failed += 1
            print(f"FAIL label_fit  k{key}Cut says lineHeight {line_h}, {name}.h has {advance_y(name)}")
        if cap_height(font) != cap:
            failed += 1
            print(f"FAIL label_fit  k{key}Cut says capHeight {cap}, {name}.h has {cap_height(font)}")
    for key in sorted(set(FILES) - set(declared)):
        checks += 1
        failed += 1
        print(f"FAIL label_fit  {FILES[key]} has no CutMetrics entry, so any layout using it is guessed")
    return checks, failed


def main():
    table = pathlib.Path(sys.argv[1]).read_text().splitlines()
    checks, failed = check_cut_metrics()
    cell_w = display_w = 0
    rungs = []
    worst = []

    for line in table:
        parts = line.split(" ")
        if parts[0] == "PAD":
            cell_w, display_w = int(parts[1]), int(parts[3])
        elif parts[0] == "RUNG":
            rungs.append((int(parts[1]), int(parts[2])))
        elif parts[0] == "LABEL":
            font_id, label = int(parts[1]), line.split(" ", 2)[2]
            width, missing = measure(label, font_for(font_id))
            budget = cell_w * (1 - 2 * SIDE_AIR)
            checks += 2
            if missing:
                failed += 1
                print(f"FAIL label_fit  key \"{label}\": {IDS[font_id]} has no glyph for "
                      f"{' '.join('U+%04X' % ord(c) for c in missing)} -- it would draw as nothing")
            if width > budget:
                failed += 1
                print(f"FAIL label_fit  key \"{label}\" is {width:.0f}px in a {cell_w}px key "
                      f"(budget {budget:.0f}px with {int(SIDE_AIR * 100)}% air a side)")
        elif parts[0] == "WORST":
            worst.append((int(parts[1]), line.split(" ", 2)[2]))
        elif parts[0] == "PENDING":
            text = line.split(" ", 1)[1]
            small = [fid for fid, n in IDS.items() if n == "Small"][0]
            width, missing = measure(text, font_for(small))
            checks += 1
            if width > display_w or missing:
                failed += 1
                print(f"FAIL label_fit  the pending line \"{text}\" is {width:.0f}px "
                      f"in a {display_w}px display")

    # The engine's own output, measured. Not samples somebody thought of: these
    # are the strings the engine produced when the suite drove it to its limits,
    # so a formatting change that lengthens a result cannot slip past.
    for fixed_font, text in worst:
        checks += 2
        # An error is words in the label cut and has its own budget: it only has
        # to fit the display, not the sixteen-character number bound.
        if not fixed_font and len(text) > MAX_CHARS:
            failed += 1
            print(f"FAIL label_fit  the engine emitted \"{text}\", {len(text)} characters, "
                  f"over its own {MAX_CHARS} character bound")
        landed = None
        for rung, font_id in ([(0, fixed_font)] if fixed_font else sorted(rungs)):
            w, missing = measure(text, font_for(font_id))
            if missing:
                failed += 1
                print(f"FAIL label_fit  {IDS[font_id]} has no glyph for "
                      f"{' '.join('U+%04X' % ord(c) for c in missing)} in \"{text}\"")
                break
            if w <= display_w:
                landed = (rung, font_id, w)
                break
        if landed is None:
            failed += 1
            print(f"FAIL label_fit  \"{text}\" does not fit a {display_w}px display at ANY rung "
                  f"-- the panel would clip it")

    print(f"{'FAILED' if failed else 'ok'}: {checks} checks, {failed} failed")
    return 1 if failed else 0


# Read from the source rather than restated: the bound is the engine's, and a
# second copy of it here would pass while the engine broke it.
MAX_CHARS = int(
    re.search(r"constexpr int kMaxDisplayChars = (\d+);",
              (REPO / "src/apps_local/calculator/CalcLayout.h").read_text()).group(1)
)

if __name__ == "__main__":
    sys.exit(main())
