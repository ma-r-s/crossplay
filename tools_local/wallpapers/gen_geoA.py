#!/usr/bin/env python3
"""Generate 5 op-art wallpaper PNGs at 1440x2400 (3:5, matches 480x800).

Everything is drawn at 2x (2880x4800) and LANCZOS-downscaled to 1440x2400 so
edges are anti-aliased before the device threshold, which keeps lines crisp and
avoids speckle. Fixed seed -> reproducible.
"""

import math
import random
import numpy as np
from PIL import Image, ImageDraw

random.seed(42)
np.random.seed(42)

OUT = "/private/tmp/claude-501/-Users-mario-Projects-Personal-Code-Xteink/8e1fa5ab-84b1-46e9-9fe0-84f6e89265b4/scratchpad/cand/geoA"
W, H = 1440, 2400
SS = 2
WW, HH = W * SS, H * SS  # 2880 x 4800


def save(img, name):
    if img.size != (W, H):
        img = img.resize((W, H), Image.LANCZOS)
    img = img.convert("L")
    path = f"{OUT}/{name}.png"
    img.save(path)
    print("PNG", path)
    return path


# ---------------------------------------------------------------------------
# 1. Concentric rotating squares -> twisting vortex (depth illusion)
# ---------------------------------------------------------------------------
def concentric_squares():
    img = Image.new("L", (WW, HH), 255)
    d = ImageDraw.Draw(img)
    cx, cy = WW / 2, HH / 2
    # first square must cover the panel even when rotated: half-diагonal >= panel
    # corner distance from centre (~ sqrt(1440^2+2400^2)/2 *SS)
    half = math.hypot(WW, HH) / 2 * 1.05  # half side-length start (square, so this
    # is half the side; use big enough that rotated square still covers corners)
    half = 2100  # tuned: covers 2880x4800 at any rotation
    ang = 0.0
    dang = math.radians(7.5)
    scale = 0.945
    fill = 0
    n = 0
    while half > 6:
        c = math.cos(ang)
        s = math.sin(ang)
        pts = []
        for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            x = sx * half
            y = sy * half
            pts.append((cx + x * c - y * s, cy + x * s + y * c))
        d.polygon(pts, fill=fill)
        fill = 255 - fill
        half *= scale
        ang += dang
        n += 1
    return save(img, "opart-concentric-rotating-squares")


# ---------------------------------------------------------------------------
# 2. Truchet tiles (Smith quarter-arc) -> flowing curves
# ---------------------------------------------------------------------------
def truchet():
    img = Image.new("L", (WW, HH), 255)
    d = ImageDraw.Draw(img)
    cols = 6
    tile = WW / cols  # 480 px @2x
    rows = int(math.ceil(HH / tile))
    stroke = int(tile * 0.20)  # bold
    r = tile / 2
    for j in range(rows):
        for i in range(cols):
            x0 = i * tile
            y0 = j * tile
            orient = random.randint(0, 1)
            if orient == 0:
                # arcs centred at top-left and bottom-right corners
                d.arc([x0 - r, y0 - r, x0 + r, y0 + r], 0, 90, fill=0, width=stroke)
                d.arc(
                    [x0 + tile - r, y0 + tile - r, x0 + tile + r, y0 + tile + r],
                    180,
                    270,
                    fill=0,
                    width=stroke,
                )
            else:
                # arcs centred at top-right and bottom-left corners
                d.arc(
                    [x0 + tile - r, y0 - r, x0 + tile + r, y0 + r],
                    90,
                    180,
                    fill=0,
                    width=stroke,
                )
                d.arc(
                    [x0 - r, y0 + tile - r, x0 + r, y0 + tile + r],
                    270,
                    360,
                    fill=0,
                    width=stroke,
                )
    return save(img, "opart-truchet-arcs")


# ---------------------------------------------------------------------------
# 3. Isometric cubes tessellation (tumbling blocks): white top, black left,
#    diagonal-hatch right -> three readable tones in 1-bit.
# ---------------------------------------------------------------------------
def iso_cubes():
    s = 300.0  # edge length @2x
    k = math.sqrt(3) / 2  # 0.866
    # diagonal stripe field for the "right" face
    xs = np.arange(WW)[None, :]
    ys = np.arange(HH)[:, None]
    period = 46
    lw = 22
    stripes = np.where(((xs - ys) % period) < lw, 0, 255).astype(np.uint8)

    left_mask = Image.new("L", (WW, HH), 0)
    right_mask = Image.new("L", (WW, HH), 0)
    dl = ImageDraw.Draw(left_mask)
    dr = ImageDraw.Draw(right_mask)

    def hexagon(cx, cy):
        # vertices at 90,150,210,270,330,30 degrees, radius s
        v = {}
        for a in (90, 150, 210, 270, 330, 30):
            rad = math.radians(a)
            v[a] = (cx + s * math.cos(rad), cy - s * math.sin(rad))
        c = (cx, cy)
        top = [v[90], v[30], c, v[150]]
        left = [v[150], c, v[270], v[210]]
        right = [v[30], v[330], v[270], c]
        return top, left, right

    tops = []
    xstep = 2 * k * s  # horizontal centre spacing
    ystep = 1.5 * s  # vertical centre spacing
    row = 0
    y = -ystep
    while y < HH + ystep:
        offset = (k * s) if (row % 2) else 0.0
        x = -xstep + offset
        while x < WW + xstep:
            top, left, right = hexagon(x, y)
            dl.polygon(left, fill=255)
            dr.polygon(right, fill=255)
            tops.append((top, left, right))
            x += xstep
        y += ystep
        row += 1

    lm = np.asarray(left_mask)
    rm = np.asarray(right_mask)
    out = np.full((HH, WW), 255, np.uint8)
    out[lm > 0] = 0
    out[rm > 0] = stripes[rm > 0]

    img = Image.fromarray(out, "L")
    d = ImageDraw.Draw(img)
    ol = int(s * 0.05)
    for top, left, right in tops:
        for poly in (top, left, right):
            d.line(poly + [poly[0]], fill=0, width=ol, joint="curve")
    return save(img, "opart-isometric-cubes")


# ---------------------------------------------------------------------------
# 4. Moire: two straight line gratings at a slight angle.
# ---------------------------------------------------------------------------
def moire():
    # Two thick square-wave gratings, one tilted a few degrees, combined with
    # XOR -> the classic amplitude-modulated moire: solid black bands where the
    # two go out of phase, open striped bands where they align. Thick bars
    # (period ~36 px on device) keep it crisp instead of drifting into speckle.
    xs = np.arange(WW)[None, :].astype(np.float32)
    ys = np.arange(HH)[:, None].astype(np.float32)
    dev = WW / 480.0
    period = 36.0 * dev
    lw = period * 0.5
    ang = math.radians(5.0)
    g1 = (xs % period) < lw
    xr = xs * math.cos(ang) + ys * math.sin(ang)
    g2 = (xr % period) < lw
    out = np.where(g1 ^ g2, 0, 255).astype(np.uint8)
    out = np.broadcast_to(out, (HH, WW)).copy()
    img = Image.fromarray(out, "L")
    return save(img, "opart-moire-gratings")


# ---------------------------------------------------------------------------
# 5. Op-art bulge checkerboard (Vasarely sphere) -- the showpiece.
# ---------------------------------------------------------------------------
def bulge_checker():
    cx, cy = WW / 2.0, HH / 2.0
    xs = np.arange(WW, dtype=np.float32)[None, :] - cx
    ys = np.arange(HH, dtype=np.float32)[:, None] - cy
    r = np.sqrt(xs * xs + ys * ys)
    R = 0.62 * HH / 2 + 0.0  # bulge region radius
    R = 1550.0
    rn = np.clip(r / R, 0, 1)
    # ease-in maps output radius to a smaller source radius near centre ->
    # cells magnified in the middle, continuous with the flat field at r=R.
    src_rn = rn**1.7
    src_r = np.where(r < R, src_rn * R, r)
    scale = np.divide(src_r, r, out=np.ones_like(r), where=r > 1e-6)
    sx = cx + xs * scale
    sy = cy + ys * scale
    cell = 150.0  # ~25 px device in the flat field
    check = (
        np.floor(sx / cell).astype(np.int64) + np.floor(sy / cell).astype(np.int64)
    ) % 2
    out = np.where(check == 0, 0, 255).astype(np.uint8)
    img = Image.fromarray(out, "L")
    return save(img, "opart-bulge-checkerboard")


if __name__ == "__main__":
    concentric_squares()
    truchet()
    iso_cubes()
    moire()
    bulge_checker()
    print("done")
