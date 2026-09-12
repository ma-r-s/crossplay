#!/usr/bin/env python3
"""Build a Wikipedia pack from more rows than fit in memory.

    build_full.py --rows all.jsonl.gz --out /wikipedia --work /tmp/wkfull \\
        [--vital vital.json | --no-vital] [--tier essentials=auto|N] \\
        [--workers 8] [--limit N] [--summary-json out.json]

build_pack.py holds every article's XHTML in memory, which is fine for the
essentials (fifty thousand articles) and not for all of English Wikipedia
(7.2 million, some forty gigabytes of XHTML on a machine with 24). This is
the same build in three passes over the rows and a working directory:

  1. titles: every row's name and date_modified (the newest wins a title),
     its person alias, the Vital order and the tier boundary; from here on
     every title's place in the pack is known, so links can be judged.
  2. convert: each winning row to XHTML in a worker pool, links to titles
     outside the pack dropped to text, the record written into a bucket
     file by its place (a hundred thousand places per bucket), and a
     reservoir of records kept for the dictionary.
  3. write: the dictionary from the reservoir, then bucket by bucket, each
     sorted in memory, into the PackWriter in order; redirects and aliases
     last, as build_pack does.

The pack that comes out is the one build_pack.py would write from the same
rows (test_build_full checks that on the fixture), except that the
dictionary is trained on a different sample, so the compressed bytes differ.
"""

import argparse
import gzip
import json
import multiprocessing as mp
import os
import random
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import pack_format as pf  # noqa: E402
from article_html import article_xhtml, person_alias, strip_unknown_links  # noqa: E402
from build_pack import order_articles, read_redirects, read_rows, row_stamp, vital_levels  # noqa: E402

FRAME = struct.Struct("<IHII")  # place, title bytes, headings-json bytes, xhtml bytes


def say(msg):
    print(msg, file=sys.stderr, flush=True)


def convert(row):
    """Worker: one row to (title, headings, xhtml) or None with the reason."""
    try:
        title, headings, xhtml = article_xhtml(row, None)
    except (ValueError, TypeError) as e:
        return None, str(e)
    if len(title.encode("utf-8")) > pf.MAX_TITLE_BYTES:
        return None, "title too long"
    return (title, headings, xhtml), None


def pass_titles(paths, limit, stats):
    """{title: stamp} of the newest row per title, plus the aliases."""
    stamps = {}
    aliases = {}
    n = 0
    for row in read_rows(paths):
        n += 1
        name = (row.get("name") or "").strip()
        if not name:
            stats["rows_without_name"] = stats.get("rows_without_name", 0) + 1
            continue
        stamp = row_stamp(row)
        old = stamps.get(name)
        if old is not None:
            stats["duplicates_dropped"] = stats.get("duplicates_dropped", 0) + 1
            if stamp <= old:
                continue
        stamps[name] = stamp
        alias = person_alias(row)
        if alias:
            aliases[name] = alias
        elif name in aliases:
            del aliases[name]
        if limit and len(stamps) >= limit:
            break
        if n % 200000 == 0:
            say(f"  titles: {n:,} rows, {len(stamps):,} titles")
    stats["rows"] = n
    return stamps, aliases


def bucket_path(work, k):
    return os.path.join(work, f"bucket-{k:04d}.bin")


def pass_convert(paths, stamps, place, work, bucket_size, workers, sample_n, seed, limit, stats):
    """Every winning row to its bucket; returns the dictionary reservoir."""
    os.makedirs(work, exist_ok=True)
    done = bytearray(len(place))
    files = {}
    rng = random.Random(seed)
    reservoir = []
    seen = 0
    links_in = links_out = 0
    refused = 0
    t0 = time.time()

    def rows_to_convert():
        for row in read_rows(paths):
            name = (row.get("name") or "").strip()
            if not name or name not in place:
                continue
            if row_stamp(row) != stamps[name]:
                continue
            i = place[name]
            if done[i]:
                continue
            done[i] = 1
            yield row

    ctx = mp.get_context("fork")
    with ctx.Pool(processes=workers) as pool:
        for converted, why in pool.imap(convert, rows_to_convert(), chunksize=16):
            if converted is None:
                refused += 1
                continue
            title, headings, xhtml = converted
            i = place.get(title)
            if i is None:
                # article_xhtml may normalise the title; keep the row's place
                # by the name it was queued under only when they agree.
                refused += 1
                continue
            before = xhtml.count(b"<a href=")
            xhtml = strip_unknown_links(xhtml, place)
            after = xhtml.count(b"<a href=")
            links_in += after
            links_out += before - after
            k = i // bucket_size
            f = files.get(k)
            if f is None:
                f = files[k] = open(bucket_path(work, k), "ab")
            hb = json.dumps(headings, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            tb = title.encode("utf-8")
            f.write(FRAME.pack(i, len(tb), len(hb), len(xhtml)))
            f.write(tb)
            f.write(hb)
            f.write(xhtml)
            seen += 1
            rec = pf.encode_article(title, headings, xhtml)
            if len(reservoir) < sample_n:
                reservoir.append(rec)
            else:
                j = rng.randrange(seen)
                if j < sample_n:
                    reservoir[j] = rec
            if seen % 100000 == 0:
                say(f"  convert: {seen:,} articles, {time.time() - t0:.0f}s")
            if limit and seen >= limit:
                break
    for f in files.values():
        f.close()
    stats["rows_refused"] = refused
    stats["links_in_pack"] = links_in
    stats["links_outside_pack"] = links_out
    stats["converted"] = seen
    return reservoir, sorted(files)


def read_bucket(path):
    out = []
    with open(path, "rb") as f:
        while True:
            head = f.read(FRAME.size)
            if not head:
                break
            i, tl, hl, xl = FRAME.unpack(head)
            tb = f.read(tl)
            hb = f.read(hl)
            xb = f.read(xl)
            out.append((i, tb.decode("utf-8"), json.loads(hb.decode("utf-8")), xb))
    out.sort(key=lambda r: r[0])
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rows", action="append", required=True, help="jsonl or jsonl.gz, repeatable")
    ap.add_argument("--out", required=True, help="pack directory to write")
    ap.add_argument("--work", required=True, help="bucket directory (tens of gigabytes for all of enwiki)")
    ap.add_argument("--vital", help="merged {title: level} JSON (default: fetch and cache)")
    ap.add_argument("--no-vital", action="store_true", help="order by title only")
    ap.add_argument("--tier", action="append", default=[], help="name=N, or name=auto for the Vital matches")
    ap.add_argument("--redirects", help="TSV of redirect<TAB>target")
    ap.add_argument("--snapshot", help="YYYY-MM-DD (default: the newest date_modified)")
    ap.add_argument("--pack", default="en")
    ap.add_argument("--block-bytes", type=int, default=pf.BLOCK_TARGET)
    ap.add_argument("--shard-bytes", type=int, default=pf.SHARD_BYTES)
    ap.add_argument("--dict-bytes", type=int, default=pf.DICT_BYTES)
    ap.add_argument("--train-samples", type=int, default=20000)
    ap.add_argument("--bucket-size", type=int, default=100000, help="places per bucket file")
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--limit", type=int, help="stop after N articles (smoke builds)")
    ap.add_argument("--seed", type=int, default=20260911)
    ap.add_argument("--cache-dir", default=os.path.join(HERE, "cache"))
    ap.add_argument("--summary-json")
    args = ap.parse_args(argv)

    t0 = time.time()
    stats = {}
    say(f"pass 1, titles: {len(args.rows)} row file(s)")
    stamps, aliases = pass_titles(args.rows, args.limit, stats)
    if not stamps:
        sys.exit("no articles")
    say(f"{stats['rows']:,} rows -> {len(stamps):,} titles ({stats.get('duplicates_dropped', 0):,} duplicates)")
    levels = vital_levels(args, args.cache_dir)
    order, matched = order_articles(list(stamps), levels, stats)
    say(f"{len(levels):,} vital titles known, {matched:,} matched")
    place = {t: i for i, t in enumerate(order)}
    del order

    tiers = []
    for spec in args.tier:
        name, _, n = spec.partition("=")
        if n == "auto":
            if matched:
                tiers.append((name, matched))
            continue
        if n == "all":
            tiers.append((name, len(place)))
            continue
        if not n.isdigit() or int(n) <= 0:
            sys.exit(f"--tier {spec}: want name=N, name=auto or name=all")
        tiers.append((name, int(n)))
    if not args.tier and 0 < matched < len(place):
        tiers = [("essentials", matched)]
    tiers = [(n, c) for n, c in tiers if c <= len(place)]

    say(f"pass 2, convert: {len(place):,} articles on {args.workers} workers -> {args.work}")
    reservoir, buckets = pass_convert(
        args.rows, stamps, place, args.work, args.bucket_size, args.workers,
        args.train_samples, args.seed, args.limit, stats,
    )
    snapshot = args.snapshot or (max(stamps.values(), default="")[:10] or None)
    del stamps

    os.makedirs(args.out, exist_ok=True)
    writer = pf.PackWriter(
        args.out, pack=args.pack, snapshot=snapshot, tiers=tiers,
        block_target=args.block_bytes, shard_bytes=args.shard_bytes,
    )
    say(f"dictionary from {len(reservoir):,} sampled articles")
    dict_bytes = writer.train(reservoir)
    del reservoir
    say(f"dictionary: {dict_bytes:,} bytes")

    say(f"pass 3, write: {len(buckets)} buckets")
    written = 0
    for k in buckets:
        for _i, title, headings, xhtml in read_bucket(bucket_path(args.work, k)):
            writer.add_article(title, headings, xhtml)
            written += 1
        say(f"  bucket {k}: {written:,} written, {len(writer.shards)} shards closed")
    redirects = read_redirects(args.redirects)
    kept = dropped = 0
    for title, target in redirects:
        if target in writer.by_title and writer.add_redirect(title, target):
            kept += 1
        else:
            dropped += 1
    names = 0
    for target, alias in aliases.items():
        if target in writer.by_title and writer.add_redirect(alias, target):
            names += 1
    manifest = writer.finish()

    summary = {
        "out": os.path.abspath(args.out),
        "rows": stats.get("rows", 0),
        "rows_without_name": stats.get("rows_without_name", 0),
        "duplicates_dropped": stats.get("duplicates_dropped", 0),
        "rows_refused": stats.get("rows_refused", 0),
        "articles": manifest["articles"],
        "links_in_pack": stats.get("links_in_pack", 0),
        "links_outside_pack": stats.get("links_outside_pack", 0),
        "vital_known": len(levels),
        "vital_matched": matched,
        "redirects_kept": kept,
        "redirects_dropped": dropped,
        "name_entries": names,
        "raw_bytes": writer.raw_bytes,
        "shard_bytes": sum(s["bytes"] for s in manifest["shards"]),
        "dict_bytes": dict_bytes,
        "tiers": manifest["tiers"],
        "snapshot": snapshot,
        "seconds": round(time.time() - t0, 1),
    }
    text = json.dumps(summary, indent=2)
    if args.summary_json:
        with open(args.summary_json, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
