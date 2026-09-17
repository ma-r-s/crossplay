#!/usr/bin/env python3
"""Every book, ranked on the best open signals: Goodreads, Amazon, Open Library.

    universe2.py --goodreads DIR --amazon meta_Books.jsonl.gz --ol DIR
                 --out universe2.md --merged merged.jsonl [--pool ranked.jsonl] [--views views.jsonl]

Each source becomes a share of its own total, and a work's value is the
mixture 0.4 x Goodreads ratings (site-wide counters, 2.4 M books, 2017)
+ 0.3 x Amazon review counts (4.4 M books, to 2023) + 0.2 x Open Library
intent (want-to-read, reading, read, ratings; 3.3 M works, to 2026)
+ 0.1 x Wikipedia page views (if --views is given: per work, a month).
A work missing from a source keeps the others. Sources are joined on a
folded title (subtitle and series dropped) plus the first author's
surname: no ISBN join, because Open Library's editions dump is 9 GB and a
ranking does not need it.

Writes the report (each source's own top 20 so the mixture can be judged,
the concentration curve, the fit table at 300 KB a book, the top 100, and
the public-domain pool's share) and the merged table for later passes.
"""

import argparse
import collections
import gzip
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rank import fold  # noqa: E402

CARDS_GB = [16, 32, 64, 128, 256]
SLIDER = [30, 95]
CURVE_N = [100, 1000, 10000, 50000, 100000, 200000, 500000, 1000000]
W = {"gr": 0.4, "az": 0.3, "ol": 0.2, "pv": 0.1}


def title_key(title):
    t = title or ""
    t = re.sub(r"\(.*?\)|\[.*?\]", " ", t)          # (Harry Potter, #1), [Illustrated]
    t = re.split(r"[:;\n]", t, maxsplit=1)[0]          # subtitle
    t = re.sub(r"\b(vol(ume)?|part|book|tome)\.?\s*([0-9]+|[ivxlc]+)\b.*$", "", t, flags=re.I)
    t = re.sub(r",?\s*\b(complete|unabridged|illustrated|annotated)\b\.?\s*$", "", t, flags=re.I)
    t = re.sub(r",\s*(the|a|an)\s*$", "", t, flags=re.I)  # "Girl on the Train, The"
    return fold(t)[:60]


def surname(name):
    name = name or ""
    if "," in name:
        return fold(name.split(",", 1)[0])
    parts = fold(name).split()
    return parts[-1] if parts else ""


def load_goodreads(d):
    """work key -> (title, author, ratings, to-read), from works + books + authors."""
    names = {}
    with gzip.open(os.path.join(d, "goodreads_book_authors.json.gz"), "rt") as f:
        for line in f:
            a = json.loads(line)
            names[a["author_id"]] = a.get("name")
    works = {}
    with gzip.open(os.path.join(d, "goodreads_book_works.json.gz"), "rt") as f:
        for line in f:
            w = json.loads(line)
            if (w.get("media_type") or "book") in NOT_A_BOOK:
                continue  # Goodreads' own label: periodicals, articles, "not a book"
            works[w["work_id"]] = {"title": w.get("original_title") or "", "ratings": int(w.get("ratings_count") or 0),
                                   "year": w.get("original_publication_year") or "", "author": None, "toread": 0,
                                   "best": w.get("best_book_id")}
    n = 0
    with gzip.open(os.path.join(d, "goodreads_books.json.gz"), "rt") as f:
        for line in f:
            n += 1
            if n % 500000 == 0:
                print(f"  goodreads books {n}", file=sys.stderr, flush=True)
            b = json.loads(line)
            w = works.get(b.get("work_id"))
            if w is None:
                continue
            # The "best book" is the edition Goodreads shows for the work, in
            # practice the English one; the original title can be Dutch or
            # Swedish (Anne Frank, Stieg Larsson) and then joins nothing.
            if not w["title"] or b.get("book_id") == w["best"]:
                w["title"] = b.get("title_without_series") or b.get("title") or w["title"]
            if w["author"] is None and b.get("authors"):
                w["author"] = names.get(b["authors"][0].get("author_id"))
            for shelf in b.get("popular_shelves") or []:
                if shelf.get("name") == "to-read":
                    w["toread"] += int(shelf.get("count") or 0)
                    break
    return works


AMAZON_MARKETING = re.compile(
    r"^\s*(best ?seller[^a-z]*(no\.?\s*\d+)?\s*(author)?[\s_:-]*|by\s+[A-Z][\w.'-]+(\s+[A-Z][\w.'-]+)*\s+(?=[A-Z]))"
    r"|\s+(the\s+)?(richard and judy|sunday times|new york times|book ?club|regular print|large print|thorndike press|"
    r"tv tie-in|film tie-in|movie tie-in|now a major|export)\b.*$", re.I)
AMAZON_DRESSING = re.compile(
    r"\s*(\[.*$|\b(paperback|hardcover|mass market|kindle edition|large print|deluxe edition|export|"
    r"international edition|audio cd|library binding|board book|spiral-bound)\b.*$|\bby\s+[A-Z][^,]*,.*$|"
    r"\((spanish|french|german|italian|portuguese) edition\).*$)", re.I)


NOT_A_BOOK = {"not a book", "periodical", "article"}
NOT_A_BOOK_FORMAT = re.compile(r"audio|audible|\bcd\b|cassette|mp3|dvd|blu-ray|vhs|calendar|poster|cards?\b|game|toy|puzzle|"
                               r"sticker|stationery|journal\b|notebook|planner|diary\b", re.I)
NOT_A_BOOK_CATEGORY = re.compile(r"Movies & TV|Music|Calendars|Video Games|Toys & Games|Office Products|Software", re.I)
AUTHOR_ROLES = ("Author", "Editor", "Translator", "Illustrator", "Contributor", "Introduction", "Foreword", "Compiler")
NOT_AUTHOR_ROLES = ("Narrator", "Reader", "Actor", "Director", "Artist", "Performer", "Producer", "Composer")


AUTHOR_DRESSING = re.compile(r"\b(spanish|french|german|italian|portuguese|english|chinese|japanese)\s+edition\b", re.I)


def amazon_author(b):
    """The person credited as the book's author, or None when the listing credits only performers."""
    store = b.get("store") or ""
    credits = [(name.strip(), [r.strip() for r in roles.split(",")])
               for name, roles in re.findall(r"([^,()]+?)\s*\(([A-Za-z ,]+)\)", store)]
    if credits:
        # "Michelle Obama (Author, Narrator), Penguin Audio (Publisher)" with no
        # format line is the Audible listing: the book is elsewhere in the dump.
        if any("Narrator" in roles for _, roles in credits) and not (b.get("subtitle") or "").strip():
            return None
        for name, roles in credits:
            if any(r in AUTHOR_ROLES for r in roles):
                return name
        if all(all(r in NOT_AUTHOR_ROLES or r == "Publisher" for r in roles) for _, roles in credits):
            return None
    a = b.get("author")
    if isinstance(a, dict) and a.get("name"):
        return AUTHOR_DRESSING.sub("", a["name"]).strip(" .") or None
    for name, roles in credits:
        if "Publisher" not in roles:
            return name
    return None


def load_amazon(path):
    """(title key, surname) -> (title, author, reviews): the max over editions."""
    out = {}
    n = 0
    with gzip.open(path, "rt") as f:
        for line in f:
            n += 1
            if n % 500000 == 0:
                print(f"  amazon {n}", file=sys.stderr, flush=True)
            try:
                b = json.loads(line)
            except ValueError:
                continue
            title = b.get("title") or ""
            title = AMAZON_MARKETING.sub("", AMAZON_DRESSING.sub("", title)).strip(" :-_")
            title = re.sub(r"\s*-\s*common\s*$", "", title, flags=re.I)
            fmt = re.split(r"\s[–-]\s", b.get("subtitle") or "", maxsplit=1)[0]
            cats = " ".join(b.get("categories") or [])
            if NOT_A_BOOK_FORMAT.search(fmt) or NOT_A_BOOK_CATEGORY.search(cats):
                continue  # an audiobook, a film, a calendar, a toy: filed under Books, not a book
            author = amazon_author(b)
            if author is None:
                continue  # only narrators, actors or artists credited: not a book listing either
            # An Amazon marketplace title can carry the author's name at either end
            # ("Anthony Doerr All the Light We Cannot See", "the midnight library matt haig").
            an = fold(author)
            if an:
                ft = fold(title)
                for name in (an, " ".join(reversed(an.split()))):
                    if ft.startswith(name + " ") or ft.endswith(" " + name):
                        title = re.sub(r"(?i)^\s*" + re.escape(name.split()[0]) + r"\b.*?\b" + re.escape(name.split()[-1]) + r"\s+|\s+\b" + re.escape(name.split()[0]) + r"\b.*?\b" + re.escape(name.split()[-1]) + r"\s*$", "", title, count=1) if len(name.split()) > 1 else title
                        break
            k = (title_key(title), surname(author))
            if not k[0]:
                continue
            # "Rooney  Sally" and "Doerr Anthony" are surname-first and "Michael Grant" is
            # not; nothing in the string tells them apart, so a two-token author's other
            # reading rides along and the caller keeps whichever key Goodreads already has.
            alt = None
            toks = fold(author).split()
            if len(toks) == 2:
                alt = (k[0], toks[0])
            r = int(b.get("rating_number") or 0)
            cur = out.get(k)
            if cur is None or r > cur[2]:
                out[k] = (title, author, r, alt)
    return out


def load_ol(d):
    """(title key, surname) -> (title, author, intent), works with 2+ events only."""
    intent = collections.Counter()
    with gzip.open(os.path.join(d, "ol_dump_reading-log_latest.txt.gz"), "rt") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 3 and parts[2] in ("Want to Read", "Currently Reading", "Already Read"):
                intent[parts[0]] += 1
    with gzip.open(os.path.join(d, "ol_dump_ratings_latest.txt.gz"), "rt") as f:
        for line in f:
            parts = line.split("\t")
            if parts and parts[0].startswith("/works/"):
                intent[parts[0]] += 1
    keep = {k for k, v in intent.items() if v >= 2}
    meta = {}
    n = 0
    with gzip.open(os.path.join(d, "ol_dump_works_latest.txt.gz"), "rt") as f:
        for line in f:
            n += 1
            if n % 5000000 == 0:
                print(f"  ol works {n}", file=sys.stderr, flush=True)
            cols = line.split("\t", 2)
            if len(cols) < 2 or cols[1] not in keep:
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
            meta[cols[1]] = (data.get("title") or "", akeys[0] if akeys else None)
    need = {a for _, a in meta.values() if a}
    names = {}
    with gzip.open(os.path.join(d, "ol_dump_authors_latest.txt.gz"), "rt") as f:
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
    out = collections.defaultdict(lambda: ["", None, 0])
    for wkey, (title, akey) in meta.items():
        author = names.get(akey) if akey else None
        k = (title_key(title), surname(author))
        if not k[0]:
            continue
        e = out[k]
        if not e[0]:
            e[0], e[1] = title, author
        e[2] += intent[wkey]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--goodreads", required=True)
    ap.add_argument("--amazon")
    ap.add_argument("--ol", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--merged", required=True)
    ap.add_argument("--pool", help="ranked.jsonl of the public-domain pool")
    ap.add_argument("--views", help="views.jsonl: {title, author, views} per book article")
    ap.add_argument("--kb", type=int, default=300)
    args = ap.parse_args()

    merged = collections.defaultdict(lambda: {"title": "", "author": None, "gr": 0, "toread": 0, "az": 0, "ol": 0, "pv": 0, "year": ""})

    def slot(k, title, author):
        e = merged[k]
        cur = e["title"]
        better = (not cur) or (cur.startswith("[") and not title.startswith("[")) or \
                 (not title.startswith("[") and 0 < len(title) < len(cur) and not cur.startswith("["))
        if better and title:
            e["title"] = title
        if not e["author"] and author:
            e["author"] = author
        return e

    print("goodreads", file=sys.stderr, flush=True)
    gr = load_goodreads(args.goodreads)
    for w in gr.values():
        k = (title_key(w["title"]), surname(w["author"]))
        if not k[0]:
            continue
        e = slot(k, w["title"], w["author"])
        e["gr"] += w["ratings"]
        e["toread"] += w["toread"]
        if not e["year"]:
            e["year"] = w["year"]
    del gr
    if args.amazon:
        print("amazon", file=sys.stderr, flush=True)
        for k, (title, author, r, alt) in load_amazon(args.amazon).items():
            if k not in merged and alt and alt[1] and alt in merged:
                k = alt  # the surname-first reading is the book Goodreads knows
            e = slot(k, title, author)
            e["az"] = max(e["az"], r)
    print("open library", file=sys.stderr, flush=True)
    for k, (title, author, n) in load_ol(args.ol).items():
        e = slot(k, title, author)
        e["ol"] += n
    if args.views:
        for line in open(args.views):
            d = json.loads(line)
            k = (title_key(d["title"]), surname(d.get("author")))
            if k in merged:
                merged[k]["pv"] += d["views"]

    tot = {s: (sum(e[s] for e in merged.values()) or 1) for s in W}
    weights = {s: w for s, w in W.items() if tot[s] > 1}
    wsum = sum(weights.values())
    for e in merged.values():
        e["value"] = 1e6 * sum(weights[s] * e[s] / tot[s] for s in weights) / wsum
    ranked = sorted(merged.items(), key=lambda kv: -kv[1]["value"])
    total = sum(e["value"] for _, e in ranked)
    print(f"{len(ranked)} merged works", file=sys.stderr, flush=True)

    with open(args.merged, "w") as f:
        for i, (k, e) in enumerate(ranked, 1):
            f.write(json.dumps({"rank": i, "key": list(k), **e}, ensure_ascii=False) + "\n")

    pool_share = None
    if args.pool:
        keys = set()
        for line in open(args.pool):
            r = json.loads(line)
            keys.add((title_key(r["title"]), surname(r["creators"][0] if r.get("creators") else "")))
        pool_share = sum(e["value"] for k, e in ranked if k in keys) / total

    out = []
    p = out.append
    p("# Every book, ranked on Goodreads, Amazon and Open Library together\n")
    p(f"{len(ranked):,} merged works. Sources and totals: Goodreads ratings {tot['gr']:,} over 2.4 M books "
      f"(site-wide counters, crawled 2017); Amazon review counts {tot['az']:,} over 4.4 M books (to 2023); "
      f"Open Library intent {tot['ol']:,} events (to 2026)"
      + (f"; Wikipedia views {tot['pv']:,}" if 'pv' in weights else "") + ". Weights: "
      + ", ".join(f"{s} {w / wsum:.2f}" for s, w in weights.items()) + ".\n")
    if pool_share is not None:
        p(f"**The public-domain pool a site can copy today carries {100 * pool_share:.1f}% of this value.**\n")

    p("## Each source's own top 20\n")
    p("So the mixture can be judged against its parts.\n")
    cols = [("gr", "Goodreads ratings"), ("az", "Amazon reviews"), ("ol", "Open Library intent")] + ([("pv", "Wikipedia views")] if 'pv' in weights else [])
    tops = {s: sorted(merged.values(), key=lambda e: -e[s])[:20] for s, _ in cols}
    p("| # | " + " | ".join(n for _, n in cols) + " |")
    p("| --- | " + " | ".join("---" for _ in cols) + " |")
    for i in range(20):
        p(f"| {i + 1} | " + " | ".join(f"{tops[s][i]['title'][:38]} ({tops[s][i][s]:,})" for s, _ in cols) + " |")
    p("")

    p("## How concentrated it is\n")
    p("| top N works | share of all value | \n| --- | --- |")
    cum = 0
    marks = {}
    for i, (_, e) in enumerate(ranked, 1):
        cum += e["value"]
        if i in CURVE_N:
            marks[i] = cum
    for n in CURVE_N:
        if n in marks:
            p(f"| {n:,} | {100 * marks[n] / total:.1f}% |")
    p("")

    p(f"## What fits, at {args.kb} KB per book\n")
    cum_at = []
    cum = 0
    for _, e in ranked:
        cum += e["value"]
        cum_at.append(cum)
    p("| card | " + " | ".join(f"fill to {s}%" for s in SLIDER) + " |")
    p("| --- | " + " | ".join("---" for _ in SLIDER) + " |")
    for card in CARDS_GB:
        cells = []
        for s in SLIDER:
            books = int(card * 1e9 * s / 100 / (args.kb * 1000))
            share = cum_at[min(books, len(cum_at)) - 1] / total
            cells.append(f"{books:,} books, {100 * share:.0f}%")
        p(f"| {card} GB | " + " | ".join(cells) + " |")
    p("")

    p("## The top 100\n")
    p("| # | title | author | year | Goodreads ratings | Amazon reviews | OL intent |\n| --- | --- | --- | --- | --- | --- | --- |")
    for i, (_, e) in enumerate(ranked[:100], 1):
        p(f"| {i} | {e['title'][:55]} | {(e['author'] or '?')[:30]} | {e['year']} | {e['gr']:,} | {e['az']:,} | {e['ol']:,} |")
    p("")
    open(args.out, "w").write("\n".join(out) + "\n")
    print(f"wrote {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
