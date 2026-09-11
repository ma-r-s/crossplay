#!/usr/bin/env python3
"""Wikipedia's Vital Articles, levels 1 to 5, as {title: level}.

The data lives on Wikipedia:Vital_articles/data/A.json ... Z.json and
others.json (one object per title, with "level"). fetch_all() reads them
with ?action=raw, caches every page under cache/vital/ and writes the
merged cache/vital.json, which is what build_pack.py orders articles by.

    python3 tools_local/wikipedia/vital.py            # fetch or refresh, print counts
"""

import json
import os
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE_DIR = os.path.join(HERE, "cache")
PAGES = [chr(c) for c in range(ord("A"), ord("Z") + 1)] + ["others"]
URL = "https://en.wikipedia.org/wiki/Wikipedia:Vital_articles/data/{}.json?action=raw"
USER_AGENT = "CrossPlay-wikipedia-tools/0.1 (https://crossplay.ma-r-s.com)"


def fetch_page(name, cache_dir=CACHE_DIR):
    """The raw page as a dict, from cache or the wiki. None for a page that
    does not exist (404)."""
    d = os.path.join(cache_dir, "vital")
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, f"{name}.json")
    if os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            return json.load(f)
    req = urllib.request.Request(URL.format(name), headers={"User-Agent": USER_AGENT})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                raw = r.read()
            break
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            if attempt == 3:
                raise
            time.sleep(2 * (attempt + 1))
        except urllib.error.URLError:
            if attempt == 3:
                raise
            time.sleep(2 * (attempt + 1))
    data = json.loads(raw.decode("utf-8"))
    with open(p + ".part", "wb") as f:
        f.write(raw)
    os.replace(p + ".part", p)
    return data


def fetch_all(cache_dir=CACHE_DIR, log=None):
    """{title: level} over every page, written to cache_dir/vital.json."""
    merged = {}
    for name in PAGES:
        page = fetch_page(name, cache_dir)
        if page is None:
            if log:
                log(f"vital: {name}.json does not exist")
            continue
        n = 0
        for title, info in page.items():
            level = info.get("level") if isinstance(info, dict) else None
            if isinstance(level, int) and 1 <= level <= 5:
                if title not in merged or level < merged[title]:
                    merged[title] = level
                n += 1
        if log:
            log(f"vital: {name}.json {n} titles")
    out = os.path.join(cache_dir, "vital.json")
    with open(out + ".part", "w", encoding="utf-8") as f:
        json.dump(merged, f, ensure_ascii=False, indent=0, sort_keys=True)
    os.replace(out + ".part", out)
    return merged


def load(path):
    """{title: level} from the merged file, or from a raw data page."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    out = {}
    for title, v in data.items():
        level = v.get("level") if isinstance(v, dict) else v
        if isinstance(level, int) and 1 <= level <= 5:
            out[title] = level
    return out


if __name__ == "__main__":
    levels = fetch_all(log=lambda s: print(s, file=sys.stderr))
    counts = {}
    for lv in levels.values():
        counts[lv] = counts.get(lv, 0) + 1
    print(
        f"{len(levels)} vital titles: "
        + ", ".join(f"level {k}: {counts[k]}" for k in sorted(counts))
    )
