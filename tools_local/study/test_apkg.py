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
        ok(sat_cards == 31, f"expected 31 cards (30 basic + 1 cloze), got {sat_cards}")

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
            "30 cards" in result.stdout,
            f"expected 30 converted cards in:\n{result.stdout}",
        )
        ok(
            "1 skipped" in result.stdout,
            f"expected the cloze note skipped in:\n{result.stdout}",
        )

        notes = dict(check_deck.read_deck(deck_out / "deck.dat"))
        ok(len(notes) == 30, f"deck.dat holds {len(notes)} notes, wanted 30")
        headwords = {fields[0] for fields in notes.values()}
        ok("incontrovertible" in headwords, "a known headword is missing from deck.dat")
        ok(
            all("<img" not in f for fields in notes.values() for f in fields),
            "markup leaked into a converted field",
        )

        # Scheduling state survived the export + convert round trip.
        cards = (deck_out / "cards.dat").read_bytes()
        ok(len(cards) == 30 * 32, f"cards.dat is {len(cards)} bytes, wanted {30 * 32}")
        with_state = 0
        for i in range(30):
            stability = struct.unpack_from("<f", cards, i * 32 + 8)[0]
            if stability > 0:
                with_state += 1
        ok(with_state == 10, f"{with_state} cards carry FSRS state, wanted 10")

        # --- the legacy package is refused, politely ----------------------
        try:
            apkg.extract(fixtures / "legacy.apkg", tmp / "legacy")
            ok(False, "legacy package was not refused")
        except apkg.ApkgError as exc:
            ok(
                "Support older Anki versions" in str(exc),
                f"refusal lacks re-export advice: {exc}",
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
