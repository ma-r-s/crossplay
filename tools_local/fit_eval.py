#!/usr/bin/env python3
"""Fit the Toy Battle evaluation to self-play outcomes.

Mario's question, 2026-08-11: chess engines learn without human games, so why
are we hand-tuning heuristics? They do, and this is the version of that which
fits on an ESP32. Play a great many games, record every position with how its
game actually ended, then solve for the weights that best predict the result.
Standard practice in chess engines; the device runs the same search either way
and only gets better numbers.

The weights being replaced -- 400 a medal, 25 a base, 8 a reachable slot, 6 a
rack troop -- were all chosen by eye and never fitted. The tournament could only
RANK variants that shared them, so it ranked depth and could not see the goal.

Logistic regression on win/loss, plain gradient descent, no dependencies: this
has to run anywhere and the problem is eight features wide.

    host-tests/toybattle/tune.sh 200 > /tmp/pos.tsv
    tools_local/fit_eval.py /tmp/pos.tsv            # all boards pooled
    tools_local/fit_eval.py /tmp/pos.tsv --per-board
"""

import math
import sys


def load(path):
    rows, names = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                names = line.strip().lstrip("# ").split("\t")[1:-1]
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            rows.append((int(parts[0]), [int(v) for v in parts[1:-1]], int(parts[-1])))
    return names, rows


def standardise(rows, n):
    """Scale each feature to unit spread.

    Without this the medal difference (range about +-8) and region progress
    (range about +-40) get gradients that differ by an order of magnitude, and
    the fit spends its time on whichever happens to be numerically largest
    rather than whichever predicts. The scales are undone at the end.
    """
    mean = [0.0] * n
    for _, xs, _ in rows:
        for i, x in enumerate(xs):
            mean[i] += x
    mean = [m / max(1, len(rows)) for m in mean]
    sd = [0.0] * n
    for _, xs, _ in rows:
        for i, x in enumerate(xs):
            sd[i] += (x - mean[i]) ** 2
    sd = [math.sqrt(s / max(1, len(rows))) or 1.0 for s in sd]
    return mean, sd


def fit(rows, n, epochs=400, lr=0.5):
    mean, sd = standardise(rows, n)
    w = [0.0] * n
    b = 0.0
    for epoch in range(epochs):
        gw = [0.0] * n
        gb = 0.0
        for label, xs, _ in rows:
            z = b
            for i in range(n):
                z += w[i] * (xs[i] - mean[i]) / sd[i]
            p = 1.0 / (1.0 + math.exp(-max(-30.0, min(30.0, z))))
            err = p - label
            for i in range(n):
                gw[i] += err * (xs[i] - mean[i]) / sd[i]
            gb += err
        m = len(rows)
        for i in range(n):
            w[i] -= lr * gw[i] / m
        b -= lr * gb / m
    # Undo the scaling so the weights apply to the raw feature values.
    return [w[i] / sd[i] for i in range(n)], b


def accuracy(rows, n, w, b):
    hit = 0
    for label, xs, _ in rows:
        z = b + sum(w[i] * xs[i] for i in range(n))
        hit += (1 if z > 0 else 0) == label
    return hit / max(1, len(rows))


def report(tag, names, rows, w, b):
    n = len(names)
    # Integers, scaled so a medal is worth what it is worth today. That keeps
    # the search's tie-breaks and overflow behaviour in the range it was built
    # and measured for; only the RATIOS are what the fit actually determined.
    scale = 400.0 / abs(w[0]) if abs(w[0]) > 1e-9 else 1.0
    print(
        f"\n=== {tag}: {len(rows)} positions, predicts {100 * accuracy(rows, n, w, b):.1f}% ==="
    )
    shipped = {"medals_diff": 400, "bases_diff": 25, "reach_diff": 8, "rack_diff": 6}
    print(f"  {'feature':<18}{'fitted':>9}{'guessed':>9}")
    for i, name in enumerate(names):
        got = int(round(w[i] * scale))
        was = shipped.get(name)
        print(f"  {name:<18}{got:>9}{('' if was is None else str(was)):>9}")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    per_board = "--per-board" in sys.argv
    names, rows = load(args[0])
    n = len(names)
    if not rows:
        sys.exit("no positions")

    w, b = fit(rows, n)
    report("all boards", names, rows, w, b)

    if per_board:
        # The reason per-board weights are worth asking about at all: nine
        # boards with different special bases, and a heuristic needs a human to
        # decide what a Nullify base is worth against a gated pier. A fit does
        # not ask; it just sees different games.
        for board in sorted({r[2] for r in rows}):
            sub = [r for r in rows if r[2] == board]
            if len(sub) < 500:
                print(f"\n=== board {board}: only {len(sub)} positions, skipped ===")
                continue
            wb, bb = fit(sub, n)
            report(f"board {board}", names, sub, wb, bb)


if __name__ == "__main__":
    main()
