#!/usr/bin/env python3
"""Is a header's back control clear of words, and on the same line as its
title? Measured in the screenshot, so it holds whoever drew which half.

    python3 tools_local/header_row_check.py shot.png [shot.png ...]

Prints one HEADER ROW line per screenshot whose header fails either question,
and nothing for the rest. Exits 1 when any did. sim-shot.sh runs it on every
screenshot it takes.

Why pixels and not the layout: both cases that prompted it were two pieces of
code each placing half of one row. Study, 2026-11-17: the theme drew the back
arrow centred on the row below the battery strip and Study drew "3 LEFT"
centred on the whole band, 12.5px above its arrow. Study again, 1.15.0: in a
theme that centres its title (Classic) the header keeps the whole band for
the title, Study started its count at that band's left margin, and "6 LEFT"
was drawn over the arrow. Each was right by its own arithmetic. Only the
picture contains both halves.

Everything is measured in LOGICAL pixels: the panel is 480 wide in portrait
and 800 in landscape, and the simulator captures at 2x. A limit counted in
capture pixels is half a limit on a 2x shot, which failed correct headers.

Two kinds of back control, found two ways:
  - the theme's arrow (src/components/icons/headerIcons.h, read from the
    source, not copied here) is found by its shape wherever it is drawn. That
    is what lets a band with no rule under it (Classic, RoundedRaff) be read
    at all, and it is what answers the first question: any ink inside the
    arrow's icon box that is not the arrow is words drawn over it;
  - any other control (an outlined chip on a black band) is the leftmost
    component of the band's ink starting within LEAD_X of the edge and at
    least MIN_H tall, the band being the rows above the first row whose colour
    flips away from the band's own (the rule under a white band, the paper
    under a black one). Words running into a chip are the ui suite's
    question (checkBandTextClearsChips), asked of the drawn rects.

The title is the run of glyphs starting within TITLE_GAP of the control's
right edge, on its row. Its line is from the tallest glyph's top to the
baseline the most glyphs share, so a descender does not drag it.
"""

import re
import sys
from collections import Counter, deque
from pathlib import Path

from PIL import Image

# Logical pixels, every one of them.
LIMIT = 3  # between the control's centre and the title's
LEAD_X = 60  # a leading control starts this close to the edge
MIN_H = 14  # shorter ink in the band is status text, not a control
TITLE_GAP = 60  # the title starts this close after the control
MIN_GAP = 8  # ...and at least this far: closer is the next letter of a title

MAX_BAND = 110  # the tallest header band any theme draws, status strip included
RUN_GAP = 15  # a wider gap ends the title's run of glyphs
LINE_SPLIT = 10  # glyph centres further apart than this are on different lines
OVERLAP_MIN = 4  # stray ink pixels inside the arrow's box before it counts as words
ARROW_MATCH = 0.9  # share of the arrow's ink that must be dark to be the arrow
ARROW_CLEAR = 0.5  # share of the rest of its ink box that must be paper

ICONS = (
    Path(__file__).resolve().parent.parent
    / "src"
    / "components"
    / "icons"
    / "headerIcons.h"
)


def load_arrow():
    """The theme's back arrow as (ink, box): its dark pixels, and the icon's
    width and height. Bit 0 is ink, as gen_icons.py writes it."""
    text = ICONS.read_text()
    m = re.search(r"icon_header_back_32_bits\[\]\s*=\s*\{(.*?)\};", text, re.S)
    size = re.search(r"icon_header_back_32\s*=\s*\{\s*(\d+)\s*,\s*(\d+)", text)
    if not m or not size:
        sys.exit(
            f"header_row_check: no back arrow in {ICONS}; the icon was renamed or regenerated"
        )
    data = [int(b, 16) for b in re.findall(r"0x[0-9A-Fa-f]+", m.group(1))]
    w, h = int(size.group(1)), int(size.group(2))
    stride = (w + 7) // 8
    ink = [
        (x, y)
        for y in range(h)
        for x in range(w)
        if not (data[y * stride + x // 8] >> (7 - x % 8)) & 1
    ]
    return ink, (w, h)


ARROW_INK, ARROW_BOX = load_arrow()


def dark(v):
    return v < 128


def logical_scale(w, h):
    return w / (480 if h >= w else 800)


def band_of(px, w, h, s):
    """(bottom, band_is_dark) or None: rows [0, bottom) are the header band,
    found by the rule under it or the paper under a black band."""

    def row_dark_share(y):
        return sum(1 for x in range(w) if dark(px[x, y])) / w

    top = row_dark_share(int(2 * s)) > 0.5
    for y in range(int(6 * s), min(h, int(MAX_BAND * s * 2))):
        share = row_dark_share(y)
        if top and share < 0.15:
            return y, True
        if not top and share > 0.85:
            return y, False
    return None


def components(px, w, bottom, band_dark):
    seen = bytearray(w * bottom)
    out = []
    for y0 in range(bottom):
        for x0 in range(w):
            i = y0 * w + x0
            if seen[i] or dark(px[x0, y0]) == band_dark:
                continue
            seen[i] = 1
            q = deque([(x0, y0)])
            xs = [x0, x0]
            ys = [y0, y0]
            while q:
                x, y = q.popleft()
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < w and 0 <= ny < bottom:
                            j = ny * w + nx
                            if not seen[j] and dark(px[nx, ny]) != band_dark:
                                seen[j] = 1
                                q.append((nx, ny))
                                xs[0] = min(xs[0], nx)
                                xs[1] = max(xs[1], nx)
                                ys[0] = min(ys[0], ny)
                                ys[1] = max(ys[1], ny)
            out.append((xs[0], ys[0], xs[1], ys[1]))
    return out


def find_arrow(im, s):
    """The theme's back arrow on a white band, in logical pixels:
    (x, y, stray) for the icon box's top-left and the count of dark pixels
    inside that box that are not the arrow, or None."""
    lw, lh = round(im.width / s), round(im.height / s)
    small = im.resize((lw, lh), Image.BOX) if s != 1 else im
    lp = small.load()
    bw, bh = ARROW_BOX
    ink = set(ARROW_INK)
    # One pixel of slack around the arrow's own ink: a 2x capture boxed back
    # down to 1x moves an edge by one pixel, which is not a word.
    near = {(x + dx, y + dy) for x, y in ink for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
    ix0 = min(x for x, _ in ink)
    iy0 = min(y for _, y in ink)
    ix1 = max(x for x, _ in ink)
    iy1 = max(y for _, y in ink)
    gaps = [
        (x, y)
        for y in range(iy0, iy1 + 1)
        for x in range(ix0, ix1 + 1)
        if (x, y) not in ink
    ]
    # A white band only: on a black one the arrow is not the theme's.
    if dark(lp[min(lw - 1, LEAD_X), min(lh - 1, 2)]):
        return None
    best = None
    for oy in range(0, min(lh, MAX_BAND) - bh + 1):
        # The topmost arrow, settled over the rows a one-pixel rounding can
        # move it by.
        if best is not None and oy > best[1] + 2:
            break
        for ox in range(-ix0, LEAD_X - ix0 + 1):
            if ox + ix1 >= lw:
                break
            hit = sum(1 for x, y in ARROW_INK if dark(lp[ox + x, oy + y]))
            if hit < ARROW_MATCH * len(ARROW_INK):
                continue
            clear = sum(1 for x, y in gaps if not dark(lp[ox + x, oy + y]))
            if clear < ARROW_CLEAR * len(gaps):
                continue
            if best is None or hit > best[2]:
                best = (ox, oy, hit)
    if best is None:
        return None
    ox, oy, _ = best
    stray = sum(
        1
        for y in range(bh)
        for x in range(bw)
        if 0 <= ox + x < lw
        and 0 <= oy + y < lh
        and (x, y) not in near
        and dark(lp[ox + x, oy + y])
    )
    return ox, oy, stray


def title_lines(comps, lead, s):
    """The title beside `lead`, as its lines top to bottom, each the run of
    glyphs from that line's left end; [] when nothing starts beside it."""
    glyphs = [
        c
        for c in comps
        if c[0] > lead[2]
        and c[1] < lead[3] + 20 * s
        and c[3] > lead[1] - 20 * s
        and (c[3] - c[1] + 1) >= 6 * s
    ]
    # Lines by height: consecutive glyph centres within one line are a few
    # pixels apart, and two lines are a line box apart.
    lines = []
    for g in sorted(glyphs, key=lambda c: c[1] + c[3]):
        if lines and (g[1] + g[3]) - (lines[-1][-1][1] + lines[-1][-1][3]) <= 2 * LINE_SPLIT * s:
            lines[-1].append(g)
        else:
            lines.append([g])
    runs = []
    for line in lines:
        line.sort(key=lambda c: c[0])
        run = [line[0]]
        for g in line[1:]:
            if g[0] - run[-1][2] > RUN_GAP * s:
                break
            run.append(g)
        runs.append(run)
    if not runs:
        return []
    # The title is the line that starts nearest the control, and any line
    # below or above it that starts where it does: a wrapped title.
    first = min(runs, key=lambda r: r[0][0])
    return [r for r in runs if abs(r[0][0] - first[0][0]) <= RUN_GAP * s]


def check(path):
    """(measured, message): measured is False when the shot has no back
    control in its header; message is None unless the header fails."""
    im = Image.open(path).convert("L")
    w, h = im.size
    s = logical_scale(w, h)
    px = im.load()

    arrow = find_arrow(im, s)
    if arrow is not None:
        ox, oy, stray = arrow
        if stray >= OVERLAP_MIN:
            return True, (
                f"{stray} pixels of ink inside the back arrow's {ARROW_BOX[0]}x{ARROW_BOX[1]} box at "
                f"x={ox}, y={oy}: words are drawn over the back control"
            )
        ix = [x for x, _ in ARROW_INK]
        iy = [y for _, y in ARROW_INK]
        lead = (
            round((ox + min(ix)) * s),
            round((oy + min(iy)) * s),
            round((ox + max(ix) + 1) * s) - 1,
            round((oy + max(iy) + 1) * s) - 1,
        )
        # The arrow's row ends at the first blank row under it: a band with no
        # rule (Classic, RoundedRaff) has nothing else to end it, and a rule
        # sits below that row anyway.
        bottom = lead[3] + 1
        while bottom < min(h, int(MAX_BAND * s)) and any(dark(px[x, bottom]) for x in range(w)):
            bottom += 1
        comps = [c for c in components(px, w, bottom, False) if c[2] < w - 1]
        chip = False
    else:
        band = band_of(px, w, h, s)
        if band is None:
            return False, None
        bottom, band_dark = band
        comps = components(px, w, bottom, band_dark)
        # A component touching the band's edge is the band itself or its rule.
        comps = [
            c
            for c in comps
            if c[0] > 0 and c[2] < w - 1 and c[1] > 0 and c[3] < bottom - 1
        ]
        leads = [
            c
            for c in comps
            if c[0] < LEAD_X * s
            and (c[3] - c[1] + 1) >= MIN_H * s
            and (c[2] - c[0]) < 90 * s
        ]
        if not leads:
            return False, None
        lead = min(leads, key=lambda c: c[0])
        # An outlined chip holds its icon inside its box, and a title may start
        # right after its outline; a bare arrow holds nothing.
        chip = any(
            c is not lead
            and lead[0] < c[0]
            and c[2] < lead[2]
            and lead[1] < c[1]
            and c[3] < lead[3]
            for c in comps
        )

    lines = title_lines(comps, lead, s)
    gap = (min(r[0][0] for r in lines) - lead[2]) / s if lines else None
    nearest = 1 if chip or arrow is not None else MIN_GAP
    if gap is None or not nearest <= gap <= TITLE_GAP:
        # A control with no title beside it: a centred title, or none. The
        # arrow was still looked at, which is what "measured" counts.
        return arrow is not None, None
    # From the first line's tallest glyph to the baseline most of the last
    # line's glyphs share: a wrapped title is centred on its control as a block.
    top = min(g[1] for g in lines[0])
    baseline = Counter(g[3] for g in lines[-1]).most_common(1)[0][0]
    title_mid = (top + baseline) / 2 / s
    lead_mid = (lead[1] + lead[3]) / 2 / s
    off = lead_mid - title_mid
    if abs(off) <= LIMIT:
        return True, None
    return True, (
        f"back control centred at y={lead_mid:.1f}, title at y={title_mid:.1f}: "
        f"{abs(off):.1f}px apart (limit {LIMIT}); the title sits {'above' if off > 0 else 'below'} its control"
    )


def main(paths):
    bad = measured = 0
    for p in paths:
        seen, msg = check(p)
        measured += seen
        if msg:
            bad += 1
            print(f"HEADER ROW {p}: {msg}")
    # The count, so "no shot had a back control" never reads as "all clean".
    print(
        f"header row: {measured} of {len(paths)} screenshot(s) had a back control in the header; {bad} failed"
    )
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
