#!/usr/bin/env python3
"""Put a plausible wallpaper library on a simulator SD card, so the picker can
be rendered against the state a person really has.

    tools_local/wallpapers/seed_sim_card.py fs_agent

A layout judged against an empty card is a layout nobody will ever see
(docs/building-apps.md), and this app is one of the worst for it: the grid draws
thumbnails, the page dots draw a page count, and on a fresh card there is
nothing in any cell.

WHY THIS IS IN THE REPOSITORY. The renders that chose the ADD A WALLPAPER
arrangement were produced from a seeding script that lived in an agent's
scratchpad and a build flag typed on a command line, so nothing in the tree
could reproduce them -- and one of the two files it wrote was named
"kids-on-the-beach.bmp", which is a name the upload route cannot produce at all
(CrossPointWebServer::nextWallpaperPath renames every upload w0007.bmp and
throws the phone's name away). The render looked convincing and showed a
sentence no reader can emit. A harness whose inputs are not in the repository
cannot be checked by anyone, which is the whole of that failure.

So: the built-in stems come from WallpapersCore.cpp's own table, read out of the
source rather than retyped, and the user's own file is named the way the upload
handler names them. If either moves, this stops matching and says so.

No PIL: this machine has neither ImageMagick nor Pillow in the default
environment, and a 1-bit BMP is a header and packed scanlines.

    # the winner's render, end to end, from a clean tree:
    tools_local/wallpapers/seed_sim_card.py fs_agent
    PLATFORMIO_BUILD_FLAGS="-DWALLPAPERS_ADD_ARRIVED=1" ./scripts_local/sim-shot.sh \
      '1800:TAP:150,723;3400:TAP:240,480;7000:TAP:152,290;10000:TAP:240,246;15000:QUIT' \
      '13500:./qa-artifacts/winner.bmp'
"""

import math
import pathlib
import re
import struct
import sys

W, H = 480, 800
ROW = (W + 31) // 32 * 4  # 60 bytes, 4-byte aligned
OFFSET = 14 + 40 + 8  # file header + BITMAPINFOHEADER + 2-entry palette
SIZE = OFFSET + ROW * H  # 48062, which is WallpapersCore::kWallpaperFileBytes

HERE = pathlib.Path(__file__).resolve().parents[2]
CORE = HERE / "src/apps_local/wallpapers/WallpapersCore.cpp"


def built_in_stems():
    """The built-in set's file stems, READ OUT OF THE SOURCE.

    Retyped here they would drift the first time the table changed, and a card
    seeded with stems the app does not know is a card whose every tile draws the
    "did not decode" cross -- which looks like a decoder bug rather than a
    seeding one.
    """
    text = CORE.read_text()
    table = text[
        text.index("constexpr Entry kBuiltIns[]") : text.index(
            "static_assert(sizeof(kBuiltIns)"
        )
    ]
    stems = re.findall(r'\{"([a-z0-9-]+)",', table)
    if len(stems) < 20:
        sys.exit(
            "seed_sim_card: read %d built-in stems from %s; the table has moved"
            % (len(stems), CORE)
        )
    return stems


def upload_name(slot):
    """What the upload handler calls an arrival: wallpapers::uploadFileName."""
    return "w%04d.bmp" % slot


def pattern(kind, x, y):
    """True means ink. Deterministic, and varied enough that adjacent cells in
    the 2x2 grid do not look like the same picture twice."""
    k = kind % 7
    if k == 0:
        return ((x // 40) + (y // 40)) % 2 == 0
    if k == 1:
        return (x * x + y * y) % 9973 < 3000
    if k == 2:
        return math.hypot(x - W / 2, y - H / 2) % 70 < 26
    if k == 3:
        return (x + y) % 36 < 12
    if k == 4:
        return (x % 48 < 6) or (y % 64 < 6)
    if k == 5:
        return math.sin(x / 26.0) + math.cos(y / 34.0) > 0.35
    return ((x // 24) % 3 == 0) != ((y // 24) % 4 == 0)


def write_bmp(path, kind):
    hdr = struct.pack("<2sIHHI", b"BM", SIZE, 0, 0, OFFSET)
    # biHeight positive: bottom-up, which is the ordinary BMP the tools emit.
    info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 1, 0, ROW * H, 2835, 2835, 2, 2)
    palette = struct.pack("<8B", 0, 0, 0, 0, 255, 255, 255, 0)  # 0 = ink, 1 = paper
    rows = []
    for ry in range(H):
        y = H - 1 - ry
        row = bytearray(ROW)
        for x in range(W):
            if not pattern(kind, x, y):  # paper bit set
                row[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(row))
    data = hdr + info + palette + b"".join(rows)
    assert len(data) == SIZE, (len(data), SIZE)
    path.write_bytes(data)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = pathlib.Path(sys.argv[1]).resolve()
    lib = root / "wallpapers"
    lib.mkdir(parents=True, exist_ok=True)
    names = [stem + ".bmp" for stem in built_in_stems()]
    # ONE ARRIVAL, named the way an upload really is. Without it the screenshot
    # harness's WALLPAPERS_ADD_ARRIVED refuses to pick anything and says so,
    # which is the correct failure: it will not substitute a built-in with an
    # editorial name for a picture somebody's phone sent.
    names.append(upload_name(7))
    for i, name in enumerate(names):
        write_bmp(lib / name, i)
    print(
        "seeded %d wallpapers into %s (including %s, the upload shape)"
        % (len(names), lib, upload_name(7))
    )


main()
