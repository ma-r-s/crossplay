#!/usr/bin/env python3
"""Pack a deck's sentence images into a 1-bit blob the device can blit.

290 of the 301 cards in Mario's deck carry a SentenceImage -- a photograph
illustrating the example sentence. Anki shows it under the answer; the device
could not, because measuring the answer side showed a third of cards leave
under 160px and the tightest leaves nine. So the image is a second view: the
answer offers it, a tap shows it full screen.

That decision is what sets the size here. The image is not squeezed under the
text, it gets the screen, so it is scaled to fit the body rect and dithered
once at conversion time rather than on a device with no floating point to
spare.

Floyd-Steinberg, via PIL's default for mode "1". Photographs dither far better
than type does -- the reader already uses the same approach for book covers, so
this follows a path rather than inventing one.

    .venv-study/bin/python tools_local/study/make_images.py \\
        --collection ~/…/collection.anki2 --out /Volumes/SDCARD/study/mandarin

Run it after anki_to_deck.py: the card order comes out of the deck's own
cards.dat, so the index can never drift from the notes it belongs to.
"""

import argparse
import pathlib
import re
import sqlite3
import struct
import sys
import urllib.parse

MAGIC = b"XSTUDYI\0"
VERSION = 1

# The body rect on the image view: full screen less the header, less a margin.
# Anything larger would be scaled down by the device, which is work it should
# not be doing.
MAX_W, MAX_H = 448, 620

# Anki's editor writes src="..." but a hand-written template or an imported
# deck may write src='...' or src=... unquoted, and each of those was a note
# whose picture was simply not found.
IMG_RE = re.compile(r"""<img[^>]+src=["']?([^"'>\s]+)""", re.IGNORECASE)


def first_image(parts):
    """The first picture on a note, wherever the note keeps it.

    This read field 18 and nothing else, because that is where SentenceImage
    sits on the HSK note type Mario's deck uses -- so every other deck in the
    world converted with no pictures at all and nothing said why. A picture
    can be in any field: the stock Basic type puts it in Front or Back, and an
    imported deck puts it wherever its author did.

    First match in field order, which is the order Anki shows them in and
    therefore the order the author thought about them in.
    """
    for raw in parts:
        match = IMG_RE.search(raw or "")
        if match:
            return match.group(1)
    return None


def open_collection(path):
    db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    db.create_collation(
        "unicase", lambda a, b: (a.lower() > b.lower()) - (a.lower() < b.lower())
    )
    return db


def card_order(deck_dir):
    """The card ids the deck actually contains, in its own order.

    Read out of cards.dat rather than re-derived from the collection. The
    converter skips notes it cannot map (unknown note type, empty headword), so
    re-running the same query here would silently produce a different order the
    moment one is skipped -- and every image after that point would belong to
    the wrong card. Deriving the order from the file being indexed makes that
    impossible rather than unlikely.
    """
    data = (deck_dir / "cards.dat").read_bytes()
    if len(data) % 32:
        sys.exit(f"{deck_dir / 'cards.dat'} is not a whole number of card records")
    return [struct.unpack_from("<q", data, i * 32)[0] for i in range(len(data) // 32)]


def note_fields(db):
    """Anki card id -> its note's fields, for every card in the collection."""
    out = {}
    for cid, flds in db.execute("select c.id, n.flds from cards c join notes n on n.id = c.nid"):
        out[cid] = flds.split("\x1f")
    return out


def pack_bitmap(image):
    """1 bit per pixel, MSB first, each row padded to a byte boundary.

    Matches what GfxRenderer's bitmap blit expects, and what toybox::blit1bpp
    reads. A set bit is ink.
    """
    width, height = image.size
    stride = (width + 7) // 8
    out = bytearray(stride * height)
    pixels = image.load()
    for y in range(height):
        row = y * stride
        for x in range(width):
            # PIL mode "1": 0 is black. Ink is a set bit.
            if pixels[x, y] == 0:
                out[row + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out), stride


def main():
    from PIL import Image

    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--collection", required=True, type=pathlib.Path)
    ap.add_argument(
        "--out", required=True, type=pathlib.Path, help="the converted deck directory"
    )
    ap.add_argument(
        "--media",
        type=pathlib.Path,
        help="collection.media (else beside the collection)",
    )
    ap.add_argument("--max-width", type=int, default=MAX_W)
    ap.add_argument("--max-height", type=int, default=MAX_H)
    args = ap.parse_args()

    media = args.media or args.collection.parent / "collection.media"
    if not media.is_dir():
        sys.exit(f"no media folder at {media}")

    if not (args.out / "cards.dat").is_file():
        sys.exit(f"no converted deck at {args.out} -- run anki_to_deck.py first")
    order = card_order(args.out)
    fields_by_card = note_fields(open_collection(args.collection))
    print(f"indexing {len(order)} cards, in the order deck.dat has them")

    entries = []  # (offset, width, height, stride) per note, zeros when absent
    blob = bytearray()
    found = missing = skipped = 0

    for card_id in order:
        parts = fields_by_card.get(card_id, [])
        source = first_image(parts)
        if not source:
            entries.append((0, 0, 0, 0))
            skipped += 1
            continue

        # Anki stores media under the name in the field, percent-decoded: the
        # editor writes %20 for a space in a filename and the file on disk has
        # the space.
        path = media / urllib.parse.unquote(source)
        if not path.is_file():
            entries.append((0, 0, 0, 0))
            missing += 1
            continue

        try:
            image = Image.open(path).convert("L")
        except Exception:
            entries.append((0, 0, 0, 0))
            missing += 1
            continue

        image.thumbnail((args.max_width, args.max_height), Image.LANCZOS)
        packed, stride = pack_bitmap(image.convert("1"))
        entries.append((len(blob), image.width, image.height, stride))
        blob += packed
        found += 1

    out = args.out / "images.dat"
    with open(out, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<HHI", VERSION, 0, len(entries)))
        base = len(MAGIC) + 8 + len(entries) * 10
        for offset, w, h, stride in entries:
            # A width of zero means "this note has no image"; the offset is then
            # meaningless and the reader must not follow it.
            f.write(struct.pack("<IHHH", base + offset if w else 0, w, h, stride))
        f.write(blob)

    print(
        f"images: {found} packed, {skipped} cards without one, {missing} referenced but unreadable"
    )
    print(f"  {out}  {out.stat().st_size / 1024 / 1024:.1f} MB")
    if found:
        print(
            f"  mean {len(blob) // found // 1024} KB each, scaled to fit {args.max_width}x{args.max_height}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
