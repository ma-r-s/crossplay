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

**Monochrome rasterisation.** `GfxRenderer.cpp:448` draws a pixel for any
non-zero coverage, so an antialiased edge floods to solid black: stems fatten,
counters close, and a twenty-stroke hanzi turns to mush. The obvious fix -- the
format is 2-bit, so collapse every sample to 0 or 3 and the renderer's test
becomes a real threshold -- was tried, shipped, and was wrong. U+4E00 (yi,
"one") is one thin horizontal stroke that the stock converter quantises to
*light* along its whole length, so a 50% cut erased it and 我有一百块钱
rendered as 我有 -百块钱: a different sentence.

No per-pixel rule could have worked, because the information was not in the
antialiased bitmap. It is in the outline. `mono_cpfont.py` rasterises through
FreeType with `FT_LOAD_TARGET_MONO`, so the hinter snaps each stem to a whole
pixel *before* rasterising -- which is what CJK bitmap fonts have always done.
Measured on SimSun at sentence size: U+4E00 goes from two fat rows (64 px of
ink) to one clean stroke with its serif (33 px), and 统 from 314 px to 243 with
its counters open.

`--antialiased` keeps the old path for comparison, and `--threshold` the old
mistake, because that comparison is how the mistake was found.
"""

import argparse
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import mono_cpfont  # noqa: E402  (needs the path line above)
import scripts  # noqa: E402

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

# The intervals are CHOSEN PER DECK, by scripts.intervals_for, from the
# characters the deck actually contains.
#
# It was the fixed string "latin-ext,cjk,punctuation", which had two problems
# pulling in opposite directions. Hangul is in none of those presets, so a
# Korean deck built a face with no interval its characters could live in and
# every card drew empty -- and CJK Extension A, where a few thousand rare
# hanzi and kanji sit, was missing the same way. Meanwhile simply adding both
# to the fixed string would make every Chinese deck pay for them: a codepoint
# inside an interval but absent from the subset still costs an empty 16-byte
# record on the card, and `hangul` alone is 11184 of them.
#
# latin-ext is always in, whatever the deck. The device draws a headword in
# the deck's own face whatever script the headword is, so a face with no ASCII
# draws nothing at all for "T恤" or for a deck of English words -- silently,
# because a missing glyph is simply not painted. The subset carries the
# letters; this is what lets them through.


def deck_headwords(deck_dir):
    """Every headword in the converted deck, straight out of deck.dat."""
    data = (deck_dir / "deck.dat").read_bytes()
    _version, fields, _flags, count = struct.unpack_from("<HBBI", data, 8)
    index = struct.unpack_from("<%dI" % (count + 1), data, 16)
    out = []
    for i in range(count):
        offset = index[i]
        (length,) = struct.unpack_from("<H", data, offset)
        out.append(data[offset + 2 : offset + 2 + length].decode("utf-8"))
    return out


def fit_headword_size(font_path, deck_dir, wanted, max_width):
    """The largest size (capped at `wanted`) where every word fits the screen.

    Measured from the font's own advance table in em units, so no rasterising
    happens: px = ems * size * 2.38 at the converter's 150dpi. Hinting moves
    real widths by a pixel or two, so 5% is held back; the device's per-card
    fallback catches anything that still escapes.
    """
    from fontTools.ttLib import TTFont

    font = TTFont(str(font_path))
    upem = font["head"].unitsPerEm
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]

    def word_ems(word):
        total = 0
        for ch in word:
            glyph = cmap.get(ord(ch))
            total += hmtx[glyph][0] if glyph else upem
        return total / upem

    widest = 0.0
    for headword in deck_headwords(deck_dir):
        for word in headword.split(" "):
            widest = max(widest, word_ems(word))
    font.close()
    if widest <= 0:
        return wanted
    fits = int(max_width * 0.95 / (widest * 2.38))
    return max(12, min(wanted, fits))


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


def convert(script, font, name, size, out_dir, mono, interval_str, name_size=None):
    """Build one face at one size, over the intervals this deck needs.

    `mono` rasterises through FreeType's 1-bit hinting (mono_cpfont.py), which
    is the default and the reason thin strokes survive. The antialiased path is
    kept only so the two can be compared, because that comparison is how the
    thresholding mistake was found in the first place.
    """
    output = out_dir / f"{name}_{name_size if name_size is not None else size}.cpfont"
    if mono:
        repo_root = script.parents[3]
        intervals = mono_cpfont.resolve_intervals(repo_root, interval_str)
        mono_cpfont.build(repo_root, font, size, intervals, output)
        return output

    result = subprocess.run(
        [
            sys.executable,
            str(script),
            str(font),
            "--intervals",
            interval_str,
            "--size",
            str(size),
            "--style",
            "regular",
            "--name",
            name,
            "-o",
            str(output),
        ],
        cwd=str(script.parent),
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        sys.exit(
            f"converting {name} at {size}px failed:\n{result.stdout}\n{result.stderr}"
        )
    return output


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--media",
        type=pathlib.Path,
        help="Anki collection.media directory (holds _simsun.ttf and friends)",
    )
    ap.add_argument(
        "--font",
        type=pathlib.Path,
        help="build one family, 'Custom', from this TTF instead of the five"
        " CJK faces -- for decks in any other language",
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
    # The furigana cut. 10 against the sentence's 17 is roughly the half-size
    # ratio Japanese typesetting uses for ruby, and at the converter's 2.38x
    # it lands near 24px -- small enough to sit above a 40px line and still
    # large enough that four kana are legible on e-ink.
    ap.add_argument("--ruby-size", type=int, default=10)
    ap.add_argument("--only", help="build just this family (for a quick look)")
    ap.add_argument(
        "--antialiased",
        action="store_true",
        help="rasterise the stock way instead of 1-bit. For comparing against the mono path.",
    )
    ap.add_argument(
        "--threshold",
        action="store_true",
        help="with --antialiased, collapse samples to 0 or 3. Erases thin strokes; see the docstring.",
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

    # The Latin set goes into both CJK cuts as well as its own.
    #
    # The device draws a headword in the randomised CJK face whatever script the
    # headword is, so a face with no ASCII draws nothing at all for "T恤" or for
    # a deck of English words -- silently, since a missing glyph is simply not
    # painted. 131 codepoints against 2663 is nothing; a blank headword is not.
    latin = (args.deck / "glyphs-latin.txt").read_text(encoding="utf-8")
    headword += latin
    sentence += latin

    # Furigana. The readings are drawn above the text they read, so they need
    # a face of their own at a size the base is not -- and only the readings
    # go in it, which is why a deck with furigana pays about a hundred kana
    # rather than its whole sentence set at a third size.
    #
    # Absent or empty means this deck has no furigana, and no third cut is
    # built. A deck converted before glyphs-ruby.txt existed reads as the
    # same thing, which is correct: it has no ruby markup in it either.
    ruby_path = args.deck / "glyphs-ruby.txt"
    ruby = ruby_path.read_text(encoding="utf-8") if ruby_path.is_file() else ""
    if ruby:
        ruby += latin

    intervals = scripts.intervals_for(scripts.scripts_in([headword, sentence, ruby]))
    print(f"  intervals {intervals}")

    # --font swaps the whole table for one family named Custom. The device
    # knows that name (StudyFonts::kFamilies) and rolls over whichever families
    # are present, so one family simply means no randomisation.
    if args.font:
        if not args.font.is_file():
            sys.exit(f"no font at {args.font}")
        faces = [(args.font, "Custom")]
        # Fit the headword size to the deck rather than trusting the CJK
        # default. 50 is right for hanzi, which are one em wide and four of
        # them make a long headword; "incontrovertible" at the same size hangs
        # off both edges of the screen -- 927 of the GRE deck's headwords did.
        # The size is chosen so the widest single word fits, so overflow is
        # impossible by construction instead of caught later. The file is still
        # named _<headword-size>.cpfont: the name is the device's lookup key,
        # not a measurement.
        fitted = fit_headword_size(
            args.font, args.deck, args.headword_size, max_width=448
        )
        if fitted != args.headword_size:
            print(f"  headword size {args.headword_size} -> {fitted} so the longest word fits")
    elif args.media:
        faces = [(args.media / filename, name) for filename, name in FACES]
    else:
        sys.exit("need --media (CJK faces from Anki) or --font (any TTF)")

    total = 0
    built = 0
    for src, family in faces:
        if args.only and args.only != family:
            continue
        if not src.exists():
            print(f"  skip {family}: no {src.name} in the media folder")
            continue
        out_dir = args.out / family
        out_dir.mkdir(parents=True, exist_ok=True)

        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            headword_size = fitted if args.font else args.headword_size
            cuts = [
                ("headword", headword, headword_size),
                ("sentence", sentence, args.sentence_size),
            ]
            if ruby:
                cuts.append(("ruby", ruby, args.ruby_size))
            for label, chars, size in cuts:
                cut = tmp / f"{family}-{label}.ttf"
                subset(src, chars, cut)
                produced = convert(script, cut, family, size, out_dir, mono=not args.antialiased,
                                   interval_str=intervals,
                                   name_size=args.headword_size if label == "headword" else None)
                note = ""
                if args.threshold:
                    changed, samples = threshold_bitmaps(produced)
                    note = f", {100.0 * changed / max(samples, 1):.0f}% of bytes thresholded"
                size_kb = produced.stat().st_size / 1024
                total += produced.stat().st_size
                built += 1
                print(
                    f"  {family:16} {label:8} {size:3}px  {len(chars):5} glyphs  {size_kb:7.0f} KB{note}"
                )

    if built == 0:
        sys.exit(
            "no faces built. A CJK deck wants _simsun.ttf and friends in your"
            " Anki media folder; any other deck can use --font YourFont.ttf."
        )
    print(f"\ntotal {total / (1024 * 1024):.1f} MB -> {args.out}")


if __name__ == "__main__":
    main()
