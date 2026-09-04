#!/usr/bin/env python3
"""Does the local rater agree with the cloud one, and where does it not.

Two rating files on the SAME 0-10 scale go in; what comes out is the pair of
answers that decide whether the local run is usable:

  1. THE DISTRIBUTION, FIRST AND ALWAYS. A model that answers 5 to everything
     correlates at zero and passes every spot check. So the histogram of each
     side is printed before any correlation is, and a side whose ratings sit in
     two or three buckets is flagged in words.
  2. THE AGREEMENT. Spearman on the overlap (rank, so a constant offset does
     not hide), Pearson, the mean signed difference (is it harsher or softer),
     and the mean local rating per Claude bucket -- which is where "it collapses
     the extremes" shows up and a single rho cannot.

With --difficulty-py it also cuts both sides into the five shipped levels and
prints the confusion, because the rating is not what the player sees: the LEVEL
is, and two ratings 1.5 apart can land in the same tier or in different ones.

    python3 tools_local/trivia/compare_ratings.py \\
        --a .../difficulty.tsv --a-name claude \\
        --b .rate/local.tsv   --b-name qwen14b \\
        --difficulty-py .../difficulty.py
"""

import argparse, importlib.util, os, statistics, sys


def load(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        p = line.split("\t")
        if len(p) >= 2:
            try:
                out[p[0].strip()] = float(p[1])
            except ValueError:
                pass
    return out


def ranks(v):
    """Average ranks, so ties do not silently order themselves. Integer ratings
    tie constantly -- a local model uses eleven values on fifty thousand items
    -- and ranking ties arbitrarily would invent agreement or destroy it."""
    order = sorted(range(len(v)), key=lambda i: v[i])
    r = [0.0] * len(v)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and v[order[j + 1]] == v[order[i]]:
            j += 1
        avg = (i + j) / 2.0 + 1.0
        for k in range(i, j + 1):
            r[order[k]] = avg
        i = j + 1
    return r


def pearson(x, y):
    n = len(x)
    if n < 2:
        return float("nan")
    mx, my = sum(x) / n, sum(y) / n
    sxy = sum((a - mx) * (b - my) for a, b in zip(x, y))
    sxx = sum((a - mx) ** 2 for a in x)
    syy = sum((b - my) ** 2 for b in y)
    if sxx <= 0 or syy <= 0:
        return float("nan")
    return sxy / (sxx * syy) ** 0.5


def spearman(x, y):
    return pearson(ranks(x), ranks(y))


def histogram(name, vals):
    n = len(vals)
    buckets = [0] * 11
    for v in vals:
        buckets[max(0, min(10, int(round(v))))] += 1
    used = sum(1 for b in buckets if b >= 0.01 * n)
    print(
        f"  {name}: n={n:,} mean {statistics.mean(vals):.2f} "
        f"sd {statistics.pstdev(vals):.2f} median {statistics.median(vals):.2f}"
    )
    wide = max(buckets) or 1
    for i, b in enumerate(buckets):
        bar = "#" * int(40 * b / wide)
        print(f"    {i:2d} {b:7,} {100 * b / n:5.1f}%  {bar}")
    if used <= 3:
        print(
            f"    ** {name} uses only {used} buckets above 1% of its mass. "
            f"A rater this flat cannot rank anything; treat every correlation "
            f"below as meaningless. **"
        )
    return buckets


def load_levels(path):
    spec = importlib.util.spec_from_file_location("difficulty_levels", path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--a-name", default="a")
    ap.add_argument("--b-name", default="b")
    ap.add_argument(
        "--difficulty-py",
        default="",
        help="difficulty.py, for the level cut. Omitted, the level "
        "section is SKIPPED rather than guessed at.",
    )
    ap.add_argument(
        "--dump-worst",
        type=int,
        default=0,
        help="print the N biggest disagreements (needs --corpus)",
    )
    ap.add_argument("--corpus", default="")
    ap.add_argument("--brief", action="store_true",
                    help="one line: rho, spread and how many buckets the candidate uses")
    a = ap.parse_args()

    A, B = load(a.a), load(a.b)
    ids = sorted(set(A) & set(B))
    if not ids:
        sys.exit("no overlap: nothing to compare")
    if a.brief:
        # One line per candidate, for picking a model. `buckets` is how many of
        # the eleven the candidate puts at least 1% of its mass in: a rater
        # that answers 5 to everything scores 1 here, and its rho is noise.
        x = [A[i] for i in ids]
        y = [B[i] for i in ids]
        h = [0] * 11
        for v in y:
            h[max(0, min(10, int(round(v))))] += 1
        used = sum(1 for c in h if c >= 0.01 * len(y))
        print(f"{a.b_name:<34} n={len(ids):<5} rho {spearman(x, y):+.3f}  "
              f"r {pearson(x, y):+.3f}  mean {statistics.mean(y):.2f}  "
              f"sd {statistics.pstdev(y):.2f}  bias {statistics.mean(y)-statistics.mean(x):+.2f}  "
              f"buckets {used}/11")
        return
    print(f"{a.a_name} {len(A):,}   {a.b_name} {len(B):,}   overlap {len(ids):,}")

    print("\nDISTRIBUTION (the thing to read before any correlation)")
    histogram(a.a_name, [A[i] for i in ids])
    histogram(a.b_name, [B[i] for i in ids])

    x = [A[i] for i in ids]
    y = [B[i] for i in ids]
    d = [b - c for c, b in zip(x, y)]
    print(f"\nAGREEMENT on {len(ids):,} shared questions")
    print(f"  Spearman rho   {spearman(x, y):+.3f}")
    print(f"  Pearson  r     {pearson(x, y):+.3f}")
    print(
        f"  mean {a.b_name} - {a.a_name}   {statistics.mean(d):+.2f} "
        f"({'harsher' if statistics.mean(d) < 0 else 'softer'} than {a.a_name})"
    )
    print(f"  mean |diff|    {statistics.mean(abs(v) for v in d):.2f}")
    print(f"  within 1       {100 * sum(1 for v in d if abs(v) <= 1) / len(d):.1f}%")
    print(f"  within 2       {100 * sum(1 for v in d if abs(v) <= 2) / len(d):.1f}%")

    print(f"\nWHERE IT DIFFERS: mean {a.b_name} for each {a.a_name} bucket")
    print(f"  {a.a_name:>10}  n       mean {a.b_name}")
    for k in range(11):
        sub = [B[i] for i in ids if round(A[i]) == k]
        if sub:
            print(f"  {k:>10}  {len(sub):6,}  {statistics.mean(sub):5.2f}")

    if a.difficulty_py:
        L = load_levels(a.difficulty_py)
        print(f"\nLEVELS ({os.path.basename(a.difficulty_py)}), what the player picks")
        la = [L.level_for(A[i]) for i in ids]
        lb = [L.level_for(B[i]) for i in ids]
        same = sum(1 for p, q in zip(la, lb) if p == q)
        near = sum(1 for p, q in zip(la, lb) if abs(p - q) <= 1)
        print(
            f"  same level {100 * same / len(ids):.1f}%   within one {100 * near / len(ids):.1f}%"
        )
        print(f"  rows = {a.a_name}, cols = {a.b_name}")
        print("        " + "".join(f"{c:>8}" for c in range(1, 6)))
        for r in range(1, 6):
            row = [
                sum(1 for p, q in zip(la, lb) if p == r and q == c) for c in range(1, 6)
            ]
            print(f"    L{r}  " + "".join(f"{v:>8,}" for v in row))
        for lvl, floor, words in L.LEVELS:
            sub = [A[i] for i, q in zip(ids, lb) if q == lvl]
            if sub:
                print(
                    f"  {a.b_name} L{lvl} ({words}): true {a.a_name} mean "
                    f"{statistics.mean(sub):.2f} over {len(sub):,}"
                )

    if a.dump_worst and a.corpus:
        import json

        text = {}
        for line in open(a.corpus, encoding="utf-8"):
            o = json.loads(line)
            text[o["id"]] = (o["q"], o["a"])
        print(f"\n{a.dump_worst} BIGGEST DISAGREEMENTS")
        for i in sorted(ids, key=lambda i: -abs(B[i] - A[i]))[: a.dump_worst]:
            q, ans = text.get(i, ("?", "?"))
            print(
                f"  {a.a_name} {A[i]:.1f}  {a.b_name} {B[i]:.1f}   {q[:110]} -> {ans}"
            )


if __name__ == "__main__":
    main()
