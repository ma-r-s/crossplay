#!/usr/bin/env python3
"""Compose simulator screenshots side by side, with a caption over each.

Exists because this machine has neither ImageMagick nor PIL, and because
choosing between two layouts from separate images does not work: the version
that wins usually wins on one specific element, and that is only visible when
they are next to each other. See docs/building-apps.md.

Pure stdlib: PNG is zlib plus a scanline filter, and the simulator writes 8-bit
greyscale or RGB with no interlacing, which is the easy corner of the format.

  tools_local/compose_shots.py out.png "Caption A" a.png "Caption B" b.png ...
"""

import struct
import sys
import zlib

GLYPH_W, GLYPH_H = 5, 7

# A 5x7 caption face, enough for A-Z, 0-9 and a few marks. Hand-set rather than
# pulled from a font file so this script keeps working with nothing installed.
FONT = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "11110", "10001", "10001", "10001", "11110"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "11110", "10000", "10000", "10000", "11111"),
    "F": ("11111", "10000", "11110", "10000", "10000", "10000", "10000"),
    "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "11111", "10001", "10001", "10001", "10001"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "J": ("00111", "00010", "00010", "00010", "10010", "10010", "01100"),
    "K": ("10001", "10010", "11100", "10100", "10010", "10001", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10001", "10001", "10001", "10001"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "01110", "00001", "00001", "10001", "01110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "11011", "10001"),
    "X": ("10001", "01010", "00100", "00100", "00100", "01010", "10001"),
    "Y": ("10001", "01010", "00100", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00110", "01000", "10000", "11111"),
    "3": ("11111", "00010", "00100", "00010", "00001", "10001", "01110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "11110", "00001", "00001", "10001", "01110"),
    "6": ("01110", "10000", "11110", "10001", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00001", "01110"),
    " ": ("00000",) * 7,
    "-": ("00000", "00000", "00000", "11111", "00000", "00000", "00000"),
    ".": ("00000", "00000", "00000", "00000", "00000", "01100", "01100"),
    ":": ("00000", "01100", "01100", "00000", "01100", "01100", "00000"),
    "/": ("00001", "00010", "00010", "00100", "01000", "01000", "10000"),
    "(": ("00010", "00100", "01000", "01000", "01000", "00100", "00010"),
    ")": ("01000", "00100", "00010", "00010", "00010", "00100", "01000"),
    "+": ("00000", "00100", "00100", "11111", "00100", "00100", "00000"),
    ",": ("00000", "00000", "00000", "00000", "01100", "01100", "11000"),
}


def read_png(path):
    """Return (width, height, rows) with rows as bytearrays of RGB triples."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")

    pos = 8
    idat = bytearray()
    width = height = depth = colour = None
    palette = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(
                ">IIBBBBB", body
            )
            if interlace:
                raise ValueError(f"{path}: interlaced PNGs are not handled")
            if depth != 8:
                raise ValueError(f"{path}: only 8 bits per sample, got {depth}")
        elif kind == b"PLTE":
            palette = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels

    out = []
    previous = bytearray(stride)
    at = 0
    for _ in range(height):
        filt = raw[at]
        at += 1
        line = bytearray(raw[at : at + stride])
        at += stride
        # The five PNG filters, undone in place against the row above.
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
            x = line[i]
            if filt == 1:
                x += a
            elif filt == 2:
                x += b
            elif filt == 3:
                x += (a + b) // 2
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                x += a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            line[i] = x & 0xFF
        previous = line

        rgb = bytearray(width * 3)
        for x in range(width):
            if colour == 0:
                v = line[x]
                rgb[x * 3 : x * 3 + 3] = bytes((v, v, v))
            elif colour == 2:
                rgb[x * 3 : x * 3 + 3] = line[x * 3 : x * 3 + 3]
            elif colour == 3:
                i = line[x] * 3
                rgb[x * 3 : x * 3 + 3] = palette[i : i + 3]
            elif colour == 4:
                v = line[x * 2]
                rgb[x * 3 : x * 3 + 3] = bytes((v, v, v))
            else:
                rgb[x * 3 : x * 3 + 3] = line[x * 4 : x * 4 + 3]
        out.append(rgb)
    return width, height, out


def write_png(path, width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw += row
    body = zlib.compress(bytes(raw), 9)

    def chunk(kind, payload):
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        )
        handle.write(chunk(b"IDAT", body))
        handle.write(chunk(b"IEND", b""))


def draw_text(rows, x, y, text, scale, ink=(0, 0, 0)):
    for ch in text.upper():
        # A caption that silently loses characters reads as a deliberate one:
        # "8 MAY 2026" came out "MAY 202" for as long as 6-9 were missing here.
        if ch not in FONT:
            print(f"compose_shots: no glyph for {ch!r}, dropping it", file=sys.stderr)
        glyph = FONT.get(ch, FONT[" "])
        for gy, line in enumerate(glyph):
            for gx, bit in enumerate(line):
                if bit != "1":
                    continue
                for sy in range(scale):
                    py = y + gy * scale + sy
                    if py >= len(rows):
                        continue
                    row = rows[py]
                    for sx in range(scale):
                        px = x + gx * scale + sx
                        if 0 <= px * 3 < len(row):
                            row[px * 3 : px * 3 + 3] = bytes(ink)
        x += (GLYPH_W + 1) * scale
    return x


def main():
    args = sys.argv[1:]
    if len(args) < 3 or len(args) % 2 == 0:
        print(__doc__)
        return 1
    out = args[0]
    pairs = [(args[i], args[i + 1]) for i in range(1, len(args), 2)]

    shots = [(caption, *read_png(path)) for caption, path in pairs]
    cell_w = max(s[1] for s in shots)
    cell_h = max(s[2] for s in shots)
    scale = 3
    band = GLYPH_H * scale + 20
    gap = 16
    width = len(shots) * cell_w + (len(shots) + 1) * gap
    height = band + cell_h + gap * 2

    rows = [bytearray(b"\xd0" * (width * 3)) for _ in range(height)]

    for i, (caption, w, h, pixels) in enumerate(shots):
        x0 = gap + i * (cell_w + gap)
        draw_text(rows, x0, 10, caption, scale)
        for y in range(h):
            dst = rows[band + gap + y]
            src = pixels[y]
            dst[x0 * 3 : (x0 + w) * 3] = src

    write_png(out, width, height, rows)
    print(f"wrote {out}  {width}x{height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
