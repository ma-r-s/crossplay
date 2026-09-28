#!/usr/bin/env python3
"""Cut the ranked pool into the shards the library page copies to a card.

    build_pack.py --ranked ranked.jsonl --epubs DIR --out DIR [--shard-mb 500] [--limit N]

DIR/<id>/pg<id>.epub is the rsync mirror of Gutenberg's text-only EPUBs
(gutenberg.pglaf.org::gutenberg-epub, the route their terms ask bulk
users to take). Books are taken in rank order (value per byte, from
rank.py), each given its path on the card,

    /Library/<lang>/<Initial>/<Author, Name>/<Title>.epub

with ASCII names cut to the budgets the reader's own downloader uses
(author 34 bytes, whole name 100), and written into uncompressed tar
shards of about --shard-mb in that order, so a partial copy is the most
useful prefix. The manifest lists every file with its shard and byte
offset, so the page can stream a shard and write files as they pass, and
resume by checking which paths already exist at their size. A book whose
file the mirror does not have yet is skipped and counted, never invented.

Writes DIR/manifest.json, DIR/shards/NNN.tar, and prints the largest
folders, which is the number the device-side layout decision needs.
"""

import argparse
import collections
import datetime
import hashlib
import io
import json
import os
import re
import sys
import tarfile
import unicodedata

RESERVED = {"con", "prn", "aux", "nul"} | {f"com{i}" for i in range(1, 10)} | {f"lpt{i}" for i in range(1, 10)}
AUTHOR_BUDGET = 34
NAME_BUDGET = 100


def ascii_name(s, budget):
    s = unicodedata.normalize("NFKD", s or "")
    s = "".join(c for c in s if not unicodedata.combining(c))
    s = s.encode("ascii", "ignore").decode()
    s = re.sub(r'[\\/:*?"<>|\x00-\x1f]+', "-", s)
    s = re.sub(r"\s+", " ", s).strip(" .")
    if not s:
        s = "book"
    if s.lower().split(".")[0] in RESERVED:
        s = "_" + s
    return s[:budget].rstrip(" .")


def card_path(r):
    lang = r["lang"] or "und"
    author = (r["creators"][0] if r.get("creators") else "") or "Anonymous"
    author = ascii_name(author, AUTHOR_BUDGET)
    initial = author[0].upper() if author[0].isalpha() else "0"
    title = re.split(r"[\n]", r["title"] or "Untitled", maxsplit=1)[0]
    title = ascii_name(title, NAME_BUDGET - len(".epub"))
    return f"Library/{lang}/{initial}/{author}/{title}.epub"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ranked", required=True)
    ap.add_argument("--epubs", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--shard-mb", type=int, default=500)
    ap.add_argument("--limit", type=int, default=0, help="stop after this many books (0 = all)")
    args = ap.parse_args()

    os.makedirs(os.path.join(args.out, "shards"), exist_ok=True)
    shard_bytes = args.shard_mb * 1000 * 1000
    files, shards = [], []
    seen_paths = set()
    folder_sizes = collections.Counter()
    missing = 0
    tar = None
    shard_no = -1
    shard_path = None
    shard_written = 0

    def open_shard():
        nonlocal tar, shard_no, shard_path, shard_written
        close_shard()
        shard_no += 1
        shard_path = os.path.join(args.out, "shards", f"{shard_no:03d}.tar")
        tar = tarfile.open(shard_path, "w", format=tarfile.USTAR_FORMAT)
        shard_written = 0

    def close_shard():
        nonlocal tar
        if tar is None:
            return
        tar.close()
        tar = None
        h = hashlib.sha256()
        with open(shard_path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        shards.append({"name": os.path.basename(shard_path), "size": os.path.getsize(shard_path), "sha256": h.hexdigest()})

    for line in open(args.ranked):
        r = json.loads(line)
        if args.limit and len(files) >= args.limit:
            break
        src = os.path.join(args.epubs, str(r["id"]), f"pg{r['id']}.epub")
        if not os.path.exists(src):
            missing += 1
            continue
        path = card_path(r)
        base, ext = path[:-5], ".epub"
        n = 2
        while path in seen_paths:  # two works with one sanitised name: number the second
            path = f"{base} ({n}){ext}"
            n += 1
        seen_paths.add(path)
        size = os.path.getsize(src)
        if tar is None or (shard_written + size > shard_bytes and shard_written > 0):
            open_shard()
        info = tarfile.TarInfo(name=path)
        info.size = size
        info.mtime = 0
        # tarfile records where the header went; the bytes start one 512-byte
        # header later (ustar names fit in the header, so never a long-name block).
        offset = tar.offset + 512
        with open(src, "rb") as f:
            tar.addfile(info, f)
        shard_written += 512 + size + (-size % 512)
        folder_sizes[os.path.dirname(path)] += 1
        files.append({"rank": r["rank"], "id": r["id"], "path": path, "size": size, "shard": shard_no, "offset": offset,
                      "title": r["title"], "author": (r["creators"][0] if r.get("creators") else None), "lang": r["lang"],
                      "value": r.get("value", r.get("downloads"))})
        if len(files) % 5000 == 0:
            print(f"{len(files)} books, shard {shard_no}", file=sys.stderr, flush=True)
    close_shard()

    total = sum(f["size"] for f in files)
    manifest = {"pack": "library", "built": datetime.date.today().isoformat(), "books": len(files), "bytes": total,
                "shardBytes": shard_bytes, "shards": shards, "files": files}
    with open(os.path.join(args.out, "manifest.json"), "w") as f:
        json.dump(manifest, f, ensure_ascii=False)
    print(f"{len(files)} books, {total / 1e9:.2f} GB, {len(shards)} shards; {missing} ranked books not in the mirror yet",
          file=sys.stderr)
    print("largest folders:", file=sys.stderr)
    for folder, n in folder_sizes.most_common(8):
        print(f"  {n:6d}  {folder}", file=sys.stderr)
    print("authors per letter folder (top 5):", file=sys.stderr)
    authors_per_letter = collections.Counter()
    for folder in folder_sizes:
        authors_per_letter[os.path.dirname(folder)] += 1
    for folder, n in authors_per_letter.most_common(5):
        print(f"  {n:6d}  {folder}", file=sys.stderr)


if __name__ == "__main__":
    main()
