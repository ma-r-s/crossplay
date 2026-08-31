#!/usr/bin/env python3
"""The two routes onto a card must agree on a deck's directory name.

A deck reaches /study/<slug>/ two ways: the installer (this directory, run
from the CLI and from the browser via tools.zip) and the sync service
(server/study-bridge/bridge/decks.py). The reader cannot tell which route a
folder arrived by -- it opens every directory under /study holding a meta.dat.

So where the two disagree, one deck becomes two directories with two separate
review histories, studied independently, and nothing on the device says why.
They HAD drifted: the installer capped at 31 characters where the service caps
at 40, and reduced every name without Latin letters to the single directory
"deck", so two Chinese decks shared one folder and one review log.

This test exists because that drift is invisible: both sides were correct on
their own terms and the disagreement only appears when a card sees both.
"""

import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "server" / "study-bridge"))

CHECKS = 0
FAILED = 0


def ok(condition, what):
    global CHECKS, FAILED
    CHECKS += 1
    if not condition:
        FAILED += 1
        print(f"  FAIL: {what}")


# Every shape that has ever disagreed, plus the ordinary case.
NAMES = [
    "Default",
    "Mandarin: Vocabulary",
    "Japanese::Core 2000::Step 01",
    "Spanish::Food & Drink",
    # Longer than the installer's old 31-character cap.
    "AnkiDroid Japanese Core 2000 Step 01 Vocab and Sentences",
    # No Latin letters at all: both used to reduce these to one directory.
    "日本語",
    "日本語::漢字",
    "Русский",
    "Ελληνικά",
    # Punctuation-only difference, which must not collapse either.
    "Travel::Food",
    "Spanish::Food",
]


def main():
    from study import slug as installer_slug

    try:
        from bridge.decks import slugify as service_slug
    except ImportError as e:
        # The service's deps are not installed everywhere the installer tests
        # run. Skipping loudly beats a green run that checked nothing.
        print(f"SKIP: the bridge is not importable here ({e})")
        return 0

    for name in NAMES:
        a, b = installer_slug(name), service_slug(name)
        ok(a == b, f"{name!r}: installer {a!r} != service {b!r}")

    # Distinct names must reach distinct directories. A collision here is two
    # decks sharing one folder, one review log and one identity.
    slugs = [installer_slug(n) for n in NAMES]
    ok(len(set(slugs)) == len(slugs), f"two names share a directory: {sorted(slugs)}")

    # Whatever the rule, it has to fit the device's name buffer.
    for name in NAMES:
        ok(len(installer_slug(name)) <= 47, f"{name!r} slug is too long for the reader")

    if FAILED:
        print(f"FAIL ({CHECKS} checks, {FAILED} failed)")
        return 1
    print(f"PASS ({CHECKS} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
