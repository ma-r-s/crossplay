#!/usr/bin/env python3
"""Draw a fresh blind head-to-head panel from the LOCAL rater's own output.

    python3 tools_local/trivia/make_local_panel.py <enriched.jsonl> <corpus.jsonl> <outdir>

WHY A SECOND PANEL EXISTS. difficulty_panel.tsv was sampled from the extremes of
the OLD cloud-rated file, so that file scores 100% against it partly by
construction and no threshold there transfers to a different rater -- panel_score.py
says so itself. This draws the pairs from the rater actually being shipped, so a
wrong rating here has somewhere to show up.

WHAT IT WRITES, and the split is the whole point:

  pairs_blind.tsv   pair id, band, and the two clues with their answers. NO
                    rating, NO level, NO question id. The judge cannot look the
                    rating up even by accident, because it is not in the file
                    and neither is the key to find it. Blindness is structural
                    here, not a promise somebody made.
  key.tsv           pair id -> question ids, ratings, levels, topics, and which
                    side the rater called harder. Read only AFTER a verdict file
                    exists.

THE BANDS ARE THE EXPERIMENT. One agreement number cannot tell a working rater
from a lucky one, because a panel drawn only from the extremes (r=1 against r=9)
is one any judge agrees with and proves only that 1 < 9. So the pairs are
stratified by how far apart the rater put them:

  wide    |dr| >= 6   the rater's own extremes; if THIS fails the labels are noise
  mid     |dr| 4-5
  narrow  |dr| 2-3    the band a real rater can still fail

A rater emitting well-spread noise scores ~50% in ALL THREE. A rater carrying
real signal rises monotonically wide > mid > narrow. The SHAPE is the evidence;
the headline percentage on its own is not, and that is the mistake this file
exists to avoid repeating.

Every pair also straddles a level boundary (assemble_pack.level), so each one is
a disagreement about the label that actually ships, not only about the raw r.

Scope: bad=false and us=false. The intl set is what the 0-10 scale in
rate_local.SCALE_INTL describes and what the pack's headline distribution is
quoted from. Whether the rater really pushes US-centric clues down is a
different question and us_shift_check.py already asks it.
"""

import argparse
import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import assemble_pack

# 30 pairs a band, 90 total. 30 puts a band's 95% interval at about +/-18
# points, enough to separate chance from a working rater INSIDE one band, and 90
# puts the headline at about +/-10. Smaller and the per-band trend that is the
# actual evidence stops being readable; larger and one judge cannot do it
# carefully, and a careless judgement is worth less than a smaller careful one.
PER_BAND = 30

BANDS = (
    ("wide", 6, 10),
    ("mid", 4, 5),
    ("narrow", 2, 3),
)

SEED = 20260904


def load_enriched(path):
    rows = {}
    torn = 0
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            try:
                o = json.loads(line)
            except ValueError:
                torn += 1  # expected: the writer is appending while we read
                continue
            if "id" not in o or not isinstance(o.get("r"), int):
                torn += 1
                continue
            rows[o["id"]] = o
    return rows, torn


def load_corpus(path):
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            try:
                o = json.loads(line)
            except ValueError:
                continue
            if o.get("id") and o.get("q") and o.get("a"):
                out[o["id"]] = o
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("enriched")
    ap.add_argument("corpus")
    ap.add_argument("outdir")
    ap.add_argument("--per-band", type=int, default=PER_BAND)
    ap.add_argument("--seed", type=int, default=SEED)
    a = ap.parse_args(argv)

    enr, torn = load_enriched(a.enriched)
    corpus = load_corpus(a.corpus)
    print(
        f"{len(enr):,} rated rows ({torn} unparseable/skipped), "
        f"{len(corpus):,} corpus clues"
    )

    # Eligible: rated, in the corpus, not flagged bad, not US-centric.
    pool = []
    for qid, o in enr.items():
        if o.get("bad") or o.get("us"):
            continue
        c = corpus.get(qid)
        if not c:
            continue
        pool.append(
            (qid, o["r"], c["q"].strip(), str(c["a"]).strip(), o.get("topic") or "?")
        )
    print(f"{len(pool):,} eligible (bad=false, us=false, clue present)")

    rng = random.Random(a.seed)
    by_r = {}
    for row in pool:
        by_r.setdefault(row[1], []).append(row)
    for v in by_r.values():
        rng.shuffle(v)

    used = set()
    pairs = []
    for band, lo, hi in BANDS:
        # Candidate (easy_r, hard_r) combinations for this gap band that also
        # land the two questions on DIFFERENT shipped levels. Enumerated rather
        # than rejection-sampled so the band cannot quietly come up short.
        combos = [
            (re_, rh)
            for re_ in range(11)
            for rh in range(11)
            if lo <= re_ - rh <= hi
            and assemble_pack.level(re_) != assemble_pack.level(rh)
        ]
        want = a.per_band
        got = 0
        guard = 0
        while got < want and guard < want * 200:
            guard += 1
            re_, rh = rng.choice(combos)
            ea = next((x for x in by_r.get(re_, []) if x[0] not in used), None)
            ha = next((x for x in by_r.get(rh, []) if x[0] not in used), None)
            if not ea or not ha:
                continue
            used.add(ea[0])
            used.add(ha[0])
            # Randomise which side of the sheet the harder clue lands on, so the
            # judge cannot learn a position habit.
            flip = rng.random() < 0.5
            pairs.append(
                {
                    "band": band,
                    "left": ha if flip else ea,
                    "right": ea if flip else ha,
                    "harder_side": "A" if flip else "B",
                }
            )
            got += 1
        if got < want:
            print(f"  !! band {band}: only {got}/{want} pairs available")

    rng.shuffle(pairs)

    os.makedirs(a.outdir, exist_ok=True)
    blind = os.path.join(a.outdir, "pairs_blind.tsv")
    key = os.path.join(a.outdir, "key.tsv")

    with open(blind, "w", encoding="utf-8") as f:
        f.write(
            "# BLIND SHEET. For each pair, which clue would FEWER groups of\n"
            "# four ordinary internationally-mixed adults answer correctly,\n"
            "# spoken aloud, 20 seconds, no options? That one is HARDER.\n"
            "# Write A or B, or TIE when you genuinely cannot separate them.\n"
            "# Ratings, levels, question ids and the band are deliberately absent.\n"
        )
        f.write("pair\tA_clue\tA_answer\tB_clue\tB_answer\n")
        for i, p in enumerate(pairs, 1):
            la, ra = p["left"], p["right"]
            f.write(f"P{i:03d}\t{la[2]}\t{la[3]}\t{ra[2]}\t{ra[3]}\n")

    with open(key, "w", encoding="utf-8") as f:
        f.write("# Do not open before a verdict file exists.\n")
        f.write(
            "pair\tband\tA_id\tA_r\tA_level\tA_topic\t"
            "B_id\tB_r\tB_level\tB_topic\trater_says_harder\n"
        )
        for i, p in enumerate(pairs, 1):
            la, ra = p["left"], p["right"]
            f.write(
                f"P{i:03d}\t{p['band']}\t"
                f"{la[0]}\t{la[1]}\t{assemble_pack.level(la[1])}\t{la[4]}\t"
                f"{ra[0]}\t{ra[1]}\t{assemble_pack.level(ra[1])}\t{ra[4]}\t"
                f"{p['harder_side']}\n"
            )

    counts = {}
    for p in pairs:
        counts[p["band"]] = counts.get(p["band"], 0) + 1
    print(
        f"wrote {len(pairs)} pairs: "
        + ", ".join(f"{b}={counts.get(b, 0)}" for b, _, _ in BANDS)
    )
    print(f"  blind sheet {blind}")
    print(f"  key         {key}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
