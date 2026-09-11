#!/usr/bin/env python3
"""A pretend pack, so the Wikipedia page can be looked at from a laptop.

Writes the files manifest.json names beside this script: dict.zst, titles.idx,
blocks.dir and three shards, all deterministic noise from a fixed seed, and a
manifest with their real sizes and sha256s. The page uses it when opened with
?mock=1 (see site/wikipedia/wikipedia.js). Sizes are chosen so the copy is
long enough to measure (12 MB, past the projection threshold) and short enough
to be quick locally.

The data files are gitignored; this script is the record. Run it once per
checkout:

    python3 site/wikipedia/mock/make_mock.py

The manifest it writes IS committed, because it is what the page's own tests
and the flow check read, and it does not change unless this script does.
"""

import hashlib
import json
import pathlib
import random

HERE = pathlib.Path(__file__).resolve().parent

FILES = [
    ("dict.zst", 110_000),
    ("titles.idx", 6_000_000),
    ("blocks.dir", 48_000),
    ("shards/000.blk", 2_000_000),
    ("shards/001.blk", 2_000_000),
    ("shards/002.blk", 2_000_000),
]


def write(rel, size, seed):
    path = HERE / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    data = random.Random(seed).randbytes(size)
    path.write_bytes(data)
    return {"file": rel, "bytes": size, "sha256": hashlib.sha256(data).hexdigest()}


def main():
    entries = {rel: write(rel, size, i + 1) for i, (rel, size) in enumerate(FILES)}
    shards = []
    for i in range(3):
        e = entries[f"shards/{i:03d}.blk"]
        shards.append({**e, "firstBlock": i * 30, "blocks": 30})
    small = (
        entries["dict.zst"]["bytes"]
        + entries["titles.idx"]["bytes"]
        + entries["blocks.dir"]["bytes"]
    )
    manifest = {
        "format": 1,
        "pack": "en-mock",
        "snapshot": "2026-05-13",
        "built": "2026-09-11T02:00:00Z",
        "articles": 7238251,
        "entries": 19217771,
        "blocks": 90,
        "dict": entries["dict.zst"],
        "titles": entries["titles.idx"],
        "blocksdir": entries["blocks.dir"],
        "shards": shards,
        "tiers": [
            {
                "name": "essentials",
                "shards": 1,
                "articles": 49938,
                "bytes": small + shards[0]["bytes"],
            },
            {
                "name": "all",
                "shards": 3,
                "articles": 7238251,
                "bytes": small + sum(s["bytes"] for s in shards),
            },
        ],
    }
    (HERE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    total = sum(size for _, size in FILES)
    print(f"mock pack: {len(FILES)} files, {total / 1e6:.1f} MB, manifest written")


if __name__ == "__main__":
    main()
