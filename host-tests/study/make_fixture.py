#!/usr/bin/env python3
"""Build the converted deck the study host tests parse.

WHY THIS EXISTS. test_deck.cpp and test_images.cpp were written to read a deck
produced by the real converter, and they were right to be: a synthetic writer
beside our own reader agrees with itself about a format neither of them owns.
But nothing in check.sh or in CI ever produced one, so both tests took their
"no converted deck, skipping" branch on every run this repo has ever had, and
printed `PASS (0 checks, 0 failures)`. Two of the study app's five binaries
were an empty slot wearing the word PASS.

So the deck is built here, from a synthetic ANKI COLLECTION rather than from
synthetic deck.dat bytes. That distinction is the whole point:

    this file writes  ->  a schema-18 sqlite collection
    anki_to_deck.py   ->  deck.dat / meta.dat / cards.dat   (the real converter)
    StudyDeck.cpp     ->  reads them back                   (the real firmware)

Only the first step is ours. The format itself is written by the tool that
writes Mario's own decks and read by the code the device runs, so the round
trip the tests were written for actually happens.

Standard library only, on purpose. make_fixture_apkg.py builds far better
fixtures with Anki's own exporter, and needs the `anki` package in a venv;
CI has python and nothing else, and a fixture generator that CI cannot run
puts the tests straight back to skipping.

    host-tests/study/make_fixture.py --out DIR

Writes DIR/deck.dat, meta.dat, cards.dat, revlog.dat, glyphs-*.txt and
images.dat.
"""

import argparse
import pathlib
import runpy
import sqlite3
import struct
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
TOOLS = HERE.parent.parent / "tools_local" / "study"

# Enough notes that the tests have somewhere to go: they load card index 3,
# read past the end, and walk the whole index.
WORDS = [
    ("abate", "to lessen in intensity", "The storm began to <b>abate</b>."),
    ("banal", "so ordinary as to be obvious", "A <b>banal</b> remark about weather."),
    ("cogent", "clear and convincing", "She made a <b>cogent</b> case."),
    ("dearth", "a scarcity of something", "There is a <b>dearth</b> of evidence."),
    ("ebullient", "cheerful and full of energy", "An <b>ebullient</b> welcome."),
    ("fervid", "intensely enthusiastic", "A <b>fervid</b> supporter."),
    ("garrulous", "excessively talkative", "Our <b>garrulous</b> neighbour."),
    ("hackneyed", "overused and unoriginal", "A <b>hackneyed</b> phrase."),
    ("impugn", "to call into question", "He did not <b>impugn</b> her motives."),
    ("laconic", "using very few words", "A <b>laconic</b> reply."),
    ("maudlin", "self-pityingly sentimental", "He turned <b>maudlin</b> after two."),
    ("nascent", "just coming into existence", "A <b>nascent</b> industry."),
]

# Cloze notes, in the shape Anki's stock Cloze type produces: one text field
# with the markup and a Back Extra beside it. Between them they cover what the
# converter has to get right -- a plain hole, a hole with a hint, two ordinals
# in one note (which becomes two cards), two holes sharing one ordinal (which
# becomes one card with two holes), and a card whose ordinal is no longer in
# the text, which Anki calls an empty card and which must be dropped rather
# than shown with no hole in it.
CLOZE_NOTES = [
    # (text, back extra, how many cards Anki would generate)
    ("The capital of France is {{c1::Paris}}.", "Geography", 1),
    ("Mitochondria are the {{c1::powerhouse::organelle}} of the cell.", "", 1),
    ("{{c1::Rome}} was founded in {{c2::753 BC}}.", "Livy's date", 2),
    ("{{c1::Berlin}} is the capital of {{c1::Germany}}.", "", 1),
    # Card ord 1 exists, {{c2::}} does not: an empty card.
    ("Only {{c1::one}} hole remains here.", "", 2),
]

# A Japanese note type in the shape the Japanese Support add-on produces --
# Expression, Reading (the same words with furigana in Anki's " 漢字[かんじ]"
# syntax), Meaning -- and a Korean one. Between them they cover the two
# script gaps the converter used to have: a reading that must become ruby
# rather than literal brackets, and Hangul, which is in no CJK range and was
# therefore classified as Latin and sent to a face with no Hangul in it.
JAPANESE_NOTES = [
    ("学生", "学生[がくせい]", "student"),
    ("私は学生です", "私[わたし]は 学生[がくせい]です", "I am a student"),
    ("水", "水[みず]", "water"),
]

KOREAN_NOTES = [
    ("학생", "haksaeng", "student"),
    ("물", "mul", "water"),
]

# FSRS-5 defaults. Any 19 plausible numbers would do; these are the ones Anki
# ships, so a deck built here schedules the way a real one does.
FSRS_PARAMS = [
    0.4072,
    1.1829,
    3.1262,
    15.4722,
    7.2102,
    0.5316,
    1.0651,
    0.0234,
    1.616,
    0.1544,
    1.0824,
    1.9813,
    0.0953,
    0.2975,
    2.2042,
    0.2407,
    2.9466,
    0.5034,
    0.6567,
]


def _varint(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)


def _packed_floats(field, values):
    body = struct.pack("<%df" % len(values), *values)
    return _varint(field << 3 | 2) + _varint(len(body)) + body


def _float(field, value):
    return _varint(field << 3 | 5) + struct.pack("<f", value)


def deck_config_blob():
    """A deck preset's protobuf, carrying only what the converter reads.

    Hand-encoded because that is exactly what parse_deck_config does at the
    other end -- it walks the wire format and skips what it does not know, so
    a message carrying four known fields and nothing else exercises the same
    path a real preset does.
    """
    return (
        _packed_floats(1, [1.0, 10.0])  # learning steps, minutes
        + _packed_floats(2, [10.0])  # relearning steps
        + _packed_floats(5, FSRS_PARAMS)  # the FSRS-5 weight vector
        + _float(37, 0.9)  # desired retention
    )


def build_collection(path, crt):
    """A schema-18 collection holding one deck of Basic notes.

    Only the tables and columns anki_to_deck.py and make_images.py actually
    read. A fuller schema would be more faithful and no more useful: what is
    under test is the converter, and the converter's whole view of a
    collection is these queries.
    """
    db = sqlite3.connect(path)
    db.executescript(
        """
        create table col (crt integer);
        create table config (key text primary key, val blob);
        create table decks (id integer primary key, name text, kind blob);
        create table deck_config (id integer primary key, config blob);
        create table notetypes (id integer primary key, name text);
        create table fields (ntid integer, ord integer, name text);
        create table templates (ntid integer, ord integer, name text);
        create table notes (id integer primary key, mid integer, flds text);
        create table cards (id integer primary key, nid integer, did integer,
                            odid integer, ord integer, type integer, queue integer,
                            due integer, ivl integer, reps integer, lapses integer,
                            left integer, data text);
        create table revlog (id integer primary key, cid integer, ease integer,
                             type integer);
        """
    )
    db.execute("insert into col (crt) values (?)", (crt,))
    db.execute("insert into config (key, val) values ('rollover', '4')")

    # DeckKind { normal { config_id: 1 } }, which is what deck_config_for walks.
    kind = bytes([0x0A, 0x02, 0x08, 0x01])
    db.execute(
        "insert into decks (id, name, kind) values (?, ?, ?)",
        (1, "Fixture\x1fVocabulary", kind),
    )
    db.execute(
        "insert into deck_config (id, config) values (?, ?)", (1, deck_config_blob())
    )

    # A note type with no written profile, whose fields are named the way a
    # real shared deck names them. That routes through generic_profile, which
    # claims by name: Word -> headword, Definition -> meaning, Sentence ->
    # sentence. The sentence slot is the one that matters here, because it is
    # what carries the emphasis span test_deck.cpp checks the bounds of.
    db.execute("insert into notetypes (id, name) values (1, 'Vocabulary')")
    for ordinal, name in enumerate(["Word", "Definition", "Sentence"]):
        db.execute(
            "insert into fields (ntid, ord, name) values (1, ?, ?)", (ordinal, name)
        )
    db.execute("insert into templates (ntid, ord, name) values (1, 0, 'Card 1')")

    # Card ids are milliseconds in a real collection, and card_order() in
    # make_images.py reads them straight out of cards.dat as int64, so they
    # have to be large and distinct.
    base_id = (crt + 86400) * 1000
    for i, (front, back, sentence) in enumerate(WORDS):
        note_id = base_id + i * 2
        card_id = base_id + i * 2 + 1
        db.execute(
            "insert into notes (id, mid, flds) values (?, 1, ?)",
            (note_id, "\x1f".join([front, back, sentence])),
        )
        # A spread of states: new, learning, and reviewed cards with history.
        # The reviewed ones arrive with NO stored memory state, which is the
        # case current Anki produces and seed_memory_from_revlog exists for.
        if i < 3:
            state, queue, due, ivl, reps = 0, 0, i, 0, 0
        elif i < 5:
            state, queue, due, ivl, reps = 1, 1, crt + 3600 * (i + 1), 0, 1
        else:
            state, queue, due, ivl, reps = 2, 2, 30 + i, 10 + i, 4
        db.execute(
            """insert into cards (id, nid, did, odid, ord, type, queue, due, ivl,
                                  reps, lapses, left, data)
               values (?, ?, 1, 0, 0, ?, ?, ?, ?, ?, 0, 0, '')""",
            (card_id, note_id, state, queue, due, ivl, reps),
        )
        for r in range(reps):
            db.execute(
                "insert into revlog (id, cid, ease, type) values (?, ?, ?, 1)",
                ((crt + 86400 * (r + 1)) * 1000 + i, card_id, 3),
            )
    # Japanese and Korean, each with its own note type, both routed through
    # generic_profile by field name.
    for model_id, name, field_names, rows in (
        (3, "Japanese", ["Expression", "Reading", "Meaning"], JAPANESE_NOTES),
        (4, "Korean", ["Expression", "Reading", "Meaning"], KOREAN_NOTES),
    ):
        db.execute("insert into notetypes (id, name) values (?, ?)", (model_id, name))
        for ordinal, field_name in enumerate(field_names):
            db.execute(
                "insert into fields (ntid, ord, name) values (?, ?, ?)",
                (model_id, ordinal, field_name),
            )
        db.execute(
            "insert into templates (ntid, ord, name) values (?, 0, 'Card 1')",
            (model_id,),
        )
        for i, values in enumerate(rows):
            note_id = base_id + model_id * 100000 + i * 10
            db.execute(
                "insert into notes (id, mid, flds) values (?, ?, ?)",
                (note_id, model_id, "\x1f".join(values)),
            )
            db.execute(
                """insert into cards (id, nid, did, odid, ord, type, queue, due, ivl,
                                      reps, lapses, left, data)
                   values (?, ?, 1, 0, 0, 0, 0, ?, 0, 0, 0, 0, '')""",
                (note_id + 1, note_id, 200 + i),
            )

    # The cloze note type, and its cards. Anki generates one card per cloze
    # ordinal present in the text, with the card's `ord` one less than the
    # cloze number -- which is the mapping render_cloze depends on, so the
    # fixture states it here rather than assuming it.
    db.execute("insert into notetypes (id, name) values (2, 'Cloze')")
    for ordinal, name in enumerate(["Text", "Back Extra"]):
        db.execute(
            "insert into fields (ntid, ord, name) values (2, ?, ?)", (ordinal, name)
        )
    db.execute("insert into templates (ntid, ord, name) values (2, 0, 'Cloze')")

    cloze_base = base_id + len(WORDS) * 2 + 2
    for i, (text, extra, cards) in enumerate(CLOZE_NOTES):
        note_id = cloze_base + i * 10
        db.execute(
            "insert into notes (id, mid, flds) values (?, 2, ?)",
            (note_id, "\x1f".join([text, extra])),
        )
        for card_ord in range(cards):
            db.execute(
                """insert into cards (id, nid, did, odid, ord, type, queue, due, ivl,
                                      reps, lapses, left, data)
                   values (?, ?, 1, 0, ?, 0, 0, ?, 0, 0, 0, 0, '')""",
                (note_id + 1 + card_ord, note_id, card_ord, 100 + i),
            )

    db.commit()
    db.close()


def convert(collection, out_dir):
    """Run the real converter, in-process, so a traceback lands here."""
    sys.path.insert(0, str(TOOLS))
    argv = sys.argv
    sys.argv = [
        str(TOOLS / "anki_to_deck.py"),
        str(collection),
        "--deck",
        "Fixture::Vocabulary",
        "--name",
        "FIXTURE",
        "--out",
        str(out_dir),
    ]
    try:
        runpy.run_path(str(TOOLS / "anki_to_deck.py"), run_name="__main__")
    finally:
        sys.argv = argv


class TinyImage:
    """The duck type make_images.pack_bitmap wants: .size and .load().

    PIL is what packs a real deck's photographs, and CI has no PIL. Rather
    than reimplement the packing here -- which would put a second copy of the
    format beside the one under test -- the pattern is generated and handed to
    make_images.py's own packer, so the bytes in images.dat are written by the
    same function that writes them for Mario's deck.
    """

    def __init__(self, width, height):
        self.size = (width, height)
        self.width = width
        self.height = height

    def load(self):
        w, h = self.size

        class Pixels:
            def __getitem__(self, xy):
                x, y = xy
                # A diagonal, so a wrong stride shears visibly rather than
                # producing another uniform block that still reads end to end.
                return 0 if (x + y) % 7 < 3 else 255

        del w, h
        return Pixels()


def deck_note_count(path):
    """How many notes the converter actually wrote. Read back rather than
    counted here, because the converter is the one that decides -- it drops an
    empty cloze card and generates two cards from one two-ordinal note."""
    with open(path, "rb") as f:
        header = f.read(16)
    return struct.unpack_from("<I", header, 12)[0]


def write_images(out_dir, card_count, every):
    """images.dat, through make_images.py's own packer and header writer."""
    sys.path.insert(0, str(TOOLS))
    import make_images

    entries = []
    blob = bytearray()
    for i in range(card_count):
        if i % every:
            entries.append((0, 0, 0, 0))
            continue
        # Taller than one band, and deliberately not all one size.
        #
        # kImageBandRows is 16, so an image under 17 rows is read in a single
        # readBand(firstRow=0) and the `firstRow * stride` arithmetic never
        # runs. An 11-row fixture therefore let a stride off by one pass:
        # checked by breaking StudyImages.cpp exactly that way, and watching
        # this suite stay green. Every image here spans several bands, and
        # widths that are and are not byte-aligned so the padding differs.
        image = TinyImage(29 + i * 3, 37 + (i % 5) * 11)
        packed, stride = make_images.pack_bitmap(image)
        entries.append((len(blob), image.width, image.height, stride))
        blob += packed

    path = out_dir / "images.dat"
    with open(path, "wb") as f:
        f.write(make_images.MAGIC)
        f.write(struct.pack("<HHI", make_images.VERSION, 0, len(entries)))
        base = len(make_images.MAGIC) + 8 + len(entries) * 10
        for offset, w, h, stride in entries:
            f.write(struct.pack("<IHHH", base + offset if w else 0, w, h, stride))
        f.write(blob)
    return sum(1 for e in entries if e[1])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    collection = args.out / "collection.anki2"
    if collection.exists():
        collection.unlink()

    # A fixed epoch, so day numbering is the same on every machine and every
    # run. test_deck asserts day 10 plus an hour is still day 10, which a
    # collection created "now" would answer identically and by luck.
    crt = int(time.mktime((2024, 1, 1, 4, 0, 0, 0, 1, -1)))
    build_collection(collection, crt)
    convert(collection, args.out)
    # One entry per CARD, which is what make_images.py writes for a real deck
    # -- not one per word. The cloze cards pushed the two apart, and an
    # images.dat shorter than deck.dat is a file the device indexes past the
    # end of.
    notes = deck_note_count(args.out / "deck.dat")
    packed = write_images(args.out, notes, 3)
    print("fixture: %d notes, %d with an image -> %s" % (notes, packed, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
