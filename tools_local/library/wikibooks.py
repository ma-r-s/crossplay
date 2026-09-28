#!/usr/bin/env python3
"""Every book with a Wikipedia article, from Wikidata: title, author, language.

    wikibooks.py --out wikibooks.jsonl [--langs en,es,...]

Pages through Wikidata for items that are a novel, literary work, short
story, play, poem, novella, essay, story collection or book, and have an
article in each language asked for, writing one line per article:
{"qid": "Q170583", "lang": "en", "title": "Pride and Prejudice", "author": "Jane Austen"}.
This is the key that lets a page-view dump be joined to books by article
title without guessing, which the title-only match cannot do for "It" or
"Emma". Progress and any query that times out go to stderr; the output is
appended per page so a rerun can resume from what is there.
"""

import argparse
import json
import sys
import time
import urllib.parse
import urllib.request

UA = "CrossPlayLibrary/0.1 (https://crossplay.ma-r-s.com; library research)"
# Wikidata models a book as a "literary work" whose form (novel, play...) is
# a separate property, so the old novel class Q8261 has no instances. Two
# classes cover the books with articles; the endpoint answers a 10,000-row
# page in about 45 s and times out on deep offsets, so the run goes as far
# as it gets, oldest items (the notable ones) first, and says where it stopped.
CLASSES = {"Q7725634": "literary work", "Q571": "book"}
PAGE = 10000
QUERY = """
SELECT ?item ?title ?author WHERE {
  ?item wdt:P31 wd:%s .
  ?article schema:about ?item ; schema:isPartOf <https://%s.wikipedia.org/> ; schema:name ?title .
  OPTIONAL { ?item wdt:P50 ?a . ?a rdfs:label ?author FILTER(LANG(?author) = "%s") }
}
ORDER BY ?item
LIMIT %d OFFSET %d
"""


def run(query):
    url = "https://query.wikidata.org/sparql?format=json&query=" + urllib.parse.quote(query)
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/json"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                return json.load(r)["results"]["bindings"]
        except Exception as e:
            print(f"  retry {attempt + 1}: {e}", file=sys.stderr, flush=True)
            time.sleep(10 * (attempt + 1))
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--langs", default="en,es,fr,de,it,pt,nl,fi")
    args = ap.parse_args()
    seen = set()
    try:
        for line in open(args.out):
            d = json.loads(line)
            seen.add((d["qid"], d["lang"]))
    except FileNotFoundError:
        pass
    total = len(seen)
    with open(args.out, "a") as out:
        for lang in args.langs.split(","):
            for cls, name in CLASSES.items():
                offset = 0
                while True:
                    rows = run(QUERY % (cls, lang, lang, PAGE, offset))
                    if rows is None:
                        print(f"{lang} {name}: gave up at offset {offset}", file=sys.stderr, flush=True)
                        break
                    new = 0
                    for b in rows:
                        qid = b["item"]["value"].rsplit("/", 1)[-1]
                        key = (qid, lang)
                        if key in seen:
                            continue
                        seen.add(key)
                        new += 1
                        out.write(json.dumps({"qid": qid, "lang": lang, "title": b["title"]["value"],
                                              "author": b.get("author", {}).get("value")}, ensure_ascii=False) + "\n")
                    out.flush()
                    total += new
                    print(f"{lang} {name}: offset {offset}, {len(rows)} rows, {new} new, {total} total",
                          file=sys.stderr, flush=True)
                    if len(rows) < PAGE:
                        break
                    offset += PAGE
    print(f"done: {total} articles", file=sys.stderr)


if __name__ == "__main__":
    main()
