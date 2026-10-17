#!/usr/bin/env python3
"""A deck that converts to nothing must say why, in a line the bridge can read.

    .venv-study/bin/python tools_local/study/test_unbuildable.py

On 2026-10-04 a user's reader said only "<deck> could not be built". He
retyped the deck under every note type Anki has, including the default, and
none of it could have helped: the converter knew what was wrong and said it
only on a stderr nobody but the sync bridge's log ever saw. This builds the
real shapes with Anki's own library and runs the real converter on each.

Every assertion is counted and the count printed, because "PASS (0 checks)"
has bitten this project before.
"""

import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
CHECKS = 0


def ok(condition, what):
    global CHECKS
    CHECKS += 1
    if not condition:
        sys.exit(f"FAIL check {CHECKS}: {what}")


def build(base):
    from anki.collection import Collection

    col = Collection(str(base / "collection.anki2"))
    basic = col.models.by_name("Basic")

    def deck(name, notes):
        did = col.decks.id(name)
        for front, back in notes:
            note = col.new_note(basic)
            note.fields[0], note.fields[1] = front, back
            col.add_note(note, did)

    # The shape that failed in the field: a picture on the front, its name on
    # the back. Two levels deep, because that deck was a subdeck.
    deck(
        "Exam::Famous Americans",
        [
            ('<img src="lincoln.jpg">', "Abraham Lincoln"),
            ('<img src="king.jpg">', "Martin Luther King Jr."),
        ],
    )
    # One text card among picture cards still converts: only the pictures drop.
    deck(
        "Mixed", [('<img src="a.jpg">', "pictured"), ("ubiquitous", "found everywhere")]
    )
    col.decks.id("Nothing In It")
    col.close()
    return base / "collection.anki2"


def convert(collection, deck, out):
    return subprocess.run(
        [
            sys.executable,
            str(HERE / "anki_to_deck.py"),
            str(collection),
            "--deck",
            deck,
            "--out",
            str(out),
        ],
        capture_output=True,
        text=True,
    )


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        collection = build(tmp)

        r = convert(collection, "Exam::Famous Americans", tmp / "pictures")
        ok(r.returncode != 0, "a deck of picture fronts converts to nothing")
        ok(
            "reason: picture-front" in r.stderr.splitlines(),
            f"picture fronts are named: {r.stderr!r}",
        )
        ok(
            "picture" in r.stderr.splitlines()[-1],
            "and the human sentence says picture",
        )

        r = convert(collection, "Nothing In It", tmp / "empty")
        ok(r.returncode != 0, "an empty deck converts to nothing")
        ok(
            "reason: empty-deck" in r.stderr.splitlines(),
            f"an empty deck is named: {r.stderr!r}",
        )

        r = convert(collection, "Renamed On The Desktop", tmp / "missing")
        ok(r.returncode != 0, "a deck that is not there converts to nothing")
        ok(
            "reason: missing-deck" in r.stderr.splitlines(),
            f"a missing deck is named: {r.stderr!r}",
        )

        r = convert(collection, "Mixed", tmp / "mixed")
        ok(
            r.returncode == 0,
            f"a deck with one text card still converts: {r.stderr[-300:]!r}",
        )
        ok("reason:" not in r.stderr, "and says no reason, because it did not fail")

    print(f"test_unbuildable: {CHECKS} checks passed")


if __name__ == "__main__":
    main()
