#!/usr/bin/env python3
"""Prove the .apkg path end to end: export, unwrap, convert, check.

    .venv-study/bin/python tools_local/study/test_apkg.py

Builds fixtures with Anki's own exporter (make_fixture_apkg.py), opens them
with apkg.py, runs anki_to_deck.py on the unwrapped collection, and parses
what came out. Every assertion is counted, and the count is printed, because
"PASS (0 checks)" has bitten this project before.
"""

import pathlib
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
        ok(sat_cards == 34, f"expected 34 cards (30 basic + 3 vocab + 1 cloze), got {sat_cards}")

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
            "33 cards" in result.stdout,
            f"expected 33 converted cards in:\n{result.stdout}",
        )
        ok(
            "1 skipped" in result.stdout,
            f"expected the cloze note skipped in:\n{result.stdout}",
        )

        notes = dict(check_deck.read_deck(deck_out / "deck.dat"))
        ok(len(notes) == 33, f"deck.dat holds {len(notes)} notes, wanted 33")
        headwords = {fields[0] for fields in notes.values()}
        ok("incontrovertible" in headwords, "a known headword is missing from deck.dat")

        # The Barron's-shaped note type: field NAMES must win over position,
        # or every answer face reads "V." instead of the definition.
        abase = next(f for f in notes.values() if f[0] == "abase")
        ok(abase[2] == "lower; humiliate", f"meaning took the wrong field: {abase[2]!r}")
        ok(abase[3] == "V.", f"part of speech took the wrong field: {abase[3]!r}")
        ok(abase[4].startswith("He refused"), f"sentence took the wrong field: {abase[4]!r}")
        ok(
            all("<img" not in f for fields in notes.values() for f in fields),
            "markup leaked into a converted field",
        )

        # Scheduling state survived the export + convert round trip.
        cards = (deck_out / "cards.dat").read_bytes()
        ok(len(cards) == 33 * 32, f"cards.dat is {len(cards)} bytes, wanted {33 * 32}")
        with_state = 0
        for i in range(33):
            stability = struct.unpack_from("<f", cards, i * 32 + 8)[0]
            if stability > 0:
                with_state += 1
        ok(with_state == 10, f"{with_state} cards carry FSRS state, wanted 10")

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
        ok(
            "33 cards" in result.stdout and "1 skipped" in result.stdout,
            f"legacy conversion counts wrong:\n{result.stdout}",
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

    print(f"PASS ({CHECKS} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
