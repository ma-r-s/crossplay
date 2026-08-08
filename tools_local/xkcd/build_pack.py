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

Two renditions per comic
------------------------
The PAGE rendition is the whole comic, fitted so its full width is on the
panel, so the page view has no horizontal axis and the only motion is down.
93% of the archive opens here and never moves at all.

The CLOSER rendition is for comics that fit-to-width cannot render legible --
the big near-square ones like #3266 and #256, which rotation cannot help. Those
comics OPEN in it, and Confirm pulls back to the whole comic. It is always
exactly two columns wide, which is how the "whole extra view for one pixel of
new artwork" defect is prevented rather than merely tested for.

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

# The page rendition is fitted to the panel width, so it is never wider than
# the panel and the page view therefore has no horizontal axis at all. This is
# the whole repair: the previous build kept any comic wider than 480 at full
# size and read it in columns, which gave #1606 -- 481px wide -- a second
# column revealing one pixel.
MAX_PAGE_WIDTH = PANEL_WIDTH

# The closer rendition is ALWAYS exactly two columns, overlapping by
# COLUMN_OVERLAP so a word split at the seam is readable on both sides. The
# anti-sliver guarantee lives in these numbers rather than in a check, because
# a runtime "is this column worth it?" test is the shape of rule that produced
# the one-pixel column in the first place.
COLUMN_OVERLAP = 48
COLUMN_STEP = PANEL_WIDTH - COLUMN_OVERLAP  # 432
MAX_CLOSER_COLUMNS = 8

# Widest thing the format may hold, which is the closer rendition's ceiling.
MAX_COMIC_WIDTH = COLUMN_STEP * MAX_CLOSER_COLUMNS + COLUMN_OVERLAP  # 3504

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
        "--portrait-width", type=int, default=480, help="the portrait panel's width"
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
        "--rotate-gain",
        type=float,
        default=1.30,
        help="turn a comic sideways when doing so makes it at least this much "
        "bigger. Turning the device has to be earned, so a near-square comic "
        "(which gains almost nothing) stays upright.",
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
        "--max-closer-scale",
        type=float,
        default=6.0,
        help="never enlarge a closer view past this, whatever the lettering "
        "says. A safety bound, not a design constant.",
    )
    ap.add_argument(
        "--no-rotate",
        action="store_true",
        help="never store a comic sideways; shrink wide ones to fit instead",
    )
    ap.add_argument(
        "--no-closer",
        action="store_true",
        help="build page renditions only, for a smaller pack with no zoom",
    )
    ap.add_argument(
        "--fit-height-slack",
        type=float,
        default=0.08,
        help="when fitting the width overflows the screen by less than this, "
        "fit the height instead so the whole comic is on one screen. Stops a "
        "comic from carrying a pan control that moves it three pixels.",
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
            # Two renditions, and one fact underneath both: xkcd letters at a
            # roughly constant size in source pixels, so how far a comic has
            # been *shrunk* is the only thing deciding whether it can be read.
            #
            # 1. WHICH WAY ROUND. Compare what each posture actually buys and
            #    turn the comic when turning it helps by --rotate-gain. The
            #    reader turns the device; the app never turns the panel, so the
            #    bar and the controls never move. (Rotating the panel was the
            #    first attempt and it shuffled the whole UI around underneath
            #    the reader.)
            #
            # 2. THE PAGE RENDITION, which is how every comic opens. Fitted so
            #    its full width is on the panel -- up as well as down, since
            #    44% of the archive is narrower than 480. A sideways comic is
            #    fitted *whole*, because the reason to turn one is to see all
            #    of it. Either way the result is never wider than the panel, so
            #    **the page view has no horizontal axis at all**.
            #
            # 3. THE CLOSER RENDITION, for the 4% that fit-to-width cannot
            #    render legible -- the big near-square ones like #3266 and
            #    #256, which rotation cannot help. Always exactly two columns
            #    wide, so its second column always reveals at least half a
            #    screen of new artwork. Comics that cannot satisfy that get no
            #    closer view, which is the honest answer.
            sw, sh = gray.size
            vw, vh = args.portrait_width, PORTRAIT_VIEWPORT_H

            # Measured on the UPRIGHT source, before any rotation. Cap height
            # is a property of the artwork; measuring it after a transpose
            # returns the letters' *width*, which sent a third of the archive
            # into a closer view it did not need.
            cap = cap_height(gray)

            upright_scale = min(args.max_upscale, vw / sw)
            sideways_scale = min(args.max_upscale, vw / sh, vh / sw)
            sideways = (
                not args.no_rotate
            ) and sideways_scale >= upright_scale * args.rotate_gain

            if sideways:
                from PIL import Image as _Image

                # Turned so the comic's own top ends up on the left of the
                # portrait screen, i.e. you tip the device clockwise to read
                # it. One transpose, and --rotate-cw flips which way.
                gray = gray.transpose(
                    _Image.ROTATE_90 if args.rotate_cw else _Image.ROTATE_270
                )
                sw, sh = gray.size
                page_scale = sideways_scale
            else:
                page_scale = upright_scale

            # A pan control that moves the comic by a few pixels is the same
            # defect as a column that reveals one, and the page view had it
            # too: #3179 is *enlarged* 1.51x to 480x757 and then pans by a
            # single pixel. So when fitting the width overflows the screen by
            # only a little, fit the height instead and put the whole comic on
            # one screen. Costs at most --fit-height-slack of scale and a
            # margin down the sides; removes 96 one-tap-for-nothing controls.
            if not sideways:
                fitted_h = sh * page_scale
                if vh < fitted_h <= vh * (1.0 + args.fit_height_slack):
                    page_scale = vh / sh

            if sh * page_scale > MAX_COMIC_HEIGHT:
                page_scale = MAX_COMIC_HEIGHT / sh

            page = resample(gray, sw, sh, page_scale)
            pw, ph = page.size
            if pw > MAX_PAGE_WIDTH or ph > MAX_COMIC_HEIGHT or pw == 0 or ph == 0:
                # Nothing in the archive trips this, but it is reported loudly
                # rather than dropped: a comic missing from the app with no
                # explanation is a bug report waiting to happen. #2067 once
                # vanished exactly this way, with one line in the build log.
                skipped.append(
                    (num, f"page rendition {pw}x{ph} exceeds the pack limits")
                )
                continue

            stride, bits = conv.convert(page)

            # The closer rendition, for comics the page view cannot render
            # readable. **These OPEN in the closer view**, because showing a
            # comic too small to read and making you ask for the readable one
            # is the wrong way round; OK pulls back to the whole comic.
            #
            # How far to zoom comes from THIS COMIC's lettering, not from a
            # column count. The first version fixed the closer view at two
            # columns because that was a tidy invariant, which zoomed #3266 to
            # 1.23x and left its lettering 5px tall. Cap height across the
            # archive runs 3px to 25px; one multiplier cannot serve both ends.
            closer_bits = None
            closer_stride = closer_h = closer_w = 0
            want = args.max_closer_scale
            if cap:
                want = min(args.max_closer_scale, args.target_cap / cap)
            # Snap to the column grid, so every column reveals a full step and
            # the last one ends flush. This is the anti-sliver guarantee, and
            # it holds at any number of columns.
            cols = max(2, round((sw * want - COLUMN_OVERLAP) / COLUMN_STEP))
            cols = min(cols, MAX_CLOSER_COLUMNS)
            closer_w = COLUMN_STEP * cols + COLUMN_OVERLAP
            closer_scale = closer_w / sw
            if (
                not args.no_closer
                and cap
                and cap * page_scale < args.min_cap
                and closer_scale > page_scale
                and sh * closer_scale <= MAX_COMIC_HEIGHT
            ):
                closer = resample(gray, sw, sh, closer_scale)
                closer_w, closer_h = closer.size
                closer_stride, closer_bits = conv.convert(closer)

            title = fold(meta.get("safe_title") or meta.get("title") or f"#{num}")
            alt = fold(meta.get("alt") or "")

            rec = dict(
                num=num,
                width=pw,
                height=ph,
                stride=stride,
                year=int(meta.get("year") or 0),
                month=int(meta.get("month") or 0),
                day=int(meta.get("day") or 0),
                imageOffset=images.tell(),
                textOffset=text.tell(),
                flags=1 if sideways else 0,
                closerWidth=0,
                closerHeight=0,
                closerStride=0,
                closerOffset=0,
            )
            images.write(bits)
            if closer_bits is not None:
                # Straight after its own page rendition, so reading one and
                # then the other is a short seek rather than a jump across a
                # 130MB file.
                rec.update(
                    closerWidth=closer_w,
                    closerHeight=closer_h,
                    closerStride=closer_stride,
                    closerOffset=images.tell(),
                )
                images.write(closer_bits)
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
                r["closerWidth"],
                r["closerHeight"],
                r["closerStride"],
                r["closerOffset"],
            )
            assert len(rec) == 32, len(rec)
            f.write(rec + b"\0" * (INDEX_RECORD_BYTES - len(rec)))

    total = (args.out / "images.dat").stat().st_size
    n = max(1, len(records))
    land = sum(1 for r in records if r["flags"] & 1)
    close = sum(1 for r in records if r["closerWidth"])
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
    print(
        f"  {close} have a closer view ({100 * close / n:.0f}%)",
        file=sys.stderr,
    )

    # The two guarantees this build exists to make, checked against what was
    # actually written rather than asserted in a comment. If either of these
    # ever prints, the sliver is back.
    wide = [r for r in records if r["width"] > MAX_PAGE_WIDTH]
    thin = [
        r
        for r in records
        if r["closerWidth"]
        and r["closerWidth"] - PANEL_WIDTH < PORTRAIT_VIEWPORT_H // 2
    ]
    if wide:
        print(
            f"  BROKEN: {len(wide)} page renditions are wider than the panel",
            file=sys.stderr,
        )
    if thin:
        print(
            f"  BROKEN: {len(thin)} closer views are not a whole number of columns wide",
            file=sys.stderr,
        )
    if not wide and not thin:
        widest = max((r["closerWidth"] for r in records if r["closerWidth"]), default=0)
        cols = (widest - COLUMN_OVERLAP) // COLUMN_STEP if widest else 0
        print(
            f"  no page rendition pans sideways; every closer column reveals a "
            f"full {COLUMN_STEP}px; widest closer view {widest}px ({cols} columns)",
            file=sys.stderr,
        )
    if skipped:
        print(f"skipped {len(skipped)}:", file=sys.stderr)
        for num, why in skipped[:40]:
            print(f"  #{num}: {why}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
