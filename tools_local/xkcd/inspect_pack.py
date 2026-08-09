#!/usr/bin/env -S uv run --quiet --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///
"""Render a comic out of the pack with the reader's step positions drawn on it,
so they can be looked at rather than reasoned about.

    tools_local/xkcd/inspect_pack.py --pack fs_agent/xkcd --num 1205 --out /tmp/x.png

Red lines are where each step of the reader lands, walked with the same
halve-then-snap rule the firmware uses. Pink lines are the bottom of each of
those screens. What you are checking is that no red line cuts through a speech
balloon or a line of lettering: that is the entire reason snapping exists.

This deliberately re-implements the rule in Python rather than calling
XkcdCore. For a diagnostic that is a feature -- if the two disagree, one of
them is wrong, and finding that out is the point of looking. The *firmware* has
exactly one implementation, which is what matters.
"""

import argparse
import pathlib
import struct
import sys

INDEX_HEADER_BYTES = 16
INDEX_RECORD_BYTES = 40
MAGIC = 0x44434B58

# Kept in step with XkcdCore.h.
SNAP_TOL_NUM, SNAP_TOL_DEN = 1, 5
GAP_INK_PERCENT, GAP_INK_FLOOR = 3, 12
MIN_GUTTER_ROWS, GUTTER_PAD = 4, 6

POPCOUNT = bytes(bin(i).count("1") for i in range(256))


def read_index(pack: pathlib.Path):
    raw = (pack / "index.dat").read_bytes()
    magic, version, _, count, maxnum = struct.unpack("<IHHII", raw[:INDEX_HEADER_BYTES])
    if magic != MAGIC:
        sys.exit(f"not an xkcd pack: magic {magic:#x}")
    out = []
    for i in range(count):
        off = INDEX_HEADER_BYTES + i * INDEX_RECORD_BYTES
        f = struct.unpack("<HHHHHBBIIBxHHHI", raw[off : off + 32])
        out.append(
            dict(
                num=f[0],
                width=f[1],
                height=f[2],
                stride=f[3],
                year=f[4],
                month=f[5],
                day=f[6],
                imageOffset=f[7],
                textOffset=f[8],
                flags=f[9],
                overviewWidth=f[10],
                overviewHeight=f[11],
                overviewStride=f[12],
                overviewOffset=f[13],
            )
        )
    return out, version, maxnum


def gap_rows(bits: bytes, width: int, height: int, stride: int) -> list[bool]:
    """Which rows carry little enough ink to be a gap."""
    spare = stride * 8 - width
    mask = (0xFF << spare) & 0xFF if spare else 0xFF
    budget = max(GAP_INK_FLOOR, width * GAP_INK_PERCENT // 100)
    out = []
    for y in range(height):
        row = bits[y * stride : (y + 1) * stride]
        ink = (
            sum(POPCOUNT[b] for b in row[: stride - 1])
            + POPCOUNT[row[stride - 1] & mask]
        )
        out.append(ink <= budget)
    return out


def landings(gaps: list[bool], height: int) -> list[int]:
    """Every row a step is allowed to land on: a little above the art that
    resumes after a long enough gap."""
    out, run = [], 0
    for y in range(height):
        if gaps[y]:
            run += 1
            continue
        if run >= MIN_GUTTER_ROWS:
            out.append(max(0, y - GUTTER_PAD))
        run = 0
    return out


def steps(height: int, land: list[int], viewport: int) -> list[int]:
    max_scroll = max(0, height - viewport)
    if max_scroll == 0:
        return [0]
    step = max(1, viewport // 2)
    tol = viewport * SNAP_TOL_NUM // SNAP_TOL_DEN
    out, s = [0], 0
    while s < max_scroll:
        target = s + step
        if target >= max_scroll:
            s = max_scroll
        else:
            best, best_d = target, tol + 1
            for g in land:
                d = abs(g - target)
                if d <= tol and d < best_d:
                    best, best_d = g, d
            s = min(max_scroll, max(0, best))
        out.append(s)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", required=True, type=pathlib.Path)
    ap.add_argument("--num", type=int, required=True)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--viewport", type=int, default=480)
    ap.add_argument(
        "--show-gaps",
        action="store_true",
        help="also draw every candidate landing in green",
    )
    ap.add_argument(
        "--overview",
        action="store_true",
        help="render the whole-comic overview instead of the artwork",
    )
    args = ap.parse_args()

    from PIL import Image, ImageDraw

    records, _version, maxnum = read_index(args.pack)
    rec = next((r for r in records if r["num"] == args.num), None)
    if rec is None:
        sys.exit(
            f"#{args.num} is not in this pack ({len(records)} comics, up to {maxnum})"
        )

    # Which of the two renditions to look at. The page one is what every comic
    # opens as; --closer is the opt-in second image, which only 4% of the
    # archive has.
    if args.overview:
        if not rec["overviewWidth"]:
            sys.exit(f"#{args.num} has no closer view")
        w, h, stride = rec["overviewWidth"], rec["overviewHeight"], rec["overviewStride"]
        offset = rec["overviewOffset"]
    else:
        w, h, stride = rec["width"], rec["height"], rec["stride"]
        offset = rec["imageOffset"]
    bits = (args.pack / "images.dat").read_bytes()[offset : offset + stride * h]
    text = (args.pack / "text.dat").read_bytes()
    title = text[rec["textOffset"] :].split(b"\0", 1)[0].decode("ascii", "replace")

    img = Image.new("1", (w, h), 1)
    px = img.load()
    for y in range(h):
        row = bits[y * stride : (y + 1) * stride]
        for x in range(w):
            if row[x >> 3] & (0x80 >> (x & 7)):
                px[x, y] = 0

    gaps = gap_rows(bits, w, h, stride)
    land = landings(gaps, h)
    stops = steps(h, land, args.viewport)

    canvas = img.convert("RGB")
    d = ImageDraw.Draw(canvas)
    if args.show_gaps:
        for g in land:
            d.line([(0, g), (w, g)], fill=(0, 190, 0), width=1)
    for s in stops:
        d.line([(0, s), (w, s)], fill=(220, 0, 0), width=2)
        bottom = min(h - 1, s + args.viewport - 1)
        d.line([(0, bottom), (w, bottom)], fill=(255, 165, 165), width=1)

    canvas.save(args.out)
    cols = max(1, -(-w // 480))
    fits = "fits, no panning" if h <= args.viewport else f"{len(stops)} steps"
    if cols > 1:
        fits += f", {cols} columns (column 2 reveals {w - 480}px)"
    print(
        f"#{rec['num']} {title}  {w}x{h}  {len(land)} candidate landings  {fits}  -> {args.out}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
