#!/usr/bin/env python3
"""Hold fsrs.py to the same oracle as the device scheduler.

host-tests/study/FsrsVectors.h is real cards from Mario's collection with the
stability/difficulty Anki itself computed; test_fsrs.cpp pins the C++ FSRS at
287/301 agreement within its tolerances. This test replays the same vectors
through the Python mirror with the same tolerances and pins the same number,
parsing both the vectors and the reference parameters out of the committed
C++ files so there is exactly one copy of each and nothing to drift.

Run directly, no framework:  .venv-study/bin/python tools_local/study/test_fsrs.py
"""

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fsrs  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
VECTORS = REPO / "host-tests" / "study" / "FsrsVectors.h"
CPP_TEST = REPO / "host-tests" / "study" / "test_fsrs.cpp"

# Mirrors of test_fsrs.cpp: same tolerances, same pinned agreement.
STABILITY_TOLERANCE = 0.02  # relative
DIFFICULTY_TOLERANCE = 0.02  # absolute
EXPECTED_MATCHES = 287

failures = 0
checks = 0


def fail(message):
    global failures
    print(f"  FAIL: {message}")
    failures += 1


def parse_vectors(text):
    steps_by_name = {}
    for name, body in re.findall(
        r"constexpr Step (kSteps\d+)\[\] = \{(.*?)\};", text, re.DOTALL
    ):
        steps_by_name[name] = [
            (int(e), int(r)) for e, r in re.findall(r"\{(-?\d+), (\d+)\}", body)
        ]
    cards = []
    for cid, steps_name, count, s, d in re.findall(
        r"\{(\d+)LL, (kSteps\d+), (\d+), ([\d.]+)f, ([\d.]+)f\}", text
    ):
        steps = steps_by_name[steps_name]
        if len(steps) != int(count):
            raise ValueError(
                f"card {cid}: {len(steps)} steps parsed, header says {count}"
            )
        cards.append((int(cid), steps, float(s), float(d)))
    return cards


def parse_params(text):
    m = re.search(
        r"kCurrentDeckParams\[study::kNumParams\] = \{(.*?)\};", text, re.DOTALL
    )
    if not m:
        raise ValueError("kCurrentDeckParams not found in test_fsrs.cpp")
    return tuple(float(x) for x in re.findall(r"([\d.]+)f", m.group(1)))


def test_agreement_with_anki():
    global checks
    cards = parse_vectors(VECTORS.read_text())
    params = parse_params(CPP_TEST.read_text())
    if len(params) != fsrs.NUM_PARAMS:
        fail(
            f"parsed {len(params)} params from test_fsrs.cpp, wanted {fsrs.NUM_PARAMS}"
        )
        return
    if not cards:
        fail("parsed 0 vectors from FsrsVectors.h")
        return

    matched = 0
    for _cid, steps, expected_s, expected_d in cards:
        s, d = fsrs.replay(steps, params)
        ds = abs(s - expected_s) / expected_s
        dd = abs(d - expected_d)
        if ds < STABILITY_TOLERANCE and dd < DIFFICULTY_TOLERANCE:
            matched += 1
    print(f"  agreement with Anki: {matched}/{len(cards)} cards")
    if matched != EXPECTED_MATCHES:
        fail(
            f"expected {EXPECTED_MATCHES} matches, got {matched}. If this went UP the"
            " model improved -- update EXPECTED_MATCHES (and test_fsrs.cpp) and say"
            " what changed. If it went DOWN, something regressed."
        )
    checks += 1


def test_empty_and_first_review():
    global checks
    s, d = fsrs.replay([])
    if (s, d) != (0.0, 0.0):
        fail(f"empty history should stay stateless, got s={s} d={d}")
    s, d = fsrs.replay([(0, 3)])
    if not (s > 0.0 and 1.0 <= d <= 10.0):
        fail(f"single Good review should learn the card, got s={s} d={d}")
    checks += 1


def test_seed_from_revlog():
    """The converter fallback: a pos/dr-era card gets its state replayed."""
    global checks
    import sqlite3

    import anki_to_deck

    db = sqlite3.connect(":memory:")
    db.execute("create table col (crt integer)")
    db.execute("insert into col values (0)")
    db.execute(
        "create table revlog (id integer, cid integer, ease integer, type integer)"
    )
    ms = lambda day: day * 86400 * 1000  # noqa: E731
    db.executemany(
        "insert into revlog values (?, ?, ?, ?)",
        [
            (ms(0), 7, 3, 0),
            (ms(0) + 1, 7, 0, 4),  # manual reschedule: ease 0, must be ignored
            (ms(2), 7, 4, 3),  # filtered-deck review: must be ignored
            (ms(5), 7, 3, 1),
        ],
    )
    reviewed = {"ankiCardId": 7, "stability": 0.0, "difficulty": 0.0, "reps": 2}
    untouched = {"ankiCardId": 8, "stability": 0.0, "difficulty": 0.0, "reps": 0}
    stored = {"ankiCardId": 9, "stability": 4.5, "difficulty": 6.0, "reps": 3}
    seeded = anki_to_deck.seed_memory_from_revlog(
        db, [reviewed, untouched, stored], None
    )

    if seeded != 1:
        fail(f"expected exactly the reviewed card seeded, got {seeded}")
    if not (reviewed["stability"] > 0.0 and 1.0 <= reviewed["difficulty"] <= 10.0):
        fail(f"reviewed card not seeded: {reviewed}")
    expected_s, expected_d = fsrs.replay([(0, 3), (5, 3)])
    if abs(reviewed["stability"] - expected_s) > 1e-9:
        fail(
            "filtered/manual entries leaked into the replay:"
            f" got s={reviewed['stability']}, want {expected_s}"
        )
    if abs(reviewed["difficulty"] - expected_d) > 1e-9:
        fail(f"difficulty mismatch: got {reviewed['difficulty']}, want {expected_d}")
    if untouched["stability"] != 0.0:
        fail("a never-reviewed card grew state out of nothing")
    if stored["stability"] != 4.5:
        fail("a stored memory state was overwritten by replay")
    checks += 1


def main():
    test_agreement_with_anki()
    test_empty_and_first_review()
    test_seed_from_revlog()
    print(f"{checks} checks, {failures} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
