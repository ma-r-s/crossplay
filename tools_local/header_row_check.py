#!/usr/bin/env python3
"""Does a header's back control sit on the same line as its title? Measured in
the screenshot, so it holds whoever drew which half.

    python3 tools_local/header_row_check.py shot.png [shot.png ...]

Prints one HEADER ROW line per screenshot whose leading control (an arrow, a
chip) and title disagree by more than LIMIT pixels, and nothing for the rest.
Exits 1 when any did. sim-shot.sh runs it on every screenshot it takes.

Why pixels and not the layout: the case that prompted it (Study, 2026-11-17)
was two pieces of code each placing half of one row. The theme drew the back
arrow centred on the row below the battery strip; Study drew "3 LEFT" centred
on the whole band. Each was right by its own arithmetic and the title sat
12.5px above its arrow for weeks. Only the picture contains both halves.

How it reads a header:
  - the band is the rows above the first row whose colour flips away from the
    band's own (the rule under a white band, the paper under a black one);
  - ink is whatever differs from the band's colour, split into 8-connected
    components;
  - the leading control is the leftmost component starting within LEAD_X of
    the edge and at least MIN_H tall (the battery and a corner clock are
    smaller than that);
  - the title is the run of glyphs starting within TITLE_GAP of the control's
    right edge, on its row. Its line is from the tallest glyph's top to the
    baseline, the bottom most glyphs share, so a descender does not drag it.
"""

import sys
from collections import Counter, deque

from PIL import Image

LIMIT = 3  # px between the control's centre and the title's
LEAD_X = 60  # a leading control starts this close to the edge
MIN_H = 14  # shorter ink in the band is status text, not a control
TITLE_GAP = 60  # the title starts this close after the control
MIN_GAP = 8  # ...and at least this far: closer is the next letter of a title, not a control
MAX_BAND = 220


def dark(v):
    return v < 128


def band_of(px, w, h):
    """(bottom, band_is_dark) or None: rows [0, bottom) are the header band."""

    def row_dark_share(y):
        return sum(1 for x in range(w) if dark(px[x, y])) / w

    top = row_dark_share(2) > 0.5
    for y in range(6, min(h, MAX_BAND)):
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


def check(path):
    """(measured, message): measured is False when the shot has no back control
    beside a title; message is None unless the two disagree."""
    im = Image.open(path).convert("L")
    w, h = im.size
    px = im.load()
    band = band_of(px, w, h)
    if band is None:
        return False, None
    bottom, band_dark = band
    comps = components(px, w, bottom, band_dark)
    # A component touching the band's edge is the band itself or its rule.
    comps = [
        c for c in comps if c[0] > 0 and c[2] < w - 1 and c[1] > 0 and c[3] < bottom - 1
    ]
    leads = [
        c
        for c in comps
        if c[0] < LEAD_X and (c[3] - c[1] + 1) >= MIN_H and (c[2] - c[0]) < 90
    ]
    if not leads:
        return False, None
    lead = min(leads, key=lambda c: c[0])
    lead_mid = (lead[1] + lead[3]) / 2
    glyphs = sorted(
        (
            c
            for c in comps
            if c[0] > lead[2]
            and c[1] < lead[3] + 20
            and c[3] > lead[1] - 20
            and (c[3] - c[1] + 1) >= 6
        ),
        key=lambda c: c[0],
    )
    # An outlined chip holds its icon inside its box, and a title may start
    # right after its outline; a bare arrow holds nothing.
    chip = any(c is not lead and lead[0] < c[0] and c[2] < lead[2] and lead[1] < c[1] and c[3] < lead[3] for c in comps)
    if not glyphs or not (2 if chip else MIN_GAP) <= glyphs[0][0] - lead[2] <= TITLE_GAP:
        return False, None
    run = [glyphs[0]]
    for g in glyphs[1:]:
        if g[0] - run[-1][2] > 30:
            break
        run.append(g)
    top = min(g[1] for g in run)
    baseline = Counter(g[3] for g in run).most_common(1)[0][0]
    title_mid = (top + baseline) / 2
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
    # The count, so "no shot had a back control" never reads as "all aligned".
    print(f"header row: {measured} of {len(paths)} screenshot(s) had a back control beside a title; {bad} misaligned")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
