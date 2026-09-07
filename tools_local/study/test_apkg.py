#!/usr/bin/env python3
"""Prove the .apkg path end to end: export, unwrap, convert, check.

    .venv-study/bin/python tools_local/study/test_apkg.py

Builds fixtures with Anki's own exporter (make_fixture_apkg.py), opens them
with apkg.py, runs anki_to_deck.py on the unwrapped collection, and parses
what came out. Every assertion is counted, and the count is printed, because
"PASS (0 checks)" has bitten this project before.
"""

import pathlib
import sqlite3
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import apkg  # noqa: E402
import check_deck  # noqa: E402

CHECKS = 0


def ok(condition, what):
    global CHECKS
    CHECKS += 1
    if not condition:
        sys.exit(f"FAIL check {CHECKS}: {what}")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        fixtures = tmp / "fixtures"
        result = subprocess.run(
            [
                sys.executable,
                str(HERE / "make_fixture_apkg.py"),
                "--out",
                str(fixtures),
            ],
            capture_output=True,
            text=True,
        )
        ok(result.returncode == 0, f"fixture build failed:\n{result.stderr}")

        # --- the modern package unwraps -----------------------------------
        info = apkg.extract(fixtures / "sat-vocabulary.apkg", tmp / "unpacked")
        ok(info["version"] >= 3, f"expected a v3 package, got v{info['version']}")
        ok(info["collection"].exists(), "no collection extracted")
        ok(
            info["media_kept"] == 1,
            f"expected 1 media file kept, got {info['media_kept']}",
        )
        ok(
            (info["media_dir"] / "sample.png").exists(),
            "sample.png not in media dir",
        )

        decks = apkg.list_decks(info["collection"])
        names = [name for name, _ in decks]
        ok(
            any("SAT Vocabulary" in n for n in names),
            f"deck list missing SAT Vocabulary: {names}",
        )
        sat_cards = next(c for n, c in decks if "SAT Vocabulary" in n)
        ok(
            sat_cards == 34,
            f"expected 34 cards (30 basic + 3 vocab + 1 cloze), got {sat_cards}",
        )

        summary = apkg.scheduling_summary(info["collection"])
        ok(
            summary["cards_with_state"] == 10,
            f"expected 10 cards with state, got {summary['cards_with_state']}",
        )
        ok(
            summary["reviews"] == 10,
            f"expected 10 revlog rows, got {summary['reviews']}",
        )

        # --- the converter reads what came out ----------------------------
        deck_name = next(n for n in names if "SAT Vocabulary" in n)
        deck_out = tmp / "deck"
        result = subprocess.run(
            [
                sys.executable,
                str(HERE / "anki_to_deck.py"),
                str(info["collection"]),
                "--deck",
                deck_name,
                "--out",
                str(deck_out),
            ],
            capture_output=True,
            text=True,
        )
        ok(
            result.returncode == 0,
            f"anki_to_deck failed on the unwrapped export:\n{result.stdout}\n{result.stderr}",
        )
        ok(
            "34 cards" in result.stdout,
            f"expected 34 converted cards in:\n{result.stdout}",
        )
        ok(
            "0 skipped" in result.stdout,
            f"expected nothing skipped in:\n{result.stdout}",
        )

        notes = dict(check_deck.read_deck(deck_out / "deck.dat"))
        ok(len(notes) == 34, f"deck.dat holds {len(notes)} notes, wanted 34")
        headwords = {fields[0] for fields in notes.values()}
        ok("incontrovertible" in headwords, "a known headword is missing from deck.dat")

        # The cloze note is a card now, and it is a card with a hole: the
        # question face must not contain the word the answer face reveals.
        cloze_notes = [f for f in notes.values() if len(f) > 7 and f[7]]
        ok(len(cloze_notes) == 1, f"expected one cloze card, got {len(cloze_notes)}")
        question, answer = cloze_notes[0][7], cloze_notes[0][4]
        ok(
            "ubiquitous" not in question,
            f"the cloze question face gives away its answer: {question!r}",
        )
        ok("[...]" in question, f"the cloze question has no hole: {question!r}")
        ok(
            "ubiquitous" in answer,
            f"the cloze answer face does not fill the hole: {answer!r}",
        )

        # The Barron's-shaped note type: field NAMES must win over position,
        # or every answer face reads "V." instead of the definition.
        abase = next(f for f in notes.values() if f[0] == "abase")
        ok(
            abase[2] == "lower; humiliate",
            f"meaning took the wrong field: {abase[2]!r}",
        )
        ok(abase[3] == "V.", f"part of speech took the wrong field: {abase[3]!r}")
        ok(
            abase[4].startswith("He refused"),
            f"sentence took the wrong field: {abase[4]!r}",
        )
        ok(
            all("<img" not in f for fields in notes.values() for f in fields),
            "markup leaked into a converted field",
        )

        # Scheduling state survived the export + convert round trip.
        cards = (deck_out / "cards.dat").read_bytes()
        # Derived from the notes, not repeated as a literal: cards.dat is
        # indexed BY note index (StudyDeck::loadCard reads index * 32), so one
        # record per deck.dat entry is the actual invariant. Written as "33"
        # here it silently went stale the moment cloze added a card, and the
        # count had already been updated in the two places above.
        ok(
            len(cards) == len(notes) * 32,
            f"cards.dat is {len(cards)} bytes, wanted {len(notes) * 32} for {len(notes)} notes",
        )
        with_state = 0
        for i in range(len(notes)):
            stability = struct.unpack_from("<f", cards, i * 32 + 8)[0]
            if stability > 0:
                with_state += 1
        ok(with_state == 10, f"{with_state} cards carry FSRS state, wanted 10")

        # --- the faces: which side each field lands on --------------------
        import anki_to_deck

        # A generic vocabulary deck keeps its sentence on the answer face.
        meta = (deck_out / "meta.dat").read_bytes()
        (flags,) = struct.unpack_from("<H", meta, 10)
        ok(
            flags & anki_to_deck.META_SENTENCE_ON_QUESTION == 0,
            "a vocabulary deck must not put its sentence on the question face",
        )
        # An HSK deck does, because reading the word in context is the point.
        ok(
            "HSK" in anki_to_deck.SENTENCE_ON_QUESTION_TYPES,
            "the HSK note type lost its sentence-on-question habit",
        )
        hsk_meta = tmp / "hsk-meta.dat"
        anki_to_deck.write_meta(
            hsk_meta, "HSK", {}, 0, 4, anki_to_deck.META_SENTENCE_ON_QUESTION
        )
        (hsk_flags,) = struct.unpack_from("<H", hsk_meta.read_bytes(), 10)
        ok(
            hsk_flags & anki_to_deck.META_SENTENCE_ON_QUESTION != 0,
            "write_meta dropped the sentence-on-question flag",
        )

        # A field that is empty everywhere must never win a slot on its name
        # alone: one real deck is [Front, Back, Meaning] with Meaning blank on
        # every note, and name-matching alone gave 115 cards a blank answer.
        blank = anki_to_deck.generic_profile(
            ["Front", "Back", "Meaning"], None, {"Meaning"}
        )
        ok(
            blank["meaning"] == "Back",
            f"the empty field won the answer face: {blank['meaning']!r}",
        )
        named = anki_to_deck.generic_profile(
            ["Word", "Part of Speech", "Definition", "Sentence"]
        )
        ok(
            named["meaning"] == "Definition"
            and named["partOfSpeech"] == "Part of Speech",
            f"named fields mapped wrong: {named}",
        )

        # --- the legacy package converts too ------------------------------
        # Every AnkiWeb shared deck ships in this format, so it is the common
        # case for a new user, not a corner: the schema-11 JSON metadata is
        # upgraded into real tables and the same pipeline runs.
        legacy_info = apkg.extract(fixtures / "legacy.apkg", tmp / "legacy")
        legacy_decks = apkg.list_decks(legacy_info["collection"])
        ok(
            any("SAT Vocabulary" in n for n, _ in legacy_decks),
            f"legacy deck list: {legacy_decks}",
        )
        result = subprocess.run(
            [
                sys.executable,
                str(HERE / "anki_to_deck.py"),
                str(legacy_info["collection"]),
                "--deck",
                next(n for n, _ in legacy_decks if "SAT Vocabulary" in n),
                "--out",
                str(tmp / "legacy-deck"),
            ],
            capture_output=True,
            text=True,
        )
        ok(
            result.returncode == 0,
            f"legacy conversion failed:\n{result.stdout}\n{result.stderr}",
        )
        # Tied to the modern package's own count rather than repeated as a
        # literal. Both halves of this test convert the SAME fixture deck
        # through the same pipeline, so any number that is right for one is
        # right for the other -- and a literal here went stale on its own when
        # cloze turned the "1 skipped" note into a card.
        ok(
            f"{len(notes)} cards" in result.stdout and "0 skipped" in result.stdout,
            f"legacy conversion counts wrong, wanted {len(notes)} cards and nothing "
            f"skipped:\n{result.stdout}",
        )
        ok(
            "learn [1.0, 10.0]" in result.stdout,
            f"legacy deck preset (steps) did not come through:\n{result.stdout}",
        )

        # --- a package that is not readable in any format is refused ------
        import zipfile as zf_mod

        broken = tmp / "broken.apkg"
        junk_db = tmp / "junkdb.anki2"
        db = __import__("sqlite3").connect(junk_db)
        db.execute("create table empty_table (x)")
        db.commit()
        db.close()
        with zf_mod.ZipFile(broken, "w") as zf:
            zf.write(junk_db, "collection.anki2")
        try:
            apkg.extract(broken, tmp / "brokenout")
            ok(False, "unreadable package was not refused")
        except apkg.ApkgError as exc:
            ok(
                "Support older Anki" in str(exc),
                f"refusal lacks advice: {exc}",
            )

        # --- garbage is refused too ---------------------------------------
        junk = tmp / "junk.apkg"
        junk.write_bytes(b"not a zip at all")
        try:
            apkg.extract(junk, tmp / "junkout")
            ok(False, "non-zip was not refused")
        except apkg.ApkgError:
            ok(True, "non-zip refused")

        # --- split decks roll up to a pickable parent ---------------------
        # A real user's export held two subdecks under a parent with no cards
        # of its own; the old per-deck counts hid the parent, so the one
        # choice that takes the split whole could not be made. list_decks
        # touches only decks(id, name) and cards(id, did), so a minimal
        # schema exercises the rollup without disturbing the shared fixture.
        split = tmp / "split.sqlite"
        db = sqlite3.connect(split)
        db.execute("create table decks (id integer primary key, name text)")
        db.execute("create table cards (id integer primary key, did integer)")
        db.executemany(
            "insert into decks values (?, ?)",
            [(1, "Parent"), (2, "Parent\x1fA"), (3, "Parent\x1fB"), (4, "Empty")],
        )
        db.executemany(
            "insert into cards values (?, ?)",
            [(i, 2) for i in range(2)] + [(10 + i, 3) for i in range(3)],
        )
        db.commit()
        db.close()
        rolled = apkg.list_decks(split)
        ok(
            rolled == [("Parent", 5), ("Parent::A", 2), ("Parent::B", 3)],
            f"rollup wrong: {rolled}",
        )

    # anki_is_running() must not see its own kind: pgrep -f used to match any
    # process carrying the pattern as an argument, so two concurrent check.sh
    # runs each saw the other's probe and both refused to sync against an Anki
    # that was not running. Eight concurrent probes force the overlap; every
    # one must agree with the solo answer.
    import deck_to_anki  # noqa: E402

    if deck_to_anki.anki_is_running():
        print("note: Anki is running; the concurrent-probe check did NOT run")
    else:
        probe = (
            "import sys; sys.path.insert(0, sys.argv[1]); import deck_to_anki; "
            "sys.exit(1 if any(deck_to_anki.anki_is_running() for _ in range(25)) else 0)"
        )
        probes = [
            subprocess.Popen([sys.executable, "-c", probe, str(HERE)]) for _ in range(8)
        ]
        codes = [p.wait() for p in probes]
        ok(
            codes == [0] * 8,
            f"concurrent anki_is_running probes saw each other: {codes}",
        )

    print(f"PASS ({CHECKS} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
