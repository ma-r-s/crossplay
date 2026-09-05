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
META_MAGIC = b"XSTUDYM\0"
META_SENTENCE_ON_QUESTION = 1 << 0
FIELD_NAMES = [
    "headword",
    "reading",
    "meaning",
    "partOfSpeech",
    "sentence",
    "sentenceReading",
    "sentenceMeaning",
    # deck.dat v3. A note with text here is a cloze card; a v2 deck has no
    # such field at all, which zip() below handles by simply running out.
    "clozeQuestion",
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


def sentence_on_question(deck_dir):
    """Whether this deck shows its example sentence while asking the question.

    Read from meta.dat rather than assumed, because the whole point of the
    face check below is to compare against what the device will really draw.
    """
    path = deck_dir / "meta.dat"
    if not path.is_file():
        return False
    head = path.read_bytes()[:12]
    if len(head) < 12 or head[:8] != META_MAGIC:
        return False
    (flags,) = struct.unpack_from("<H", head, 10)
    return bool(flags & META_SENTENCE_ON_QUESTION)


def check_faces(fields, on_question):
    """What each face of one card will show, and what is wrong with it.

    Rendering is only half of "will this deck work". The other half is
    whether the two faces are a question and an answer: a card whose front
    is blank cannot be asked, a card whose back adds nothing cannot be
    answered, and a card whose front already contains its own answer is
    worse than useless because it teaches you that you knew it.

    This check exists because none of that was verified until a Barron's SAT
    deck put every card's example sentence on the question face, and the
    only thing that noticed was a human reading the screen.
    """
    padded = list(fields) + [""] * (len(FIELD_NAMES) - len(fields))
    headword, reading, meaning, pos, sentence, s_reading, s_meaning, cloze = padded

    if cloze.strip():
        # A cloze card asks with the hole and answers by filling it, so the two
        # faces share their text on purpose and every check below that reads
        # sharing as a leak would fire on all of them. What can still go wrong
        # is a hole that never got made: a question identical to its answer is
        # a card that gives itself away.
        problems = []
        if not sentence.strip():
            problems.append("nothing on the answer face")
        identical = cloze.strip() == sentence.strip()
        if identical:
            problems.append("the question face shows no hole: it is the answer")
        return cloze, "\n".join(filter(None, [sentence, meaning])), problems, identical

    question = [headword] + ([sentence] if on_question else [])
    answer = [reading, meaning, pos, s_reading, s_meaning]
    if not on_question:
        answer.append(sentence)

    problems = []
    identical = False
    if not headword.strip():
        problems.append("nothing on the question face")
    if not any(part.strip() for part in answer):
        problems.append("nothing on the answer face that was not already on the question")

    # The answer, given away by the question -- but only when the question is
    # genuinely bigger than the answer it contains. A card whose word and
    # meaning are simply the same string is a cognate ("Moment" / "moment"),
    # and a German deck has hundreds: calling those broken cried wolf on 320
    # perfectly good cards. What that pattern is really for is one field
    # mapped to two slots, and THAT shows up as most of the deck at once, so
    # it is counted here and judged deck-wide by the caller.
    lowered_question = " ".join(question).lower().strip()
    lowered_meaning = meaning.lower().strip()
    if lowered_meaning and lowered_meaning == lowered_question:
        identical = True
    elif len(lowered_meaning) >= 4 and lowered_meaning in lowered_question:
        problems.append("the answer is already visible on the question face")
    return question, answer, problems, identical


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
    ap.add_argument(
        "--fonts",
        type=pathlib.Path,
        help="the deck's font directory; omit for a deck that uses only the built-in face",
    )
    ap.add_argument("--headword-size", type=int, default=50)
    ap.add_argument("--sentence-size", type=int, default=17)
    ap.add_argument(
        "--show", type=int, default=6, help="how many examples of each problem to print"
    )
    args = ap.parse_args()

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    families = []
    if args.fonts and args.fonts.is_dir():
        families = sorted(p.name for p in args.fonts.iterdir() if p.is_dir())
    if args.fonts and not families:
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
    on_question = sentence_on_question(args.deck)
    print(f"deck   {args.deck}")
    print(f"faces  {', '.join(families) if families else 'built-in serif only'}")
    print(
        f"sentence  {'on the question face too' if on_question else 'on the answer face only'}"
    )
    print()

    problems = {
        "a card whose faces do not work as a question and an answer": [],
        "the question and the answer are the same text, deck-wide": [],
        "a character no installed font can draw at all": [],
        "a reading or meaning the reader cannot draw": [],
        "a glyph the headword face cannot draw": [],
        "a Latin glyph the built-in serif cannot draw": [],
        "headword too wide for the screen": [],
        f"sentence wrapping past {MAX_SENTENCE_LINES} lines": [],
        f"an unbreakable run longer than the {LINE_BYTES}-byte line buffer": [],
    }
    notes = 0
    identical_faces = 0
    first_faces = None
    for index, fields in read_deck(args.deck / "deck.dat"):
        notes += 1
        headword, sentence = fields[0], fields[4]
        # A cloze card's question IS its sentence; naming it in a report line
        # by an empty headword identifies nothing.
        label = headword or (fields[7] if len(fields) > 7 else "")

        question, answer, face_problems, identical = check_faces(fields, on_question)
        if identical:
            identical_faces += 1
        if first_faces is None:
            first_faces = (question, answer)
        for problem in face_problems:
            problems["a card whose faces do not work as a question and an answer"].append(
                f"note {index} {label[:24]!r}: {problem}"
            )

        for family in families:
            # Every character, not just the CJK ones. The device draws the
            # headword in the randomised CJK face whatever script it is, so a
            # Latin headword needs Latin glyphs *in that face* -- and this check
            # used to skip them, which is how a deck of English words passed
            # while rendering nothing at all.
            missing_hw = {
                c
                for c in headword
                if c != " " and ord(c) not in headword_fonts[family][0]
            }
            missing_st = {
                c
                for c in sentence
                if is_cjk(c) and ord(c) not in sentence_fonts[family][0]
            }
            if missing_hw or missing_st:
                problems["a glyph the headword face cannot draw"].append(
                    f"note {index} in {family}: {''.join(sorted(missing_hw | missing_st))!r}"
                )
                break

        # With no font families installed, the built-in serif is all there is,
        # and it has no CJK. The per-family checks below simply do not run in
        # that case, so a Japanese deck with no fonts built used to be told
        # "every card renders" and would have drawn blank on the device.
        if not families:
            unreachable = {c for c in headword + sentence if is_cjk(c)}
            if unreachable:
                problems["a character no installed font can draw at all"].append(
                    f"note {index}: {''.join(sorted(unreachable))[:12]!r}"
                    " needs a CJK font, and none is installed"
                )

        # Only the headword and the sentence are drawn in the deck's own face.
        # Everything else -- reading, meaning, part of speech, the sentence's
        # gloss -- goes through the built-in serif, which has no CJK at all.
        # Nothing checked that: the serif check below skips CJK by design and
        # the face checks above only look at two fields, so a Japanese deck
        # whose readings were katakana rendered them as boxes on the device
        # while every line here said "0". Scope is English and Chinese, so
        # this does not chase a fix; it makes the failure visible.
        serif_drawn = [
            fields[FIELD_NAMES.index(name)]
            for name in (
                "reading",
                "meaning",
                "partOfSpeech",
                "sentenceReading",
                "sentenceMeaning",
            )
        ]
        undrawable = {c for text in serif_drawn for c in text if is_cjk(c)}
        if undrawable:
            problems["a reading or meaning the reader cannot draw"].append(
                f"note {index}: {''.join(sorted(undrawable))[:12]!r} sits in a"
                " field the reader draws in its built-in face, which has no"
                " such characters"
            )

        if serif is not None:
            for name, text in zip(FIELD_NAMES, fields):
                if families and name in ("headword", "sentence"):
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

    if families:
        noun = "face" if len(families) == 1 else "faces"
        print(f"checked {notes} notes across {len(families)} {noun}\n")
    else:
        print(f"checked {notes} notes against the built-in serif\n")
    if notes >= 10 and identical_faces > notes * 0.3:
        problems[
            "the question and the answer are the same text, deck-wide"
        ].append(
            f"{identical_faces} of {notes} cards -- one field is probably"
            f" filling both the word and the meaning"
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
    if first_faces:
        print("\nthe first card, as the device will draw it:")
        for label, parts in (("question", first_faces[0]), ("answer", first_faces[1])):
            shown = [part for part in parts if part.strip()]
            print(f"  {label}: {' | '.join(shown) if shown else '(nothing)'}"[:200])
        print()
    print("every card renders" if failed == 0 else f"{failed} problems found")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
