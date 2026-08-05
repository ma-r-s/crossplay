#!/usr/bin/env python3
"""Build the study app's CJK fonts from the ones already in Anki's media folder.

Mario's card template randomises the hanzi across five faces, and the five TTFs
are already sitting in `collection.media` as `_simsun.ttf` and friends. This
script turns those exact files into `.cpfont` files the device can read, so the
device randomises across the same five faces he has been reading in Anki.

    tools_local/study/make_fonts.py \
        --media ~/Library/Application\\ Support/Anki2/User\\ 1/collection.media \
        --deck /tmp/studytest/mandarin \
        --out /Volumes/SDCARD/.fonts

Needs fonttools and freetype-py. The repo keeps a venv for this:

    uv venv .venv-study && uv pip install --python .venv-study/bin/python fonttools freetype-py
    .venv-study/bin/python tools_local/study/make_fonts.py ...

## Two things this does that the stock converter does not

**Subsetting.** A full CJK face is ~21000 glyphs; this deck uses 2769. At the
headword size a glyph bitmap is the dominant cost, so rasterising the other
18000 would multiply the output by eight for characters Mario will never see.
The subset keeps the codepoints the deck actually contains, and the converter
emits an empty record for the rest -- which is what lets us keep using a single
broad interval, so the interval table that gets loaded into RAM stays tiny
while the waste stays on the SD card where it is free.

**Thresholding.** This is the important one. `GfxRenderer.cpp:448` draws a
pixel for *any* non-zero coverage:

    if (renderMode == GfxRenderer::BW && bmpVal < 3) { drawPixel(...); }

so every antialiased edge floods to solid black. On Latin type that fattens the
stems; on a 20-stroke hanzi it closes the counters and the character turns into
a blob. The fix needs no firmware change: the on-disk format is 2-bit, and if
the only values present are 0 and 3, "any coverage" and "50% coverage" are the
same test. So after conversion every 2-bit sample is remapped

    0, 1 -> 0   (off)      2, 3 -> 3   (on)

which turns the BW path into a correctly thresholded 1-bit blit by construction.
Keeping it as a post-process rather than a patch to `fontconvert_sdcard.py` is
deliberate: that file is upstream's, and this fork's whole merge strategy rests
on not editing it.
"""

import argparse
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

# The five faces Mario's HSK card template randomises between, and the family
# name each becomes on the device. Order is the order they appear in the
# template's `var fonts = [...]`.
FACES = [
    ("_simsun.ttf", "SimSun"),
    ("_simhei.ttf", "SimHei"),
    ("_msyahei.ttf", "MicrosoftYaHei"),
    ("_kaiti.ttf", "KaiTi"),
    ("_fangsong.ttf", "FangSong"),
]

CPFONT_MAGIC = b"CPFONT\x00\x00"
HEADER_SIZE = 32
STYLE_TOC_ENTRY_SIZE = 32
STYLE_TOC_FORMAT = "<B3xIIBhhHHBBBI4x"
GLYPH_STRUCT_SIZE = 16
INTERVAL_STRUCT_SIZE = 12

# Broad enough to cover every CJK codepoint the deck uses, and narrow enough
# that the resident interval table stays at a handful of entries. Codepoints in
# range but absent from the subset become empty records on disk.
INTERVALS = "cjk,punctuation"


def subset(src, codepoints, dest):
    """Cut a font down to the codepoints the deck uses."""
    from fontTools import subset as ftsubset

    options = ftsubset.Options()
    options.set(layout_features=["*"], notdef_outline=True, recalc_bounds=True)
    options.drop_tables += ["DSIG"]
    font = ftsubset.load_font(str(src), options)
    subsetter = ftsubset.Subsetter(options=options)
    subsetter.populate(unicodes=[ord(c) for c in codepoints])
    subsetter.subset(font)
    ftsubset.save_font(font, str(dest), options)
    font.close()


def threshold_bitmaps(path):
    """Collapse every 2-bit sample to fully-on or fully-off.

    Returns (samples_changed, total_samples). See the module docstring for why
    this is what makes dense hanzi legible through the BW blit path.
    """
    data = bytearray(path.read_bytes())
    if data[:8] != CPFONT_MAGIC:
        raise ValueError(f"{path} is not a .cpfont")
    (style_count,) = struct.unpack_from("<B", data, 12)

    # Work out where each style's bitmap section starts. Sections are laid out
    # in a fixed order after the style's dataOffset, and every one of them is
    # sized by a count in the TOC -- so the bitmaps are simply whatever is left.
    regions = []
    for i in range(style_count):
        entry = HEADER_SIZE + i * STYLE_TOC_ENTRY_SIZE
        (
            _sid,
            n_intervals,
            n_glyphs,
            _advY,
            _asc,
            _desc,
            n_kl,
            n_kr,
            n_klc,
            n_krc,
            n_lig,
            offset,
        ) = struct.unpack_from(STYLE_TOC_FORMAT, data, entry)
        start = offset
        start += n_intervals * INTERVAL_STRUCT_SIZE
        start += n_glyphs * GLYPH_STRUCT_SIZE
        start += n_kl * 3 + n_kr * 3
        start += n_klc * n_krc
        start += n_lig * 8
        regions.append(start)

    ends = regions[1:] + [len(data)]
    # A style's bitmaps run to the next style's data, or to end of file.
    for i in range(style_count):
        if i + 1 < style_count:
            entry = HEADER_SIZE + (i + 1) * STYLE_TOC_ENTRY_SIZE
            ends[i] = struct.unpack_from(STYLE_TOC_FORMAT, data, entry)[11]

    # 0 -> 0, 1 -> 0, 2 -> 3, 3 -> 3, applied to all four samples in a byte.
    table = bytes(
        sum((3 if ((b >> shift) & 3) >= 2 else 0) << shift for shift in (0, 2, 4, 6))
        for b in range(256)
    )

    changed = total = 0
    for start, end in zip(regions, ends):
        if end <= start:
            continue
        chunk = data[start:end]
        mapped = chunk.translate(table)
        changed += sum(1 for a, b in zip(chunk, mapped) if a != b)
        total += len(chunk)
        data[start:end] = mapped

    path.write_bytes(bytes(data))
    return changed, total


def convert(script, font, name, size, out_dir, work):
    """Run the stock converter for one face at one size."""
    result = subprocess.run(
        [
            sys.executable,
            str(script),
            str(font),
            "--intervals",
            INTERVALS,
            "--size",
            str(size),
            "--style",
            "regular",
            "--name",
            name,
            "-o",
            str(out_dir / f"{name}_{size}.cpfont"),
        ],
        cwd=str(script.parent),
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        sys.exit(
            f"converting {name} at {size}px failed:\n{result.stdout}\n{result.stderr}"
        )
    return out_dir / f"{name}_{size}.cpfont"


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--media",
        required=True,
        type=pathlib.Path,
        help="Anki collection.media directory",
    )
    ap.add_argument(
        "--deck",
        required=True,
        type=pathlib.Path,
        help="converted deck dir (for glyphs-*.txt)",
    )
    ap.add_argument(
        "--out",
        required=True,
        type=pathlib.Path,
        help="output font root, e.g. SD /.fonts",
    )
    ap.add_argument("--headword-size", type=int, default=50)
    ap.add_argument("--sentence-size", type=int, default=17)
    ap.add_argument("--only", help="build just this family (for a quick look)")
    ap.add_argument(
        "--no-threshold",
        action="store_true",
        help="skip thresholding, to compare against it",
    )
    args = ap.parse_args()

    script = (
        pathlib.Path(__file__).resolve().parents[2]
        / "lib/EpdFont/scripts/fontconvert_sdcard.py"
    )
    if not script.exists():
        sys.exit(f"stock converter not found at {script}")

    headword = (args.deck / "glyphs-headword.txt").read_text(encoding="utf-8")
    sentence = (args.deck / "glyphs-sentence.txt").read_text(encoding="utf-8")

    total = 0
    for filename, family in FACES:
        if args.only and args.only != family:
            continue
        src = args.media / filename
        if not src.exists():
            print(f"  skip {family}: no {filename} in the media folder")
            continue
        out_dir = args.out / family
        out_dir.mkdir(parents=True, exist_ok=True)

        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            for label, chars, size in (
                ("headword", headword, args.headword_size),
                ("sentence", sentence, args.sentence_size),
            ):
                cut = tmp / f"{family}-{label}.ttf"
                subset(src, chars, cut)
                produced = convert(script, cut, family, size, out_dir, tmp)
                note = ""
                if not args.no_threshold:
                    changed, samples = threshold_bitmaps(produced)
                    note = f", {100.0 * changed / max(samples, 1):.0f}% of bytes thresholded"
                size_kb = produced.stat().st_size / 1024
                total += produced.stat().st_size
                print(
                    f"  {family:16} {label:8} {size:3}px  {len(chars):5} glyphs  {size_kb:7.0f} KB{note}"
                )

    print(f"\ntotal {total / (1024 * 1024):.1f} MB -> {args.out}")


if __name__ == "__main__":
    main()
