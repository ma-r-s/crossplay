#!/usr/bin/env python3
"""Convert any image to the device's wallpaper format: 480x800 portrait, 1-bit,
uncompressed BMP with a black/white palette (~48KB), byte-compatible with what
the on-device Wallpapers app pins and the sleep screen renders.

Fit is COVER (fill the panel, centre-crop the overflow) so nothing is stretched.
Dithering:
  atkinson  - error diffusion, high-contrast, the RIGHT default for tonal art
  bayer     - ordered 8x8, good for smooth gradients / halftone fields
  threshold - no dither, for pure black/white LINE ART (keeps lines crisp)

Usage: to_device_bmp.py <in> <out.bmp> [atkinson|bayer|threshold] [--invert]
"""
import sys
import numpy as np
from PIL import Image, ImageOps

W, H = 480, 800


def fit_cover(img):
    img = img.convert("L")
    iw, ih = img.size
    scale = max(W / iw, H / ih)
    nw, nh = max(1, round(iw * scale)), max(1, round(ih * scale))
    img = img.resize((nw, nh), Image.LANCZOS)
    left, top = (nw - W) // 2, (nh - H) // 2
    return img.crop((left, top, left + W, top + H))


def atkinson(gray):
    g = gray.astype(np.float32).copy()
    out = np.zeros((H, W), np.uint8)
    for y in range(H):
        for x in range(W):
            old = g[y, x]
            new = 255.0 if old >= 128 else 0.0
            out[y, x] = 1 if new == 255.0 else 0
            err = (old - new) / 8.0
            for dx, dy in ((1, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (0, 2)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < W and 0 <= ny < H:
                    g[ny, nx] += err
    return out


_BAYER8 = np.array([
    [0, 48, 12, 60, 3, 51, 15, 63], [32, 16, 44, 28, 35, 19, 47, 31],
    [8, 56, 4, 52, 11, 59, 7, 55], [40, 24, 36, 20, 43, 27, 39, 23],
    [2, 50, 14, 62, 1, 49, 13, 61], [34, 18, 46, 30, 33, 17, 45, 29],
    [10, 58, 6, 54, 9, 57, 5, 53], [42, 26, 38, 22, 41, 25, 37, 21],
], np.float32)


def bayer(gray):
    thr = (_BAYER8 + 0.5) / 64.0 * 255.0
    tiled = np.tile(thr, (H // 8 + 1, W // 8 + 1))[:H, :W]
    return (gray.astype(np.float32) > tiled).astype(np.uint8)


def threshold(gray):
    return (gray.astype(np.float32) >= 128).astype(np.uint8)


def to_bmp(inp, out, mode="atkinson", invert=False, auto=False):
    img = fit_cover(Image.open(inp))
    if auto:
        # Stretch the scan's tonal range before dithering. A museum scan of an
        # engraving sits in a narrow band of greys; dithered as-is the fine
        # hatching of a FACE collapses into the hatching of the FOLIAGE behind
        # it, which is how Adam lost his head at 480x800.
        img = ImageOps.autocontrast(img, cutoff=1)
    gray = np.asarray(img, np.uint8)
    if invert:
        gray = 255 - gray
    bits = {"atkinson": atkinson, "bayer": bayer, "threshold": threshold}[mode](gray)
    # bits: 1 = white, 0 = black. PIL mode '1': 255 = white.
    Image.fromarray((bits * 255).astype(np.uint8), "L").convert("1", dither=Image.NONE).save(out, "BMP")
    # Verify the device-critical facts.
    v = Image.open(out)
    assert v.size == (W, H), f"size {v.size}"
    assert v.mode == "1", f"mode {v.mode}"
    import struct
    b = open(out, "rb").read()
    assert struct.unpack_from("<H", b, 28)[0] == 1, "not 1bpp"
    assert struct.unpack_from("<I", b, 30)[0] == 0, "compressed"
    return len(b)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a not in ("--invert", "--autocontrast")]
    inv = "--invert" in sys.argv
    auto = "--autocontrast" in sys.argv
    inp, out = args[0], args[1]
    mode = args[2] if len(args) > 2 else "atkinson"
    n = to_bmp(inp, out, mode, inv, auto)
    print(f"wrote {out} ({n} bytes, {mode}{', autocontrast' if auto else ''}{', inverted' if inv else ''})")
