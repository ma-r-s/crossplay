#!/usr/bin/env python3
"""Convert an Anki collection into an SD-card study deck.

    tools_local/study/anki_to_deck.py \
        ~/Library/Application\\ Support/Anki2/User\\ 1/collection.anki2 \
        --deck 'Mandarin: Vocabulary::Current' \
        --out /Volumes/SDCARD/study/mandarin

The format, and the reasoning behind it, is docs/study-deck-format.md. This
script only writes; nothing here reads back from the device. Sync in the other
direction is deck_to_anki.py.

Reading Anki's collection directly (rather than an .apkg export) is deliberate:
the scheduling state and the deck's FSRS parameters only exist in the live
collection, and shipping a deck without them would mean the device scheduling
differently from the phone on the very first review.
"""

import argparse
import json
import pathlib
import re
import struct
import sqlite3
import sys
import unicodedata

DECK_MAGIC = b"XSTUDYD\0"
META_MAGIC = b"XSTUDYM\0"
FORMAT_VERSION = 2

CARD_RECORD_SIZE = 32
NUM_PARAMS = 19
MAX_STEPS = 6

# Card states, matching StudyCard::State on the device.
STATE_NEW = 0

# Revlog `type` 3 is a filtered-deck review; Anki excludes those from memory
# state. See StudyFsrs.h -- this is the same exclusion, applied at import.
REVLOG_FILTERED = 3


# --- note type profiles ------------------------------------------------------
#
# A profile says which of a note type's fields become the seven fields the
# device knows about. Adding a language means adding a profile here, not
# touching the device.
#
# The HSK note type is the one Mario's 5001-note Mandarin deck uses; its field
# order is fixed by the shared deck it came from.
PROFILES = {
    "HSK": {
        "headword": "Simplified",
        "reading": "Pinyin.1",
        "meaning": "Meaning",
        "partOfSpeech": "Part of speech",
        "sentence": "SentenceSimplified",
        "sentenceReading": "SentencePinyin.1",
        "sentenceMeaning": "SentenceMeaning",
    },
    "HSK+": {
        "headword": "Simplified",
        "reading": "Pinyin",
        "meaning": "English",
        "partOfSpeech": "Classifier",
        "sentence": "",
        "sentenceReading": "",
        "sentenceMeaning": "",
    },
    "Basic+": {
        "headword": "Front",
        "reading": "",
        "meaning": "Back",
        "partOfSpeech": "",
        "sentence": "",
        "sentenceReading": "",
        "sentenceMeaning": "",
    },
}

FIELD_ORDER = [
    "headword",
    "reading",
    "meaning",
    "partOfSpeech",
    "sentence",
    "sentenceReading",
    "sentenceMeaning",
]

# Characters a few notes use that are typographically identical to an ASCII
# letter but sit outside the built-in serif's coverage, so they would draw as a
# box in the middle of a reading. Found by tools_local/study/check_deck.py
# walking all 5001 notes -- four of them use the phonetic script g.
#
# Mapped rather than shipped: adding a glyph for U+0261 would mean widening the
# built-in font, which is upstream's, for four cards that mean plain "g".
LOOKALIKES = {
    0x0261: "g",  # LATIN SMALL LETTER SCRIPT G
    0x0269: "i",  # LATIN SMALL LETTER IOTA
    0x02BC: "'",  # MODIFIER LETTER APOSTROPHE
    0x2019: "'",  # RIGHT SINGLE QUOTATION MARK
    0x0320: None,  # COMBINING MINUS SIGN BELOW: a phonetic mark, not pinyin
}

BOLD_RE = re.compile(r"<b>(.*?)</b>", re.IGNORECASE | re.DOTALL)
TAG_RE = re.compile(r"<[^>]+>")
SOUND_RE = re.compile(r"\[sound:[^\]]*\]")
WHITESPACE_RE = re.compile(r"\s+")


def open_collection(path):
    db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    # Anki installs a custom collation; any query touching a name column fails
    # without it.
    db.create_collation(
        "unicase", lambda a, b: (a.lower() > b.lower()) - (a.lower() < b.lower())
    )
    return db


def clean(text):
    """Strip Anki markup down to the plain text the device renders."""
    text = SOUND_RE.sub("", text)
    text = re.sub(r"<br\s*/?>", " ", text, flags=re.IGNORECASE)
    text = re.sub(r"</div>\s*<div>", " ", text, flags=re.IGNORECASE)
    text = TAG_RE.sub("", text)
    text = (
        text.replace("&nbsp;", " ")
        .replace("&amp;", "&")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
    )
    # NFC matters: some pinyin in this collection carries combining tone marks
    # rather than precomposed vowels, and the two forms would otherwise need
    # different glyphs in every font we ship.
    text = unicodedata.normalize("NFC", text)
    text = text.translate(LOOKALIKES)
    return WHITESPACE_RE.sub(" ", text).strip()


def clean_sentence(raw):
    """Clean an example sentence and locate the emphasised target word.

    Returns (text, offset_in_codepoints, length_in_codepoints). The device has
    no bold CJK face, so the emphasis survives as a span the renderer may
    underline rather than as markup.
    """
    match = BOLD_RE.search(raw)
    if not match:
        return clean(raw), 0, 0
    before = clean(raw[: match.start()])
    target = clean(match.group(1))
    full = clean(BOLD_RE.sub(r"\1", raw))
    # Locate the cleaned target inside the cleaned whole, rather than trusting
    # the offset from the raw string: stripping tags moves everything.
    offset = full.find(target, max(0, len(before) - 2)) if target else -1
    if offset < 0 or not target:
        return full, 0, 0
    # Codepoints, not bytes -- that is what the renderer counts.
    return full, len(full[:offset]), len(target)


def parse_deck_config(blob):
    """Pull the FSRS block out of a deck preset's protobuf.

    Anki has no stable public export for this and the generated bindings are
    not worth vendoring for six numbers, so this walks the wire format and
    picks out the fields it recognises. Unknown fields are skipped by length,
    which is what makes that safe across Anki versions.
    """
    out = {}
    i = 0
    while i < len(blob):
        try:
            tag, i = _varint(blob, i)
        except IndexError:
            break
        field, wire = tag >> 3, tag & 7
        if wire == 0:
            value, i = _varint(blob, i)
            if field == 9:
                out["newPerDay"] = value
            elif field == 10:
                out["revPerDay"] = value
            elif field == 16:
                out["maxInterval"] = value
        elif wire == 5:
            (value,) = struct.unpack_from("<f", blob, i)
            i += 4
            if field == 37:
                out["desiredRetention"] = value
        elif wire == 1:
            i += 8
        elif wire == 2:
            length, i = _varint(blob, i)
            chunk = blob[i : i + length]
            i += length
            # Field 5 is the FSRS-5 weight vector: 19 little-endian floats.
            if field == 5 and len(chunk) == NUM_PARAMS * 4:
                out["params"] = list(struct.unpack("<%df" % NUM_PARAMS, chunk))
            # Fields 1 and 2 are the learning and relearning steps, in minutes,
            # as packed float arrays. Mario's deck ships [1, 10] and [10].
            elif field in (1, 2) and len(chunk) % 4 == 0 and chunk:
                key = "learnSteps" if field == 1 else "relearnSteps"
                out[key] = list(struct.unpack("<%df" % (len(chunk) // 4), chunk))[
                    :MAX_STEPS
                ]
        else:
            break
    return out


def _varint(buf, i):
    result = shift = 0
    while True:
        byte = buf[i]
        i += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, i
        shift += 7


def deck_config_for(db, deck_name):
    """Find the preset a deck uses, and read its FSRS parameters."""
    row = db.execute(
        "select kind from decks where name = ?", (deck_name.replace("::", "\x1f"),)
    ).fetchone()
    config_id = 1
    if row and row[0]:
        # DeckKind { normal { config_id } } -- one nested message, one varint.
        blob = row[0]
        if len(blob) > 2 and blob[0] == 0x0A:
            inner = blob[2 : 2 + blob[1]]
            if inner and inner[0] == 0x08:
                config_id, _ = _varint(inner, 1)
    row = db.execute(
        "select config from deck_config where id = ?", (config_id,)
    ).fetchone()
    return parse_deck_config(row[0]) if row else {}


def collect_notes(db, deck_name, limit=None):
    like = deck_name.replace("::", "\x1f")
    rows = db.execute(
        """select c.id, n.flds, nt.name, c.data, c.due, c.ivl, c.reps, c.lapses, c.type
           from cards c
           join notes n on n.id = c.nid
           join notetypes nt on nt.id = n.mid
           join decks d on d.id = c.did
           where d.name = ? or d.name like ?
           order by c.id""",
        (like, like + "\x1f%"),
    ).fetchall()
    if limit:
        rows = rows[:limit]

    # Field name -> ordinal, per note type.
    ordinals = {}
    for nt_name, ord_, f_name in db.execute(
        "select nt.name, f.ord, f.name from fields f join notetypes nt on nt.id = f.ntid"
    ):
        ordinals.setdefault(nt_name, {})[f_name] = ord_

    notes, skipped = [], 0
    for cid, flds, nt_name, data, due, ivl, reps, lapses, ctype in rows:
        profile = PROFILES.get(nt_name)
        if not profile:
            skipped += 1
            continue
        parts = flds.split("\x1f")
        by_ord = ordinals.get(nt_name, {})

        def get(key):
            name = profile.get(key, "")
            idx = by_ord.get(name, -1) if name else -1
            return parts[idx] if 0 <= idx < len(parts) else ""

        headword = clean(get("headword"))
        if not headword:
            skipped += 1
            continue
        sentence, bold_off, bold_len = clean_sentence(get("sentence"))

        memory = {}
        if data:
            try:
                memory = json.loads(data)
            except ValueError:
                memory = {}

        notes.append(
            {
                "ankiCardId": cid,
                "fields": [
                    headword,
                    clean(get("reading")),
                    clean(get("meaning")),
                    clean(get("partOfSpeech")),
                    sentence,
                    clean(get("sentenceReading")),
                    clean(get("sentenceMeaning")),
                ],
                "bold": (bold_off, bold_len),
                "stability": float(memory.get("s", 0.0)),
                "difficulty": float(memory.get("d", 0.0)),
                "due": due,
                "ivl": ivl,
                "reps": reps,
                "lapses": lapses,
                "ctype": ctype,
            }
        )
    return notes, skipped


def write_deck(notes, path):
    index, blob = [], bytearray()
    for note in notes:
        index.append(len(blob))
        payload = bytearray()
        for i, text in enumerate(note["fields"]):
            encoded = text.encode("utf-8")
            if i == 4:
                # The two emphasis bytes ride inside the sentence field's
                # length, so a reader that ignores them still sees valid text.
                # Appended *unconditionally*, including as (0, 0): if they were
                # only present when Anki had a <b>, the reader could not tell a
                # sentence ending in two low bytes from one carrying a span,
                # and would chop two bytes off the wrong records.
                encoded += bytes((min(note["bold"][0], 255), min(note["bold"][1], 255)))
            if len(encoded) > 0xFFFF:
                encoded = encoded[:0xFFFF]
            payload += struct.pack("<H", len(encoded)) + encoded
        blob += payload
    index.append(len(blob))  # sentinel: makes a record's length a subtraction

    with open(path, "wb") as f:
        f.write(DECK_MAGIC)
        f.write(struct.pack("<HBBI", FORMAT_VERSION, len(FIELD_ORDER), 0, len(notes)))
        base = len(DECK_MAGIC) + 8 + 4 * len(index)
        f.write(b"".join(struct.pack("<I", base + off) for off in index))
        f.write(blob)
    return len(blob)


def write_cards(notes, path):
    """Seed scheduling state from Anki, so the device resumes rather than restarts."""
    with open(path, "wb") as f:
        for note in notes:
            learned = note["stability"] > 0.0
            state = 2 if learned else STATE_NEW
            # Anki stores a review card's due as a day number in its own
            # numbering, which is the numbering we keep. A new card's due is a
            # position in the new queue, not a day, so it is not carried over.
            due_day = note["due"] if learned else 0
            # Anki does not store "when was this last reviewed" on the card, but
            # for a review card it is exactly due - ivl: the interval was
            # counted forward from that day. Without this every card's first
            # review on the device looks like a *same-day* review to FSRS, which
            # takes the short-term stability path and inflates the interval --
            # a card last seen forty days ago was being treated as one seen
            # forty seconds ago.
            last_day = (due_day - note["ivl"]) if (learned and note["ivl"] > 0) else -1
            f.write(
                struct.pack(
                    "<qffiiHHBBH",
                    note["ankiCardId"],
                    note["stability"],
                    note["difficulty"],
                    due_day,
                    last_day,
                    min(note["reps"], 0xFFFF),
                    min(note["lapses"], 0xFFFF),
                    state,
                    0,
                    0,
                )
            )
    assert path.stat().st_size == len(notes) * CARD_RECORD_SIZE


def write_meta(path, name, config, crt, rollover):
    params = config.get("params") or []
    if len(params) != NUM_PARAMS:
        print(
            "  warning: deck has no FSRS-5 parameters; the device will use Anki's defaults"
        )
        params = [0.0] * NUM_PARAMS
    encoded = name.encode("utf-8")[:255]
    with open(path, "wb") as f:
        f.write(META_MAGIC)
        f.write(struct.pack("<HH", FORMAT_VERSION, 0))
        f.write(struct.pack("<%df" % NUM_PARAMS, *params))
        f.write(
            struct.pack(
                "<fiii",
                config.get("desiredRetention", 0.9),
                config.get("maxInterval", 36500),
                config.get("newPerDay", 20),
                config.get("revPerDay", 200),
            )
        )
        f.write(struct.pack("<qB", crt, rollover))
        learn = config.get("learnSteps") or []
        relearn = config.get("relearnSteps") or []
        f.write(struct.pack("<BB", len(learn), len(relearn)))
        f.write(
            struct.pack("<%df" % MAX_STEPS, *(learn + [0.0] * MAX_STEPS)[:MAX_STEPS])
        )
        f.write(
            struct.pack("<%df" % MAX_STEPS, *(relearn + [0.0] * MAX_STEPS)[:MAX_STEPS])
        )
        f.write(struct.pack("<B", len(encoded)))
        f.write(encoded)


def is_cjk(ch):
    o = ord(ch)
    return 0x2E80 <= o <= 0x9FFF or 0xF900 <= o <= 0xFAFF or 0xFF00 <= o <= 0xFFEF


def write_glyphs(notes, out_dir):
    """Write the codepoint sets the font pipeline subsets against.

    Split by *size*, not by script, because that is what costs SD card space.
    A bitmap font stores one rendered bitmap per glyph per size, so a glyph at
    the 100px headword size costs roughly ten times what it costs at the 34px
    sentence size. The headword set is only the characters that ever appear as
    a headword -- far fewer than the characters that appear in sentences -- so
    keeping the two apart is most of the difference between a deck that fits
    comfortably and one that does not.

    Latin gets its own file: Mario's card template applies the random font only
    to the hanzi, and the pinyin needs tone-marked vowels that four of the five
    CJK faces do not carry at all.
    """
    headword, sentence, latin = set(), set(), set()
    for note in notes:
        for i, text in enumerate(note["fields"]):
            for ch in text:
                if not is_cjk(ch):
                    latin.add(ch)
                elif i == 0:
                    headword.add(ch)
                else:
                    sentence.add(ch)
    # A headword character also has to render at sentence size, because the
    # word appears inside its own example sentence.
    sentence |= headword
    # Everything the app draws itself: intervals, counts, button labels.
    latin.update("0123456789dmy%+/<>?!.,:;-()[]'\" AGAINHARDGOODEASYagainhardgoodeasy")

    sizes = {}
    for name, chars in (
        ("headword", headword),
        ("sentence", sentence),
        ("latin", latin),
    ):
        path = out_dir / f"glyphs-{name}.txt"
        path.write_text("".join(sorted(chars)), encoding="utf-8")
        sizes[name] = len(chars)
    return sizes


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("collection", type=pathlib.Path)
    ap.add_argument(
        "--deck", required=True, help="deck name, e.g. 'Mandarin: Vocabulary::Current'"
    )
    ap.add_argument("--out", required=True, type=pathlib.Path, help="output directory")
    ap.add_argument(
        "--name", help="display name (defaults to the deck's last component)"
    )
    ap.add_argument(
        "--limit", type=int, help="only convert the first N cards (for testing)"
    )
    args = ap.parse_args()

    if not args.collection.exists():
        sys.exit(f"no collection at {args.collection}")
    db = open_collection(args.collection)

    (crt,) = db.execute("select crt from col").fetchone()
    rollover = 4
    row = db.execute("select val from config where key = 'rollover'").fetchone()
    if row:
        try:
            rollover = int(row[0])
        except (TypeError, ValueError):
            pass

    notes, skipped = collect_notes(db, args.deck, args.limit)
    if not notes:
        sys.exit(
            f"no convertible cards in '{args.deck}'. Known note types: {', '.join(PROFILES)}"
        )
    config = deck_config_for(db, args.deck)

    args.out.mkdir(parents=True, exist_ok=True)
    name = args.name or args.deck.split("::")[-1]
    blob_size = write_deck(notes, args.out / "deck.dat")
    write_cards(notes, args.out / "cards.dat")
    write_meta(args.out / "meta.dat", name, config, crt, rollover)
    (args.out / "revlog.dat").touch()
    glyphs = write_glyphs(notes, args.out)

    learned = sum(1 for n in notes if n["stability"] > 0)
    print(
        f"deck '{name}': {len(notes)} cards ({learned} with scheduling state, {skipped} skipped)"
    )
    print(f"  content   {blob_size / 1024:.0f} KB")
    print(f"  state     {len(notes) * CARD_RECORD_SIZE / 1024:.0f} KB")
    print(
        f"  glyphs    {glyphs['headword']} headword, {glyphs['sentence']} sentence, {glyphs['latin']} latin"
    )
    if config.get("params"):
        print(
            f"  FSRS      {len(config['params'])} parameters, retention {config.get('desiredRetention', 0.9):.2f}"
        )
    learn = config.get("learnSteps") or []
    relearn = config.get("relearnSteps") or []
    print(
        f"  steps     learn {learn or 'Anki default'}, relearn {relearn or 'Anki default'} (minutes)"
    )
    print(f"  -> {args.out}")


if __name__ == "__main__":
    main()
