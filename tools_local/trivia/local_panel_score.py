#!/usr/bin/env python3
"""Score a blind verdict sheet against the local rater that the pairs came from.

    python3 tools_local/trivia/local_panel_score.py <dir> [--verbose]

<dir> holds pairs_blind.tsv, key.tsv and one or more verdict_*.tsv, as written
by make_local_panel.py. Prints the headline agreement, the agreement per gap
band, a sign-test p-value against the "the ratings are noise" null, and every
disagreement in full so a pattern can be read off them.

READ THE BANDS, NOT THE HEADLINE. The headline moves with how the pairs were
drawn: sample only the extremes and it approaches 100% for any rater whose
numbers are ordered at all, sample only adjacent levels and it approaches 50%
for every rater alive. The falsifiable claim is the SHAPE. A rater emitting
well-spread noise scores about 50% in every band, because it separated those
pairs for no reason. A rater carrying signal scores highest where it claimed the
biggest gap and lowest where it claimed the smallest.

TIES COUNT AGAINST. A pair the judge could not call is kept in the denominator
of the headline and excluded from the sign test, which is where it belongs:
dropping it would quietly raise the score, and the sign test has no way to read
it. Both numbers are printed so neither convention can hide behind the other.

NOT A GATE, and it exits 0 whatever it finds. The bar is one judge's opinion.
One judge is not a room of people, and a judge that shares a blind spot with the
model it is testing agrees with it for the wrong reason. Failing somebody's
build on that would be a worse error than the one this file exists to catch.
"""

import argparse
import glob
import math
import os
import sys


def read_tsv(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        head = None
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if head is None:
                head = parts
                continue
            if len(parts) >= len(head):
                rows.append(dict(zip(head, parts)))
    return rows


def binom_tail(k, n, p=0.5):
    """P(X >= k) for X ~ Binomial(n, p). Exact; n here is under 100."""
    return sum(math.comb(n, i) * p**i * (1 - p) ** (n - i) for i in range(k, n + 1))


def wilson(k, n):
    """95% Wilson interval, which behaves at the ends where normal-approx does not."""
    if n == 0:
        return (0.0, 0.0)
    z = 1.96
    ph = k / n
    d = 1 + z * z / n
    c = ph + z * z / (2 * n)
    s = z * math.sqrt((ph * (1 - ph) + z * z / (4 * n)) / n)
    return (100 * (c - s) / d, 100 * (c + s) / d)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument(
        "--verbose",
        action="store_true",
        help="print every pair, not only the disagreements",
    )
    a = ap.parse_args(argv)

    key = {r["pair"]: r for r in read_tsv(os.path.join(a.dir, "key.tsv"))}
    blind = {r["pair"]: r for r in read_tsv(os.path.join(a.dir, "pairs_blind.tsv"))}
    sheets = sorted(glob.glob(os.path.join(a.dir, "verdict_*.tsv")))
    if not sheets:
        print("no verdict_*.tsv here -- judge the blind sheet first")
        return 0

    for sheet in sheets:
        verdicts = read_tsv(sheet)
        judge = os.path.basename(sheet)[len("verdict_") : -len(".tsv")]
        print("=" * 78)
        print(f"judge: {judge}   ({len(verdicts)} verdicts)")
        print("=" * 78)

        bands = {}
        disagree = []
        ties = []
        by_conf = {}
        for v in verdicts:
            k = key.get(v["pair"])
            if not k:
                continue
            band = k["band"]
            ok = v["harder"] == k["rater_says_harder"]
            tie = v["harder"] == "TIE"
            b = bands.setdefault(band, {"n": 0, "agree": 0, "tie": 0})
            b["n"] += 1
            b["agree"] += ok
            b["tie"] += tie
            c = by_conf.setdefault(v.get("conf", "?"), {"n": 0, "agree": 0})
            c["n"] += 1
            c["agree"] += ok
            if tie:
                ties.append((v, k))
            elif not ok:
                disagree.append((v, k))

        tot = sum(b["n"] for b in bands.values())
        agr = sum(b["agree"] for b in bands.values())
        tie_n = sum(b["tie"] for b in bands.values())
        decided = tot - tie_n
        lo, hi = wilson(agr, decided)
        print(
            f"\nAGREEMENT  {agr}/{decided} decided pairs = "
            f"{100.0 * agr / decided:.1f}%   (95% CI {lo:.0f}-{hi:.0f}%)"
        )
        print(
            f"           {agr}/{tot} counting the {tie_n} tie(s) against = "
            f"{100.0 * agr / tot:.1f}%"
        )
        p = binom_tail(agr, decided)
        print(
            f"           sign test against 'the ratings are noise' (50%): p = {p:.3g}"
        )

        print("\nBY GAP BAND -- this is the reading that matters")
        print("  band     pairs  agree   rate    95% CI")
        for name in ("wide", "mid", "narrow"):
            b = bands.get(name)
            if not b:
                continue
            d = b["n"] - b["tie"]
            blo, bhi = wilson(b["agree"], d)
            print(
                f"  {name:8s} {b['n']:5d}  {b['agree']:5d}  "
                f"{100.0 * b['agree'] / d:5.1f}%   {blo:.0f}-{bhi:.0f}%"
                + (f"   ({b['tie']} tie)" if b["tie"] else "")
            )
        print("  a rater emitting spread NOISE sits near 50% in all three;")
        print("  a rater carrying signal falls as the band narrows.")

        if by_conf:
            print("\nBY THE JUDGE'S OWN STATED CONFIDENCE")
            for c in ("high", "med", "low"):
                v = by_conf.get(c)
                if v:
                    print(
                        f"  {c:5s} {v['agree']:3d}/{v['n']:<3d} = "
                        f"{100.0 * v['agree'] / v['n']:5.1f}%"
                    )
            print(
                "  confidence written before the key was opened. If the low band is\n"
                "  near chance and the high band is not, the judge knew which calls\n"
                "  were guesses, and the disagreements below are mostly guesses."
            )

        def show(v, k, tag):
            bl = blind[v["pair"]]
            ra, rb = int(k["A_r"]), int(k["B_r"])
            print(
                f"\n  {v['pair']}  [{k['band']}]  {tag}: judge said "
                f"{v['harder']} is harder, conf {v.get('conf', '?')}"
            )
            print(f"    A  r={ra} level {k['A_level']}  ({k['A_topic']})")
            print(f"       {bl['A_clue']}")
            print(f"       -> {bl['A_answer']}")
            print(f"    B  r={rb} level {k['B_level']}  ({k['B_topic']})")
            print(f"       {bl['B_clue']}")
            print(f"       -> {bl['B_answer']}")
            print(
                f"    rater called {k['rater_says_harder']} harder "
                f"(r {ra} vs {rb}; high r = easy)"
            )

        if ties:
            print(f"\n{'-' * 78}\nTHE {len(ties)} PAIR(S) THE JUDGE COULD NOT CALL")
            for v, k in ties:
                show(v, k, "TIE")

        print(f"\n{'-' * 78}\nTHE {len(disagree)} DISAGREEMENT(S), VERBATIM")
        print(
            "This is the part worth reading. A pattern here -- one topic, one\n"
            "shape of clue, one length -- is a systematic blind spot and is worth\n"
            "more than the headline. Scattered ones are noise in the judge, the\n"
            "rater, or both, and cannot be told apart from 90 pairs."
        )
        for v, k in disagree:
            show(v, k, "DISAGREE")

        if a.verbose:
            print(f"\n{'-' * 78}\nALL PAIRS")
            for v in verdicts:
                k = key.get(v["pair"])
                if k and v["harder"] == k["rater_says_harder"]:
                    show(v, k, "agree")

        print("\n" + "=" * 78)
        print(
            "NO VERDICT AND NO EXIT CODE. One judge is not a room of people, and a\n"
            "judge that shares a blind spot with the model it is testing agrees with\n"
            "it for the wrong reason. Read the band shape and the disagreements."
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
