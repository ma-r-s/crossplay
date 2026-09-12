#!/usr/bin/env python3
"""Build the Wikipedia pack from structured-wikipedia rows.

    build_pack.py --rows rows.jsonl.gz [--rows more.jsonl] --out DIR
                  [--vital vital.json | --no-vital] [--tier essentials=N]
                  [--redirects redirects.tsv] [--snapshot YYYY-MM-DD]

Reads rows (one JSON object per line, jsonl or jsonl.gz, the columns of
wikimedia/structured-wikipedia), converts each with article_html.py, orders
them by importance (Vital Articles level 1 to 5 first, then everything else
by folded title), trains the dictionary on a sample, and writes the pack
with pack_format.py. Prints a summary; --summary-json keeps it.

Every row is held in memory as its XHTML until the write, which is fine for
the essentials (fifty thousand articles, half a gigabyte) and is the limit
of this tool: the full seven-million-article pack needs an external sort
and a streaming pass, which is v2 of this file.

Redirects: the source rows carry no redirect list, so a pack built from
them alone has none. --redirects takes a TSV of "redirect<TAB>target"; a
redirect whose target is not in the pack, or whose title collides with an
article, is dropped and counted.

Parquet input is not supported here: pyarrow is not installed on this
machine, and the tool has no dependencies. build_essentials.sh shows how
the parquet files are turned into rows first.
"""

import argparse
import datetime
import gzip
import json
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import pack_format as pf  # noqa: E402
import vital  # noqa: E402
from article_html import article_xhtml, person_alias, strip_unknown_links  # noqa: E402
from fold import fold, fold_bytes  # noqa: E402


def say(msg):
    print(msg, file=sys.stderr, flush=True)


def read_rows(paths):
    for path in paths:
        opener = gzip.open if path.endswith(".gz") else open
        with opener(path, "rt", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    yield json.loads(line)


def row_stamp(row):
    return row.get("date_modified") or ""


def convert_rows(paths, stats, limit=None):
    """{title: (headings, xhtml, snapshot_date)} with duplicates resolved
    to the latest date_modified."""
    articles = {}
    stamps = {}
    aliases = {}
    n = 0
    for row in read_rows(paths):
        n += 1
        stats["rows"] = n
        if limit and len(articles) >= limit:
            break
        if not (row.get("name") or "").strip():
            stats["rows_without_name"] = stats.get("rows_without_name", 0) + 1
            continue
        try:
            title, headings, xhtml = article_xhtml(row, stats)
        except (ValueError, TypeError) as e:
            stats["rows_refused"] = stats.get("rows_refused", 0) + 1
            say(f"refused row {n}: {e}")
            continue
        if len(title.encode("utf-8")) > pf.MAX_TITLE_BYTES:
            stats["titles_too_long"] = stats.get("titles_too_long", 0) + 1
            continue
        stamp = row_stamp(row)
        if title in articles:
            stats["duplicates_dropped"] = stats.get("duplicates_dropped", 0) + 1
            if stamp <= stamps[title]:
                continue
        articles[title] = (headings, xhtml)
        stamps[title] = stamp
        alias = person_alias(row)
        if alias:
            aliases[title] = alias
        if n % 1000 == 0:
            say(f"  {n:,} rows, {len(articles):,} articles")
    stats["snapshot"] = max(stamps.values(), default="")[:10] or None
    stats["aliases"] = aliases
    return articles


def vital_levels(args, cache_dir):
    if args.no_vital:
        return {}
    if args.vital:
        return vital.load(args.vital)
    merged = os.path.join(cache_dir, "vital.json")
    if os.path.exists(merged):
        return vital.load(merged)
    return vital.fetch_all(cache_dir, log=say)


def order_articles(articles, levels, stats):
    by_fold = {}
    for t in levels:
        by_fold.setdefault(fold(t), t)
    keyed = []
    matched = 0
    for title in articles:
        level = levels.get(title)
        if level is None:
            alias = by_fold.get(fold(title))
            level = levels.get(alias) if alias else None
        if level is not None:
            matched += 1
        keyed.append((level or 6, fold_bytes(title), title.encode("utf-8"), title))
    keyed.sort(key=lambda k: k[:3])
    stats["vital_matched"] = matched
    return [k[3] for k in keyed], matched


def read_redirects(path):
    out = []
    if not path:
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) == 2 and parts[0] and parts[1]:
                out.append((parts[0], parts[1]))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument(
        "--rows", action="append", required=True, help="jsonl or jsonl.gz, repeatable"
    )
    ap.add_argument(
        "--out",
        required=True,
        help="pack directory to write (the /wikipedia/ of the card)",
    )
    ap.add_argument(
        "--vital", help="merged {title: level} JSON (default: fetch and cache)"
    )
    ap.add_argument("--no-vital", action="store_true", help="order by title only")
    ap.add_argument(
        "--tier",
        action="append",
        default=[],
        help="name=N: the first N articles close a tier (default: essentials=<vital matches>)",
    )
    ap.add_argument("--redirects", help="TSV of redirect<TAB>target")
    ap.add_argument("--snapshot", help="YYYY-MM-DD (default: the newest date_modified)")
    ap.add_argument("--pack", default="en")
    ap.add_argument("--block-bytes", type=int, default=pf.BLOCK_TARGET)
    ap.add_argument("--shard-bytes", type=int, default=pf.SHARD_BYTES)
    ap.add_argument("--dict-bytes", type=int, default=pf.DICT_BYTES)
    ap.add_argument("--train-samples", type=int, default=20000)
    ap.add_argument(
        "--limit", type=int, help="stop after N articles (for smoke builds)"
    )
    ap.add_argument("--seed", type=int, default=20260911)
    ap.add_argument("--cache-dir", default=vital.CACHE_DIR)
    ap.add_argument("--summary-json", help="also write the summary here")
    args = ap.parse_args(argv)

    t0 = time.time()
    stats = {}
    say(f"reading {len(args.rows)} row file(s)")
    articles = convert_rows(args.rows, stats, args.limit)
    if not articles:
        sys.exit("no articles")
    say(
        f"{stats['rows']:,} rows -> {len(articles):,} articles ({stats.get('duplicates_dropped', 0)} duplicates dropped)"
    )

    levels = vital_levels(args, args.cache_dir)
    order, matched = order_articles(articles, levels, stats)
    say(f"{len(levels):,} vital titles known, {matched:,} matched")

    tiers = []
    for spec in args.tier:
        name, _, n = spec.partition("=")
        # name=all: every article is in this tier. The essentials build reads
        # rows already filtered to the Vital list, and the two whose names
        # differ from the list's spelling used to form a second tier of their
        # own: a 4 KB shard the page never copied and the reader counted as a
        # missing part.
        if n == "all":
            tiers.append((name, len(order)))
            continue
        if not n.isdigit() or int(n) <= 0:
            sys.exit(f"--tier {spec}: want name=N or name=all")
        tiers.append((name, int(n)))
    if not args.tier and 0 < matched < len(order):
        tiers = [("essentials", matched)]
    tiers = [(n, c) for n, c in tiers if c <= len(order)]

    os.makedirs(args.out, exist_ok=True)
    writer = pf.PackWriter(
        args.out,
        pack=args.pack,
        snapshot=args.snapshot or stats.get("snapshot"),
        tiers=tiers,
        block_target=args.block_bytes,
        shard_bytes=args.shard_bytes,
    )
    rng = random.Random(args.seed)
    sample_titles = (
        order
        if len(order) <= args.train_samples
        else rng.sample(order, args.train_samples)
    )
    say(f"training the dictionary on {len(sample_titles):,} articles")
    dict_bytes = pf.train_dictionary(
        (pf.encode_article(t, *articles[t]) for t in sample_titles),
        writer.dict_path,
        args.dict_bytes,
    )
    say(f"dictionary: {dict_bytes:,} bytes")

    # Every title the pack answers to, before any article is written: a link
    # to anything else is dropped to plain text, so no link in a pack is dead.
    redirects = read_redirects(args.redirects)
    known = set(articles)
    known.update(title for title, target in redirects if target in articles)
    say(f"writing {len(order):,} articles")
    for i, title in enumerate(order):
        headings, xhtml = articles[title]
        xhtml = strip_unknown_links(xhtml, known, stats)
        writer.add_article(title, headings, xhtml)
        if (i + 1) % 5000 == 0:
            say(f"  {i + 1:,} written, {len(writer.shards)} shards closed")
    kept = dropped = 0
    for title, target in redirects:
        if target in writer.by_title and writer.add_redirect(title, target):
            kept += 1
        else:
            dropped += 1
    # "Mozart, Wolfgang Amadeus": the surname entry every printed index has,
    # so a person is found by the name people know.
    names = 0
    for target, alias in stats.pop("aliases", {}).items():
        if target in writer.by_title and writer.add_redirect(alias, target):
            names += 1
    manifest = writer.finish()

    raw = writer.raw_bytes
    shard_bytes = sum(s["bytes"] for s in manifest["shards"])
    summary = {
        "out": os.path.abspath(args.out),
        "rows": stats.get("rows", 0),
        "rows_without_name": stats.get("rows_without_name", 0),
        "duplicates_dropped": stats.get("duplicates_dropped", 0),
        "titles_too_long": stats.get("titles_too_long", 0),
        "articles": manifest["articles"],
        "links_in_pack": stats.get("links_in_pack", 0),
        # What the run rules removed, most common first: the evidence the
        # symbol table and the font's ranges are grown from.
        "removed_chars": sorted(stats.get("removed_chars", {}).items(), key=lambda kv: -kv[1])[:300],
        "symbols_translated": stats.get("symbols_translated", 0),
        "diacritics_dropped": stats.get("diacritics_dropped", 0),
        "links_outside_pack": stats.get("links_outside_pack", 0),
        "vital_known": len(levels),
        "vital_matched": matched,
        "redirects_kept": kept,
        "redirects_dropped": dropped,
        "name_entries": names,
        "entries": manifest["entries"],
        "blocks": manifest["blocks"],
        "shards": len(manifest["shards"]),
        "raw_bytes": raw,
        "shard_bytes": shard_bytes,
        "ratio": round(raw / shard_bytes, 3) if shard_bytes else None,
        "dict_bytes": manifest["dict"]["bytes"],
        "blocksdir_bytes": manifest["blocksdir"]["bytes"],
        "titles_bytes": [t["bytes"] for t in manifest["titles"]],
        "tiers": manifest["tiers"],
        "snapshot": manifest["snapshot"],
        "runs_removed": stats.get("runs_removed", 0),
        "parentheticals_removed": stats.get("parentheticals_removed", 0),
        "tables_kept": stats.get("tables_kept", 0),
        "tables_omitted": stats.get("tables_omitted", 0),
        "links_kept": stats.get("links_kept", 0),
        "links_dropped": stats.get("links_dropped", 0),
        "links_unanchored": stats.get("links_unanchored", 0),
        "sections_skipped": stats.get("sections_skipped", 0),
        "facts": stats.get("facts", 0),
        "seconds": round(time.time() - t0, 1),
        "built": manifest["built"],
    }
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    if args.summary_json:
        with open(args.summary_json, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2, ensure_ascii=False)
            f.write("\n")
    say(
        f"{summary['articles']:,} articles, {summary['entries']:,} entries, {summary['blocks']:,} blocks, "
        f"{summary['shards']} shards; raw {raw:,} -> {shard_bytes:,} bytes (ratio {summary['ratio']}); "
        f"{summary['runs_removed']} undrawable runs removed; {summary['seconds']}s"
    )
    for t in manifest["tiers"]:
        say(
            f"  tier {t['name']}: {t['articles']:,} articles, {t['shards']} shards, {t['bytes']:,} bytes"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
