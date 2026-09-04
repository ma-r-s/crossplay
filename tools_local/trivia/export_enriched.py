#!/usr/bin/env python3
"""Turn enrich_pack.py's jsonl into the files a pack build already knows how to read.

enrich_pack.py writes one record per question with five fields on it. Nothing
consumes that. This splits it into the three shapes the existing tools take,
so the local pass plugs into the build instead of needing a new one:

  --out-ratings   id<TAB>rating<TAB>judges   what difficulty.py reads
  --out-verdicts  id<TAB>bad                 what build_pack.py --verdicts reads
  --out-options   {"id":..,"w":[..]}         three wrong options per question

THE RATINGS ARE ON THE INTERNATIONAL SCALE AND THE OLD ONES ARE NOT. They are
written to their own file rather than merged into difficulty.tsv, because the
two disagree by about 2.2 points by construction: a fresh pass rates the same
question softer than the archive, and the international framing then pushes the
American questions back down. Averaging them would produce a number on neither
scale. Point the build at one file or the other.

KICKING IS A FLAG, NOT A DEFAULT. --kick-us adds us_centric questions to the
verdicts file; without it only `bad` is kicked. A US-inclusive pack stays
buildable from exactly the same data, which is the reason enrich_pack.py stored
the two as separate fields in the first place.

    python3 tools_local/trivia/export_enriched.py --enriched .rate/enriched.jsonl \\
        --out-ratings .rate/local_difficulty.tsv \\
        --out-verdicts .rate/local_verdicts.tsv \\
        --out-options .rate/local_options.jsonl
"""
import argparse, collections, json, statistics, sys


def load(path):
    out, dupes = {}, 0
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except ValueError:
            continue
        if "id" not in r:
            continue
        # The jsonl is append-only and resumable, so a killed-and-resumed run
        # can hold the same id twice. Last write wins, and the count is
        # reported rather than swallowed: silently deduping a file that should
        # not have duplicates hides a resume bug.
        if r["id"] in out:
            dupes += 1
        out[r["id"]] = r
    return out, dupes


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--enriched", required=True)
    ap.add_argument("--out-ratings", default="")
    ap.add_argument("--out-verdicts", default="")
    ap.add_argument("--out-options", default="")
    ap.add_argument("--kick-us", action="store_true",
                    help="also kick us_centric questions (default: only `bad`)")
    ap.add_argument("--min-options", type=int, default=3,
                    help="only export option sets with at least this many (default 3)")
    a = ap.parse_args()

    recs, dupes = load(a.enriched)
    if not recs:
        sys.exit(f"{a.enriched}: no usable records")
    print(f"{len(recs):,} questions read" + (f"  ({dupes} duplicate ids, last kept)" if dupes else ""))

    if a.out_ratings:
        rated = {i: r["r"] for i, r in recs.items() if r.get("r") is not None}
        with open(a.out_ratings, "w", encoding="utf-8") as f:
            f.write("# id\trating\tjudges\n")
            f.write("# INTERNATIONAL scale, one local judge. NOT the same scale as\n")
            f.write("# difficulty.tsv; see export_enriched.py before merging them.\n")
            for i in sorted(rated):
                f.write(f"{i}\t{float(rated[i]):.2f}\t1\n")
        v = sorted(rated.values())
        print(f"  ratings  -> {a.out_ratings}  {len(rated):,} "
              f"(mean {statistics.mean(v):.2f}, median {statistics.median(v):.2f})")

    if a.out_verdicts:
        bad = {i for i, r in recs.items() if r.get("bad")}
        us = {i for i, r in recs.items() if r.get("us")}
        kick = bad | us if a.kick_us else bad
        with open(a.out_verdicts, "w", encoding="utf-8") as f:
            f.write("# id\tverdict\n")
            for i in sorted(kick):
                f.write(f"{i}\tbad\n")
        print(f"  verdicts -> {a.out_verdicts}  {len(kick):,} kicked "
              f"({len(bad):,} unanswerable"
              + (f" + {len(us - bad):,} us_centric)" if a.kick_us
                 else f"; {len(us):,} us_centric NOT kicked, pass --kick-us)"))

    if a.out_options:
        n = 0
        with open(a.out_options, "w", encoding="utf-8") as f:
            for i in sorted(recs):
                w = recs[i].get("w") or []
                if len(w) >= a.min_options:
                    f.write(json.dumps({"id": i, "w": w[:3]}, ensure_ascii=False) + "\n")
                    n += 1
        print(f"  options  -> {a.out_options}  {n:,} sets of >={a.min_options} "
              f"({100*n/len(recs):.1f}% of what was rated)")

    topics = collections.Counter(r.get("topic") for r in recs.values() if r.get("topic"))
    print("  topics   : " + ", ".join(f"{t} {c:,}" for t, c in topics.most_common(8)))


if __name__ == "__main__":
    main()
