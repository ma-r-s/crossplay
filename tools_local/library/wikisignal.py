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
    for attempt in range(3):
        try:
            data = get(url)
            return sum(item["views"] for item in data.get("items", []))
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return 0
            time.sleep(1 + attempt)
        except Exception:
            time.sleep(1 + attempt)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--months", type=int, default=12)
    ap.add_argument("--workers", type=int, default=8)
    args = ap.parse_args()

    today = datetime.date.today().replace(day=1)
    end = (today - datetime.timedelta(days=1))
    start = (today - datetime.timedelta(days=1))
    for _ in range(args.months - 1):
        start = start.replace(day=1) - datetime.timedelta(days=1)
    start = start.replace(day=1)
    s, e = start.strftime("%Y%m%d"), end.strftime("%Y%m%d")

    by_id = sparql()
    articles = sorted({a for arts in by_id.values() for a in arts})
    print(f"{len(by_id)} gutenberg ids on wikidata, {len(articles)} articles, views {s}..{e}",
          file=sys.stderr, flush=True)

    cache = {}
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for (lang, title), v in zip(articles, pool.map(lambda a: views(a[0], a[1], s, e), articles)):
            cache[(lang, title)] = v
            if len(cache) % 500 == 0:
                print(f"{len(cache)} articles fetched", file=sys.stderr, flush=True)

    missing = sum(1 for v in cache.values() if v is None)
    with open(args.out, "w") as out:
        for pg, arts in sorted(by_id.items()):
            total = sum(cache.get(a) or 0 for a in arts)
            out.write(json.dumps({"id": pg, "views": total,
                                  "articles": [f"{l}:{t}" for l, t in sorted(arts)]}) + "\n")
    print(f"wrote {len(by_id)} rows; {missing} articles failed", file=sys.stderr)


if __name__ == "__main__":
    main()
