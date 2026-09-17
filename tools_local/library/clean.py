#!/usr/bin/env python3
"""The clean list: no book that only exists in another language, no duplicates.

    clean.py --merged merged.jsonl --isbns isbns.jsonl --out clean.jsonl

Two rules, from Mario on 2026-09-17 ("I want this list as clean as possible"):

- A book whose known editions are all in another language is dropped
  (isbns.py names that language as "only"); so is a book with no English
  ISBN whose title is not in Latin script.
- A book is a duplicate of a higher-ranked one when they share any ISBN
  (editions of one work), or, for a book with no ISBN, when a higher row
  by the same author has a title that contains its title or is contained
  by it, or carries the same Amazon review count (Amazon shares one count
  across every listing of a book). Duplicates are dropped; a dropped duplicate's ISBNs join the
  survivor's list (after the survivor's own, which are already in
  popularity order).

Writes one line per kept book with a new contiguous rank, the original
rank kept as "was", and prints what each rule removed.
"""

import argparse
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from universe2 import fold  # noqa: E402

NON_LATIN = re.compile(r"[Ѐ-ӿͰ-Ͽ֐-׿؀-ۿऀ-ॿ฀-๿぀-ヿ㐀-鿿가-힯]")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--merged", required=True)
    ap.add_argument("--isbns", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    isbn = {}
    for line in open(args.isbns):
        d = json.loads(line)
        isbn[d["rank"]] = d
    rows = [json.loads(line) for line in open(args.merged)]
    rows.sort(key=lambda r: r["rank"])
    print(f"{len(rows)} books in, {len(isbn)} with edition data", file=sys.stderr, flush=True)

    removed = collections.Counter()
    keep = []
    owner = {}          # isbn -> index in keep of the book that owns it
    by_author = collections.defaultdict(list)  # surname -> [(folded title, keep index)]
    for r in rows:
        i = isbn.get(r["rank"])
        isbns = i["isbns"] if i else []
        title_f = fold(r["title"] or "")
        surname = r["key"][1] if r.get("key") else ""

        # rule 1: another language only
        if i and not isbns and i.get("only"):
            removed["only in " + i["only"]] += 1
            continue
        if not isbns and NON_LATIN.search(r["title"] or ""):
            removed["title not in Latin script"] += 1
            continue

        # rule 2: a duplicate of a higher-ranked book
        dup = None
        for x in isbns:
            if x in owner:
                dup = owner[x]
                break
        if dup is None and not isbns and surname and title_f:
            for other_title, other_az, idx in by_author.get(surname, ()):
                # Amazon shares one review count across every listing of a book, so
                # the same author with the same count is the same book ("memoir",
                # "A Novel": listings whose title is a fragment of the subtitle).
                if other_title and (other_title in title_f or title_f in other_title):
                    dup = idx
                    break
                if r["az"] > 100 and r["az"] == other_az:
                    dup = idx
                    break
        if dup is not None:
            removed["duplicate (shared ISBN)" if isbns else "duplicate (same author, nested title)"] += 1
            k = keep[dup]
            for x in isbns:
                if x not in k["isbns"]:
                    k["isbns"].append(x)
                    owner[x] = dup
            continue

        idx = len(keep)
        r["isbns"] = list(isbns)
        r["isbn"] = isbns[0] if isbns else ""
        r["editions"] = i["editions"] if i else 0
        keep.append(r)
        for x in isbns:
            owner[x] = idx
        if surname and title_f:
            by_author[surname].append((title_f, r["az"], idx))

    with open(args.out, "w") as out:
        for n, r in enumerate(keep, 1):
            r["was"] = r["rank"]
            r["rank"] = n
            out.write(json.dumps(r, ensure_ascii=False) + "\n")
    print(f"{len(keep)} books kept", file=sys.stderr)
    for k, v in removed.most_common():
        print(f"  removed, {k}: {v:,}", file=sys.stderr)


if __name__ == "__main__":
    main()
