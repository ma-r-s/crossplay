#!/usr/bin/env python3
"""Check every card in a converted deck against the fonts that will draw it.

Rendering one card and looking at it proves that card works. This walks all of
them and asks the questions a screenshot cannot answer at scale:

  * is every codepoint present in **all five** CJK faces? The face is chosen at
    random per card, so a glyph missing from one of them is a card that renders
    correctly four times out of five and as a blank the fifth. That is the
    worst possible failure mode: rare, unreproducible, and indistinguishable
    from a bug in the app.
  * is every codepoint in the Latin fields present in the built-in serif? The
    pinyin carries tone marks, and a missing one is a box in the middle of a
    reading.
  * does the headword fit across the screen at 100px?
  * does the example sentence wrap into a sane number of lines?
  * is any single unbreakable run longer than the renderer's line buffer?

Run it after converting a deck and before trusting it:

    .venv-study/bin/python tools_local/study/check_deck.py \\
        fs_mario/study/mandarin --fonts fs_mario/study/fonts
"""

import argparse
import pathlib
import re
import struct
import sys

# Must match StudyActivity.cpp.
LINE_BYTES = 256
SCREEN_WIDTH = 480
MARGIN = 16
MAX_WIDTH = SCREEN_WIDTH - 2 * MARGIN
# Beyond this the answer side runs into the footer. Six is generous: the
# longest sentence in Mario's deck wraps to three.
MAX_SENTENCE_LINES = 6

DECK_MAGIC = b"XSTUDYD\0"
FIELD_NAMES = [
    "headword",
    "reading",
    "meaning",
    "partOfSpeech",
    "sentence",
    "sentenceReading",
    "sentenceMeaning",
]


def read_deck(path):
    """Yield each note's fields, exactly as StudyDeck parses them."""
    data = path.read_bytes()
    if data[:8] != DECK_MAGIC:
        sys.exit(f"{path} is not a study deck")
    version, fields, _flags, count = struct.unpack_from("<HBBI", data, 8)
    index = struct.unpack_from("<%dI" % (count + 1), data, 16)
    for i in range(count):
        offset = index[i]
        note = []
        for f in range(fields):
            (length,) = struct.unpack_from("<H", data, offset)
            offset += 2
            raw = data[offset : offset + length]
            offset += length
            # The sentence field always ends in two emphasis bytes.
            if f == 4:
                raw = raw[:-2]
            note.append(raw.decode("utf-8"))
        yield i, note


def read_cpfont(path):
    """Return {codepoint: advance_px} and the line height, from a .cpfont."""
    data = path.read_bytes()
    (style_count,) = struct.unpack_from("<B", data, 12)
    if style_count < 1:
        return {}, 0
    (
        _sid,
        n_intervals,
        n_glyphs,
        adv_y,
        _asc,
        _desc,
        _kl,
        _kr,
        _klc,
        _krc,
        _lig,
        offset,
    ) = struct.unpack_from("<B3xIIBhhHHBBBI4x", data, 32)
    intervals = [
        struct.unpack_from("<III", data, offset + i * 12) for i in range(n_intervals)
    ]
    glyph_base = offset + n_intervals * 12
    advances = {}
    for start, end, first in intervals:
        for cp in range(start, end + 1):
            gi = first + (cp - start)
            if gi >= n_glyphs:
                continue
            width, height, adv, _l, _t, _r, _off = struct.unpack_from(
                "<BBHhhH2xI", data, glyph_base + gi * 16
            )
            # advance is 12.4 fixed point; a zero-size glyph is an empty record.
            if width or height or adv:
                advances[cp] = adv / 16.0
    return advances, adv_y


def builtin_serif_coverage(repo_root):
    """Codepoints the built-in Noto Serif can draw, from its generated table."""
    header = repo_root / "lib/EpdFont/builtinFonts/notoserif_16_regular.h"
    if not header.exists():
        return None
    covered = set()
    inside = False
    for line in header.read_text(errors="replace").splitlines():
        if "Intervals[]" in line:
            inside = True
            continue
        if inside:
            if line.strip().startswith("};"):
                break
            m = re.match(r"\s*\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)", line)
            if m:
                covered.update(range(int(m.group(1), 16), int(m.group(2), 16) + 1))
    return covered


def is_cjk(ch):
    o = ord(ch)
    return 0x2E80 <= o <= 0x9FFF or 0xF900 <= o <= 0xFAFF or 0xFF00 <= o <= 0xFFEF


def wrap_lines(text, advances, max_width):
    """Mirror StudyActivity::drawWrapped closely enough to count lines."""
    lines, width, longest_run, run = 1, 0.0, 0, 0
    for ch in text:
        adv = advances.get(ord(ch), 0.0)
        breakable = ch == " " or is_cjk(ch)
        run = 0 if breakable else run + len(ch.encode("utf-8"))
        longest_run = max(longest_run, run)
        if width + adv > max_width and width > 0:
            lines += 1
            width = 0.0
        width += adv
    return lines, longest_run


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("deck", type=pathlib.Path)
    ap.add_argument("--fonts", type=pathlib.Path, required=True)
    ap.add_argument("--headword-size", type=int, default=50)
    ap.add_argument("--sentence-size", type=int, default=17)
    ap.add_argument(
        "--show", type=int, default=6, help="how many examples of each problem to print"
    )
    args = ap.parse_args()

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    families = sorted(p.name for p in args.fonts.iterdir() if p.is_dir())
    if not families:
        sys.exit(f"no font families under {args.fonts}")

    headword_fonts, sentence_fonts = {}, {}
    for family in families:
        hw = args.fonts / family / f"{family}_{args.headword_size}.cpfont"
        st = args.fonts / family / f"{family}_{args.sentence_size}.cpfont"
        if not hw.exists() or not st.exists():
            sys.exit(f"{family} is missing a size ({hw.name} / {st.name})")
        headword_fonts[family] = read_cpfont(hw)
        sentence_fonts[family] = read_cpfont(st)

    serif = builtin_serif_coverage(repo_root)
    print(f"deck   {args.deck}")
    print(f"faces  {', '.join(families)}")
    print()

    problems = {
        "a CJK glyph missing from at least one face": [],
        "a Latin glyph the built-in serif cannot draw": [],
        "headword too wide for the screen": [],
        f"sentence wrapping past {MAX_SENTENCE_LINES} lines": [],
        f"an unbreakable run longer than the {LINE_BYTES}-byte line buffer": [],
    }
    notes = 0
    for index, fields in read_deck(args.deck / "deck.dat"):
        notes += 1
        headword, sentence = fields[0], fields[4]

        for family in families:
            missing_hw = {
                c
                for c in headword
                if is_cjk(c) and ord(c) not in headword_fonts[family][0]
            }
            missing_st = {
                c
                for c in sentence
                if is_cjk(c) and ord(c) not in sentence_fonts[family][0]
            }
            if missing_hw or missing_st:
                problems["a CJK glyph missing from at least one face"].append(
                    f"note {index} in {family}: {''.join(sorted(missing_hw | missing_st))!r}"
                )
                break

        if serif is not None:
            for name, text in zip(FIELD_NAMES, fields):
                if name in ("headword", "sentence"):
                    continue
                bad = {c for c in text if not is_cjk(c) and ord(c) not in serif}
                if bad:
                    problems["a Latin glyph the built-in serif cannot draw"].append(
                        f"note {index} {name}: {''.join(sorted(bad))!r}"
                    )
                    break

        for family in families:
            advances = headword_fonts[family][0]
            width = sum(advances.get(ord(c), 0.0) for c in headword)
            if width > MAX_WIDTH:
                problems["headword too wide for the screen"].append(
                    f"note {index} {headword!r} is {width:.0f}px in {family}"
                )
                break

        worst_lines, worst_run = 0, 0
        for family in families:
            lines, run = wrap_lines(sentence, sentence_fonts[family][0], MAX_WIDTH)
            worst_lines, worst_run = max(worst_lines, lines), max(worst_run, run)
        if sentence and worst_lines > MAX_SENTENCE_LINES:
            problems[f"sentence wrapping past {MAX_SENTENCE_LINES} lines"].append(
                f"note {index}: {worst_lines} lines"
            )
        for name, text in zip(FIELD_NAMES, fields):
            run = max((len(w.encode("utf-8")) for w in text.split(" ")), default=0)
            if run >= LINE_BYTES:
                problems[
                    f"an unbreakable run longer than the {LINE_BYTES}-byte line buffer"
                ].append(f"note {index} {name}: {run} bytes")

    print(
        f"checked {notes} notes across {len(families)} faces "
        f"({notes * len(families)} card/face combinations)\n"
    )
    failed = 0
    for what, found in problems.items():
        mark = "ok  " if not found else "FAIL"
        print(f"  {mark}  {what}: {len(found)}")
        failed += len(found)
        for line in found[: args.show]:
            print(f"          {line}")
        if len(found) > args.show:
            print(f"          ... and {len(found) - args.show} more")
    print()
    print("every card renders" if failed == 0 else f"{failed} problems found")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
