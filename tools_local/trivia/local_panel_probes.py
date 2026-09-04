#!/usr/bin/env python3
"""The two corpus-wide probes the 90-pair panel pointed at, kept runnable.

    python3 tools_local/trivia/local_panel_probes.py <enriched.jsonl> <corpus.jsonl>

A panel of 90 pairs can show that a rater is ordered. It cannot show WHY it gets
one wrong, and 15 disagreements are far too few to read a pattern off safely --
stare at 15 anecdotes and a pattern always appears. So each hunch the panel
raised was turned into a query over all 21,000 eligible rows, phrased so it
could come back negative. One did. Both are kept, including the one that
refuted the hunch, because a probe only reported when it agrees with you is not
a probe.

PROBE 1 -- SELF-ANSWERING CLUES. Rows where the clue NAMES the answer's country
and the answer is that country's capital. Nothing in the wrapper can make the
answer hard to reach: the clue hands over the country and the answer is its
best-known city. These should pile up at the top of the scale. They do not.
CONFIRMED the hunch.

PROBE 2 -- THE US FLAG'S LEAK. Rows the rater's own us flag called false whose
clue says "U.S." or "American" outright. If the rater's floor of assumed general
knowledge were American, these would ride high. They do not: they sit almost a
full point BELOW the rest and ship easy at 33% against 53%. REFUTED the hunch --
the one bad case in the panel (a Pentagon clue at r=9) does not generalise, and
saying so is the point of running it.
"""

import json
import re
import statistics
import sys
from collections import Counter, defaultdict

CAP = {
    "norwegian": "oslo",
    "swedish": "stockholm",
    "danish": "copenhagen",
    "finnish": "helsinki",
    "italian": "rome",
    "french": "paris",
    "spanish": "madrid",
    "portuguese": "lisbon",
    "german": "berlin",
    "austrian": "vienna",
    "russian": "moscow",
    "japanese": "tokyo",
    "greek": "athens",
    "turkish": "ankara",
    "polish": "warsaw",
    "dutch": "amsterdam",
    "belgian": "brussels",
    "egyptian": "cairo",
    "mexican": "mexico city",
    "irish": "dublin",
    "scottish": "edinburgh",
    "hungarian": "budapest",
    "czech": "prague",
}
SELF = re.compile(r"\bthis (" + "|".join(CAP) + r") (?:capital|city)\b", re.I)
USMARK = re.compile(r"\b(U\.S\.|United States|American|Americans)\b")


def load(enriched, corpus):
    enr = {}
    with open(enriched, encoding="utf-8", errors="replace") as f:
        for line in f:
            try:
                o = json.loads(line)
            except ValueError:
                continue  # the writer appends while we read; a torn tail is normal
            if isinstance(o.get("r"), int):
                enr[o["id"]] = o
    cor = {}
    with open(corpus, encoding="utf-8") as f:
        for line in f:
            try:
                o = json.loads(line)
            except ValueError:
                continue
            cor[o["id"]] = o
    return enr, cor


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__.strip().splitlines()[2].strip())
    enr, cor = load(argv[1], argv[2])

    intl = []
    for qid, o in enr.items():
        if o.get("bad") or o.get("us"):
            continue
        c = cor.get(qid)
        if c:
            intl.append((o, c))
    print(f"{len(intl):,} eligible rows (bad=false, us=false)\n")

    # ---- probe 1 -----------------------------------------------------------
    hits = []
    for o, c in intl:
        m = SELF.search(c["q"])
        if m and str(c["a"]).strip().lower() == CAP[m.group(1).lower()]:
            hits.append((o["r"], c["q"], c["a"]))
    hits.sort()
    rs = [h[0] for h in hits]
    print("PROBE 1  clue names the country, answer IS that country's capital")
    print(
        f"  n={len(rs)}   mean r {statistics.mean(rs):.2f}   "
        f"dist {sorted(Counter(rs).items())}"
    )
    bad = sum(1 for r in rs if r <= 6)
    print(
        f"  {bad}/{len(rs)} = {100.0 * bad / len(rs):.0f}% ship at level 3 or WORSE, "
        f"on clues that hand over the answer"
    )
    print(
        "  the rater is grading the obscurity of the clue's WRAPPER (a museum,\n"
        "  a librarian, a theme park) instead of asking whether the answer is\n"
        "  reachable from the words in front of the player. CONFIRMED."
    )
    for r, q, aa in hits[:8]:
        print(f"    r={r}  {q[:86]} -> {aa}")

    # ---- probe 2 -----------------------------------------------------------
    leak = [o["r"] for o, c in intl if USMARK.search(c["q"])]
    rest = [o["r"] for o, c in intl if not USMARK.search(c["q"])]
    print("\nPROBE 2  us=false rows whose clue says U.S./American outright")
    print(
        f"  leaked n={len(leak)}  mean r {statistics.mean(leak):.2f}   "
        f"ship easy (r>=7) {100.0 * sum(1 for r in leak if r >= 7) / len(leak):.0f}%"
    )
    print(
        f"  rest   n={len(rest)}  mean r {statistics.mean(rest):.2f}   "
        f"ship easy (r>=7) {100.0 * sum(1 for r in rest if r >= 7) / len(rest):.0f}%"
    )
    print(
        "  REFUTED. The leaked rows sit BELOW the rest, so the rater applies the\n"
        "  international correction through r even where its own us flag missed.\n"
        "  The panel's one bad case does not generalise; the hunch was wrong."
    )

    # ---- independent signal ------------------------------------------------
    d_by_r = defaultdict(list)
    for o, c in intl:
        if isinstance(c.get("d"), int):
            d_by_r[o["r"]].append(c["d"])
    xs, ys = [], []
    for r, v in d_by_r.items():
        xs += [r] * len(v)
        ys += v
    mx, my = statistics.mean(xs), statistics.mean(ys)
    num = sum((p - mx) * (q - my) for p, q in zip(xs, ys))
    den = (sum((p - mx) ** 2 for p in xs) * sum((q - my) ** 2 for q in ys)) ** 0.5
    print("\nOUTSIDE SIGNAL  the show's own dollar tier, which the rater never saw")
    for r in sorted(d_by_r):
        print(
            f"  r={r:<3d} n={len(d_by_r[r]):<6d} mean tier {statistics.mean(d_by_r[r]):.2f}"
        )
    print(
        f"  Pearson = {num / den:+.3f} over n={len(xs):,}, and NEGATIVE is correct\n"
        "  (high r = easy = a cheaper clue). Weak on purpose: the tier is priced\n"
        "  for a US studio audience and this rater is deliberately not. Note the\n"
        "  monotone run at the easy end and the flat stretch from r=0 to r=5 --\n"
        "  the two raters are expected to part company exactly there, on the US\n"
        "  clues that are cheap on the board and near-zero at an international\n"
        "  table, so the flat end is not by itself evidence of a defect."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
