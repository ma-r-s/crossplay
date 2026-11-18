"""A ZIM file as the pack builder's input: every article converted, every
redirect listed, on a worker pool.

    articles, redirects, meta = convert_zim(path, profile, stats)

`articles` is {title: (headings, xhtml)}, the shape build_pack.convert_rows
returns; `redirects` is [(title, target)], both real redirects and the
pages the ZIM writes as a link to a section of another ("Schwarze Blattern"
is Pocken#Erreger: the section is lost, the article is not); `meta` carries
the snapshot date and the language. build_pack.py --zim is the command.
"""

import os
import re
import sys
import time
import urllib.parse
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import languages  # noqa: E402
import zim  # noqa: E402
import zim_article  # noqa: E402
from pack_format import MAX_TITLE_BYTES  # noqa: E402

_STUB_TARGET = re.compile(rb'<meta http-equiv="refresh" content="0;URL=\'\./([^\'#"]+)')

_worker = {}


def _init(path, code):
    _worker["zim"] = zim.Zim(path)
    _worker["profile"] = languages.profile(code)


def _convert(batch):
    """One cluster's articles: [(title, headings, xhtml) | ("stub", title, target)], stats."""
    z = _worker["zim"]
    profile = _worker["profile"]
    stats = {}
    out = []
    for index in batch:
        e = z.entry(index)
        html = z.cluster(e.cluster)[e.blob]
        if b"mw-parser-output" not in html:
            m = _STUB_TARGET.search(html[:600])
            if m:
                target = urllib.parse.unquote(
                    m.group(1).decode("utf-8", "replace")
                ).replace("_", " ")
                out.append(("stub", e.title, target))
            else:
                stats["pages_skipped"] = stats.get("pages_skipped", 0) + 1
            continue
        try:
            title, headings, xhtml = zim_article.zim_xhtml(
                html, e.title, profile, stats
            )
        except (ValueError, RecursionError) as err:
            stats["articles_refused"] = stats.get("articles_refused", 0) + 1
            print(f"refused {e.title!r}: {err}", file=sys.stderr)
            continue
        out.append((title, headings, xhtml))
    return out, stats


def _merge(into, stats):
    for k, v in stats.items():
        if isinstance(v, int):
            into[k] = into.get(k, 0) + v
        elif isinstance(v, dict):
            d = into.setdefault(k, {})
            for kk, vv in v.items():
                d[kk] = d.get(kk, 0) + vv


def convert_zim(path, code=None, stats=None, workers=None, limit=None, log=None):
    if stats is None:
        stats = {}
    say = log or (lambda m: print(m, file=sys.stderr, flush=True))
    z = zim.Zim(path)
    lang = code or languages.from_zim_language(z.metadata("Language"))
    meta = {
        "lang": lang,
        "snapshot": (z.metadata("Date") or "")[:10] or None,
        "source": os.path.basename(path),
        "name": z.metadata("Name"),
    }
    fronts = z.front_articles()
    main = z.entry(z.main_page) if z.main_page < z.entry_count else None
    main = z.resolve(main) if main is not None else None
    if main is not None:
        fronts = [e for e in fronts if e.index != main.index]
    if limit:
        fronts = fronts[:limit]
    # one task per cluster, so each worker decompresses a cluster once
    batches = []
    cur = None
    for e in fronts:
        if cur is None or e.cluster != cur[0]:
            cur = (e.cluster, [])
            batches.append(cur)
        cur[1].append(e.index)
    say(f"{len(fronts):,} pages in {len(batches):,} clusters, language {lang}")
    articles = {}
    stubs = []
    t0 = time.time()
    done = 0
    with ProcessPoolExecutor(
        max_workers=workers or os.cpu_count(), initializer=_init, initargs=(path, lang)
    ) as pool:
        for out, st in pool.map(_convert, [b for _, b in batches], chunksize=4):
            _merge(stats, st)
            for item in out:
                if item[0] == "stub":
                    stubs.append((item[1], item[2]))
                    continue
                title, headings, xhtml = item
                if len(title.encode("utf-8")) > MAX_TITLE_BYTES:
                    stats["titles_too_long"] = stats.get("titles_too_long", 0) + 1
                    continue
                if title in articles:
                    stats["duplicates_dropped"] = stats.get("duplicates_dropped", 0) + 1
                    continue
                articles[title] = (headings, xhtml)
            done += 1
            if done % 200 == 0:
                say(
                    f"  {done:,}/{len(batches):,} clusters, {len(articles):,} articles, {time.time() - t0:.0f}s"
                )
    stats["rows"] = len(fronts)
    stats["section_redirects"] = len(stubs)
    redirects = list(z.redirects()) + stubs
    say(
        f"{len(articles):,} articles, {len(redirects):,} redirects ({len(stubs):,} to a section), {time.time() - t0:.0f}s"
    )
    return articles, redirects, meta
