#!/usr/bin/env -S uv run --quiet --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///
"""Build the xkcd pack for the SD card.

    tools_local/xkcd/build_pack.py --out fs_agent/xkcd

Downloads every comic's metadata and artwork, converts each to the device's
native 1-bit format at **native size** (never scaled), and writes the three
files the app reads. Both the download cache and the pack are resumable:
interrupt it and run it again.

One objective: never pan on two axes if one will do
---------------------------------------------------
Posture and zoom are chosen together to make that true, and it comes out true
for 97% of the archive. Each of the comic's two dimensions lands in one of
three bands against the device's SHORT (480) and LONG (756) sides, so there are
nine cases and no others; `layout()` is that matrix, and the matrix itself is
drawn in docs/xkcd-pack-format.md.

The scale may fall from the target cap height to the floor -- about a sixth --
when that is what removes a panning axis. That single allowance takes the
archive from 68% to 85% needing no panning at all.

What is stored is the artwork at the size its lettering needs, in the posture
that costs the fewest axes. Comics that pan also carry a small whole-comic
OVERVIEW, reached with OK; only 15% need one, and each is at most a screenful.

Both are built here, on a host, with a real resampling filter before a single
dither. That is the whole reason there is a pack: resampling art that is
already 1-bit is mush, and the device cannot hold the greyscale original.

Why the dither comes from convert.cpp
-------------------------------------
The device converts newly downloaded comics itself with
lib/GfxRenderer/BitmapHelpers.cpp. convert.cpp links that same file, so a comic
from the pack and a comic fetched over wifi go through identical arithmetic.
A Python port would be a second implementation of something no user could ever
be told apart, and it would drift.
"""

import argparse
import io
import math
import json
import pathlib
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request

UA = {"User-Agent": "CrossPoint-xkcd-pack/1 (personal e-reader; contact via github)"}

MAGIC = 0x44434B58  # "XKCD"
FORMAT_VERSION = 3
INDEX_HEADER_BYTES = 16
INDEX_RECORD_BYTES = 40

# Kept in step with XkcdCore.h. Asserted against the header at startup so the
# two cannot quietly disagree about how wide a record is.
MAX_COMIC_HEIGHT = 16384

# The panel, and the drawable area (panel minus the reader's bar). Kept in step
# with xkcd::kPanelWidth and xkcdui::readerViewport.
PANEL_WIDTH = 480
PORTRAIT_VIEWPORT_H = 756

# Anything wider than the panel pans sideways, and its width is snapped onto a
# grid of COLUMN_STEP so every column reveals a full step and the last ends
# flush. The anti-sliver guarantee lives in this arithmetic rather than in a
# check, because a runtime "is this column worth it?" test is the shape of rule
# that gave #1606 -- 481px wide -- a second column revealing one pixel.
COLUMN_OVERLAP = 48
COLUMN_STEP = PANEL_WIDTH - COLUMN_OVERLAP  # 432
MAX_COLUMNS = 8

# Widest thing the format may hold.
MAX_COMIC_WIDTH = COLUMN_STEP * MAX_COLUMNS + COLUMN_OVERLAP  # 3504

# Comic 404 does not exist, and that is the joke. Requesting it returns an
# actual 404, so it is skipped by name rather than by error handling.
MISSING = {404}

# The Toybox faces are subset to ASCII, and a glyph the font does not have
# draws as *nothing at all* -- no box, no fallback, no log line. Titles and alt
# text come off someone else's server, so they get folded before they are ever
# stored. Same defect the Hacker News app hit with a real comment that opened
# with a curly apostrophe. See docs/building-apps.md.
FOLD = {
    "‘": "'",
    "’": "'",
    "‚": ",",
    "‛": "'",
    "“": '"',
    "”": '"',
    "„": '"',
    "‟": '"',
    "–": "-",
    "—": "-",
    "‒": "-",
    "―": "-",
    "−": "-",
    "…": "...",
    " ": " ",
    " ": " ",
    " ": " ",
    " ": " ",
    " ": " ",
    " ": " ",
    "•": "*",
    "·": "*",
    "′": "'",
    "″": '"',
    "«": '"',
    "»": '"',
    "‹": "'",
    "›": "'",
    "é": "e",
    "è": "e",
    "ê": "e",
    "ë": "e",
    "á": "a",
    "à": "a",
    "â": "a",
    "ä": "a",
    "å": "a",
    "í": "i",
    "ì": "i",
    "î": "i",
    "ï": "i",
    "ó": "o",
    "ò": "o",
    "ô": "o",
    "ö": "o",
    "ø": "o",
    "ú": "u",
    "ù": "u",
    "û": "u",
    "ü": "u",
    "ñ": "n",
    "ç": "c",
    "ß": "ss",
    "æ": "ae",
    "É": "E",
    "Á": "A",
    "Ö": "O",
    "Ü": "U",
    "Ñ": "N",
    "°": " deg",
    "×": "x",
    "←": "<-",
    "→": "->",
    "∞": "inf",
    "≠": "!=",
    "≤": "<=",
    "≥": ">=",
}


def fold(text: str) -> str:
    """Everything the ASCII font cannot draw, turned into something it can."""
    out = []
    for ch in text:
        if ch in FOLD:
            out.append(FOLD[ch])
        elif ch == "\n" or ch == "\t":
            out.append(" ")
        elif 32 <= ord(ch) < 127:
            out.append(ch)
        # Anything left is genuinely undrawable; dropping it is better than a
        # replacement character the font also lacks.
    return "".join(out).strip()


def get(url: str, tries: int = 4) -> bytes:
    last = None
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                raise
            last = e
        except Exception as e:  # noqa: BLE001 - network, retry anything
            last = e
        time.sleep(1.5 * (attempt + 1))
    raise last  # type: ignore[misc]


def latest_num() -> int:
    return json.loads(get("https://xkcd.com/info.0.json"))["num"]


def fetch_meta(num: int, cache: pathlib.Path) -> dict | None:
    path = cache / "meta" / f"{num}.json"
    if path.exists():
        try:
            return json.loads(path.read_text())
        except json.JSONDecodeError:
            path.unlink()
    try:
        raw = get(f"https://xkcd.com/{num}/info.0.json")
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)
    return json.loads(raw)


def fetch_image(num: int, url: str, cache: pathlib.Path) -> pathlib.Path | None:
    ext = url.rsplit(".", 1)[-1].lower()
    if ext not in ("png", "jpg", "jpeg", "gif"):
        ext = "bin"
    path = cache / "img" / f"{num}.{ext}"
    if path.exists() and path.stat().st_size > 0:
        return path
    try:
        raw = get(url)
    except urllib.error.HTTPError:
        return None
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)
    return path


def to_gray(path: pathlib.Path):
    """Load as 8-bit gray on a white ground.

    The compositing matters and is not optional: a lot of xkcd PNGs are RGBA or
    palette-with-transparency, and converting one of those straight to "L"
    makes every transparent pixel *black*. The comic would arrive as a solid
    slab with white lettering. Filling white first is what the browser does.
    """
    from PIL import Image

    img = Image.open(path)
    if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
        img = img.convert("RGBA")
        ground = Image.new("RGBA", img.size, (255, 255, 255, 255))
        img = Image.alpha_composite(ground, img)
    return img.convert("L")


def cap_height(gray) -> float | None:
    """How tall this comic's lettering is, in source pixels.

    **This is the number that decides whether a comic can be read on a 480px
    panel, and it is not constant across the archive.** Cap height runs from
    3px on a dense infographic to 25px on a big-lettered strip, so any single
    zoom multiplier necessarily over-zooms one class to rescue the other. That
    is exactly the mistake that shipped first: #3266 was zoomed to 1.23x
    because that was two columns, and two columns was a tidy invariant rather
    than a readable result. Its lettering came out 5px tall.

    Measured by connected components: a letter is one blob, so the median blob
    height over letter-shaped blobs tracks cap height. Run length in a column
    does NOT work -- that is stroke width, and it returns 2 for the entire
    archive.
    """
    w, h = gray.size
    px = gray.load()
    parent = {}

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    prev = {}
    box = {}
    nxt = 0
    for y in range(h):
        cur = {}
        run = None
        for x in range(w + 1):
            ink = x < w and px[x, y] < 128
            if ink and run is None:
                run = x
            elif not ink and run is not None:
                lab = None
                for xx in range(max(0, run - 1), min(w, x + 1)):
                    if xx in prev:
                        found = find(prev[xx])
                        if lab is None:
                            lab = found
                        elif found != lab:
                            parent[found] = lab
                if lab is None:
                    lab = nxt
                    nxt += 1
                    parent[lab] = lab
                    box[lab] = [run, y, x - 1, y]
                for xx in range(run, x):
                    cur[xx] = lab
                b = box[find(lab)]
                b[0] = min(b[0], run)
                b[1] = min(b[1], y)
                b[2] = max(b[2], x - 1)
                b[3] = max(b[3], y)
                run = None
        prev = cur

    heights = []
    for lab, b in box.items():
        if find(lab) != lab:
            continue
        bw, bh = b[2] - b[0] + 1, b[3] - b[1] + 1
        # Letter-shaped: not a stray dither dot, not a frame or a long rule.
        if 3 <= bh <= 40 and 2 <= bw <= 60 and bh * 6 >= bw:
            heights.append(bh)
    if len(heights) < 25:
        return None
    heights.sort()
    return heights[len(heights) // 2]


SHORT_SIDE = PANEL_WIDTH  # 480
LONG_SIDE = PORTRAIT_VIEWPORT_H  # 756


class Limits:
    """The three numbers layout() needs, lifted out of argparse so the rule can
    be tested without one. See host-tests/xkcd/test_layout.py."""

    def __init__(self, target_cap=12.0, min_cap=10.0, max_upscale=3.0):
        self.target_cap = target_cap
        self.min_cap = min_cap
        self.max_upscale = max_upscale


def _posture(W, H, cap, across, down, args):
    """Best outcome for one posture: (axes, scale, which axis pans).

    Scale may fall from the target cap height to the floor -- about a sixth --
    if that is what makes an axis stop panning. That single allowance rescues
    28% of the archive from panning at all.
    """
    target, floor = args.target_cap / cap, args.min_cap / cap
    s_w, s_h = across / W, down / H
    fit = min(s_w, s_h)
    if fit >= floor:
        return 0, min(args.max_upscale, max(fit, floor)), "whole"
    if s_w >= floor:
        return 1, min(args.max_upscale, s_w), "down"
    if s_h >= floor:
        return 1, min(args.max_upscale, s_h), "across"
    return 2, min(args.max_upscale, target), "both"


def _taps(W, H, s, across, down):
    w, h = round(W * s), round(H * s)
    c = 1 if w <= across else 1 + math.ceil((w - across) / COLUMN_STEP)
    r = 1 if h <= down else 1 + math.ceil((h - down) / (down / 2))
    return c * r


def layout(W, H, cap, args):
    """Which way round, and how big. Returns (posture, scale, which pans).

    The nine cases of docs/xkcd-pack-format.md, computed rather than tabulated.
    A comic with too little lettering to measure has no readable scale to aim
    at, so it simply fits whole in whichever posture shows it larger.
    """
    if not cap:
        p = min(args.max_upscale, SHORT_SIDE / W, LONG_SIDE / H)
        t = min(args.max_upscale, LONG_SIDE / W, SHORT_SIDE / H)
        return ("turned", t, "whole") if t > p else ("portrait", p, "whole")

    pa, ps, pw = _posture(W, H, cap, SHORT_SIDE, LONG_SIDE, args)
    ta, ts, tw = _posture(W, H, cap, LONG_SIDE, SHORT_SIDE, args)

    # 1. Fewest panning axes. Six of the nine cases end here.
    if pa != ta:
        return ("portrait", ps, pw) if pa < ta else ("turned", ts, tw)

    # 2. Tied, and the SAME axis pans either way: the axis that does not pan
    #    goes on the smallest side that holds it, leaving the longer side for
    #    the one that does. Deciding this by tap count instead ties far too
    #    often -- 334 wide-and-short comics stayed portrait on a tap tie, which
    #    is #1518 zoomed 3x into five columns instead of turned into three.
    if pa == 1 and pw == tw:
        if pw == "across":
            return (
                ("turned", ts, tw)
                if round(H * ts) <= SHORT_SIDE
                else ("portrait", ps, pw)
            )
        return (
            ("portrait", ps, pw) if round(W * ps) <= SHORT_SIDE else ("turned", ts, tw)
        )

    # 3. Still tied: fewest taps, then stay portrait. Turning the device is a
    #    real cost and a tie means turning buys nothing.
    pt = _taps(W, H, ps, SHORT_SIDE, LONG_SIDE)
    tt = _taps(W, H, ts, LONG_SIDE, SHORT_SIDE)
    return ("portrait", ps, pw) if pt <= tt else ("turned", ts, tw)


def snap_width(sw: int, sh: int, scale: float, pans: str) -> float:
    """Put a panning width on the column grid, without breaking layout()'s promise.

    Every column must reveal a full COLUMN_STEP and the last must end flush, so
    a width over the panel is snapped to COLUMN_STEP * N + COLUMN_OVERLAP.

    Two bugs have lived here, which is why it is a function with tests rather
    than six lines inside the build loop:

    * **One column is a legal answer.** Forcing a minimum of two blew a comic
      that came out 481px wide up to 912, where it started panning. Both-axes
      went 2.8% -> 8.9%.
    * **Snapping moves the scale, so it moves the HEIGHT too.** A comic
      layout() promised would pan across only got pushed past the viewport and
      panned both ways. 8.9% -> 6.6%. So the snap steps down until the promise
      layout() made still holds.
    * **Rounding can push a fitting width one pixel over**, and the snap would
      then send a pan-down-only comic to the next grid stop, adding a sideways
      axis. Found by test_layout.py the day it was written; the two above were
      found by hand, months of reading later.

    The invariant, stated once: **this function never increases the number of
    panning axes.**
    """
    # When layout() promised the width would fit, rounding must not be allowed
    # to push it one pixel over: the snap would then send it to the next grid
    # stop and add the very axis layout() had just avoided. Clamp instead.
    if pans in ("whole", "down"):
        if round(sw * scale) > PANEL_WIDTH:
            scale = PANEL_WIDTH / sw
        return scale

    if round(sw * scale) <= PANEL_WIDTH:
        return scale
    cols = max(1, round((sw * scale - COLUMN_OVERLAP) / COLUMN_STEP))
    cols = min(cols, MAX_COLUMNS)
    while cols > 1:
        trial = (COLUMN_STEP * cols + COLUMN_OVERLAP) / sw
        if pans != "across" or round(sh * trial) <= PORTRAIT_VIEWPORT_H:
            break
        cols -= 1
    return (COLUMN_STEP * cols + COLUMN_OVERLAP) / sw


def resample(gray, sw: int, sh: int, scale: float):
    """Scale on the host, with a real filter, before the single dither.

    This is why both renditions are built here rather than on the device:
    enlarging or shrinking 8-bit greyscale and dithering once is a completely
    different thing from resampling art that is already 1-bit, which is mush.
    """
    if abs(scale - 1.0) <= 0.001:
        return gray
    from PIL import Image as _Image

    return gray.resize(
        (max(1, round(sw * scale)), max(1, round(sh * scale))), _Image.LANCZOS
    )


class Converter:
    """One long-lived convert.cpp process for the whole archive."""

    def __init__(self, binary: pathlib.Path):
        self.proc = subprocess.Popen(
            [str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE
        )

    def convert(self, gray) -> tuple[int, bytes]:
        w, h = gray.size
        assert self.proc.stdin and self.proc.stdout
        self.proc.stdin.write(struct.pack("<II", w, h))
        self.proc.stdin.write(gray.tobytes())
        self.proc.stdin.flush()

        head = self.proc.stdout.read(4)
        if len(head) != 4:
            raise RuntimeError("convert died; see its stderr above")
        (stride,) = struct.unpack("<I", head)
        want = stride * h
        bits = b""
        while len(bits) < want:
            chunk = self.proc.stdout.read(want - len(bits))
            if not chunk:
                raise RuntimeError("convert produced a short image")
            bits += chunk
        return stride, bits

    def close(self):
        if self.proc.stdin:
            self.proc.stdin.close()
        self.proc.wait()


def build_convert_binary(here: pathlib.Path, build_dir: pathlib.Path) -> pathlib.Path:
    build_dir.mkdir(parents=True, exist_ok=True)
    binary = build_dir / "xkcd_convert"
    root = here.parent.parent
    sources = [
        here / "convert.cpp",
        root / "src" / "apps_local" / "xkcd" / "XkcdCore.cpp",
        root / "lib" / "GfxRenderer" / "BitmapHelpers.cpp",
    ]
    newest = max(s.stat().st_mtime for s in sources)
    if binary.exists() and binary.stat().st_mtime >= newest:
        return binary
    cmd = [
        "c++",
        "-std=c++17",
        "-O2",
        "-Wall",
        "-Wextra",
        f"-I{here / 'hoststub'}",
        f"-I{root / 'lib' / 'GfxRenderer'}",
        f"-I{root / 'src' / 'apps_local' / 'xkcd'}",
        *[str(s) for s in sources],
        "-o",
        str(binary),
    ]
    subprocess.run(cmd, check=True)
    return binary


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--out",
        required=True,
        type=pathlib.Path,
        help="pack directory, e.g. fs_agent/xkcd",
    )
    ap.add_argument(
        "--cache",
        type=pathlib.Path,
        default=None,
        help="download cache (default: <out>/../.xkcd-cache)",
    )
    ap.add_argument("--first", type=int, default=1)
    ap.add_argument("--last", type=int, default=0, help="0 means the newest comic")
    ap.add_argument(
        "--limit",
        type=int,
        default=0,
        help="stop after N comics (for a quick test pack)",
    )
    ap.add_argument(
        "--rotate-cw",
        action="store_true",
        help="turn sideways comics the other way, if tipping the device "
        "clockwise feels wrong",
    )
    ap.add_argument(
        "--max-upscale",
        type=float,
        default=3.0,
        help="never enlarge a comic by more than this. One comic in the "
        "archive is 106px wide and would otherwise be blown up 4.5x.",
    )
    ap.add_argument(
        "--target-cap",
        type=float,
        default=12.0,
        help="how tall this comic's lettering should be on the panel, in "
        "pixels. The closer view is zoomed until it reaches this and no "
        "further, so a dense infographic zooms hard and a big-lettered strip "
        "barely at all.",
    )
    ap.add_argument(
        "--min-cap",
        type=float,
        default=10.0,
        help="a comic whose lettering is already at least this tall on the "
        "page is readable as it is, and gets no closer view.",
    )
    ap.add_argument(
        "--no-overview",
        action="store_true",
        help="build page renditions only, for a smaller pack with no zoom",
    )
    args = ap.parse_args()

    here = pathlib.Path(__file__).resolve().parent
    cache = args.cache or (args.out.parent / ".xkcd-cache")
    args.out.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)

    binary = build_convert_binary(here, cache / "build")
    last = args.last or latest_num()
    print(
        f"building pack for comics {args.first}..{last} -> {args.out}", file=sys.stderr
    )

    conv = Converter(binary)
    records: list[dict] = []
    skipped: list[tuple[int, str]] = []

    images = open(args.out / "images.dat", "wb")
    text = open(args.out / "text.dat", "wb")

    done = 0
    try:
        for num in range(args.first, last + 1):
            if num in MISSING:
                continue
            if args.limit and done >= args.limit:
                break
            try:
                meta = fetch_meta(num, cache)
            except Exception as e:  # noqa: BLE001
                skipped.append((num, f"metadata: {e}"))
                continue
            if not meta or not meta.get("img"):
                skipped.append((num, "no metadata"))
                continue

            path = fetch_image(num, meta["img"], cache)
            if path is None:
                skipped.append((num, "image download failed"))
                continue
            try:
                gray = to_gray(path)
            except Exception as e:  # noqa: BLE001
                skipped.append((num, f"decode: {e}"))
                continue

            # --- which way round, and how big -------------------------
            #
            # ONE objective, above every other consideration: **never pan on
            # two axes if one will do.** Posture and zoom are chosen together
            # to make that true as often as possible, and it comes out true for
            # 97% of the archive.
            #
            # Each of the comic's two dimensions lands in one of three bands
            # against the device's SHORT (480) and LONG (756) sides, so there
            # are nine cases and no others. See docs/xkcd-pack-format.md for
            # the matrix; `layout()` below is that matrix, computed.
            #
            # The scale is allowed to fall from the target cap height to the
            # floor -- about a sixth -- when that buys a whole axis. That one
            # allowance takes the archive from 68% to 85% no-panning.
            sw, sh = gray.size
            cap = cap_height(gray)
            posture, scale, pans = layout(sw, sh, cap, args)

            if posture == "turned":
                from PIL import Image as _Image

                # Turned so the comic's own top-left ends up at the stored
                # image's top-right; the reader walks it in the comic's frame,
                # not the stored image's. See xkcd::startOf.
                gray = gray.transpose(
                    _Image.ROTATE_90 if args.rotate_cw else _Image.ROTATE_270
                )
                sw, sh = gray.size

            if sh * scale > MAX_COMIC_HEIGHT:
                scale = MAX_COMIC_HEIGHT / sh

            scale = snap_width(sw, sh, scale, pans)

            art = resample(gray, sw, sh, scale)
            aw, ah = art.size
            if aw > MAX_COMIC_WIDTH or ah > MAX_COMIC_HEIGHT or aw == 0 or ah == 0:
                skipped.append((num, f"{aw}x{ah} exceeds the pack limits"))
                continue

            stride, bits = conv.convert(art)

            # The overview, for comics that pan: the whole thing on one screen,
            # reached with OK. Only 15% of the archive needs one, and each is at
            # most a screenful, so it costs a fourteenth of what the old
            # zoomed-in second rendition did.
            overview_bits = None
            overview_stride = overview_h = overview_w = 0
            if not args.no_overview and (aw > PANEL_WIDTH or ah > PORTRAIT_VIEWPORT_H):
                fit = min(PANEL_WIDTH / sw, PORTRAIT_VIEWPORT_H / sh)
                whole = resample(gray, sw, sh, fit)
                overview_w, overview_h = whole.size
                overview_stride, overview_bits = conv.convert(whole)

            title = fold(meta.get("safe_title") or meta.get("title") or f"#{num}")
            alt = fold(meta.get("alt") or "")

            rec = dict(
                num=num,
                width=aw,
                height=ah,
                stride=stride,
                year=int(meta.get("year") or 0),
                month=int(meta.get("month") or 0),
                day=int(meta.get("day") or 0),
                imageOffset=images.tell(),
                textOffset=text.tell(),
                flags=1 if posture == "turned" else 0,
                overviewWidth=0,
                overviewHeight=0,
                overviewStride=0,
                overviewOffset=0,
            )
            images.write(bits)
            if overview_bits is not None:
                # Straight after its own page rendition, so reading one and
                # then the other is a short seek rather than a jump across a
                # 130MB file.
                rec.update(
                    overviewWidth=overview_w,
                    overviewHeight=overview_h,
                    overviewStride=overview_stride,
                    overviewOffset=images.tell(),
                )
                images.write(overview_bits)
            records.append(rec)
            text.write(title.encode("ascii", "ignore") + b"\0")
            text.write(alt.encode("ascii", "ignore") + b"\0")

            done += 1
            if done % 100 == 0:
                print(f"  {done} comics, {images.tell() / 1e6:.1f} MB", file=sys.stderr)
    finally:
        conv.close()
        images.close()
        text.close()

    records.sort(key=lambda r: r["num"])
    with open(args.out / "index.dat", "wb") as f:
        f.write(
            struct.pack(
                "<IHHII",
                MAGIC,
                FORMAT_VERSION,
                0,
                len(records),
                records[-1]["num"] if records else 0,
            )
        )
        for r in records:
            # Byte 21 is reserved, then the closer rendition. Bytes 32..39 are
            # reserved padding so a future field can be added without moving
            # any of the ones above it. Kept in step with xkcd::decodeRecord.
            rec = struct.pack(
                "<HHHHHBBIIBxHHHI",
                r["num"],
                r["width"],
                r["height"],
                r["stride"],
                r["year"],
                r["month"],
                r["day"],
                r["imageOffset"],
                r["textOffset"],
                r["flags"],
                r["overviewWidth"],
                r["overviewHeight"],
                r["overviewStride"],
                r["overviewOffset"],
            )
            assert len(rec) == 32, len(rec)
            f.write(rec + b"\0" * (INDEX_RECORD_BYTES - len(rec)))

    total = (args.out / "images.dat").stat().st_size
    n = max(1, len(records))
    land = sum(1 for r in records if r["flags"] & 1)
    close = sum(1 for r in records if r["overviewWidth"])
    pans = sum(1 for r in records if r["height"] > PORTRAIT_VIEWPORT_H)
    print(f"\n{len(records)} comics, {total / 1e6:.0f} MB of artwork", file=sys.stderr)
    print(
        f"  {len(records) - land} upright, {land} sideways "
        f"({100 * land / n:.0f}% ask you to turn the device)",
        file=sys.stderr,
    )
    print(
        f"  {len(records) - pans} fit on one screen ({100 * (len(records) - pans) / n:.0f}%), "
        f"{pans} pan down",
        file=sys.stderr,
    )
    across = sum(1 for r in records if r["width"] > PANEL_WIDTH)
    downs = sum(1 for r in records if r["height"] > PORTRAIT_VIEWPORT_H)
    both = sum(
        1
        for r in records
        if r["width"] > PANEL_WIDTH and r["height"] > PORTRAIT_VIEWPORT_H
    )
    print(
        f"  panning: {n - across - downs + both} none ({100 * (n - across - downs + both) / n:.0f}%), "
        f"{downs - both} down, {across - both} across, {both} BOTH ({100 * both / n:.1f}%)",
        file=sys.stderr,
    )
    print(
        f"  {close} carry a whole-comic overview ({100 * close / n:.0f}%)",
        file=sys.stderr,
    )

    # The two guarantees this build exists to make, checked against what was
    # actually written rather than asserted in a comment. If either of these
    # ever prints, the sliver is back.
    ragged = [
        r
        for r in records
        if r["width"] > PANEL_WIDTH and (r["width"] - COLUMN_OVERLAP) % COLUMN_STEP
    ]
    huge = [
        r
        for r in records
        if r["overviewWidth"]
        and (r["overviewWidth"] > PANEL_WIDTH or r["overviewHeight"] > PORTRAIT_VIEWPORT_H)
    ]
    if ragged:
        print(
            f"  BROKEN: {len(ragged)} panning widths are off the column grid",
            file=sys.stderr,
        )
    if huge:
        print(
            f"  BROKEN: {len(huge)} overviews do not fit on one screen",
            file=sys.stderr,
        )
    if not ragged and not huge:
        print(
            f"  every panning column reveals a full {COLUMN_STEP}px; "
            f"every overview fits on one screen",
            file=sys.stderr,
        )
    if skipped:
        print(f"skipped {len(skipped)}:", file=sys.stderr)
        for num, why in skipped[:40]:
            print(f"  #{num}: {why}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
