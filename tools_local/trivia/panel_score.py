#!/usr/bin/env python3
"""Score a rating file against the frozen blind panel.

    python3 tools_local/trivia/panel_score.py <ratings>

<ratings> is either enriched.jsonl (objects carrying `id` and `r`) or a
two-column TSV of `id<TAB>rating`, so it reads the rating run's own output and
the committed snapshot in local/ without a conversion step.

WHY THIS EXISTS. Every other difficulty assertion in this repo is computed from
`r`, and `d` is CUT from `r` by assemble_pack.level -- so monotone means, a wide
1-to-5 gap and a stocked tier are arithmetic, not evidence. Shuffle the ratings
across questions and all of them still pass, byte for byte. difficulty_panel.tsv
is 50 pairs decided by judges who never saw a rating, as (easier, harder), and
re-scoring them against the current levels is the only thing here that a wrong
rating can fail.

THE SCORE IS NOT COMPARABLE ACROSS RATERS, and this is the trap. The pairs in
difficulty_panel.tsv were SELECTED from the extremes of one rating file -- level
1 against level 5 as that file cut them -- and only pairs three blind judges
then decided unanimously were kept. So the file the pairs were drawn from scores
50/50 = 100% partly by construction, and any other rater is being asked about
somebody else's extremes. There is no threshold here that transfers. Draw a
fresh panel from the extremes of the rater you are shipping before treating any
number as a bar.

WHAT IT STILL MEASURES. The judges never saw a rating, so their verdicts are
independent of the cut even though the sampling was not. Against a ratings file
shuffled across questions the score collapses (measured: 34%), which no other
difficulty assertion in this repo does -- they are all computed from `r`.

NO PASS AND NO FAIL, and it exits 0. A verdict here would be a verdict against a
bar that does not transfer, and a gate that says FAIL for a structural reason
teaches people to ignore it. It prints numbers; the reading is the runbook's
(docs/trivia-curation.md) and it is not in check.sh.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import assemble_pack

# What a ratings file shuffled across these questions scores, measured. It is
# the only reference point here that does not depend on which rater the pairs
# were sampled from, so it is printed rather than a bar.
SHUFFLED_BASELINE = 34.0


def load_ratings(path):
    """id -> r, from enriched jsonl or a two-column tsv."""
    out = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line[0] == "{":
            try:
                o = json.loads(line)
            except ValueError:
                continue
            if "id" in o and "r" in o:
                out[o["id"]] = float(o["r"])
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        try:
            out[parts[0].strip()] = float(parts[1])
        except ValueError:
            continue
    return out


def load_pairs(path):
    pairs = []
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 2:
            pairs.append((parts[0], parts[1]))
    return pairs


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__.strip().splitlines()[2].strip())
    here = os.path.dirname(os.path.abspath(__file__))
    pairs = load_pairs(os.path.join(here, "difficulty_panel.tsv"))
    ratings = load_ratings(argv[1])
    print(f"{len(pairs)} panel pairs; {len(ratings):,} ratings in {argv[1]}")

    known = [(a, b) for a, b in pairs if a in ratings and b in ratings]
    missing = len(pairs) - len(known)
    if missing:
        # Not a soft warning: a pair whose questions have lost their ratings is
        # silently dropped from the denominator, which raises the score.
        print(f"  !! {missing} of {len(pairs)} pairs have lost a rating and were "
              f"NOT scored -- regenerate the panel rather than reading the rest")
    if not known:
        print("  !! nothing to score")
        return 1

    lv = {q: assemble_pack.level(r) for q, r in ratings.items()}
    agree = sum(1 for a, b in known if lv[a] < lv[b])
    same = sum(1 for a, b in known if lv[a] == lv[b])
    rate = 100.0 * agree / len(known)

    # The same question asked without the cut in the way, so a bad score can be
    # read as "the thresholds are wrong" or "the ratings are wrong" rather than
    # leaving the two confounded. r is 0-10 with HIGH meaning EASY.
    raw = sum(1 for a, b in known if ratings[a] > ratings[b])
    ties = sum(1 for a, b in known if ratings[a] == ratings[b])

    print(f"  levels agree with the panel : {agree}/{len(known)} = {rate:.1f}%"
          f"   ({same} pairs landed on the SAME level and decide nothing)")
    print(f"  raw ratings, cut removed    : {raw}/{len(known)} = "
          f"{100.0 * raw / len(known):.1f}%   ({ties} ties)")
    print(f"\n  for reference, these ratings shuffled across questions: "
          f"~{SHUFFLED_BASELINE:.0f}%")
    print("\nNO VERDICT, on purpose. The pairs were sampled from the extremes of ONE\n"
          "rating file, so that file scores near 100% partly by construction and no\n"
          "threshold here carries over to a different rater. Draw a fresh panel from\n"
          "the rater you are shipping before treating any number as a bar. See\n"
          "docs/trivia-curation.md, 'The levels against a blind panel'.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
