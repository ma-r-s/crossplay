#!/usr/bin/env python3
"""Convert an Anki collection into an SD-card study deck.

    tools_local/study/anki_to_deck.py \
        ~/Library/Application\\ Support/Anki2/User\\ 1/collection.anki2 \
        --deck 'Mandarin: Vocabulary::Current' \
        --out /Volumes/SDCARD/study/mandarin

The format, and the reasoning behind it, is docs/apps/study-deck-format.md. This
script only writes; nothing here reads back from the device. Sync in the other
direction is deck_to_anki.py.

Reading Anki's collection directly (rather than an .apkg export) is deliberate:
the deck's FSRS parameters only exist in the live collection, and shipping a
deck without them would mean the device scheduling differently from the phone
on the very first review.

Scheduling state is taken wherever it survives. Older Anki cached each card's
FSRS memory state in cards.data ({"s":..,"d":..}) and that is used when
present; current Anki stores {"pos":..,"dr":..} instead and recomputes state
from the review log on demand, so this script does the same, replaying each
card's own reviews through fsrs.py (the mirror of the device scheduler).
Without that fallback a collection written by current Anki converts every
reviewed card as brand new -- found 2026-08-25 on a deck with 6300 reviews.
"""

import argparse
import json
import pathlib
import re
import struct
import sqlite3
import sys
import unicodedata

import fsrs

DECK_MAGIC = b"XSTUDYD\0"
META_MAGIC = b"XSTUDYM\0"
# 3 added the eighth field, clozeQuestion, and with it cloze decks. A v2 deck
# is still readable by a v3 firmware; a v3 deck on a v2 firmware is refused at
# the header rather than half-read, which is why the version moved at all.
FORMAT_VERSION = 3

CARD_RECORD_SIZE = 32
NUM_PARAMS = 19
MAX_STEPS = 6

# meta.dat flags (the uint16 after the version, zero in every deck written
# before this existed -- which is why the default is the common case).
#
# Bit 0: draw the example sentence on the QUESTION face as well as the answer.
# That is an HSK-template habit: a hanzi alone is a recall test, the same hanzi
# inside a sentence is the reading exercise the deck is for, so the sentence
# belongs in front of you while you think. It is wrong for a vocabulary deck,
# where the example sentence is part of the answer -- and it silently was the
# behaviour for every deck until a Barron's SAT list made it obvious.
META_SENTENCE_ON_QUESTION = 1 << 0

# Note types whose sentence is a question-side prompt rather than an answer.
SENTENCE_ON_QUESTION_TYPES = {"HSK", "HSK+"}

# Filled during a run: note type -> the field mapping this conversion really
# used. The installer page reads it back (through runpy's namespace) to fill
# its "what goes where" dropdowns. It used to re-derive the mapping itself,
# without the empty-field and categorical filters the converter applies, so
# the page showed one mapping while the deck was built from another -- and a
# user "correcting" the page's version made their deck worse.
USED_PROFILES = {}

# Card states, matching study::State on the device. Anki's `cards.type` uses
# the same first four values, which is not a coincidence -- keeping them equal
# is what lets a card round-trip without a translation table.
STATE_NEW = 0
STATE_LEARNING = 1
STATE_REVIEW = 2
STATE_RELEARNING = 3
# Ours, with no Anki equivalent: a card Anki has suspended or buried. Kept in
# the deck rather than dropped so note indices stay stable across reconversions,
# and skipped by the device's queue.
STATE_SUSPENDED = 4

# Anki's `cards.queue`. Negative means the card is not schedulable.
QUEUE_SUSPENDED = -1
# Intraday learning: `due` is a unix timestamp, not a day number. Getting this
# wrong imports a due date of "day 1751918952".
QUEUE_LEARNING_INTRADAY = 1
QUEUE_LEARNING_DAY = 3

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


# A note type nobody wrote a profile for still converts. Fields are claimed by
# NAME first: a deck built as [Word, Part of Speech, Definition, Sentence]
# says what each field is right there in its names, and the old positional
# rule ("second field is the meaning") put "V." on the answer face of every
# card in exactly that deck -- Barron's SAT list, one of the most-downloaded
# vocabulary decks there is. Position remains the fallback for types named
# like Anki's stock "Basic", whose first field is the word and second the
# meaning (the first field is the one Anki sorts and duplicate-checks by).
# On a two-template type ("Basic (and reversed card)") the second card runs
# the other way, so its fields swap. --map overrides any of this.
#
# Cloze types are refused by name instead: the front of a cloze card is the
# text with a hole in it, and this deck format has nowhere to put a hole.
#
# Order matters within each pattern list: first match claims the field.
FIELD_NAME_PATTERNS = {
    "headword": ("word", "front", "question", "term", "expression", "vocab"),
    "reading": ("reading", "pronunciation", "pinyin", "phonetic", "ipa"),
    "meaning": ("definition", "meaning", "back", "answer", "translation"),
    "partOfSpeech": ("part of speech", "partofspeech", "pos", "word type"),
    "sentence": ("sentence", "example", "usage"),
    "sentenceReading": ("sentence reading", "example reading"),
    "sentenceMeaning": (
        "sentence meaning",
        "sentence translation",
        "example translation",
    ),
}


def generic_profile(
    fields_in_order, override=None, blank_fields=(), categorical_fields=()
):
    """Map a note type's fields onto the device's seven slots.

    `blank_fields` is the set of field names this deck leaves empty on
    (nearly) every note. They are skipped, because a name is a claim and the
    content is the evidence: one real deck has fields [Front, Back, Meaning]
    where "Meaning" is empty on all 115 notes and "Back" holds the
    definition. Matching on the name alone put the empty field on the answer
    face and produced 115 cards with nothing to reveal.

    `categorical_fields` are fields with only a handful of distinct values
    across the whole deck: a tag or a category, not a card's content. They
    are skipped by the POSITIONAL fallback only, never by a name match --
    a name is stated intent, position is a guess, and a guess should not
    land on a label. A real German deck is [type, german, english, ...]
    where "type" is one of three words; taking field one as the question
    asked 2871 cards to recall the word "other".
    """
    profile = {key: "" for key in FIELD_ORDER}
    claimed = set()
    usable = [f for f in fields_in_order if f not in blank_fields]

    def claim(slot, field_name):
        if not profile[slot] and field_name not in claimed:
            profile[slot] = field_name
            claimed.add(field_name)

    for slot, patterns in FIELD_NAME_PATTERNS.items():
        for pattern in patterns:
            for field_name in usable:
                if pattern in field_name.lower():
                    claim(slot, field_name)
                    break
            if profile[slot]:
                break

    # Positional fallback for whatever names told us nothing: the first
    # unclaimed field is the word, the next the meaning. Categorical fields
    # sit this out, but stay available if nothing else is left.
    unclaimed = [
        f for f in usable if f not in claimed and f not in categorical_fields
    ] or [f for f in usable if f not in claimed]
    if not profile["headword"] and unclaimed:
        claim("headword", unclaimed.pop(0))
    if not profile["meaning"] and unclaimed:
        claim("meaning", unclaimed.pop(0))

    if override:
        profile.update(override)
    return profile


FIELD_ORDER = [
    "headword",
    "reading",
    "meaning",
    "partOfSpeech",
    "sentence",
    "sentenceReading",
    "sentenceMeaning",
]

# What actually goes on disk. FIELD_ORDER stays the list of slots a *profile*
# can fill and that --map can name, because clozeQuestion is not something a
# user maps a field onto -- it is built, from the cloze markup, by the code
# below. Splitting the two lists is what keeps generic_profile from offering a
# slot nobody can usefully fill.
#
# A note with a non-empty clozeQuestion IS a cloze card. That is the whole
# marker: no kind byte, no second table. It costs nothing (a vocabulary note
# writes a zero length there) and it means every tool that reads deck.dat by
# looping over the header's `fields` count -- check_deck.py, make_fonts.py,
# measure_layout.py -- keeps working with no change at all. A kind byte in
# front of the lengths would have broken all three.
DEVICE_FIELDS = FIELD_ORDER + ["clozeQuestion"]

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

CLOZE_RE = re.compile(r"\{\{c\d+::")
# The full form, with the optional hint: {{c1::answer}} or {{c1::answer::hint}}.
# Non-greedy on both halves, which is what makes the two shapes one pattern --
# see the walk-through in test_cloze.py. Nested cloze (a {{c2::}} inside a
# {{c1::}}) is Anki's own corner case and is left as literal text rather than
# half-parsed: the outer hole still works, which is the behaviour that loses
# the least.
CLOZE_SPAN_RE = re.compile(r"\{\{c(\d+)::(.*?)(?:::(.*?))?\}\}", re.DOTALL)

# The hole, as it appears on the question face. Anki draws a blue [...]; there
# is no colour here, so the brackets carry the whole signal and have to be
# visible in the sentence face at 34px -- which square brackets are and an
# ellipsis alone is not.
CLOZE_BLANK = "[...]"

# Fields that hold the note's extra material rather than its cloze text.
# Anki's stock Cloze type calls it "Back Extra"; the shared decks that grew
# from it use these.
CLOZE_EXTRA_PATTERNS = ("back extra", "extra", "notes", "note", "comment", "source")


def cloze_field_index(parts):
    """Which of a note's fields carries the cloze markup.

    The template would say so -- {{cloze:Text}} names it -- but the template
    body is not in the tables apkg.py builds for a legacy package, and a rule
    that works on a live collection and not on an AnkiWeb shared deck is worse
    than no rule. The first field with markup in it is the answer in every
    real deck: the stock type has exactly one such field, and a type with two
    generates its cards from both, which this format cannot show anyway.
    """
    for index, value in enumerate(parts):
        if CLOZE_RE.search(value):
            return index
    return -1


def cloze_extra_index(parts, cloze_index, field_names):
    """Which field is the Back Extra, by name first and position second."""
    for pattern in CLOZE_EXTRA_PATTERNS:
        for index, name in enumerate(field_names):
            if pattern in name.lower() and index != cloze_index and index < len(parts):
                if clean(parts[index]):
                    return index
    for index in range(len(parts)):
        if index != cloze_index and clean(parts[index]):
            return index
    return -1


def render_cloze(raw, ordinal):
    """Build one cloze card's two faces out of the note's cloze field.

    Returns (question, answer, offset, length) with the offsets in codepoints
    into `answer`, or None when this ordinal has no hole in the text -- which
    happens when someone edits a cloze out and Anki leaves the card behind as
    an empty one. Skipping it is what Anki's own "Empty cards" does; showing
    it would put a question with no hole and its own answer on one face.

    The two faces follow Anki's {{cloze:}} exactly: on the question the target
    ordinal's holes become [...] (or [hint]), and every OTHER ordinal is shown
    filled in, because those are context this card is not testing. On the
    answer every hole is filled and the target is the one marked.
    """
    if not CLOZE_SPAN_RE.search(raw):
        return None

    question_parts, answer_parts = [], []
    prefix_raw, target_raw = None, None
    last = 0
    found = False
    for match in CLOZE_SPAN_RE.finditer(raw):
        between = raw[last : match.start()]
        question_parts.append(between)
        answer_parts.append(between)
        text, hint = match.group(2), match.group(3)
        if int(match.group(1)) == ordinal:
            if not found:
                # Where the answer's marked span begins, measured the way
                # clean_sentence measures it: on the CLEANED prefix, because
                # stripping tags moves every offset after it.
                prefix_raw = "".join(answer_parts)
                target_raw = text
                found = True
            question_parts.append(f"[{hint}]" if hint else CLOZE_BLANK)
        else:
            question_parts.append(text)
        answer_parts.append(text)
        last = match.end()
    question_parts.append(raw[last:])
    answer_parts.append(raw[last:])

    if not found:
        return None

    question = clean("".join(question_parts))
    answer = clean("".join(answer_parts))
    if not question or not answer:
        return None

    before = clean(prefix_raw)
    target = clean(target_raw)
    offset = answer.find(target, max(0, len(before) - 2)) if target else -1
    if offset < 0 or not target:
        return question, answer, 0, 0
    return question, answer, len(answer[:offset]), len(target)
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


def make_note(
    cid,
    nt_name,
    fields,
    bold,
    data,
    due,
    ivl,
    ctype,
    queue,
    left,
    reps,
    lapses,
    last_review_day,
):
    """One note record, however it was built. The vocabulary path and the cloze
    path fill `fields` differently and agree on everything else, and the
    everything-else is the scheduling state -- which is the half that must not
    drift between them."""
    memory = {}
    if data:
        try:
            memory = json.loads(data)
        except ValueError:
            memory = {}
    assert len(fields) == len(DEVICE_FIELDS), fields
    return {
        "ankiCardId": cid,
        "noteType": nt_name,
        "fields": fields,
        "bold": bold,
        "stability": float(memory.get("s", 0.0)),
        "difficulty": float(memory.get("d", 0.0)),
        "due": due,
        "ivl": ivl,
        "ctype": ctype,
        "queue": queue,
        "left": left,
        "reps": reps,
        "lapses": lapses,
        "lastReviewDay": last_review_day,
    }


def collect_notes(db, deck_name, limit=None, override=None):
    # A card sitting in a filtered deck (Custom Study) has did = the filtered
    # deck and odid = the deck it came from. Joining on did alone dropped every
    # such card from its home deck's export -- silently, and the picker's count
    # used the same join so the number agreed with the shortfall. The cards a
    # user just flagged as needing work are exactly the ones this lost.
    like = deck_name.replace("::", "\x1f")
    rows = db.execute(
        """select c.id, n.flds, nt.name, c.data, c.due, c.ivl, c.reps, c.lapses, c.type,
                  c.queue, c.left, c.ord
           from cards c
           join notes n on n.id = c.nid
           join notetypes nt on nt.id = n.mid
           join decks d on d.id = (case when c.odid = 0 then c.did else c.odid end)
           where d.name = ? or d.name like ?
           order by c.id""",
        (like, like + "\x1f%"),
    ).fetchall()
    if limit:
        rows = rows[:limit]

    # The real day of each card's last review. One query rather than one per
    # card, and exact where `due - ivl` is only an approximation: Anki rewrites
    # due dates when FSRS parameters are optimised, and a learning card's `due`
    # is not a day at all.
    crt = db.execute("select crt from col").fetchone()[0]
    last_review = {}
    for cid, ms in db.execute(
        "select cid, max(id) from revlog where ease between 1 and 4 group by cid"
    ):
        last_review[cid] = (ms // 1000 - crt) // 86400

    # Field name -> ordinal, per note type.
    ordinals = {}
    for nt_name, ord_, f_name in db.execute(
        "select nt.name, f.ord, f.name from fields f join notetypes nt on nt.id = f.ntid"
    ):
        ordinals.setdefault(nt_name, {})[f_name] = ord_

    # How many card templates each note type has, for the reversed-card swap.
    template_counts = {}
    for nt_name, count in db.execute(
        "select nt.name, count(*) from templates t join notetypes nt on nt.id = t.ntid group by nt.name"
    ):
        template_counts[nt_name] = count

    # How often each field of each note type actually carries text. A field
    # that is empty everywhere is a field the deck's author abandoned, and
    # mapping a face onto it produces a blank card face.
    filled = {}
    totals = {}
    for _cid, flds, nt_name, *_rest in rows:
        values = flds.split("\x1f")
        totals[nt_name] = totals.get(nt_name, 0) + 1
        counts = filled.setdefault(nt_name, {})
        for ordinal, value in enumerate(values):
            if clean(value):
                counts[ordinal] = counts.get(ordinal, 0) + 1

    # Distinct values per field, the signal that separates a card's content
    # from a label attached to it.
    distinct = {}
    for _cid, flds, nt_name, *_rest in rows:
        values = flds.split("\x1f")
        seen = distinct.setdefault(nt_name, {})
        for ordinal, value in enumerate(values):
            text = clean(value)
            if text:
                seen.setdefault(ordinal, set()).add(text)

    def categorical_fields_for(nt_name):
        by_ordinal = ordinals.get(nt_name, {})
        total = totals.get(nt_name, 0)
        # Under thirty notes there is no distribution to read: a small deck
        # legitimately repeats itself.
        if total < 30:
            return set()
        counts = distinct.get(nt_name, {})
        return {
            name
            for name, ordinal in by_ordinal.items()
            if 0 < len(counts.get(ordinal, ())) <= min(10, total * 0.05)
        }

    def blank_fields_for(nt_name):
        by_ordinal = ordinals.get(nt_name, {})
        total = totals.get(nt_name, 0)
        if not total:
            return set()
        counts = filled.get(nt_name, {})
        # 5%: a handful of notes missing a field is normal; a field filled on
        # almost none of them is not a field this deck uses.
        return {
            name
            for name, ordinal in by_ordinal.items()
            if counts.get(ordinal, 0) <= total * 0.05
        }

    # ord -> field name, per note type: the cloze path needs the names in
    # order and `ordinals` is keyed the other way.
    names_in_order = {
        nt: [name for name, _ in sorted(by_name.items(), key=lambda kv: kv[1])]
        for nt, by_name in ordinals.items()
    }

    USED_PROFILES.clear()
    announced = set()
    notes, skipped, cloze_empty = [], 0, 0
    for (
        cid,
        flds,
        nt_name,
        data,
        due,
        ivl,
        reps,
        lapses,
        ctype,
        queue,
        left,
        card_ord,
    ) in rows:
        profile = PROFILES.get(nt_name)
        if profile:
            USED_PROFILES.setdefault(nt_name, dict(profile))
        # By name, and by content: a renamed cloze type still has to take the
        # cloze path, because the generic profile would put the raw markup --
        # answer included -- on the question face, and leaking the answer is
        # the worst thing a flashcard converter can do.
        if not profile and ("cloze" in nt_name.lower() or CLOZE_RE.search(flds)):
            parts = flds.split("\x1f")
            cloze_index = cloze_field_index(parts)
            if cloze_index < 0:
                skipped += 1
                continue
            # Anki numbers a cloze card's template ordinal from zero and its
            # cloze from one, so this card is the hole {{c<ord+1>::}}.
            rendered = render_cloze(parts[cloze_index], card_ord + 1)
            if rendered is None:
                cloze_empty += 1
                continue
            question, answer, bold_off, bold_len = rendered
            extra_index = cloze_extra_index(
                parts, cloze_index, names_in_order.get(nt_name, [])
            )
            extra = clean(parts[extra_index]) if extra_index >= 0 else ""
            if nt_name not in announced:
                announced.add(nt_name)
                field_names = names_in_order.get(nt_name, [])
                cloze_name = (
                    field_names[cloze_index]
                    if cloze_index < len(field_names)
                    else f"field {cloze_index}"
                )
                print(
                    f"  note type {nt_name!r} is cloze; the hole comes from"
                    f" {cloze_name!r}"
                    + (
                        f", the extra from {field_names[extra_index]!r}"
                        if 0 <= extra_index < len(field_names)
                        else ""
                    )
                )
            notes.append(
                make_note(
                    cid,
                    nt_name,
                    [
                        "",  # headword: a cloze card's question is the sentence
                        "",
                        extra,
                        "",
                        answer,
                        "",
                        "",
                        question,
                    ],
                    (bold_off, bold_len),
                    data,
                    due,
                    ivl,
                    ctype,
                    queue,
                    left,
                    reps,
                    lapses,
                    last_review.get(cid, -1),
                )
            )
            continue
        if not profile:
            in_order = (
                sorted(ordinals.get(nt_name, {}), key=ordinals[nt_name].get)
                if nt_name in ordinals
                else []
            )
            profile = generic_profile(
                in_order,
                override,
                blank_fields_for(nt_name),
                categorical_fields_for(nt_name),
            )
            # The second card of a two-template type asks the question the other
            # way round, so the word and the meaning trade places. Only for the
            # generic path: a written profile said what it meant.
            if template_counts.get(nt_name, 1) == 2 and card_ord == 1 and not override:
                profile = dict(
                    profile, headword=profile["meaning"], meaning=profile["headword"]
                )
            USED_PROFILES.setdefault(nt_name, dict(profile))
            if nt_name not in announced:
                announced.add(nt_name)
                print(
                    f"  note type {nt_name!r} has no profile; using "
                    f"{profile['headword']!r} as the word and {profile['meaning']!r} as the meaning"
                    + (
                        " (reversed cards swap them)"
                        if template_counts.get(nt_name, 1) == 2
                        else ""
                    )
                )
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

        notes.append(
            make_note(
                cid,
                nt_name,
                [
                    headword,
                    clean(get("reading")),
                    clean(get("meaning")),
                    clean(get("partOfSpeech")),
                    sentence,
                    clean(get("sentenceReading")),
                    clean(get("sentenceMeaning")),
                    "",  # clozeQuestion: empty is what makes this a vocabulary card
                ],
                (bold_off, bold_len),
                data,
                due,
                ivl,
                ctype,
                queue,
                left,
                reps,
                lapses,
                last_review.get(cid, -1),
            )
        )
    if cloze_empty:
        print(
            f"  {cloze_empty} empty cloze card(s) skipped: their hole is no longer in"
            f" the text. Anki calls these empty cards and deletes them under"
            f' Tools > "Empty Cards"'
        )
    return notes, skipped + cloze_empty


# study::kMaxNoteBytes. A record the device would refuse is a card that
# silently does not appear, so the budget is enforced HERE, where there is
# somewhere to print about it, rather than discovered on the reader.
MAX_NOTE_BYTES = 1024


def trim_to_bytes(text, budget):
    """Cut `text` to at most `budget` UTF-8 bytes, on a codepoint boundary and
    preferably on a word one, ending in a marker that says it was cut."""
    if len(text.encode("utf-8")) <= budget:
        return text
    marker = "\u2026"
    room = budget - len(marker.encode("utf-8"))
    if room <= 0:
        return ""
    cut = text
    while len(cut.encode("utf-8")) > room:
        cut = cut[:-1]
    spaced = cut.rsplit(" ", 1)[0]
    # Only if that leaves most of the text: a Chinese sentence has no spaces
    # and rsplit would throw the whole thing away.
    if len(spaced) > len(cut) * 0.6:
        cut = spaced
    return cut.rstrip() + marker


def enforce_note_budget(notes):
    """Bring every record inside what StudyDeck can read.

    The device rejects an oversized record outright, which shows up as a card
    that is simply not there -- the worst failure mode available, because
    nothing on the reader can say why. Trimming loses the tail of one long
    note instead, visibly, and says how many it did.

    Trimmed by water-filling: the longest field comes down to the second
    longest, then both come down together, and so on. Taking the whole
    overflow out of the single longest field is the obvious loop and it is
    wrong for exactly the note kind this exists for -- a cloze card's question
    and answer are the same sentence twice, so they are always the two longest
    fields and always within a few bytes of each other, and the obvious loop
    empties one of them completely while leaving the other untouched.
    """
    # Two length bytes per field, one NUL per field on the device, the two
    # emphasis bytes that ride inside the sentence field, and one spare.
    overhead = 3 * len(DEVICE_FIELDS) + 2 + 1
    trimmed = 0
    for note in notes:
        fields = note["fields"]
        sizes = [len(f.encode("utf-8")) for f in fields]
        if sum(sizes) + overhead <= MAX_NOTE_BYTES:
            continue
        budget = MAX_NOTE_BYTES - overhead
        # The cap that makes the fields fit: every field longer than `cap`
        # comes down to it, everything shorter is left alone. Searched
        # downward one byte at a time -- at most MAX_NOTE_BYTES steps over
        # eight numbers, and only for a note that is actually over, which is
        # cheaper than it looks and is obviously right, which the closed form
        # was not.
        cap = budget
        while cap > 0 and sum(min(size, cap) for size in sizes) > budget:
            cap -= 1
        for i, size in enumerate(sizes):
            if size > cap:
                fields[i] = trim_to_bytes(fields[i], cap)
                trimmed += 1
        # The emphasis span points into the sentence by codepoint; once that
        # has been cut the span can point past the end of it.
        note["bold"] = (0, 0)
    return trimmed


def write_deck(notes, path):
    index, blob = [], bytearray()
    for note in notes:
        index.append(len(blob))
        payload = bytearray()
        for i, text in enumerate(note["fields"]):
            encoded = text.encode("utf-8")
            if i == FIELD_ORDER.index("sentence"):
                # The two emphasis bytes ride inside the sentence field's
                # length, so a reader that ignores them still sees valid text.
                # Appended *unconditionally*, including as (0, 0): if they were
                # only present when Anki had a <b>, the reader could not tell a
                # sentence ending in two low bytes from one carrying a span,
                # and would chop two bytes off the wrong records.
                # Dropped, not clamped, when it will not fit a byte: a
                # clamped offset points at the wrong word, and underlining the
                # wrong word is worse than underlining none. Cloze answers are
                # what made this reachable -- a vocabulary example sentence is
                # never 255 codepoints long, a cloze paragraph can be.
                off, span = note["bold"]
                if off > 255 or span > 255:
                    off, span = 0, 0
                encoded += bytes((off, span))
            if len(encoded) > 0xFFFF:
                encoded = encoded[:0xFFFF]
            payload += struct.pack("<H", len(encoded)) + encoded
        blob += payload
    index.append(len(blob))  # sentinel: makes a record's length a subtraction

    with open(path, "wb") as f:
        f.write(DECK_MAGIC)
        f.write(struct.pack("<HBBI", FORMAT_VERSION, len(DEVICE_FIELDS), 0, len(notes)))
        base = len(DECK_MAGIC) + 8 + 4 * len(index)
        f.write(b"".join(struct.pack("<I", base + off) for off in index))
        f.write(blob)
    return len(blob)


# Revlog `type` 3 is a filtered-deck ("cram") review. Anki does not feed those
# into FSRS when rebuilding memory state, because rescheduling is off in those
# decks. Same exclusion as gen_fsrs_vectors.py, and equally load-bearing.
REVLOG_FILTERED = 3


def seed_memory_from_revlog(db, notes, params):
    """Fill in stability/difficulty for cards whose collection no longer stores it.

    Only touches cards that have been reviewed (reps > 0) but arrived with no
    stored memory state; a stored state always wins, because Anki's own number
    beats our replay of the history behind it. Returns how many were seeded.
    """
    todo = {
        n["ankiCardId"]: n for n in notes if n["stability"] <= 0.0 and n["reps"] > 0
    }
    if not todo:
        return 0

    if params is not None and len(params) != NUM_PARAMS:
        params = None

    crt = db.execute("select crt from col").fetchone()[0]

    # One pass over the whole revlog rather than an IN(...) per-card query:
    # SQLite's variable cap is 999 in older builds (Pyodide included), and a
    # linear scan of even a large revlog is cheaper than being clever.
    steps = {cid: [] for cid in todo}
    prev_day = {}
    for cid, ms, ease, kind in db.execute(
        "select cid, id, ease, type from revlog order by cid, id"
    ):
        if cid not in todo or not 1 <= ease <= 4 or kind == REVLOG_FILTERED:
            continue
        d = (ms // 1000 - crt) // 86400
        steps[cid].append((0 if cid not in prev_day else d - prev_day[cid], ease))
        prev_day[cid] = d

    seeded = 0
    for cid, note in todo.items():
        stability, difficulty = fsrs.replay(steps[cid], params)
        if stability > 0.0:
            note["stability"] = stability
            note["difficulty"] = difficulty
            seeded += 1
    return seeded


def write_cards(notes, path, crt, rollover, learn_steps, relearn_steps):
    """Seed scheduling state from Anki, so the device resumes rather than restarts."""
    day_zero = crt

    with open(path, "wb") as f:
        for note in notes:
            ctype = note["ctype"]
            queue = note["queue"]

            # A card Anki will not show must not be shown here either. Suspending
            # a card in Anki and then meeting it on the device is the kind of
            # divergence that makes a user stop trusting the sync.
            if queue < 0:
                state = STATE_SUSPENDED
            elif ctype in (STATE_LEARNING, STATE_RELEARNING):
                state = ctype
            elif note["stability"] > 0.0:
                state = STATE_REVIEW
            else:
                state = STATE_NEW

            due_day, due_minute = 0, 0
            if state in (STATE_LEARNING, STATE_RELEARNING):
                # Anki stores an intraday learning card's due as a unix
                # timestamp and a day-learning card's as a day number. Reading
                # the first as the second produces a due date eight hundred
                # thousand years out, which is exactly what happened: twelve
                # relearning cards imported as garbage and the device offered
                # 2 days where Anki offered 15 minutes.
                if queue == QUEUE_LEARNING_INTRADAY or note["due"] > 10**9:
                    seconds = note["due"] - day_zero
                    due_day = max(0, seconds // 86400)
                    due_minute = max(0, (seconds % 86400) // 60)
                else:
                    due_day = note["due"]
            elif state == STATE_REVIEW:
                due_day = note["due"]

            # Where in the step list the card is sitting. Anki's `left` counts
            # steps *remaining*, so the index is the list length minus that.
            steps = relearn_steps if state == STATE_RELEARNING else learn_steps
            step_index = 0
            if state in (STATE_LEARNING, STATE_RELEARNING) and steps:
                remaining = (note["left"] or 0) % 1000
                step_index = max(0, min(len(steps) - 1, len(steps) - remaining))

            # The real day of the last review, from the review log. Anki does
            # not store it on the card, and the obvious substitute (due - ivl)
            # is only correct for review cards -- for a learning card `due` is
            # not even a day. Without it FSRS takes the same-day path for a card
            # last seen a year ago and inflates every interval.
            last_day = note["lastReviewDay"]
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
                    step_index,
                    due_minute,
                )
            )
    assert path.stat().st_size == len(notes) * CARD_RECORD_SIZE


def write_meta(path, name, config, crt, rollover, flags=0):
    params = config.get("params") or []
    if len(params) != NUM_PARAMS:
        print(
            "  warning: deck has no FSRS-5 parameters; the device will use Anki's defaults"
        )
        params = [0.0] * NUM_PARAMS
    encoded = name.encode("utf-8")[:255]
    with open(path, "wb") as f:
        f.write(META_MAGIC)
        f.write(struct.pack("<HH", FORMAT_VERSION, flags))
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
    ap.add_argument(
        "--map",
        action="append",
        metavar="SLOT=FIELD",
        help="for note types without a profile: which Anki field fills which slot"
        " (e.g. --map headword=Word --map reading=Pronunciation); repeatable."
        " Overrides apply to every card, so reversed-card templates stop swapping",
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

    override = {}
    for pair in args.map or []:
        key, sep, field = pair.partition("=")
        if key not in FIELD_ORDER or not sep:
            sys.exit(
                f"--map wants <slot>=<Anki field name>; slots: {', '.join(FIELD_ORDER)}"
            )
        # An empty field is meaningful: "--map sentence=" says leave this slot
        # blank. Rejecting it left the installer's "(not shown)" unable to
        # clear anything, so the menu snapped back to the converter's guess.
        override[key] = field

    notes, skipped = collect_notes(db, args.deck, args.limit, override or None)
    if not notes:
        sys.exit(
            f"no convertible cards in '{args.deck}'. Cards convert when their note type"
            f" has a profile ({', '.join(PROFILES)}), generically by field order, or"
            f" as cloze. If the generic guess picked the wrong fields,"
            f" name them: --map headword=Word --map meaning=Definition"
        )
    config = deck_config_for(db, args.deck)
    replayed = seed_memory_from_revlog(db, notes, config.get("params") or None)

    args.out.mkdir(parents=True, exist_ok=True)
    name = args.name or args.deck.split("::")[-1]
    over_budget = enforce_note_budget(notes)
    blob_size = write_deck(notes, args.out / "deck.dat")
    write_cards(
        notes,
        args.out / "cards.dat",
        crt,
        rollover,
        config.get("learnSteps") or [1.0, 10.0],
        config.get("relearnSteps") or [10.0],
    )
    # The sentence goes on the answer face unless this deck's note type is one
    # whose template puts it in front of you while you answer.
    flags = 0
    if any(n["noteType"] in SENTENCE_ON_QUESTION_TYPES for n in notes):
        flags |= META_SENTENCE_ON_QUESTION
    write_meta(args.out / "meta.dat", name, config, crt, rollover, flags)
    (args.out / "revlog.dat").touch()
    glyphs = write_glyphs(notes, args.out)

    learned = sum(1 for n in notes if n["stability"] > 0)
    replay_note = f", {replayed} replayed from the review log" if replayed else ""
    print(
        f"deck '{name}': {len(notes)} cards"
        f" ({learned} with scheduling state{replay_note}, {skipped} skipped)"
    )
    print(f"  content   {blob_size / 1024:.0f} KB")
    if over_budget:
        print(
            f"  trimmed   {over_budget} field(s) too long for one card"
            f" ({MAX_NOTE_BYTES} bytes); the tail is marked with an ellipsis"
        )
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
