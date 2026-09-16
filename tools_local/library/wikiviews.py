#!/usr/bin/env python3
"""Page views per book, from a monthly pageview dump, joined by article title.

    wikiviews.py --dump pageviews-YYYYMM-user.bz2 --merged merged.jsonl --out views.jsonl
                 [--cache titleviews.json] [--plain-cap 5]

The dump is Wikimedia's pageview_complete monthly file for human (user)
traffic: wiki, title, page id, access method, the month's total, then
the daily counts encoded. The eight languages the library ships are kept
(bzcat | grep does the cut), access methods are summed per title, and
each known work in merged.jsonl (from universe2.py) gets its article's
month, by folded title:

- an article whose bracketed disambiguator names a written form
  ("It (novel)", "Emma (novel)", "Dune (novel)") wins outright, because
  Wikipedia only disambiguates when the plain title is something else;
- a plain title counts only for a work a source already knows, and only
  when the article does not outdraw the book's own readership many times
  over: "The File" took the article "File", and children's biographies
  called "Dolly Parton" and "Zendaya" took the celebrities' traffic;
- a folded title shared by several known works (different authors) is
  split in proportion to their value.

--cache saves the per-title counters after a dump pass and reads them
instead of a pass when present, so a rule change costs seconds, not the
30 GB. Wikidata would settle the join exactly, but its query service
times out on the paged list of literary works.
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


def scan(dump, by_title, bookish, plain):
    """One pass over the dump: per (lang, folded title), views of the book article and of the plain one."""
    # A character class, not an escaped dot: the pattern goes through repr()
    # into a single-quoted shell string, and a backslash would be doubled.
    pattern = "^(" + "|".join(LANGS) + ")[.]wikipedia "
    proc = subprocess.Popen(f"bzcat {dump!r} | grep -E {pattern!r}", shell=True,
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
            target = bookish
            folded = title_key(base)
        else:
            target = plain
            folded = title_key(title)
        if folded in by_title:
            try:
                target[(lang, folded)] += int(parts[-2])
            except ValueError:
                pass
    proc.wait()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dump", required=True)
    ap.add_argument("--merged", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--known", default="gr>=100 or az>=50 or ol>=5", help="which merged works may take an article")
    ap.add_argument("--cache", help="JSON of the per-title counters: written after a dump pass, read instead of one if present")
    ap.add_argument("--plain-cap", type=float, default=5.0,
                    help="a plain-title article may carry at most this many views per month per unit of the work's "
                         "own readership (max of Goodreads ratings, 10 x Amazon reviews, 200 x Open Library events)")
    args = ap.parse_args()

    works = []
    by_title = collections.defaultdict(list)  # folded title -> [work index]
    for line in open(args.merged):
        d = json.loads(line)
        if not eval(args.known, {}, {"gr": d["gr"], "az": d["az"], "ol": d["ol"]}):
            continue
        by_title[d["key"][0]].append(len(works))
        works.append(d)
    print(f"{len(works)} known works, {len(by_title)} folded titles", file=sys.stderr, flush=True)

    bookish = collections.Counter()   # (lang, folded) -> views of the disambiguated book article
    plain = collections.Counter()     # (lang, folded) -> views of the plain-title article
    if args.cache and os.path.exists(args.cache):
        c = json.load(open(args.cache))
        bookish.update({tuple(k.split("\t", 1)): v for k, v in c["bookish"].items()})
        plain.update({tuple(k.split("\t", 1)): v for k, v in c["plain"].items()})
        print(f"counters from {args.cache}: {len(bookish)} disambiguated, {len(plain)} plain", file=sys.stderr, flush=True)
    else:
        scan(args.dump, by_title, bookish, plain)
        if args.cache:
            json.dump({"bookish": {"\t".join(k): v for k, v in bookish.items()},
                       "plain": {"\t".join(k): v for k, v in plain.items()}}, open(args.cache, "w"))

    views = collections.Counter()  # work index -> views
    dropped = 0
    candidates = list(bookish.items()) + [(k, v) for k, v in plain.items() if k not in bookish]
    for (lang, folded), v in candidates:
        idx = by_title[folded]
        total_value = sum(works[i]["value"] for i in idx) or len(idx)
        is_plain = (lang, folded) not in bookish
        for i in idx:
            w = works[i]
            share = w["value"] / total_value if total_value else 1 / len(idx)
            if is_plain:
                # An article that outdraws the book's own readership many times over
                # is about something else with the same name: a tool, a singer, a game.
                readership = max(w["gr"], 10 * w["az"], 200 * w["ol"], 1)
                if v * share > args.plain_cap * readership:
                    dropped += 1
                    continue
            views[i] += v * share
    with open(args.out, "w") as out:
        for i, v in views.items():
            w = works[i]
            out.write(json.dumps({"title": w["title"], "author": w["author"], "views": round(v)}, ensure_ascii=False) + "\n")
    print(f"{len(bookish)} disambiguated and {len(plain)} plain matches; {len(views)} works carry views, "
          f"{dropped} plain matches dropped by the cap; wrote {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
