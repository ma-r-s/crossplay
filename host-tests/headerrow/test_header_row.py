"""header_row_check.py against headers with known answers.

Each case draws a header the way the themes and Toybox do -- the theme's real
back arrow (read from headerIcons.h by the tool itself), a title as a run of
glyph blocks, a battery in the corner, body text under the band -- saves it,
and asks the tool. Captured at 1x and at the simulator's 2x, because the tool
measures in logical pixels and the 2x case is where it used to be wrong.

The cases are the ones that happened:
  - Study's count 12px above its arrow (2026-11-17)          -> fails
  - Study's count drawn over its arrow, Classic, 1.15.0      -> fails
  - a mixed-case upstream title 2.5 logical px off, at 2x    -> passes
    (it failed at 5 capture pixels against a 3px limit)
  - a band with no rule under it (Classic, RoundedRaff)      -> measured
"""

import importlib.util
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

TOOL = Path(__file__).resolve().parents[2] / "tools_local" / "header_row_check.py"
spec = importlib.util.spec_from_file_location("header_row_check", TOOL)
hrc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hrc)

checks = 0
failures = 0


def check(cond, label):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print(f"  FAIL: {label}")


ICON_X, ICON_Y = 8, 45  # the arrow's 32x32 icon box, as the theme places it
INK = hrc.ARROW_INK
ARROW_MID = ICON_Y + (min(y for _, y in INK) + max(y for _, y in INK) + 1) / 2


def glyphs(draw, x, top, n=5, height=14, width=10, gap=2, fill=0):
    for i in range(n):
        gx = x + i * (width + gap)
        draw.rectangle([gx, top, gx + width - 1, top + height - 1], fill=fill)


def header(
    title_dy=0.0,
    title_x=None,
    rule=False,
    arrow=True,
    over=False,
    two_lines=False,
):
    """A white theme band at 1x: arrow, title beside it, battery, body. The
    title's ink is centred on the arrow's, moved down by `title_dy`."""
    im = Image.new("L", (480, 800), 255)
    d = ImageDraw.Draw(im)
    d.rectangle([430, 5, 455, 14], fill=0)  # the battery
    if arrow:
        for x, y in INK:
            im.putpixel((ICON_X + x, ICON_Y + y), 0)
    x = title_x if title_x is not None else ICON_X + 32 + 6
    if two_lines:
        top = round(ARROW_MID - 14 - 2 + title_dy)
        glyphs(d, x, top)
        glyphs(d, x, top + 18, n=7)
    else:
        top = round(ARROW_MID - 7 + title_dy)
        glyphs(d, x, top)
    if over:
        glyphs(d, ICON_X + 10, round(ARROW_MID - 7), n=1)
    if rule:
        d.rectangle([0, 92, 479, 94], fill=0)
    for row in range(130, 400, 30):  # the page under the band
        glyphs(d, 20, row, n=20, height=12)
    return im


def at2x(im):
    return im.resize((im.width * 2, im.height * 2), Image.NEAREST)


def run(im, name):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / f"{name}.png"
        im.save(path)
        return hrc.check(str(path))


def main():
    for scale_name, scale in (("1x", lambda im: im), ("2x", at2x)):
        # Unruled (Classic, RoundedRaff) and ruled (Lyra): both are measured.
        for rule in (False, True):
            band = "ruled" if rule else "unruled"
            measured, msg = run(scale(header(rule=rule)), "aligned")
            check(
                measured and msg is None,
                f"{scale_name} {band}: an aligned title is measured and passes ({msg})",
            )
            measured, msg = run(scale(header(rule=rule, title_dy=-12)), "high")
            check(
                measured and msg is not None and "12.0px apart" in msg,
                f"{scale_name} {band}: a title 12px above its arrow fails, by 12 ({msg})",
            )
            measured, msg = run(scale(header(rule=rule, title_x=ICON_X + 10)), "over")
            check(
                measured and msg is not None and "drawn over the back control" in msg,
                f"{scale_name} {band}: words starting on the arrow fail as drawn over it ({msg})",
            )
            measured, msg = run(scale(header(rule=rule, over=True)), "over2")
            check(
                measured and msg is not None and "drawn over the back control" in msg,
                f"{scale_name} {band}: one glyph on the arrow fails ({msg})",
            )
            measured, msg = run(scale(header(rule=rule, two_lines=True)), "wrapped")
            check(
                measured and msg is None,
                f"{scale_name} {band}: a wrapped title centred as a block passes ({msg})",
            )
        # A centred title: the arrow is looked at, nothing is beside it.
        measured, msg = run(scale(header(title_x=200)), "centred")
        check(
            measured and msg is None,
            f"{scale_name}: a centred title is measured for words on the arrow only",
        )
        # No back control: not counted as measured.
        measured, msg = run(scale(header(arrow=False)), "noarrow")
        check(
            not measured and msg is None,
            f"{scale_name}: a band with no back control is not counted",
        )

    # The 2x case the old limit failed: 2.5 logical pixels is 5 capture pixels.
    im = at2x(header())
    big = Image.new("L", im.size, 255)
    big.paste(im)
    d = ImageDraw.Draw(big)
    x = (ICON_X + 38) * 2
    d.rectangle([x, 0, 479 * 2, 130 * 2], fill=255)
    glyphs(d, x, round((ARROW_MID - 7) * 2 - 5), height=28, width=20, gap=4)
    measured, msg = run(big, "mixedcase")
    check(
        measured and msg is None,
        f"2x: a title 2.5 logical px off its arrow passes ({msg})",
    )

    # Not a theme band: Toybox's black band with an outlined chip, the
    # component path. Aligned passes; 8px off fails.
    for dy, ok in ((0, True), (8, False)):
        im = Image.new("L", (480, 800), 255)
        d = ImageDraw.Draw(im)
        d.rectangle([0, 0, 479, 79], fill=0)
        d.rounded_rectangle([5, 10, 60, 70], radius=8, outline=255, width=2)
        d.line([38, 30, 26, 40, 38, 50], fill=255, width=3)
        glyphs(d, 70, 33 + dy, fill=255)
        measured, msg = run(at2x(im), f"chip{dy}")
        check(
            measured and (msg is None) == ok,
            f"black band chip, title {dy}px off: {'passes' if ok else 'fails'} ({msg})",
        )

    print(f"{'PASS' if failures == 0 else 'FAIL'} {checks} checks, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
