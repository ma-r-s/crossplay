#!/usr/bin/env python3
"""Open Library intent per Gutenberg work: want-to-read, reads, ratings.

    olsignal.py --catalog catalog.jsonl --pool ranked.jsonl --ol DIR --out ol.jsonl

DIR holds Open Library's monthly dumps (ol_dump_works_latest.txt.gz,
ol_dump_authors_latest.txt.gz, ol_dump_reading-log_latest.txt.gz,
ol_dump_ratings_latest.txt.gz). A Gutenberg work is matched to Open Library
works by folded title (the same folding rank.py uses for the work merge)
plus the first author's folded surname; the matched works' reading-log and
rating counts are summed. Writes one line per matched Gutenberg id:
{"id": 1342, "want": 6432, "reading": 300, "read": 940, "ratings": 406, "works": 3}.

Two passes over the works dump avoid parsing 25 million JSON lines: a
regex pulls the title first and only a folded-title hit is parsed. The
authors dump is read once for the names the candidates reference.
"""

import argparse
import collections
import gzip
import json
import re
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rank import fold, work_key  # noqa: E402

TITLE_RE = re.compile(r'"title":\s*"((?:[^"\\]|\\.)*)"')


def surname(name):
    name = name or ""
    if "," in name:
        return fold(name.split(",", 1)[0])
    parts = fold(name).split()
    return parts[-1] if parts else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--catalog", required=True)
    ap.add_argument("--pool", required=True, help="ranked.jsonl, for the ids in the pool")
    ap.add_argument("--ol", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    pool_ids = {json.loads(l)["id"] for l in open(args.pool)}
    index = collections.defaultdict(list)  # folded title -> [(id, surname)]
    n = 0
    for line in open(args.catalog):
        r = json.loads(line)
        if r["id"] not in pool_ids:
            continue
        r["title"] = re.sub(r"\s*:\s*\$b\s*", ": ", (r["title"] or "").replace("\r", ""))
        _, t, s = work_key(r)
        titles = {t}
        for alt in r.get("alternative", []):
            titles.add(work_key({"title": alt, "creators": r["creators"], "languages": r["languages"]})[1])
        for t2 in titles:
            if len(t2) >= 3:
                index[t2].append((r["id"], s))
        n += 1
    print(f"{n} pool works, {len(index)} folded titles", file=sys.stderr, flush=True)

    # reading log and ratings, grouped by work key
    log = collections.defaultdict(lambda: [0, 0, 0])  # want, reading, read
    with gzip.open(os.path.join(args.ol, "ol_dump_reading-log_latest.txt.gz"), "rt") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 3:
                continue
            shelf = parts[2]
            slot = 0 if shelf == "Want to Read" else 1 if shelf == "Currently Reading" else 2 if shelf == "Already Read" else None
            if slot is not None:
                log[parts[0]][slot] += 1
    ratings = collections.Counter()
    with gzip.open(os.path.join(args.ol, "ol_dump_ratings_latest.txt.gz"), "rt") as f:
        for line in f:
            parts = line.split("\t")
            if parts:
                ratings[parts[0]] += 1
    print(f"reading log: {len(log)} works; ratings: {len(ratings)} works", file=sys.stderr, flush=True)

    # pass over works: candidates by folded title
    cands = []  # (work key, folded title, [author keys])
    seen = 0
    with gzip.open(os.path.join(args.ol, "ol_dump_works_latest.txt.gz"), "rt") as f:
        for line in f:
            seen += 1
            if seen % 5_000_000 == 0:
                print(f"{seen} works scanned, {len(cands)} candidates", file=sys.stderr, flush=True)
            m = TITLE_RE.search(line)
            if not m:
                continue
            try:
                raw_title = json.loads('"' + m.group(1) + '"')
            except ValueError:
                continue
            t = work_key({"title": raw_title, "creators": [], "languages": []})[1]
            if t not in index:
                continue
            cols = line.rstrip("\n").split("\t")
            if len(cols) < 5:
                continue
            try:
                data = json.loads(cols[4])
            except ValueError:
                continue
            akeys = []
            for a in data.get("authors", []) or []:
                k = a.get("author", {}).get("key") if isinstance(a.get("author"), dict) else a.get("key")
                if k:
                    akeys.append(k)
            cands.append((cols[1], t, akeys))
    print(f"{seen} works scanned, {len(cands)} title candidates", file=sys.stderr, flush=True)

    need = {k for _, _, ks in cands for k in ks}
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
    print(f"{len(names)} of {len(need)} author names found", file=sys.stderr, flush=True)

    sig = collections.defaultdict(lambda: {"want": 0, "reading": 0, "read": 0, "ratings": 0, "works": 0})
    for wkey, t, akeys in cands:
        ol_surnames = {surname(names.get(k)) for k in akeys if names.get(k)}
        for gid, s in index[t]:
            ok = (s and s in ol_surnames) or (not s and not ol_surnames)
            if not ok:
                continue
            w, c, r = log.get(wkey, (0, 0, 0))
            d = sig[gid]
            d["want"] += w
            d["reading"] += c
            d["read"] += r
            d["ratings"] += ratings.get(wkey, 0)
            d["works"] += 1
    with open(args.out, "w") as out:
        for gid in sorted(sig):
            out.write(json.dumps({"id": gid, **sig[gid]}) + "\n")
    matched_with_signal = sum(1 for d in sig.values() if d["want"] + d["read"] + d["reading"] + d["ratings"] > 0)
    print(f"{len(sig)} pool works matched an OL work; {matched_with_signal} carry any count", file=sys.stderr)


if __name__ == "__main__":
    main()
