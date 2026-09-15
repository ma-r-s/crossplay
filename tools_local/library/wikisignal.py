#!/usr/bin/env python3
"""Page views for every Gutenberg ebook that has a Wikipedia article.

    wikisignal.py --out wiki.jsonl [--months 12] [--workers 8]

Asks Wikidata (SPARQL) for every item carrying a Project Gutenberg ebook id
(P2034) and its Wikipedia articles in the languages the library ships, then
sums each article's monthly page views by humans (agent=user) over the last
N months through the Wikimedia REST API. Writes one line per Gutenberg id:
{"id": 1342, "views": 1543671, "articles": ["en:Pride_and_Prejudice"]}.

This is the "heard of" signal for the head of the list: a book people look
up. It covers only titles with a Wikidata link (a few thousand), and that
is the point: the tail has no such link because nobody hears of it.
"""

import argparse
import datetime
import json
import re
import sys
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

UA = "CrossPlayLibrary/0.1 (https://crossplay.ma-r-s.com; library research)"
LANGS = ["en", "es", "fr", "de", "it", "pt", "nl", "fi"]
SPARQL = """
SELECT ?pg ?article WHERE {
  ?item wdt:P2034 ?pg .
  OPTIONAL {
    ?article schema:about ?item ; schema:inLanguage ?lang .
    FILTER(?lang IN (%s))
    FILTER(STRSTARTS(STR(?article), "https://"))
    FILTER(CONTAINS(STR(?article), ".wikipedia.org/"))
  }
}
""" % ", ".join('"%s"' % l for l in LANGS)


def get(url, timeout=60):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def sparql():
    url = "https://query.wikidata.org/sparql?format=json&query=" + urllib.parse.quote(SPARQL)
    data = get(url, timeout=300)
    by_id = {}
    for b in data["results"]["bindings"]:
        pg = b["pg"]["value"]
        try:
            pg = int(pg)
        except ValueError:
            continue
        arts = by_id.setdefault(pg, set())
        if "article" in b:
            u = b["article"]["value"]  # https://en.wikipedia.org/wiki/Title
            host, _, title = u.partition("/wiki/")
            lang = host.split("//", 1)[1].split(".", 1)[0]
            arts.add((lang, urllib.parse.unquote(title)))
    return by_id


def views(lang, title, start, end):
    t = urllib.parse.quote(title.replace(" ", "_"), safe="")
    url = (f"https://wikimedia.org/api/rest_v1/metrics/pageviews/per-article/"
           f"{lang}.wikipedia/all-access/user/{t}/monthly/{start}/{end}")
    for attempt in range(5):
        try:
            data = get(url)
            return sum(item["views"] for item in data.get("items", []))
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return 0
            time.sleep(5 * (attempt + 1) if e.code == 429 else 1 + attempt)
        except Exception:
            time.sleep(1 + attempt)
    return None


def fold(t):
    import unicodedata
    t = unicodedata.normalize("NFKD", t or "")
    t = "".join(c for c in t if not unicodedata.combining(c)).lower()
    t = re.sub(r"\(.*?\)", " ", t)          # "(novel)", "(1897 novel)"
    t = re.sub(r"[^a-z0-9 ]+", " ", t)
    t = re.sub(r"^(the|a|an) ", "", t.strip())
    return re.sub(r"\s+", " ", t).strip()


BOOKISH = re.compile(r"novel|book|play|poem|poetry|story|stories|essay|epic|memoir|autobiograph|"
                     r"novella|comedy|tragedy|treatise|dialogue|libro|roman|Roman|romanzo|livre|"
                     r"Buch|romaani|kirja|\b1[0-9]{3}\b", re.I)


def search_article(lang, title, author):
    """The Wikipedia article for a book, by title and author, or None."""
    q = f'intitle:"{title}" {author}'.strip()
    url = (f"https://{lang}.wikipedia.org/w/api.php?action=query&list=search&format=json"
           f"&srlimit=5&srsearch=" + urllib.parse.quote(q))
    try:
        data = get(url)
    except Exception:
        return None
    want = fold(title)
    for hit in data.get("query", {}).get("search", []):
        got = fold(hit["title"])
        # The article's title must be the book's title, allowing a bracketed
        # disambiguator; "Emma (novel)" folds to "emma". Anything longer is
        # another subject that happens to contain the words, and a
        # disambiguator that names a film, album or band is another work
        # with the same name ("Wuthering Heights (2026 film)").
        if got != want:
            continue
        m = re.search(r"\(([^)]*)\)\s*$", hit["title"])
        if m and not BOOKISH.search(m.group(1)):
            continue
        return hit["title"]
    return None


def search_fallback(pool_path, by_id, top_n, workers):
    rows = [json.loads(l) for l in open(pool_path)]
    rows.sort(key=lambda r: -r.get("value", r.get("downloads", 0)))
    todo = []
    for r in rows[:top_n]:
        if r["id"] in by_id and by_id[r["id"]]:
            continue
        lang = r["lang"] if r["lang"] in LANGS else "en"
        title = re.split(r"[:;\n]", r["title"] or "", maxsplit=1)[0].strip()
        title = re.sub(r",?\s*(complete|unabridged)$", "", title, flags=re.I)
        author = (r["creators"][0].split(",")[0] if r.get("creators") else "").strip()
        if len(title) >= 2:
            todo.append((r["id"], lang, title, author))
    found = 0
    with ThreadPoolExecutor(max_workers=workers) as pool:
        for (gid, lang, title, author), hit in zip(todo, pool.map(lambda x: search_article(x[1], x[2], x[3]), todo)):
            if hit:
                by_id.setdefault(gid, set()).add((lang, hit))
                found += 1
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--months", type=int, default=12)
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--pool", help="ranked.jsonl: works without a Wikidata link are looked up by title")
    ap.add_argument("--search-top", type=int, default=8000,
                    help="how many of the pool's top works (by value) to look up by title")
    args = ap.parse_args()

    today = datetime.date.today().replace(day=1)
    end = (today - datetime.timedelta(days=1))
    start = (today - datetime.timedelta(days=1))
    for _ in range(args.months - 1):
        start = start.replace(day=1) - datetime.timedelta(days=1)
    start = start.replace(day=1)
    s, e = start.strftime("%Y%m%d"), end.strftime("%Y%m%d")

    by_id = sparql()
    if args.pool:
        found = search_fallback(args.pool, by_id, args.search_top, args.workers)
        print(f"title search added articles for {found} works", file=sys.stderr, flush=True)
    articles = sorted({a for arts in by_id.values() for a in arts})
    print(f"{len(by_id)} gutenberg ids on wikidata, {len(articles)} articles, views {s}..{e}",
          file=sys.stderr, flush=True)

    # Per-article cache beside the output, appended as results land, so a
    # rerun after a rate-limited or interrupted run fetches only what is missing.
    cache_path = args.out + ".cache"
    cache = {}
    try:
        for line in open(cache_path):
            d = json.loads(line)
            cache[(d["lang"], d["title"])] = d["views"]
    except FileNotFoundError:
        pass
    todo = [a for a in articles if a not in cache]
    print(f"{len(cache)} cached, {len(todo)} to fetch, window {s}..{e}", file=sys.stderr, flush=True)
    with ThreadPoolExecutor(max_workers=args.workers) as pool, open(cache_path, "a") as cf:
        done = 0
        for (lang, title), v in zip(todo, pool.map(lambda a: views(a[0], a[1], s, e), todo)):
            done += 1
            if v is not None:
                cache[(lang, title)] = v
                cf.write(json.dumps({"lang": lang, "title": title, "views": v}) + "\n")
                cf.flush()
            if done % 500 == 0:
                print(f"{done} fetched, {len(cache)} known", file=sys.stderr, flush=True)

    failed = [a for a in articles if a not in cache]
    with open(args.out, "w") as out:
        for pg, arts in sorted(by_id.items()):
            known = [a for a in arts if a in cache]
            if not known:
                continue  # unknown, not zero: leave the id out rather than demote it
            out.write(json.dumps({"id": pg, "views": sum(cache[a] for a in known),
                                  "articles": [f"{l}:{t}" for l, t in sorted(known)],
                                  "unfetched": len(arts) - len(known)}) + "\n")
    print(f"wrote rows for ids with any fetched article; {len(failed)} articles still unfetched", file=sys.stderr)


if __name__ == "__main__":
    main()
