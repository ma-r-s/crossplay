#!/usr/bin/env python3
"""The best ISBN for every ranked book, with fallbacks.

    isbns.py --goodreads DIR --amazon meta_Books.jsonl.gz --merged merged.jsonl --out isbns.jsonl

An ISBN names an edition, not a work, so "the book's ISBN" is a choice.
Goodreads lists every edition of a work with its ISBN, format, language
and how many readers rated that edition; Amazon carries ISBN-10 and
ISBN-13 for most books, with the review count shared across formats. The
choice, per work: editions in the work's language first (unknown counts
as a match), audiobooks never, then the edition most readers rated; the
next two by the same rule are the fallbacks, and Amazon's ISBN-13 joins
the list when it is not already there. Writes one line per merged rank:
{"rank": 8, "isbn": "9780141439518", "isbns": [...], "editions": 213}.

"Most downloaded version" is not measurable from open data; the
most-rated edition is the same idea from the readers' side. --lang en
prefers English editions over the book's own language, and records the
chosen edition's language as isbnLang so a page can mark the ones that
had no English edition.
"""

import argparse
import collections
import gzip
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from universe2 import title_key, surname, AMAZON_DRESSING  # noqa: E402

AUDIO = re.compile(r"audio|audible|cd\b|cassette|mp3", re.I)
LANG = {"eng": "en", "en-US": "en", "en-GB": "en", "en-CA": "en", "spa": "es", "fre": "fr", "ger": "de",
        "ita": "it", "por": "pt", "dut": "nl", "fin": "fi", "": ""}


def isbn13(isbn10):
    d = re.sub(r"[^0-9Xx]", "", isbn10 or "")
    if len(d) != 10 or not d[:9].isdigit():
        return None  # an X is legal only as the check digit; anything else is a placeholder
    core = "978" + d[:9]
    s = sum((1 if i % 2 == 0 else 3) * int(c) for i, c in enumerate(core))
    return core + str((10 - s % 10) % 10)


def clean13(s):
    d = re.sub(r"[^0-9]", "", s or "")
    return d if len(d) == 13 and d.startswith(("978", "979")) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--goodreads", required=True)
    ap.add_argument("--amazon", required=True)
    ap.add_argument("--merged", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--lang", default="", help="prefer editions in this language (e.g. en) over the book's own; empty = the book's own")
    args = ap.parse_args()

    want = {}
    for line in open(args.merged):
        d = json.loads(line)
        want[tuple(d["key"])] = d["rank"]
    print(f"{len(want)} merged works", file=sys.stderr, flush=True)

    names = {}
    with gzip.open(os.path.join(args.goodreads, "goodreads_book_authors.json.gz"), "rt") as f:
        for line in f:
            a = json.loads(line)
            names[a["author_id"]] = a.get("name")
    works = {}
    with gzip.open(os.path.join(args.goodreads, "goodreads_book_works.json.gz"), "rt") as f:
        for line in f:
            w = json.loads(line)
            works[w["work_id"]] = {"title": w.get("original_title") or "", "best": w.get("best_book_id"), "author": None, "eds": []}
    n = 0
    with gzip.open(os.path.join(args.goodreads, "goodreads_books.json.gz"), "rt") as f:
        for line in f:
            n += 1
            if n % 500000 == 0:
                print(f"  goodreads books {n}", file=sys.stderr, flush=True)
            b = json.loads(line)
            w = works.get(b.get("work_id"))
            if w is None:
                continue
            if not w["title"] or b.get("book_id") == w["best"]:
                w["title"] = b.get("title_without_series") or b.get("title") or w["title"]
            if w["author"] is None and b.get("authors"):
                w["author"] = names.get(b["authors"][0].get("author_id"))
            i13 = clean13(b.get("isbn13")) or isbn13(b.get("isbn"))
            if not i13:
                continue
            w["eds"].append((i13, int(b.get("ratings_count") or 0), LANG.get(b.get("language_code") or "", b.get("language_code") or ""),
                             b.get("format") or "", b.get("is_ebook") == "true", b.get("publication_year") or ""))
    del names
    cands = collections.defaultdict(list)  # rank -> [(isbn13, ratings, lang, format, ebook, year, source)]
    for w in works.values():
        if not w["eds"]:
            continue
        r = want.get((title_key(w["title"]), surname(w["author"])))
        if r is None:
            continue
        for e in w["eds"]:
            cands[r].append(e + ("goodreads",))
    del works
    print(f"goodreads editions for {len(cands)} works", file=sys.stderr, flush=True)

    n = 0
    with gzip.open(args.amazon, "rt") as f:
        for line in f:
            n += 1
            if n % 500000 == 0:
                print(f"  amazon {n}", file=sys.stderr, flush=True)
            try:
                b = json.loads(line)
            except ValueError:
                continue
            det = b.get("details") or {}
            i13 = clean13(det.get("ISBN 13")) or isbn13(det.get("ISBN 10")) or isbn13(b.get("parent_asin"))
            if not i13:
                continue
            title = AMAZON_DRESSING.sub("", b.get("title") or "")
            a = b.get("author")
            author = a.get("name") if isinstance(a, dict) else None
            if not author:
                m = re.match(r"(.+?)\s*\((Author|Editor|Translator)", b.get("store") or "")
                author = m.group(1) if m else None
            r = want.get((title_key(title), surname(author)))
            if r is None:
                continue
            fmt = re.split(r"\s[–-]\s", b.get("subtitle") or "", maxsplit=1)[0].strip()
            lang = (det.get("Language") or "").lower()[:2]
            cands[r].append((i13, int(b.get("rating_number") or 0), lang, fmt, "kindle" in fmt.lower(), "", "amazon"))
    print(f"editions for {len(cands)} works after amazon", file=sys.stderr, flush=True)

    out_n = 0
    with open(args.out, "w") as out:
        for r, eds in cands.items():
            langs = collections.Counter(e[2] for e in eds if e[2] and e[6] == "goodreads")
            own = langs.most_common(1)[0][0] if langs else ""
            # The wanted language first (unknown counts as a match), the book's own
            # language next, so a Spanish novel gets its English translation's
            # ISBN when one exists and its own otherwise.
            lang = args.lang or own

            def score(e):
                i13, ratings, l, fmt, ebook, year, src = e
                lang_ok = (not l) or (not lang) or (l == lang)
                own_ok = (not l) or (not own) or (l == own)
                return (lang_ok, own_ok, not AUDIO.search(fmt), src == "goodreads", ratings)
            seen, ordered, best_lang = set(), [], None
            for e in sorted(eds, key=score, reverse=True):
                if e[0] not in seen:
                    seen.add(e[0])
                    ordered.append(e[0])
                    if best_lang is None:
                        best_lang = e[2]
            row = {"rank": r, "isbn": ordered[0], "isbns": ordered[:3], "editions": len(seen)}
            if args.lang:
                row["isbnLang"] = best_lang or ""  # empty = unknown; the page marks anything else
            out.write(json.dumps(row) + "\n")
            out_n += 1
    print(f"wrote {out_n} works with an ISBN of {len(want)}", file=sys.stderr)


if __name__ == "__main__":
    main()
