#!/usr/bin/env python3
"""Cut a small xkcd pack out of a full one, for the browser build's SD card.

The real archive is 126MB of images.dat. The browser build downloads its whole
card before it can boot, so shipping that is not an option -- but neither is
faking the app: the page's claim is that this is the firmware, and an xkcd
reader with drawn-on comics would make that a lie.

So this takes the real pack and keeps the first N comics of it. Nothing about
the format changes, and the firmware reads the result with the same code it
reads Mario's card with. That works because the builder appends in comic-number
order, so the blobs for the first N comics are the first bytes of images.dat
and text.dat: the subset is a prefix, and no offset needs rewriting. The script
checks that rather than assuming it.

    python3 tools_local/wasm/pack_subset.py ../fs_mario/xkcd 40

Writes tools_local/wasm/sdcard/xkcd/. Pass a count that keeps the result under
a couple of megabytes; the early comics are small.
"""

import pathlib
import struct
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
OUT = REPO / "tools_local" / "wasm" / "sdcard" / "xkcd"

# From src/apps_local/xkcd/XkcdCore.h. Kept as literals with the header named
# beside them: if the format version moves, this script must fail rather than
# emit a pack the firmware will reject at runtime with "not a pack this build
# can read".
MAGIC = 0x44434B58  # "XKCD"
VERSION = 2
HEADER_BYTES = 16
RECORD_BYTES = 32


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src = pathlib.Path(sys.argv[1]).expanduser().resolve()
    keep = int(sys.argv[2])

    index = (src / "index.dat").read_bytes()
    magic, version, count, max_num = struct.unpack_from("<IHxxII", index, 0)
    if magic != MAGIC or version != VERSION:
        sys.exit(
            f"{src}/index.dat is magic={magic:#x} v{version}; "
            f"this script knows {MAGIC:#x} v{VERSION}. Re-read XkcdCore.h."
        )
    if keep >= count:
        sys.exit(f"pack has {count} comics; asked to keep {keep}")

    records = [
        index[HEADER_BYTES + i * RECORD_BYTES : HEADER_BYTES + (i + 1) * RECORD_BYTES]
        for i in range(count)
    ]

    def offsets(record):
        # decodeRecord() in XkcdCore.cpp: num/width/height/stride/year are
        # u16 at 0..8, month/day are bytes at 10/11, then the two offsets.
        return struct.unpack_from("<II", record, 12)

    # The prefix property this whole approach rests on, asserted rather than
    # assumed: both blobs have to grow monotonically with comic number, or a
    # truncated file would cut a kept comic's data off.
    for i in range(1, count):
        prev_image, prev_text = offsets(records[i - 1])
        image, text = offsets(records[i])
        if image < prev_image or text < prev_text:
            sys.exit(
                f"record {i} goes backwards in the blobs "
                f"(image {prev_image}->{image}, text {prev_text}->{text}); "
                "this pack is not append-ordered and cannot be sliced this way"
            )

    image_end, text_end = offsets(records[keep])
    kept_max = max(struct.unpack_from("<H", r, 0)[0] for r in records[:keep])

    OUT.mkdir(parents=True, exist_ok=True)
    head = struct.pack("<IHxxII", MAGIC, VERSION, keep, kept_max)
    (OUT / "index.dat").write_bytes(head + b"".join(records[:keep]))
    (OUT / "images.dat").write_bytes((src / "images.dat").read_bytes()[:image_end])
    (OUT / "text.dat").write_bytes((src / "text.dat").read_bytes()[:text_end])

    total = sum(p.stat().st_size for p in OUT.iterdir())
    print(
        f"wrote {OUT.relative_to(REPO)}: {keep} comics, newest #{kept_max}, {total / 1024:.0f} KB"
    )


if __name__ == "__main__":
    main()
