#!/usr/bin/env python3
"""Rasterise a .cpfont in monochrome, with hinting.

This exists because of one character. U+4E00 -- yi, "one" -- is a single thin
horizontal stroke, and at sentence size the stock converter renders it as two
rows of roughly 60% coverage. Every per-pixel rule you can apply afterwards
then loses:

    keep any coverage      the stroke comes out two pixels thick, and every
                           other stroke fattens with it until the counters in
                           a dense character close and it reads as a blob
    keep >= 50% coverage   both rows quantise to "light" and the stroke
                           disappears entirely -- 我有一百块钱 rendered as
                           我有 -百块钱, which is a different sentence

No threshold fixes that, because the information needed is not in the
antialiased bitmap. It is in the *outline*: FreeType's hinter, told to target a
1-bit target, snaps that stem to exactly one pixel row before rasterising.
That is what CJK bitmap fonts have always done, and it is what this does.

The device needs no change. `.cpfont` stores two bits per pixel and
`GfxRenderer.cpp:448` draws a pixel for any non-zero value, so emitting only 0
and 3 makes that predicate exact rather than approximate.

## What is borrowed and what is ours

Everything hard -- kerning extraction, kern-class derivation, ligature
extraction, the binary layout, the header and TOC -- comes from upstream's
`fontconvert_sdcard.py` by import. Ours is the glyph loop, which is the only
part that has to differ, and the interval validation it depends on. If upstream
changes `StyleRasterData` or `GlyphProps`, this breaks loudly at import rather
than silently producing a bad font.
"""

import importlib.util
import pathlib
import sys


def load_stock(repo_root):
    """Import upstream's converter as a module without copying it."""
    script = repo_root / "lib/EpdFont/scripts/fontconvert_sdcard.py"
    if not script.exists():
        raise FileNotFoundError(f"stock converter not found at {script}")
    # Its own directory must be importable: it does `from cpfont_version import ...`.
    sys.path.insert(0, str(script.parent))
    spec = importlib.util.spec_from_file_location("fontconvert_sdcard", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    required = (
        "GlyphProps",
        "StyleRasterData",
        "rasterize_font_style",
        "generate_cpfont_multistyle",
    )
    missing = [name for name in required if not hasattr(module, name)]
    if missing:
        raise AttributeError(
            f"upstream converter no longer exposes {missing}; the mono rasteriser needs updating"
        )
    return module


def pack_mono(buf, pitch, width, rows):
    """Pack a FreeType MONO bitmap into the .cpfont's 2-bit-per-pixel layout.

    Every set bit becomes 3 (black) and every clear bit 0 (white); the two
    intermediate greys are never emitted, which is what makes the renderer's
    "any coverage" test behave as a real threshold.

    Pixels run most-significant-pair first within each byte, matching
    `bit_index = (3 - (pixelPosition & 3)) * 2` in GfxRenderer.
    """
    out = bytearray()
    accumulator = 0
    count = 0
    abs_pitch = abs(pitch)
    for y in range(rows):
        # A negative pitch means the rows are stored bottom-up.
        row = y * abs_pitch if pitch >= 0 else (rows - 1 - y) * abs_pitch
        for x in range(width):
            bit = (buf[row + (x >> 3)] >> (7 - (x & 7))) & 1
            accumulator = (accumulator << 2) | (3 if bit else 0)
            count += 1
            if count == 4:
                out.append(accumulator)
                accumulator = 0
                count = 0
    if count:
        # Left-align the tail, exactly as the stock packer does.
        out.append(accumulator << ((4 - count) * 2))
    return bytes(out)


def rasterize_mono(
    stock,
    fontfile,
    size,
    intervals,
    style_id=0,
    force_autohint=False,
    fallback_fontfile=None,
):
    """Drop-in replacement for upstream's rasterize_font_style, rendering 1-bit.

    Signature and return type match upstream's exactly, so it can be installed
    over it and `generate_cpfont_multistyle` never knows the difference.
    """
    import freetype

    face = freetype.Face(fontfile)
    # 150 DPI and this call order are upstream's: load_glyph renders at the
    # active size, so the size has to be set before any glyph is touched.
    face.set_char_size(size << 6, size << 6, 150, 150)
    ligature_glyph_indices = stock.extract_ligature_glyph_indices_fonttools(fontfile)

    fallback_face = None
    if fallback_fontfile:
        fallback_face = freetype.Face(fallback_fontfile)
        fallback_face.set_char_size(size << 6, size << 6, 150, 150)

    # The whole point: TARGET_MONO puts the hinter in 1-bit mode, so stems are
    # snapped to whole pixels, and RENDER produces a 1-bit-per-pixel bitmap.
    load_flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO
    if force_autohint:
        load_flags |= freetype.FT_LOAD_FORCE_AUTOHINT

    def load_glyph(code_point):
        glyph_index = face.get_char_index(code_point)
        if glyph_index == 0:
            glyph_index = ligature_glyph_indices.get(code_point, 0)
        if glyph_index > 0:
            face.load_glyph(glyph_index, load_flags)
            return face
        if fallback_face:
            index = fallback_face.get_char_index(code_point)
            if index > 0:
                fallback_face.load_glyph(index, load_flags)
                return fallback_face
        return None

    # Drop codepoints the font does not have, so the intervals describe what is
    # really there. Only get_char_index here -- loading would rasterise twice.
    validated = []
    for i_start, i_end in intervals:
        start = i_start
        for code_point in range(i_start, i_end + 1):
            has_primary = (
                face.get_char_index(code_point) != 0
                or code_point in ligature_glyph_indices
            )
            has_fallback = (
                fallback_face and fallback_face.get_char_index(code_point) != 0
            )
            if not has_primary and not has_fallback:
                if start < code_point:
                    validated.append((start, code_point - 1))
                start = code_point + 1
        if start <= i_end:
            validated.append((start, i_end))
    intervals = validated

    total_bitmap_size = 0
    all_glyphs = []
    for i_start, i_end in intervals:
        for code_point in range(i_start, i_end + 1):
            f = load_glyph(code_point)
            if f is None:
                all_glyphs.append(
                    (
                        stock.GlyphProps(
                            0, 0, 0, 0, 0, 0, total_bitmap_size, code_point
                        ),
                        b"",
                    )
                )
                continue
            bitmap = f.glyph.bitmap
            packed = pack_mono(bitmap.buffer, bitmap.pitch, bitmap.width, bitmap.rows)
            glyph = stock.GlyphProps(
                width=bitmap.width,
                height=bitmap.rows,
                advance_x=stock.fp4_from_ft16_16(f.glyph.linearHoriAdvance),
                left=f.glyph.bitmap_left,
                top=f.glyph.bitmap_top,
                data_length=len(packed),
                data_offset=total_bitmap_size,
                code_point=code_point,
            )
            total_bitmap_size += len(packed)
            all_glyphs.append((glyph, packed))

    # Metrics come from the face after a load, the same heuristic upstream uses.
    load_glyph(ord("|"))
    advance_y = stock.norm_ceil(face.size.height)
    ascender = stock.norm_ceil(face.size.ascender)
    descender = stock.norm_floor(face.size.descender)

    # Kerning and ligatures are upstream's code, untouched.
    ppem = size * 150.0 / 72.0
    all_cps = set(g.code_point for g, _ in all_glyphs)
    kern_map = stock.extract_kerning_fonttools(fontfile, all_cps, ppem)
    kern_map = {k: v for k, v in kern_map.items() if k[0] <= 0xFFFF and k[1] <= 0xFFFF}
    (kern_left, kern_right, kern_matrix, left_count, right_count) = (
        stock.derive_kern_classes(kern_map)
    )
    ligature_pairs = stock.extract_ligatures_fonttools(fontfile, all_cps)[:255]

    print(
        f"    mono: {len(all_glyphs)} glyphs, {total_bitmap_size / 1024:.0f} KB, advanceY={advance_y}",
        file=sys.stderr,
    )

    return stock.StyleRasterData(
        style_id=style_id,
        intervals=intervals,
        all_glyphs=all_glyphs,
        total_bitmap_size=total_bitmap_size,
        advanceY=advance_y,
        ascender=ascender,
        descender=descender,
        kern_left_classes=kern_left,
        kern_right_classes=kern_right,
        kern_matrix=kern_matrix,
        kern_left_class_count=left_count,
        kern_right_class_count=right_count,
        ligature_pairs=ligature_pairs,
    )


def build(repo_root, fontfile, size, intervals, output_path, force_autohint=False):
    """Write one .cpfont, rasterised in monochrome."""
    stock = load_stock(pathlib.Path(repo_root))
    original = stock.rasterize_font_style

    def patched(
        fontfile_,
        size_,
        intervals_,
        style_id=0,
        force_autohint_=False,
        fallback_fontfile=None,
        **kwargs,
    ):
        return rasterize_mono(
            stock,
            fontfile_,
            size_,
            intervals_,
            style_id,
            force_autohint,
            fallback_fontfile,
        )

    stock.rasterize_font_style = patched
    try:
        stock.generate_cpfont_multistyle(
            {0: str(fontfile)},
            size,
            intervals,
            str(output_path),
            force_autohint=force_autohint,
        )
    finally:
        stock.rasterize_font_style = original
    return pathlib.Path(output_path)


def resolve_intervals(repo_root, preset_string):
    return load_stock(pathlib.Path(repo_root)).resolve_intervals(preset_string)
