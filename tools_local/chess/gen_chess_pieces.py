#!/usr/bin/env python3
"""Converts the vendored chess piece SVGs into 1-bit bitmap headers.

Set: "celtic" by Maurizio Monge, MIT licensed, vendored under
assets_local/pieces/celtic/ with its LICENSE.

This replaces an earlier hand-drawn set. Drawing our own was a reasonable
engineering exercise and a mediocre design one: the shapes were legible but not
good, and a set several people have already refined beats one drawn from polygon
coordinates in an afternoon.

Celtic was chosen by rendering six candidate sets at the real 48px and
thresholding them exactly as the panel does, then looking. It won because its
black pieces stay unambiguous solid silhouettes and its white pieces reduce to a
clean outline, which is precisely the solid-versus-outline contrast the board
needs. cburnett and merida read as well but are GPLv2+, which would put copyleft
art in an MIT repository; rhosgfx (CC0) blurs into blobs at this size, and
chessnut carries more interior detail than 48px can hold.

Requires rsvg-convert (brew install librsvg).

  uv run --with pillow python firmware/tools_local/chess/gen_chess_pieces.py

Run ./bin/clang-format-fix afterwards. This emits 12 bytes per line and
clang-format rewraps them, so without it every regeneration shows a few
hundred lines of diff over a byte-identical set of arrays.
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

SIZE = 48
# A second, smaller cut for the captured-material strips. Downscaling a 1-bit
# bitmap destroys it, so each size is rasterised from the SVG separately.
SMALL = 24
# Any ink at all becomes black, matching GfxRenderer's BW path. Judging the
# candidates under the real rule is the only way to judge them honestly.
THRESHOLD = 200

PIECES = [
    ("Pawn", "P"),
    ("Knight", "N"),
    ("Bishop", "B"),
    ("Rook", "R"),
    ("Queen", "Q"),
    ("King", "K"),
]

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "assets_local" / "pieces" / "celtic"
OUT = ROOT / "src" / "apps_local" / "chess" / "ChessPieces.h"


def rasterise(svg, png, size):
    subprocess.run(
        [
            "rsvg-convert",
            "-w",
            str(size),
            "-h",
            str(size),
            "--background-color=white",
            str(svg),
            "-o",
            str(png),
        ],
        check=True,
    )
    im = Image.open(png).convert("L")
    px = im.load()
    return [[px[x, y] < THRESHOLD for x in range(size)] for y in range(size)]


def silhouette(mask):
    """Solid outer shape, interior detail discarded.

    Celtic's own drawings carry engraved interior lines. At 48px on a 1-bit panel
    those read as noise rather than detail, and they fight the toy look. Flood
    fill the background inward from the border; everything the fill cannot reach
    is the piece, so enclosed detail (the knight's eye, a mitre slit) closes up
    and we are left with the pure form.

    Computed from the WHITE piece SVG, whose outer boundary is a single closed
    stroke. The black one is a filled shape whose interior lines can touch the
    edge, and the fill would leak through them.
    """
    size = len(mask)
    outside = [[False] * size for _ in range(size)]
    stack = []
    for i in range(size):
        for x, y in ((i, 0), (i, size - 1), (0, i), (size - 1, i)):
            if not mask[y][x] and not outside[y][x]:
                outside[y][x] = True
                stack.append((x, y))
    while stack:
        x, y = stack.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < size and 0 <= ny < size and not mask[ny][nx] and not outside[ny][nx]:
                outside[ny][nx] = True
                stack.append((nx, ny))
    return [[not outside[y][x] for x in range(size)] for y in range(size)]


def erode(mask):
    size = len(mask)
    out = [[False] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            if not mask[y][x]:
                continue
            if all(
                0 <= y + dy < size and 0 <= x + dx < size and mask[y + dy][x + dx]
                for dy in (-1, 0, 1)
                for dx in (-1, 0, 1)
            ):
                out[y][x] = True
    return out


def outline_of(mask, thickness):
    inner = mask
    for _ in range(thickness):
        inner = erode(inner)
    size = len(mask)
    return [[mask[y][x] and not inner[y][x] for x in range(size)] for y in range(size)]


def ink_bounds(mask):
    """Left edge and width of the actual ink.

    The captured strips advanced by a fixed stride, which suited pawns and made
    a queen collide with the rook beside it: every bitmap is 24px wide but the
    ink inside it is not. Packing by real ink width is the only way to get an
    even optical gap between shapes of different widths.
    """
    size = len(mask)
    left, right = size, -1
    for y in range(size):
        for x in range(size):
            if mask[y][x]:
                left = min(left, x)
                right = max(right, x)
    if right < 0:
        return 0, 0
    return left, right - left + 1


def to_bytes(mask):
    size = len(mask)
    row_bytes = (size + 7) // 8
    data = []
    for y in range(size):
        for b in range(row_bytes):
            byte = 0
            for bit in range(8):
                x = b * 8 + bit
                if x < size and mask[y][x]:
                    byte |= 1 << (7 - bit)
            data.append(byte)
    return data


def format_array(name, data):
    lines = [f"static const uint8_t {name}[] = {{"]
    for i in range(0, len(data), 12):
        lines.append("    " + " ".join(f"0x{v:02X}," for v in data[i : i + 12]))
    lines.append("};")
    return "\n".join(lines)


def main():
    if shutil.which("rsvg-convert") is None:
        sys.exit("rsvg-convert not found: brew install librsvg")

    parts = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "// GENERATED by tools_local/chess/gen_chess_pieces.py from the vendored SVGs in",
        "// assets_local/pieces/celtic/. Do not edit by hand.",
        "//",
        '// Piece set: "celtic" by Maurizio Monge (https://github.com/maurimo/chess-art),',
        "// MIT licensed. The licence travels with the SVGs in assets_local/.",
        "//",
        f"// {SIZE}x{SIZE}, 1bpp, MSB first, row-major, bit set = ink.",
        "// Solid forms are the black pieces; Light forms are the white pieces, which",
        "// reduce to an outline because their fill is white and only their stroke",
        "// survives the threshold. Colour therefore reads from the drawing itself and",
        "// the board needs no background chips.",
        "",
        f"namespace chessart {{\n\nconstexpr int kPieceSize = {SIZE};\nconstexpr int kSmallPieceSize = {SMALL};\n",
    ]

    small_ink = {}
    sheet = Image.new("L", (SIZE * len(PIECES), SIZE * 2), 255)
    with tempfile.TemporaryDirectory() as tmp:
        for i, (name, code) in enumerate(PIECES):
            svg = SRC / f"w{code}.svg"
            if not svg.exists():
                sys.exit(f"missing {svg}")
            for row, suffix, size, thickness in ((0, "Solid", SIZE, 0), (1, "Light", SIZE, 3)):
                shape = silhouette(rasterise(svg, pathlib.Path(tmp) / f"{suffix}{code}.png", size))
                mask = shape if thickness == 0 else outline_of(shape, thickness)
                parts.append(format_array(f"k{name}{suffix}", to_bytes(mask)))
                parts.append("")
                for y in range(size):
                    for x in range(size):
                        if mask[y][x]:
                            sheet.putpixel((i * SIZE + x, row * SIZE + y), 0)
            for suffix, thickness in (("Solid", 0), ("Light", 2)):
                shape = silhouette(rasterise(svg, pathlib.Path(tmp) / f"s{suffix}{code}.png", SMALL))
                mask = shape if thickness == 0 else outline_of(shape, thickness)
                parts.append(format_array(f"k{name}{suffix}Small", to_bytes(mask)))
                parts.append("")
                if suffix == "Solid":
                    small_ink[name] = ink_bounds(shape)

    # Indexed by chess piece type: 1=Pawn .. 6=King, matching PIECES order.
    parts.append("// Ink extents of the small cut, so the captured strips can pack by real")
    parts.append("// width instead of a fixed stride. Index by chess::pieceType().")
    parts.append("struct SmallInk {")
    parts.append("  uint8_t left;")
    parts.append("  uint8_t width;")
    parts.append("};")
    parts.append("constexpr SmallInk kSmallInk[7] = {")
    parts.append("    {0, 0},")
    for name, _ in PIECES:
        left, width = small_ink[name]
        parts.append(f"    {{{left}, {width}}},  // {name}")
    parts.append("};")
    parts.append("")
    parts.append("}  // namespace chessart")
    OUT.write_text("\n".join(parts) + "\n")

    preview = (
        pathlib.Path(sys.argv[1])
        if len(sys.argv) > 1
        else OUT.parent / "pieces-preview.png"
    )
    sheet.resize((sheet.width * 6, sheet.height * 6), Image.NEAREST).save(preview)
    print(f"wrote {OUT}")
    print(f"preview {preview}")


if __name__ == "__main__":
    main()
