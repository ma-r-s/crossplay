#!/usr/bin/env python3
"""The list generator over every book, not over a pool we can copy.

    universe.py --ol DIR --out universe.md [--top 100] [--kb 300] [--pd ol.jsonl]

Ranks every work in Open Library by reading intent (want-to-read, currently
reading, already read, ratings: the same measure rank.py uses), reports how
concentrated intent is across all books, how many books fit on each card
at a given bytes-per-book, what share of intent that covers, and the top N
of all books with titles and authors. --pd ol.jsonl (from olsignal.py) adds
the share of all intent that the public-domain pool carries.

Open Library is the largest open catalog of books in existence, and its
reading log is the largest open record of people reaching for one. Neither
is all of humanity: the log is Internet Archive patrons, English-heavy,
nine years. It is the honest universe available without Goodreads' data,
which is licensed for academic use only.
"""

import argparse
import collections
import gzip
import json
import os
import re
import sys

CARDS_GB = [16, 32, 64, 128, 256]
SLIDER = [30, 50, 75, 95]
CURVE_N = [100, 1000, 10000, 50000, 100000, 200000, 500000, 1000000, 2000000]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ol", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--top", type=int, default=100)
    ap.add_argument("--kb", type=int, default=300, help="bytes per book, in KB, for the fit table")
    ap.add_argument("--pd", help="ol.jsonl of the public-domain pool, for its share of intent")
    args = ap.parse_args()

    intent = collections.Counter()
    want = collections.Counter()
    with gzip.open(os.path.join(args.ol, "ol_dump_reading-log_latest.txt.gz"), "rt") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 3:
                continue
            if parts[2] in ("Want to Read", "Currently Reading", "Already Read"):
                intent[parts[0]] += 1
                if parts[2] == "Want to Read":
                    want[parts[0]] += 1
    with gzip.open(os.path.join(args.ol, "ol_dump_ratings_latest.txt.gz"), "rt") as f:
        for line in f:
            parts = line.split("\t")
            if parts and parts[0].startswith("/works/"):
                intent[parts[0]] += 1
    total = sum(intent.values())
    ranked = intent.most_common()
    print(f"{len(ranked)} works with intent, {total} events", file=sys.stderr, flush=True)

    # titles and authors for the head, from the works dump (one pass, regex on the key column)
    head = {k for k, _ in ranked[:max(args.top, 2000)]}
    meta = {}
    n_works = 0
    with gzip.open(os.path.join(args.ol, "ol_dump_works_latest.txt.gz"), "rt") as f:
        for line in f:
            n_works += 1
            cols = line.split("\t", 2)
            if len(cols) < 2 or cols[1] not in head:
                continue
            try:
                data = json.loads(line.rstrip("\n").split("\t")[4])
            except (ValueError, IndexError):
                continue
            akeys = []
            for a in data.get("authors", []) or []:
                k = a.get("author", {}).get("key") if isinstance(a.get("author"), dict) else a.get("key")
                if k:
                    akeys.append(k)
            meta[cols[1]] = {"title": data.get("title"), "authors": akeys}
    need = {k for m in meta.values() for k in m["authors"]}
    names = {}
    with gzip.open(os.path.join(args.ol, "ol_dump_authors_latest.txt.gz"), "rt") as f:
        for line in f:
            cols = line.split("\t", 2)
            if len(cols) < 2 or cols[1] not in need:
                continue
            m = re.search(r'"name":\s*"((?:[^"\\]|\\.)*)"', line)
            if m:
                try:
                    names[cols[1]] = json.loads('"' + m.group(1) + '"')
                except ValueError:
                    pass
    print(f"{n_works} works in the catalog; {len(meta)} head works named", file=sys.stderr, flush=True)

    pd_share = None
    if args.pd:
        pd_mass = 0
        for line in open(args.pd):
            d = json.loads(line)
            pd_mass += d["want"] + d["reading"] + d["read"] + d["ratings"]
        pd_share = pd_mass / total

    out = []
    p = out.append
    p("# The universe: every book, ranked by reading intent\n")
    p(f"Open Library catalogs {n_works:,} works. {len(ranked):,} of them have at least one "
      f"reader event (want to read, reading, read, or a rating); {total:,} events in all, "
      f"{sum(want.values()):,} of them want-to-read. Produced by `tools_local/library/universe.py`.\n")
    if pd_share is not None:
        p(f"**The public-domain pool a site can copy today carries {100 * pd_share:.1f}% of that intent.**\n")

    p("## How concentrated intent is across all books\n")
    p("| top N works | share of all intent | events at rank N |\n| --- | --- | --- |")
    cum = 0
    marks = {}
    for i, (k, v) in enumerate(ranked, 1):
        cum += v
        if i in CURVE_N:
            marks[i] = (cum, v)
    for n in CURVE_N:
        if n in marks:
            p(f"| {n:,} | {100 * marks[n][0] / total:.1f}% | {marks[n][1]:,} |")
    p(f"| all {len(ranked):,} | 100% | 1 |")
    p("")

    p(f"## What fits, at {args.kb} KB per book\n")
    p("A text-only EPUB with its images stripped is 200 to 500 KB; the public-domain pool "
      f"measures a median of 205 KB. {args.kb} KB is the working figure. The share is of all "
      "intent, filling in rank order.\n")
    p("| card | " + " | ".join(f"fill to {s}%" for s in SLIDER) + " |")
    p("| --- | " + " | ".join("---" for _ in SLIDER) + " |")
    cum_at = []
    cum = 0
    for k, v in ranked:
        cum += v
        cum_at.append(cum)
    for card in CARDS_GB:
        cells = []
        for s in SLIDER:
            books = int(card * 1e9 * s / 100 / (args.kb * 1000))
            share = cum_at[min(books, len(cum_at)) - 1] / total
            cells.append(f"{books:,} books, {100 * share:.0f}%")
        p(f"| {card} GB | " + " | ".join(cells) + " |")
    p("")

    p(f"## The top {args.top} of all books, by reading intent\n")
    p("| # | title | author | want to read | all events |\n| --- | --- | --- | --- | --- |")
    for i, (k, v) in enumerate(ranked[:args.top], 1):
        m = meta.get(k, {})
        who = ", ".join(names.get(a, "") for a in m.get("authors", []) if names.get(a)) or "?"
        p(f"| {i} | {(m.get('title') or k)[:60]} | {who[:40]} | {want[k]:,} | {v:,} |")
    p("")
    open(args.out, "w").write("\n".join(out) + "\n")
    print(f"wrote {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
