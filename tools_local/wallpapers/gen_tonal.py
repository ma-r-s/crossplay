#!/usr/bin/env python3
"""Generate 5 smooth grayscale source PNGs for tonal e-ink wallpapers.

All output is SMOOTH tone (real gradients / analytic shading), NO random noise.
The device helper (to_device_bmp.py) does the dithering. Source res 1440x2400
(3x the 480x800 device panel, same 0.6 portrait aspect so cover-fit won't crop).
"""

import numpy as np
from PIL import Image

W, H = 1440, 2400
OUT = "/private/tmp/claude-501/-Users-mario-Projects-Personal-Code-Xteink/8e1fa5ab-84b1-46e9-9fe0-84f6e89265b4/scratchpad"


def save(arr, name):
    """arr in [0,1] float -> 8-bit grayscale PNG."""
    a = np.clip(arr, 0.0, 1.0)
    img = Image.fromarray((a * 255.0 + 0.5).astype(np.uint8), "L")
    path = f"{OUT}/{name}.png"
    img.save(path)
    print(f"png {path}  range[{a.min():.3f},{a.max():.3f}]")
    return path


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# normalized coordinate grids
yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
u = xx / (W - 1)  # 0..1 left->right
v = yy / (H - 1)  # 0..1 top->bottom
ar = W / H  # aspect for isotropic distances
# isotropic centered coords, y in [-.5,.5], x scaled by aspect
cx = (u - 0.5) * ar
cy = v - 0.5


# ---------------------------------------------------------------------------
# 1. HALFTONE DOT FIELD  -- smooth gradient, bayer mode makes the dot field.
#    A broad, gentle sweep: bright upper-right falling to dark lower-left,
#    with a low-frequency swell so the dot density undulates (never flat).
# ---------------------------------------------------------------------------
g = 0.5 * (u) + 0.5 * (1.0 - v)  # diagonal base sweep
g += 0.10 * np.sin(2.0 * np.pi * (u * 1.2 + v * 0.6))  # slow swell
g = smoothstep(0.0, 1.0, (g - g.min()) / (g.max() - g.min()))
save(g, "01_halftone_dotfield")


# ---------------------------------------------------------------------------
# 2. RADIAL BURST -- rising-sun tonal glow. Bright disc + glow low-center,
#    smooth radial falloff to dark sky, plus faint smooth rays.
# ---------------------------------------------------------------------------
sun_x, sun_y = 0.0, 0.28  # centered horizontally, low in frame
d = np.sqrt((cx - sun_x) ** 2 + (cy - sun_y) ** 2)
# core disc -> glow: smooth monotone falloff
glow = np.exp(-((d / 0.32) ** 1.7))  # broad glow
core = smoothstep(0.14, 0.10, d)  # bright solid-ish core
val = np.clip(0.86 * glow + 0.55 * core, 0.0, 1.0)
# faint radial rays, smoothly modulated, fading with distance
ang = np.arctan2(cy - sun_y, cx - sun_x)
rays = 0.06 * (0.5 + 0.5 * np.cos(ang * 12.0)) * np.exp(-((d / 0.55) ** 2))
val = np.clip(val + rays, 0.0, 1.0)
# lift dark sky a touch so it isn't pure black; keep smooth
val = 0.06 + 0.94 * val
save(val, "02_radial_burst")


# ---------------------------------------------------------------------------
# 3. TOPOGRAPHIC CONTOUR LINES -- height field from layered sines + smooth
#    gaussian bumps (NO noise). Clean concentric contour lines via
#    gradient-normalized line distance -> crisp with threshold mode.
# ---------------------------------------------------------------------------
X = cx * 6.0
Y = cy * 6.0
hgt = (
    np.sin(X * 0.9) * np.cos(Y * 0.8)
    + 0.6 * np.sin(X * 0.4 + Y * 0.5)
    + 0.5 * np.cos(X * 1.3 - Y * 0.6)
)
# a couple of smooth peaks/basins for closed contours
for px, py, amp, s in [
    (-1.6, -2.2, 2.4, 2.2),
    (2.0, 2.6, -2.0, 2.6),
    (0.4, 0.2, 1.6, 3.0),
]:
    hgt += amp * np.exp(-(((X - px) ** 2 + (Y - py) ** 2) / (2 * s * s)))
hgt = (hgt - hgt.min()) / (hgt.max() - hgt.min())  # 0..1

N = 22.0  # number of contour bands
levels = hgt * N
frac = np.abs(levels - np.round(levels))  # 0 at a contour line
gy, gx = np.gradient(levels)
grad = np.sqrt(gx * gx + gy * gy) + 1e-6  # level-units per pixel
pix = frac / grad  # distance to line in pixels
half = 2.4  # line half-width (px, source res)
line = smoothstep(half + 1.0, half - 1.0, pix)  # 1 on line, 0 off
topo = 1.0 - line  # dark lines on white paper
save(topo, "03_topographic_contours")


# ---------------------------------------------------------------------------
# 4. ATKINSON GRADIENT -- smooth diagonal black->white ramp, full range,
#    to show off Atkinson error-diffusion texture.
# ---------------------------------------------------------------------------
ramp = u * 0.55 + (1.0 - v) * 0.45
ramp = (ramp - ramp.min()) / (ramp.max() - ramp.min())
save(ramp, "04_atkinson_gradient")


# ---------------------------------------------------------------------------
# 5. SHADED SPHERE / ORB -- Lambert-shaded sphere lit upper-left, subtle
#    specular + ambient, on a smooth vignetted field. Atkinson-dithered.
# ---------------------------------------------------------------------------
# plain field: soft smooth vertical vignette (mid-dark), no noise
field = 0.30 + 0.14 * (1.0 - v) - 0.05 * np.abs(cx)
field = np.clip(field, 0.0, 1.0)

R = 0.34
ox, oy = 0.0, -0.02
sx = cx - ox
sy = cy - oy
r2 = sx * sx + sy * sy
inside = r2 < (R * R)
nz = np.sqrt(np.clip(R * R - r2, 0.0, None)) / R
nx = sx / R
ny = sy / R
# light from upper-left, toward viewer
L = np.array([-0.5, -0.62, 0.6])
L = L / np.linalg.norm(L)
ndotl = np.clip(nx * L[0] + ny * L[1] + nz * L[2], 0.0, 1.0)
ambient = 0.12
diffuse = 0.80 * ndotl
# subtle specular highlight
V = np.array([0.0, 0.0, 1.0])
Hh = L + V
Hh = Hh / np.linalg.norm(Hh)
ndoth = np.clip(nx * Hh[0] + ny * Hh[1] + nz * Hh[2], 0.0, 1.0)
spec = 0.35 * (ndoth**24)
sphere = np.clip(ambient + diffuse + spec, 0.0, 1.0)

# soft contact shadow under-right of the orb, smooth
sh_d = np.sqrt((cx - 0.10) ** 2 + ((cy - (oy + R * 0.92)) * 1.8) ** 2)
shadow = 0.22 * np.exp(-((sh_d / 0.22) ** 2))
out = field - shadow
out[inside] = sphere[inside]
out = np.clip(out, 0.0, 1.0)
save(out, "05_shaded_sphere")

print("done")
