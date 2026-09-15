#!/usr/bin/env python3
"""Page views per book article, from a monthly pageview dump and wikibooks.jsonl.

    wikiviews.py --dump pageviews-YYYYMM-user.bz2 --books wikibooks.jsonl --out views.jsonl

The dump is Wikimedia's pageview_complete monthly file for human (user)
traffic, one line per article and access method: wiki, title, page id,
access method, the month's total, then the daily counts encoded. The
eight languages the library ships are kept (bzcat | grep does the cut,
Python only sees a fraction of the 30 GB), the access methods are summed
per title, and each book from wikibooks.jsonl (Wikidata's literary works
with articles) gets its article's month: {"title", "author", "lang", "views"}.
"""

import argparse
import collections
import json
import subprocess
import sys

LANGS = ["en", "es", "fr", "de", "it", "pt", "nl", "fi"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dump", required=True)
    ap.add_argument("--books", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    books = {}
    for line in open(args.books):
        d = json.loads(line)
        books[(d["lang"], d["title"].replace(" ", "_"))] = d
    print(f"{len(books)} book articles to look up", file=sys.stderr, flush=True)

    views = collections.Counter()
    pattern = r"^(" + "|".join(LANGS) + r")\.wikipedia "
    proc = subprocess.Popen(f"bzcat {args.dump!r} | grep -E {pattern!r}", shell=True,
                            stdout=subprocess.PIPE, text=True, errors="replace", bufsize=1 << 20)
    n = 0
    for line in proc.stdout:
        n += 1
        if n % 10_000_000 == 0:
            print(f"{n} lines", file=sys.stderr, flush=True)
        parts = line.split(" ")
        if len(parts) < 6:
            continue
        lang = parts[0].split(".", 1)[0]
        key = (lang, parts[1])
        if key in books:
            try:
                views[key] += int(parts[-2])
            except ValueError:
                pass
    proc.wait()
    with open(args.out, "w") as out:
        for key, d in books.items():
            if key in views:
                out.write(json.dumps({"title": d["title"], "author": d.get("author"), "lang": d["lang"],
                                      "views": views[key]}, ensure_ascii=False) + "\n")
    print(f"{len(views)} of {len(books)} articles had views; wrote {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
