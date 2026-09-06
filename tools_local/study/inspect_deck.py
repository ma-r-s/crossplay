#!/usr/bin/env python3
"""Say what a deck directory on an SD card is, and whether its progress is safe to replay.

    tools_local/study/inspect_deck.py /Volumes/SDCARD/study/mandarin
    tools_local/study/inspect_deck.py /Volumes/SDCARD/study/mandarin --collection ~/…/collection.anki2

WHY THIS EXISTS. A card may have been written by a different build of these
tools -- another branch, an older release, someone else's fork. The card
content changes between builds; the two files that carry your PRACTICE do
not, and this says so out loud instead of leaving it to be assumed:

  deck.dat   card text. Rewritten on every conversion. Progress is NOT here.
  meta.dat   FSRS parameters and deck identity. Also rewritten. Not here either.
  cards.dat  scheduling state, keyed by ANKI CARD ID inside each record.
  revlog.dat every review, keyed by (anki card id, millisecond answered) --
             which is Anki's own revlog primary key.

`deck_to_anki.py` reads only the last two, and reads them BY CARD ID rather
than by position (read_cards builds a dict keyed on the id in the record).
So replaying a deck written by another build does not depend on that build's
field layout, note order, fonts, or rendering at all. What it does depend on
is the 32-byte record layout, and that is what this script checks, record by
record, before anyone writes into the only copy of their collection.

Exit status is 0 when the progress is safe to replay and 1 when it is not.
"""

import argparse
import datetime
import pathlib
import sqlite3
import struct
import sys

DECK_MAGIC = b"XSTUDYD\0"
META_MAGIC = b"XSTUDYM\0"

CARD_RECORD_SIZE = 32
REVLOG_RECORD_SIZE = 32
REVLOG_RECORD = "<qqBBhiIB3x"
REVLOG_VOIDED = 1 << 0

# A review before this is a clock that was never set; after it, a clock that
# is wrong the other way. The device stamps reviews from its RTC, and a flat
# battery clears that -- which is why StudyActivity refuses to log a review
# while the clock is unset. A file full of 1970 is the symptom of a build
# where that refusal was missing.
EPOCH_FLOOR = int(datetime.datetime(2015, 1, 1).timestamp() * 1000)


def _ceiling():
    return int(datetime.datetime.now().timestamp() * 1000) + 86400 * 1000


class Report:
    def __init__(self):
        self.lines = []
        self.problems = []
        self.warnings = []

    def say(self, text):
        self.lines.append(text)

    def problem(self, text):
        self.problems.append(text)

    def warn(self, text):
        self.warnings.append(text)


def inspect_deck_file(path, report):
    """deck.dat: which format wrote this card. Never carries progress."""
    if not path.is_file():
        report.warn("no deck.dat: this directory has card state but no card text")
        return
    data = path.read_bytes()
    if len(data) < 16 or data[:8] != DECK_MAGIC:
        report.warn(f"deck.dat is not a study deck ({len(data)} bytes)")
        return
    version, fields, flags, count = struct.unpack_from("<HBBI", data, 8)
    known = {2: "v2 (seven fields)", 3: "v3 (eight fields, cloze)"}
    shape = known.get(version, f"v{version} -- NOT a format this build knows")
    report.say(f"deck.dat    {shape}, {fields} fields, {count} notes, flags {flags}")
    if version > 3:
        report.warn(
            f"deck.dat is v{version}: this firmware reads v2 and v3 and will refuse it."
            " Re-convert; that costs nothing, because the card text is rebuilt from"
            " Anki every time anyway."
        )
    elif version == 3 and fields != 8:
        report.warn(
            f"deck.dat is v3 but carries {fields} fields where this build writes 8."
            " Another build's v3 is not necessarily this one's. Re-convert rather"
            " than trusting the layout."
        )
    return count


def inspect_meta(path, report):
    if not path.is_file():
        report.warn("no meta.dat: the reader will not list this directory as a deck")
        return
    data = path.read_bytes()
    if len(data) < 12 or data[:8] != META_MAGIC:
        report.warn("meta.dat is not a study meta file")
        return
    version, flags = struct.unpack_from("<HH", data, 8)
    report.say(f"meta.dat    v{version}, flags {flags}")


def inspect_cards(path, report):
    """cards.dat: scheduling state, one record per note, keyed by Anki card id."""
    if not path.is_file():
        report.problem("no cards.dat: there is no scheduling state to carry over")
        return {}
    data = path.read_bytes()
    if len(data) % CARD_RECORD_SIZE:
        report.problem(
            f"cards.dat is {len(data)} bytes, not a whole number of"
            f" {CARD_RECORD_SIZE}-byte records -- this is not the layout"
            " deck_to_anki.py reads, and replaying it would write nonsense"
        )
        return {}

    cards = {}
    bad_ids = bad_state = 0
    reviewed = 0
    for i in range(len(data) // CARD_RECORD_SIZE):
        chunk = data[i * CARD_RECORD_SIZE : (i + 1) * CARD_RECORD_SIZE]
        card_id, stability, difficulty, _due, _last, reps, _lapses = struct.unpack(
            "<qffiiHH", chunk[:28]
        )
        state = chunk[28]
        if card_id <= 0:
            bad_ids += 1
            continue
        if state > 4:
            bad_state += 1
        if reps > 0 or stability > 0:
            reviewed += 1
        cards[card_id] = state
    report.say(
        f"cards.dat   {len(data) // CARD_RECORD_SIZE} records,"
        f" {len(cards)} with an Anki card id, {reviewed} carrying review state"
    )
    if bad_ids:
        report.problem(
            f"{bad_ids} card record(s) carry no Anki card id. Without it there is"
            " nothing to replay them onto."
        )
    if bad_state:
        report.problem(
            f"{bad_state} card record(s) have a state byte outside 0..4, so this is"
            " not the record layout this tool reads"
        )
    return cards


def inspect_revlog(path, report):
    """revlog.dat: the practice itself. This is the file that matters."""
    if not path.is_file():
        report.problem("no revlog.dat: there are no reviews to carry over")
        return []
    data = path.read_bytes()
    if len(data) == 0:
        report.say("revlog.dat  empty: nothing has been reviewed on this card")
        return []
    if len(data) % REVLOG_RECORD_SIZE:
        report.problem(
            f"revlog.dat is {len(data)} bytes, not a whole number of"
            f" {REVLOG_RECORD_SIZE}-byte records -- this is not the layout"
            " deck_to_anki.py reads, and replaying it would write nonsense into"
            " the collection"
        )
        return []

    ceiling = _ceiling()
    reviews = []
    voided = 0
    bad_rating = bad_time = bad_id = bad_state = nonzero_took = 0
    for i in range(len(data) // REVLOG_RECORD_SIZE):
        chunk = data[i * REVLOG_RECORD_SIZE : (i + 1) * REVLOG_RECORD_SIZE]
        card_id, at_ms, rating, state, _elapsed, _interval, took, flags = struct.unpack(
            REVLOG_RECORD, chunk
        )
        if flags & REVLOG_VOIDED:
            voided += 1
            continue
        if card_id <= 0:
            bad_id += 1
        if not 1 <= rating <= 4:
            bad_rating += 1
        if state > 4:
            bad_state += 1
        if not EPOCH_FLOOR <= at_ms <= ceiling:
            bad_time += 1
        if took != 0:
            nonzero_took += 1
        reviews.append((card_id, at_ms, rating))

    report.say(
        f"revlog.dat  {len(data) // REVLOG_RECORD_SIZE} records,"
        f" {len(reviews)} live, {voided} taken back with UNDO"
    )
    if reviews:
        first = min(r[1] for r in reviews)
        last = max(r[1] for r in reviews)
        days = len({datetime.date.fromtimestamp(r[1] / 1000) for r in reviews})
        report.say(
            f"            from {datetime.datetime.fromtimestamp(first / 1000):%Y-%m-%d %H:%M}"
            f" to {datetime.datetime.fromtimestamp(last / 1000):%Y-%m-%d %H:%M}"
            f", across {days} day(s)"
        )
        grades = {g: sum(1 for r in reviews if r[2] == g) for g in (1, 2, 3, 4)}
        report.say(
            f"            again {grades[1]}, hard {grades[2]},"
            f" good {grades[3]}, easy {grades[4]}"
        )
        report.say(f"            {len({r[0] for r in reviews})} distinct card(s) touched")

    # Every one of these means the file is not the layout this tool reads, and
    # replaying it would write into the user's collection from a wrong offset.
    for count, what in (
        (bad_id, "carry no Anki card id"),
        (bad_rating, "have a rating outside 1..4"),
        (bad_state, "have a state byte outside 0..4"),
        (bad_time, "are stamped outside any plausible date"),
    ):
        if count:
            report.problem(f"{count} review record(s) {what}")
    if nonzero_took:
        report.warn(
            f"{nonzero_took} review(s) carry a non-zero answer time. This build"
            " writes zero on purpose (inventing one biases FSRS optimisation),"
            " so these came from a build that did something else. Harmless to"
            " replay; worth knowing before optimising parameters."
        )
    return reviews


def cross_check(collection, cards, reviews, report):
    """Do these Anki card ids exist in the collection you are replaying into?

    This is the check that catches the real mistake -- pointing the replay at
    a different profile, or at a collection the deck was never exported from.
    Every id missing means every review would be dropped.
    """
    db = sqlite3.connect(f"file:{collection}?mode=ro", uri=True)
    try:
        known = {row[0] for row in db.execute("select id from cards")}
    finally:
        db.close()
    ids = {r[0] for r in reviews} | set(cards)
    if not ids:
        return
    missing = ids - known
    report.say(
        f"collection  {len(ids) - len(missing)}/{len(ids)} card id(s) found in"
        f" {collection.name}"
    )
    if len(missing) == len(ids):
        report.problem(
            "not one card id is in this collection. This is almost certainly the"
            " wrong profile, or a collection the deck was never exported from --"
            " replaying would apply nothing at all."
        )
    elif missing:
        report.warn(
            f"{len(missing)} card id(s) are not in this collection. Those cards were"
            " probably deleted in Anki since the deck was made; their reviews will"
            " be skipped."
        )


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("deck", type=pathlib.Path, help="a deck directory under /study/")
    ap.add_argument(
        "--collection",
        type=pathlib.Path,
        help="check the card ids against this collection.anki2",
    )
    args = ap.parse_args()

    if not args.deck.is_dir():
        sys.exit(f"no directory at {args.deck}")

    report = Report()
    print(f"{args.deck}\n")
    inspect_deck_file(args.deck / "deck.dat", report)
    inspect_meta(args.deck / "meta.dat", report)
    cards = inspect_cards(args.deck / "cards.dat", report)
    reviews = inspect_revlog(args.deck / "revlog.dat", report)
    if args.collection:
        if not args.collection.is_file():
            sys.exit(f"no collection at {args.collection}")
        cross_check(args.collection, cards, reviews, report)

    for line in report.lines:
        print(line)
    if report.warnings:
        print()
        for text in report.warnings:
            print(f"  note: {text}")

    print()
    if report.problems:
        for text in report.problems:
            print(f"  PROBLEM: {text}")
        print(
            "\nDo NOT replay this deck. deck_to_anki.py writes into your collection,"
            "\nand these records are not the layout it reads."
        )
        return 1

    print(
        "Progress is safe to replay. cards.dat and revlog.dat are keyed by Anki\n"
        "card id, so nothing here depends on which build wrote the card text:\n"
        "\n"
        "  tools_local/study/deck_to_anki.py %s <collection.anki2>\n"
        "  tools_local/study/study.py setup --replace      # re-convert with this build\n"
        "\n"
        "Close Anki first. The replay is idempotent -- a review is keyed by the\n"
        "millisecond it was answered, which is Anki's own revlog primary key --\n"
        "so running it twice applies nothing the second time." % args.deck
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
