#!/usr/bin/env python3
"""Diff the device's rating buttons against Anki's own, card by card.

The question this answers is the one that decides whether the device is usable:
**does pressing Good here schedule the card the way pressing Good in Anki
would?** Reading both implementations and reasoning about them is not an
answer. This runs both.

Anki's side comes from Anki's own scheduler, through the same call the desktop
UI uses to label its buttons (`describe_next_states`), so what is compared is
literally the text Anki would print. The device's side comes from the firmware's
own StudyScheduler, compiled and run over the converted deck.

    .venv-study/bin/python tools_local/study/compare_with_anki.py \\
        ~/Library/Application\\ Support/Anki2/User\\ 1/collection.anki2 \\
        fs_mario/study/mandarin --deck 'Mandarin: Vocabulary::Current'

The collection is copied first and never written to.
"""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

# Anki writes "10m", "1.2mo", "3.05y", sometimes with a leading "<" for
# intraday steps. Normalise both sides to (value, unit) before comparing so a
# difference in rounding style is not reported as a scheduling difference.
# "mo" must precede "m": regex alternation is ordered, so (s|m|h|d|mo|y) reads
# "12mo" as twelve *minutes* and silently turns a year into nothing. That bug
# sat in this file through three rounds of measurement and made a scheduler
# that agreed with Anki look like one that disagreed wildly.
LABEL = re.compile(r"[<≈~]?\s*([0-9]*\.?[0-9]+)\s*(mo|s|m|h|d|y)")


# Anki wraps numbers in Unicode directional isolates (U+2068/U+2069) so that
# right-to-left UI languages render "10m" correctly. They sit *between* the
# digits and the unit, so a naive regex reads "10\u2069m" as unparseable and
# every card looks like a mismatch. That cost an hour and nearly got reported
# as a scheduling disagreement.
ISOLATES = str.maketrans("", "", "\u2066\u2067\u2068\u2069\u200e\u200f")


def parse_label(text):
    m = LABEL.search(text.translate(ISOLATES).strip())
    if not m:
        return None
    value, unit = float(m.group(1)), m.group(2)
    # Everything in minutes, so "1.2mo" and "36d" can be compared at all.
    factor = {
        "s": 1 / 60,
        "m": 1,
        "h": 60,
        "d": 1440,
        "mo": 1440 * 30.4,
        "y": 1440 * 365,
    }[unit]
    return value * factor


def fuzz_delta(days):
    """Anki's interval fuzz envelope, in days.

    Anki deliberately randomises every interval so cards answered on the same
    day do not come back on the same day forever. The device does not, so it
    lands on the un-fuzzed centre and individual cards differ -- exactly as two
    Anki installs would differ from each other. Measuring against this envelope
    separates "we disagree about the model" from "Anki rolled a dice".

    Mirrors rslib's fuzz_delta: no fuzz under 2.5 days, then a day of base
    spread plus a tapering fraction of the interval.
    """
    if days < 2.5:
        return 0.0
    delta = 1.0
    delta += 0.15 * (min(days, 7.0) - 2.5)
    if days > 7.0:
        delta += 0.10 * (min(days, 20.0) - 7.0)
    if days > 20.0:
        delta += 0.05 * (days - 20.0)
    return delta


def self_test():
    """Prove the parser before trusting anything it says.

    Both bugs this file has had were in the parsing, not the schedulers, and
    both made agreement look like catastrophic disagreement. Ten assertions is
    a cheap price for never reporting that again.
    """
    cases = [
        ("\u206812\u2069mo", 12 * 30.4 * 1440),
        ("\u20681\u2069mo", 30.4 * 1440),
        ("<\u206810\u2069m", 10),
        ("\u206830\u2069d", 30 * 1440),
        ("\u20684.8\u2069mo", 4.8 * 30.4 * 1440),
        ("1.0y", 365 * 1440),
        ("15m", 15),
        ("2h", 120),
    ]
    for text, expected in cases:
        got = parse_label(text)
        if got is None or abs(got - expected) > 0.5:
            sys.exit(f"parser self-test failed: {text!r} -> {got}, expected {expected}")


def build_dumper(repo_root):
    out = pathlib.Path(tempfile.mkdtemp()) / "dump_intervals"
    src = repo_root / "tools_local/study/dump_intervals.cpp"
    study = repo_root / "src/apps_local/study"
    cmd = [
        "c++",
        "-std=c++17",
        "-O2",
        "-o",
        str(out),
        str(src),
        str(study / "StudyScheduler.cpp"),
        str(study / "StudyDeck.cpp"),
        str(study / "StudyFsrs.cpp"),
    ]
    subprocess.run(cmd, check=True)
    return out


def anki_labels(collection_path, deck_name, limit):
    """What Anki's own buttons would say, for the cards it would show next."""
    import anki.collection  # noqa: F401  (breaks a circular import in the package)
    from anki.collection import Collection

    work = pathlib.Path(tempfile.mkdtemp()) / "collection.anki2"
    shutil.copy(collection_path, work)
    col = Collection(str(work))
    try:
        deck_id = col.decks.id_for_name(deck_name)
        if deck_id is None:
            sys.exit(f"no deck named {deck_name!r}")
        col.decks.select(deck_id)

        out = {}
        # get_queued_cards returns what Anki would show next, with the exact
        # states its buttons describe. Answering would advance the queue, so
        # nothing here answers anything -- we only ever read.
        queued = col.sched.get_queued_cards(fetch_limit=limit)
        for entry in queued.cards:
            labels = col.sched.describe_next_states(entry.states)
            out[entry.card.id] = list(labels)
        return out, col.sched.today
    finally:
        col.close()


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("collection", type=pathlib.Path)
    ap.add_argument("deckdir", type=pathlib.Path)
    ap.add_argument("--deck", required=True)
    ap.add_argument("--limit", type=int, default=200)
    ap.add_argument(
        "--tolerance", type=float, default=0.05, help="relative, to allow rounding"
    )
    args = ap.parse_args()

    self_test()
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    expected, today = anki_labels(args.collection, args.deck, args.limit)
    if not expected:
        sys.exit("Anki has nothing queued in that deck -- nothing to compare")

    dumper = build_dumper(repo_root)
    # Same day number and wall-clock minute Anki is using, or the learning-step
    # buttons would be compared against a different "now".
    now_minute = (int(time.time()) // 60) % 1440
    result = subprocess.run(
        [str(dumper), str(args.deckdir), str(today), str(now_minute)],
        capture_output=True,
        text=True,
        check=True,
    )

    device = {}
    for line in result.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) == 5:
            device[int(parts[0])] = parts[1:]

    names = ["Again", "Hard", "Good", "Easy"]
    compared = agreed = within_fuzz = 0
    mismatches = []
    outliers = []
    for card_id, want in expected.items():
        got = device.get(card_id)
        if got is None:
            continue
        compared += 1
        bad = []
        for i, name in enumerate(names):
            a, b = parse_label(want[i]), parse_label(got[i])
            if a is None or b is None:
                bad.append(f"{name}: anki={want[i]!r} device={got[i]!r}")
                continue
            if abs(a - b) > args.tolerance * max(a, b):
                bad.append(f"{name}: anki {want[i].strip()} vs device {got[i]}")
        if bad:
            mismatches.append((card_id, bad))
        else:
            agreed += 1
        # Separately: would Anki's own fuzz explain every difference on this
        # card? If so the two schedulers agree and only the dice differ.
        explained = True
        for i in range(4):
            a, b = parse_label(want[i]), parse_label(got[i])
            if a is None or b is None:
                explained = False
                continue
            ad, bd = a / 1440.0, b / 1440.0
            if abs(ad - bd) > fuzz_delta(max(ad, bd)) + 0.02:
                explained = False
        if explained:
            within_fuzz += 1
        else:
            for i in range(4):
                a, b = parse_label(want[i]), parse_label(got[i])
                if a is None or b is None:
                    continue
                ad, bd = a / 1440.0, b / 1440.0
                gap = abs(ad - bd) - fuzz_delta(max(ad, bd))
                if gap > 0.02:
                    outliers.append((gap, names[i], card_id, ad, bd))

    print(f"compared {compared} cards Anki has queued right now")
    print(f"  identical on all four buttons: {agreed}")
    print(f"  within Anki's own fuzz range:  {within_fuzz}")
    print(f"  outside it:                    {compared - within_fuzz}")
    if outliers:
        outliers.sort(reverse=True)
        print("\n  differences Anki's fuzz cannot explain, worst first:")
        for gap, name, card_id, a, b in outliers[:12]:
            print(f"    {name:5} card {card_id}: anki {a:.1f}d  device {b:.1f}d  ({gap:+.1f}d beyond fuzz)")
        if len(outliers) > 12:
            print(f"    ... and {len(outliers) - 12} more")
    return 0 if not outliers else 1


if __name__ == "__main__":
    sys.exit(main())
