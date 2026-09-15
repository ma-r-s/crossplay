#!/usr/bin/env python3
"""Page views per book, from a monthly pageview dump, joined by article title.

    wikiviews.py --dump pageviews-YYYYMM-user.bz2 --merged merged.jsonl --out views.jsonl

The dump is Wikimedia's pageview_complete monthly file for human (user)
traffic: wiki, title, page id, access method, the month's total, then
the daily counts encoded. The eight languages the library ships are kept
(bzcat | grep does the cut), access methods are summed per title, and
each known work in merged.jsonl (from universe2.py) gets its article's
month, by folded title:

- an article whose bracketed disambiguator names a written form
  ("It (novel)", "Emma (novel)", "Dune (novel)") wins outright, because
  Wikipedia only disambiguates when the plain title is something else;
- a plain title counts only when no such article exists and the work is
  already known to a source (so "Lost" or "Home" in the tail cannot
  collect a stranger's traffic);
- a folded title shared by several known works (different authors) is
  split in proportion to their value.

Wikidata would settle the join exactly, but its query service times out
on the paged list of literary works; this needs no service at all.
"""

import argparse
import collections
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from universe2 import title_key  # noqa: E402

LANGS = ["en", "es", "fr", "de", "it", "pt", "nl", "fi"]
BOOKISH = re.compile(r"novel|book|play|poem|poetry|story|stories|essay|epic|memoir|autobiograph|"
                     r"novella|comedy|tragedy|treatise|dialogue|libro|roman|romanzo|livre|"
                     r"Buch|romaani|kirja|libre|novela|obra|cuento|conte|Gedicht|Drama|opera", re.I)
DISAMB = re.compile(r"^(.*?)\s*\(([^()]*)\)\s*$")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dump", required=True)
    ap.add_argument("--merged", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--known", default="gr>=100 or az>=50 or ol>=5", help="which merged works may take a plain-title article")
    args = ap.parse_args()

    # Known works by folded title: {folded: [(index, value, title, author)]}
    works = []
    by_title = collections.defaultdict(list)
    for line in open(args.merged):
        d = json.loads(line)
        gr, az, ol = d["gr"], d["az"], d["ol"]
        if not eval(args.known, {}, {"gr": gr, "az": az, "ol": ol}):
            continue
        i = len(works)
        works.append(d)
        by_title[d["key"][0]].append(i)
    print(f"{len(works)} known works, {len(by_title)} folded titles", file=sys.stderr, flush=True)

    bookish = collections.Counter()   # (lang, folded) -> views of the disambiguated book article
    plain = collections.Counter()     # (lang, folded) -> views of the plain-title article
    # A character class, not an escaped dot: the pattern goes through repr()
    # into a single-quoted shell string, and a backslash would be doubled.
    pattern = "^(" + "|".join(LANGS) + ")[.]wikipedia "
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
        title = parts[1].replace("_", " ")
        m = DISAMB.match(title)
        if m:
            base, disamb = m.group(1), m.group(2)
            if not BOOKISH.search(disamb):
                continue
            folded = title_key(base)
            if folded in by_title:
                try:
                    bookish[(lang, folded)] += int(parts[-2])
                except ValueError:
                    pass
        else:
            folded = title_key(title)
            if folded in by_title:
                try:
                    plain[(lang, folded)] += int(parts[-2])
                except ValueError:
                    pass
    proc.wait()

    views = collections.Counter()  # work index -> views
    for (lang, folded), v in list(bookish.items()) + [(k, v) for k, v in plain.items() if k not in bookish]:
        idx = by_title[folded]
        total_value = sum(works[i]["value"] for i in idx) or len(idx)
        for i in idx:
            share = works[i]["value"] / total_value if total_value else 1 / len(idx)
            views[i] += v * share
    with open(args.out, "w") as out:
        for i, v in views.items():
            w = works[i]
            out.write(json.dumps({"title": w["title"], "author": w["author"], "views": round(v)}, ensure_ascii=False) + "\n")
    print(f"{len(bookish)} disambiguated and {len(plain)} plain matches; {len(views)} works carry views; wrote {args.out}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
