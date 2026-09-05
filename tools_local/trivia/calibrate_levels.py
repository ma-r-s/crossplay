#!/usr/bin/env python3
"""Choose assemble_pack.LEVELS against the population that actually ships.

    python3 tools_local/trivia/calibrate_levels.py \\
        --corpus .rate/corpus_repaired.jsonl --enriched .rate/enriched.jsonl

Prints where the SHIPPED constants stand on the current data and what it would
choose instead. Nothing is written; the output is a tuple to paste into
assemble_pack.py, on purpose -- see "why not automatic" below.

Why this file exists
--------------------
`r -> d` uses fixed absolute thresholds so that a level means the same thing at
20,000 rated rows and at 40,000 (assemble_pack.py, rule 3). Fixed does not mean
arbitrary: the constants still have to be CHOSEN on some population, and the
population changed underneath them when US-centric questions stopped shipping
(rule 4). For an international table those questions are genuinely the hard
ones -- 68% of them rate r<=1 against 2% of the rest -- so thresholds picked
while they still shipped put almost nothing in level 5, and test_pack.py's
difficulty-spread check failed on a pack that was, correctly, all one flavour.

The answer is different fixed numbers on the right population, not a switch to
quantiles. This tool is what makes that re-derivable instead of hand-tuned: a
constant nobody can reproduce is the thing to avoid, and the current ones were
chosen on a PARTIAL run that will roughly double in size.

Why not automatic
-----------------
It prints rather than edits. Constants that a build step rewrites are quantiles
with extra steps: every re-cut of the run would silently redefine level 3 and a
player's difficulty setting would stop meaning anything across builds, which is
the exact failure rule 3 exists to prevent. Changing a level is a decision with
a date on it, so it goes through a diff.

The choosing rule
-----------------
The population is the one assemble_pack.survivors() yields, so this cannot
drift from what the pack contains. Candidates are every set of four floors over
the 0-10 scale. The score is the WORST spread ratio across three views of the
run rather than the best on all of it:

    all rated rows, the half rated first, the half rated last

because enriched.jsonl is append-ordered and mixes rows rated under different
shot configurations, so its two halves really do disagree (r=5 was 20.1% of the
first half and 7.7% of the second on 2026-09-04). A candidate tuned to the
pooled distribution can be the best on paper and fail on the rows still to
come; the half rated last is the closest proxy for those. Minimax picks the set
that still passes when the population moves, which is the property actually
wanted from a constant chosen on partial data.
"""

import argparse
import collections
import itertools
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import assemble_pack  # noqa: E402

# test_pack.py's difficulty-spread check, which is the thing being satisfied:
#   check('difficulty reasonably spread', hi <= lo * 2.5)
SPREAD_LIMIT = 2.5


def level_with(r, floors):
    """assemble_pack.level(), parameterised by a candidate floor tuple."""
    for d, floor in enumerate(floors, start=1):
        if r >= floor:
            return d
    return len(floors) + 1


def spread(rs, floors):
    """Level counts and the hi/lo ratio for one r-value population."""
    counts = collections.Counter(level_with(r, floors) for r in rs)
    if len(counts) < len(floors) + 1:
        return counts, float("inf")  # an empty level is not a difficulty tier
    return counts, max(counts.values()) / min(counts.values())


def choose(samples, limit=SPREAD_LIMIT):
    """Pick the floors minimising the worst spread ratio over `samples`.

    samples is a list of (name, r-values) pairs. Returns a list of
    (worst_ratio, floors, {name: ratio}) sorted best first. Deterministic: ties
    break on the floor tuple itself, never on dict or set order.
    """
    scored = []
    for combo in itertools.combinations(range(1, 12), 4):
        floors = tuple(reversed(combo))  # descending: level 1 is the easiest
        ratios = {n: spread(rs, floors)[1] for n, rs in samples}
        worst = max(ratios.values())
        if worst == float("inf"):
            continue
        scored.append((worst, floors, ratios))
    scored.sort(key=lambda t: (t[0], t[1]))
    return scored


def views(rows):
    """The three populations a candidate must survive.

    `rows` must arrive in RATING order (enriched.jsonl's append order), which
    is why the caller sorts it that way rather than taking the assembler's
    corpus order. The run appends as it goes and its shot configuration changed
    mid-file, so rating order is the axis the drift lies along: on 2026-09-04
    r=5 was 20.1% of the first half and 7.7% of the second. Splitting on any
    other axis averages that away and reports a stability the data lacks, and
    the second half is the closest proxy for the rows still to be rated.
    """
    half = len(rows) // 2
    return [
        ("all", rows),
        ("rated first", rows[:half]),
        ("rated last", rows[half:]),
    ]


def table(samples, floors, note=""):
    counts, ratio = spread(samples[0][1], floors)
    bands = []
    for d, f in enumerate(list(floors) + [0], start=1):
        hi = 10 if d == 1 else floors[d - 2] - 1
        lo = f if d <= len(floors) else 0
        bands.append((d, lo, hi))
    print(f"\n  floors {list(floors)}{note}")
    print(f"    {'level':<6} {'r band':<10} {'questions':>10}   share")
    total = sum(counts.values()) or 1
    for d, lo, hi in bands:
        band = f"{lo}-{hi}" if lo != hi else str(lo)
        print(
            f"    {d:<6} {band:<10} {counts.get(d, 0):>10,}   "
            f"{100 * counts.get(d, 0) / total:4.1f}%"
        )
    for name, rs in samples:
        _, r = spread(rs, floors)
        verdict = "PASS" if r <= SPREAD_LIMIT else "FAIL"
        print(f"    spread on {name:<12} {r:6.3f}  (limit {SPREAD_LIMIT})  {verdict}")
    return ratio


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--enriched", required=True)
    ap.add_argument(
        "--verdicts",
        default="tools_local/trivia/verdicts.tsv",
        help="same hand verdicts the assembler applies",
    )
    ap.add_argument(
        "--keep-us",
        action="store_true",
        help="calibrate on the US-inclusive pack instead (default: as shipped)",
    )
    ap.add_argument("--top", type=int, default=5, help="candidates to list")
    a = ap.parse_args()

    corpus = [
        json.loads(line) for line in open(a.corpus, encoding="utf-8") if line.strip()
    ]
    corpus = assemble_pack.apply_verdicts(corpus, a.verdicts)
    enriched, _ = assemble_pack.load_enriched(a.enriched)
    if not enriched:
        sys.exit(f"{a.enriched}: no usable records")

    # The population is whatever the assembler would ship, taken from the
    # assembler itself so the two cannot drift apart.
    #
    # Then re-ordered by RATING order. survivors() walks the corpus, which is a
    # fixed 50k file in its own arbitrary order and says nothing about when a
    # row was rated; views() needs the append order of enriched.jsonl, because
    # that is the axis the run's configuration drift lies along. load_enriched
    # builds its dict by reading the file top to bottom, so dict order is that
    # order (a duplicate id keeps its first position, which is what we want:
    # resume artefacts should not reshuffle the run).
    stats = collections.Counter()
    kick = not a.keep_us
    rank = {rid: i for i, rid in enumerate(enriched)}
    surviving = list(assemble_pack.survivors(corpus, enriched, kick, stats))
    surviving.sort(key=lambda xe: rank[xe[0]["id"]])
    rows = [e["r"] for _, e in surviving]
    if not rows:
        sys.exit("no rated rows survive the filters; nothing to calibrate")

    kicked = sum(v for k, v in stats.items() if k.startswith("rejected: us_centric"))
    print(f"corpus            : {len(corpus):,}")
    print(f"rated             : {len(enriched):,}")
    print(f"population        : {len(rows):,} rows that would ship")
    print(
        f"  us_centric      : {kicked:,} dropped"
        if kick
        else "  us_centric      : kept (--keep-us)"
    )
    print(f"  r distribution  : {dict(sorted(collections.Counter(rows).items()))}")

    samples = views(rows)

    shipped = tuple(f for f, _ in assemble_pack.LEVELS)
    print("\n--- the constants in assemble_pack.py today ---")
    table(samples, shipped)

    scored = choose(samples)
    print(f"\n--- best {a.top} candidates on this data (minimax over the views) ---")
    for worst, floors, ratios in scored[: a.top]:
        detail = "  ".join(f"{n} {r:.3f}" for n, r in ratios.items())
        mark = "  <- shipped" if floors == shipped else ""
        print(f"  worst {worst:6.3f}  floors {list(floors)}   {detail}{mark}")

    best = scored[0][1]
    print("\n--- recommended ---")
    table(samples, best)
    if best == shipped:
        print("\nThe shipped constants are still the best choice. Nothing to do.")
    else:
        print(
            "\nTo adopt, edit LEVELS in tools_local/trivia/assemble_pack.py:\n"
            f"    LEVELS = {tuple((f, d) for d, f in enumerate(best, start=1))}\n"
            "and update the CALIBRATED ON comment above it with today's date and\n"
            f"population ({len(rows):,} rows). Then rebuild the pack and re-run\n"
            "test_pack.py, which is what the spread numbers above predict."
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
