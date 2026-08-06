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

Why native size, and why landscape
----------------------------------
Measured over 1048 comics sampled evenly across the archive: the widest xkcd
ever published is 780px and p99 is 740. The landscape panel is 800. So at 1:1
**nothing is ever downscaled and nothing ever needs to pan sideways** -- 75% of
the archive needs no panning at all. Portrait would mean fit-to-width, which
shrinks 56% of the archive (worst case 0.62), and hand-lettered strokes one or
two pixels wide do not survive that on a 1-bit panel.

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
FORMAT_VERSION = 1
INDEX_HEADER_BYTES = 16
INDEX_RECORD_BYTES = 32

# Kept in step with XkcdCore.h. Asserted against the header at startup so the
# two cannot quietly disagree about how wide a record is.
MAX_COMIC_WIDTH = 800
MAX_COMIC_HEIGHT = 16384

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
        "--max-width",
        type=int,
        default=0,
        help="scale anything wider down to this, preserving aspect ratio "
        "(0 = never scale). Portrait fit-to-width wants 480.",
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

            # Scaled here, on the host, with a real resampling filter -- never
            # on the device. LANCZOS then Atkinson keeps far more of a
            # hand-lettered stroke than anything the firmware could afford at
            # draw time, and the device then blits 1:1 whatever it is handed.
            if args.max_width and gray.size[0] > args.max_width:
                from PIL import Image as _Image

                scale = args.max_width / gray.size[0]
                gray = gray.resize(
                    (args.max_width, max(1, round(gray.size[1] * scale))), _Image.LANCZOS
                )

            w, h = gray.size
            if w > MAX_COMIC_WIDTH or h > MAX_COMIC_HEIGHT or w == 0 or h == 0:
                # Nothing sampled in the archive trips this, but it is reported
                # loudly rather than dropped: a comic missing from the app with
                # no explanation is a bug report waiting to happen.
                skipped.append((num, f"{w}x{h} exceeds the pack limits"))
                continue

            stride, bits = conv.convert(gray)

            title = fold(meta.get("safe_title") or meta.get("title") or f"#{num}")
            alt = fold(meta.get("alt") or "")

            records.append(
                dict(
                    num=num,
                    width=w,
                    height=h,
                    stride=stride,
                    year=int(meta.get("year") or 0),
                    month=int(meta.get("month") or 0),
                    day=int(meta.get("day") or 0),
                    imageOffset=images.tell(),
                    textOffset=text.tell(),
                )
            )
            images.write(bits)
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
            rec = struct.pack(
                "<HHHHHBBII",
                r["num"],
                r["width"],
                r["height"],
                r["stride"],
                r["year"],
                r["month"],
                r["day"],
                r["imageOffset"],
                r["textOffset"],
            )
            # Bytes 20..31 are reserved padding, so a future field can be added
            # without moving any of the ones above it. Kept in step with
            # xkcd::decodeRecord.
            assert len(rec) == 20, len(rec)
            f.write(rec + b"\0" * (INDEX_RECORD_BYTES - len(rec)))

    total = (args.out / "images.dat").stat().st_size
    print(f"\n{len(records)} comics, {total / 1e6:.0f} MB of artwork", file=sys.stderr)
    if skipped:
        print(f"skipped {len(skipped)}:", file=sys.stderr)
        for num, why in skipped[:40]:
            print(f"  #{num}: {why}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
