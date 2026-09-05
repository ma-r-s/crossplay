#!/usr/bin/env python3
"""Generate 5 high-res B/W geometric wallpaper PNGs (1440x2400, portrait 3:5)."""

import math
import cmath
import random
from PIL import Image, ImageDraw

W, H = 1440, 2400
SEED = 20260905
random.seed(SEED)

OUT = "png"
BLACK, WHITE = 0, 255


def new_canvas(w=W, h=H, bg=WHITE):
    img = Image.new("L", (w, h), bg)
    return img, ImageDraw.Draw(img)


# ---------------------------------------------------------------------------
# 1. ART DECO SUNBURST / FAN  (rising-sun radiating wedges + stepped arcs)
# ---------------------------------------------------------------------------
def art_deco_sunburst():
    img, d = new_canvas()
    ox, oy = W / 2, H * 0.92  # origin near bottom-center
    R = math.hypot(W, H) * 1.15  # overshoot to fill panel
    n = 36  # wedges over 200 deg
    span = math.radians(200)
    start = math.radians(-10)  # slightly past horizontal on both sides
    for i in range(n):
        a0 = start + span * i / n
        a1 = start + span * (i + 1) / n
        if i % 2 == 0:
            p = [
                (ox, oy),
                (ox + R * math.cos(a0), oy - R * math.sin(a0)),
                (ox + R * math.cos((a0 + a1) / 2), oy - R * math.sin((a0 + a1) / 2)),
                (ox + R * math.cos(a1), oy - R * math.sin(a1)),
            ]
            d.polygon(p, fill=BLACK)
    # concentric stepped arc bands near the origin (the "sun")
    bands = [0.10, 0.17, 0.24, 0.31]
    ring = int(R * 0.030)
    for k, frac in enumerate(bands):
        rad = R * frac
        bbox = [ox - rad, oy - rad, ox + rad, oy + rad]
        # draw full ring in inverse tone so it reads over the wedges
        d.arc(bbox, 180, 360, fill=(WHITE if k % 2 == 0 else BLACK), width=ring)
    # solid half-disc core
    core = R * 0.055
    d.pieslice([ox - core, oy - core, ox + core, oy + core], 180, 360, fill=BLACK)
    # crisp keyline along the horizon
    d.rectangle([0, int(oy), W, int(oy) + 10], fill=BLACK)
    img.save(f"{OUT}/deco_sunburst.png")
    print("deco_sunburst.png done")


# ---------------------------------------------------------------------------
# 2. HERRINGBONE  (two-tone parquet weave, bricks rotated 45)
# ---------------------------------------------------------------------------
def herringbone():
    S = 3600
    img, d = new_canvas(S, S, WHITE)
    u = 70  # brick short side
    L = 2 * u  # brick long side
    v1 = (2 * u, 2 * u)
    v2 = (3 * u, 1 * u)
    rng = 40
    for i in range(-rng, rng + 1):
        for j in range(-rng, rng + 1):
            bx = i * v1[0] + j * v2[0]
            by = i * v1[1] + j * v2[1]
            # motif: H brick (black) + V brick (white, = background)
            hx0, hy0 = bx, by
            d.rectangle([hx0, hy0, hx0 + L, hy0 + u], fill=BLACK)
            # V brick region stays white; draw an explicit white rect to be safe
            vx0, vy0 = bx + L, by
            d.rectangle([vx0, vy0, vx0 + u, vy0 + L], fill=WHITE)
    img = img.rotate(45, expand=True, fillcolor=WHITE, resample=Image.NEAREST)
    cw, ch = img.size
    left, top = (cw - W) // 2, (ch - H) // 2
    img = img.crop((left, top, left + W, top + H))
    img.save(f"{OUT}/herringbone.png")
    print("herringbone.png done")


# ---------------------------------------------------------------------------
# 3. HOUNDSTOOTH  (classic dogtooth check, large & crisp)
# ---------------------------------------------------------------------------
def houndstooth():
    img, d = new_canvas()
    # 4x4 boolean tile that tessellates into houndstooth teeth
    tile = [
        [1, 1, 0, 1],
        [1, 1, 1, 0],
        [0, 1, 1, 1],
        [1, 0, 1, 1],
    ]
    cells_x = 8  # 8 tiles-of-4 -> big, crisp
    cell = W / (cells_x * 4)  # size of one small cell in px
    ny = int(H / cell) + 4
    for gy in range(ny):
        for gx in range(cells_x * 4 + 4):
            if tile[gy % 4][gx % 4]:
                x0, y0 = gx * cell, gy * cell
                d.rectangle([x0, y0, x0 + cell + 1, y0 + cell + 1], fill=BLACK)
    img.save(f"{OUT}/houndstooth.png")
    print("houndstooth.png done")


# ---------------------------------------------------------------------------
# 4. PENROSE P3 RHOMBUS TILING (aperiodic, two-tone + edges)
# ---------------------------------------------------------------------------
def penrose():
    phi = (1 + 5**0.5) / 2

    def subdivide(triangles):
        result = []
        for color, A, B, C in triangles:
            if color == 0:  # thin (acute) Robinson triangle
                P = A + (B - A) / phi
                result += [(0, C, P, B), (1, P, C, A)]
            else:  # thick (obtuse)
                Q = B + (A - B) / phi
                R = B + (C - B) / phi
                result += [(1, R, C, A), (1, Q, R, B), (0, R, Q, A)]
        return result

    tris = []
    for i in range(10):
        B = cmath.rect(1, (2 * i - 1) * math.pi / 10)
        C = cmath.rect(1, (2 * i + 1) * math.pi / 10)
        if i % 2 == 0:
            B, C = C, B
        tris.append((0, 0j, B, C))
    for _ in range(6):
        tris = subdivide(tris)

    img, d = new_canvas()
    cx, cy = W / 2, H / 2
    scale = 1.35 * H  # overscan so tiling fills panel to corners

    def pt(z):
        return (cx + z.real * scale, cy + z.imag * scale)

    # fill: thin rhombi (color 0) black, thick (color 1) white
    for color, A, B, C in tris:
        if color == 0:
            d.polygon([pt(A), pt(B), pt(C)], fill=BLACK)
        else:
            d.polygon([pt(A), pt(B), pt(C)], fill=WHITE)
    # stroke rhombus sides (edges A-B and B-C; skip A-C internal diagonal)
    lw = 7
    for color, A, B, C in tris:
        d.line([pt(A), pt(B)], fill=BLACK, width=lw, joint="curve")
        d.line([pt(B), pt(C)], fill=BLACK, width=lw, joint="curve")
    img.save(f"{OUT}/penrose_rhombus.png")
    print("penrose_rhombus.png done")


# ---------------------------------------------------------------------------
# 5. BAUHAUS COMPOSITION  (bold circles, triangles, bars, a diagonal)
# ---------------------------------------------------------------------------
def bauhaus():
    img, d = new_canvas(W, H, WHITE)

    # thick vertical bar (right third)
    d.rectangle([980, 0, 1080, H], fill=BLACK)
    # thick horizontal bar
    d.rectangle([0, 1560, W, 1690], fill=BLACK)

    # big solid circle upper area
    cx, cy, r = 470, 620, 380
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=BLACK)
    # white inner half to create a crescent/eclipse (offset white circle)
    d.ellipse([cx - r + 150, cy - r - 60, cx + r + 150, cy + r - 60], fill=WHITE)

    # bold diagonal bar across the whole panel
    diag = Image.new("L", (W, H), 0)
    dd = ImageDraw.Draw(diag)
    dd.line([(-50, 300), (W + 50, H - 200)], fill=255, width=70)
    # paste diagonal as black
    black = Image.new("L", (W, H), BLACK)
    img.paste(black, (0, 0), diag)

    # big black triangle bottom-left, pointing up-right
    d.polygon([(60, H - 60), (60, 1850), (720, H - 60)], fill=BLACK)

    # outlined circle (thick ring) lower-right
    rx, ry, rr = 1180, 2050, 210
    d.ellipse([rx - rr, ry - rr, rx + rr, ry + rr], fill=BLACK)
    d.ellipse([rx - rr + 70, ry - rr + 70, rx + rr - 70, ry + rr - 70], fill=WHITE)

    # small solid triangle top-right (white knock-out on the vertical bar)
    d.polygon([(1030, 180), (1030, 460), (1260, 320)], fill=BLACK)

    # a couple of quarter-disc accents
    d.pieslice([120, 120, 520, 520], 0, 90, fill=BLACK)

    img.save(f"{OUT}/bauhaus.png")
    print("bauhaus.png done")


if __name__ == "__main__":
    art_deco_sunburst()
    herringbone()
    houndstooth()
    penrose()
    bauhaus()
    print("ALL DONE")
