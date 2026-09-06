#!/usr/bin/env python3
"""Pin what inspect_deck.py refuses.

The point of that script is the refusal, not the report: it runs immediately
before deck_to_anki.py, which writes into the user's collection -- often the
only copy of years of study. A card written by another build whose record
layout is not this one must be caught HERE, because the replay itself cannot
tell a wrong offset from a strange review.

The case worth having a test for is the one a size check alone misses: a
build using a 40-byte record produces a file whose length still divides by
32, so every field is read from the wrong offset and every value is garbage.
Only the content checks catch it.

Standard library only. Run directly:

    python3 tools_local/study/test_inspect_deck.py
"""

import datetime
import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE / "inspect_deck.py"

failures = 0
checks = 0


def check(condition, label):
    global failures, checks
    checks += 1
    if not condition:
        failures += 1
        print(f"  FAIL: {label}")


def review(card_id, at_ms, rating=3, state=2, took=0, flags=0):
    return struct.pack("<qqBBhiIB3x", card_id, at_ms, rating, state, 1, 5, took, flags)


def card(card_id, state=2):
    return struct.pack("<qffiiHH", card_id, 5.0, 5.0, 10, 5, 3, 0) + bytes(
        [state, 0, 0, 0]
    )


def build(tmp, reviews=None, cards=None, revlog_bytes=None):
    deck = pathlib.Path(tmp)
    now = int(datetime.datetime.now().timestamp() * 1000) - 86400 * 1000
    if cards is None:
        cards = b"".join(card(1000 + i) for i in range(4))
    if revlog_bytes is None:
        if reviews is None:
            reviews = [review(1000 + i, now + i * 60000) for i in range(4)]
        revlog_bytes = b"".join(reviews)
    (deck / "cards.dat").write_bytes(cards)
    (deck / "revlog.dat").write_bytes(revlog_bytes)
    return deck


def run(deck):
    result = subprocess.run(
        [sys.executable, str(SCRIPT), str(deck)], capture_output=True, text=True
    )
    return result.returncode, result.stdout + result.stderr


def case(label, expect_ok, **kwargs):
    with tempfile.TemporaryDirectory() as tmp:
        deck = build(tmp, **kwargs)
        code, out = run(deck)
        check(
            (code == 0) == expect_ok,
            f"{label}: expected {'accept' if expect_ok else 'refuse'}, got exit {code}",
        )
        return out


now = int(datetime.datetime.now().timestamp() * 1000) - 86400 * 1000

# A deck with no deck.dat and no meta.dat is still replayable: the progress
# does not live in either of them, and saying so is half of what this tool is
# for.
out = case("a card with only cards.dat and revlog.dat", True)
check("safe to replay" in out, "and it says the progress is safe")

# The stride case. Same 32-byte records padded to 40: the length still divides
# by 32, so only reading the fields catches it.
padded = b"".join(review(1000 + i, now + i * 60000) + bytes(8) for i in range(4))
out = case("a 40-byte record layout", False, revlog_bytes=padded)
check("PROBLEM" in out, "and says what is wrong with it")

case(
    "a truncated revlog",
    False,
    revlog_bytes=b"".join(review(1000 + i, now) for i in range(3))[:-5],
)
case("a rating outside 1..4", False, reviews=[review(1000, now, rating=7)])
case("a state byte outside 0..4", False, reviews=[review(1000, now, state=9)])
case("a review stamped at the epoch", False, reviews=[review(1000, 0)])
case("a review stamped in the far future", False, reviews=[review(1000, now * 4)])
case("a review with no card id", False, reviews=[review(0, now)])
case("a card record with no card id", False, cards=card(0))

# An empty revlog is not an error: a card that has been written but not yet
# studied is the ordinary state of a fresh install.
out = case("a card that has not been studied yet", True, revlog_bytes=b"")
check("nothing has been reviewed" in out, "and says so rather than reporting a problem")

# A non-zero answer time is another build's convention, not a corruption: it
# is worth knowing before optimising FSRS, and it must not block the replay.
out = case("a review carrying an answer time", True, reviews=[review(1000, now, took=4200)])
check("answer time" in out, "which is reported as a note")

# An undone review is skipped rather than counted or refused.
out = case(
    "a review taken back with UNDO",
    True,
    reviews=[review(1000, now), review(1000, now + 1000, flags=1)],
)
check("1 taken back with UNDO" in out, "and is counted separately")

print(f"{'PASS' if failures == 0 else 'FAIL'} {checks} checks, {failures} failed")
sys.exit(1 if failures else 0)
