#!/usr/bin/env python3
"""The Wikipedia pack: writer and reference reader.

docs/apps/wikipedia-pack-format.md is the format; this module is the same
format executable. PackWriter writes manifest.json, dict.zst, blocks.dir,
titles.<tier>.idx and shards/NNN.blk; Pack opens a directory and can look a
title up, list prefix matches, fetch an article by locator, and iterate
everything, so a test can round-trip a build. The device reader
(src/apps_local/wikipedia/WikipediaCore.*) mirrors Pack.

zstd is the command-line tool (1.5.7 here) driven through subprocess:
training with `zstd --train`, frames with `zstd -19 -D dict`, which keeps
zstd's content checksum on so a corrupt frame fails to decode. No Python
package is needed.

    python3 tools_local/wikipedia/pack_format.py info <dir>
    python3 tools_local/wikipedia/pack_format.py verify <dir>
    python3 tools_local/wikipedia/pack_format.py lookup <dir> <title>
    python3 tools_local/wikipedia/pack_format.py prefix <dir> <query>
    python3 tools_local/wikipedia/pack_format.py cat <dir> <title>
"""

import bisect
import collections
import datetime
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fold import fold_bytes  # noqa: E402

FORMAT = 1
BLOCK_TARGET = 65536
SHARD_BYTES = 268435456
INDEX_BLOCK_BYTES = 4096
DICT_BYTES = 110000
ZSTD_LEVEL = 19
MAX_SLOTS = 4096  # a locator has 12 bits of slot
MAX_BLOCKS = 1 << 20  # and 20 of block index
MAX_TITLE_BYTES = 255  # an index entry's suffix is one byte long
HEADING_MAX_BYTES = 255

BLOCKSDIR_MAGIC = b"WKBD"
TITLES_MAGIC = b"WKTI"
BLOCKSDIR_HEADER = struct.Struct("<4sBBHI")  # magic, version, reserved, reserved, count
BLOCK_RECORD = struct.Struct("<HHIII")  # shard, slots, offset, csize, usize
TITLES_HEADER = struct.Struct("<4sBBHIIIII")  # 28 bytes, padded to 32
TITLES_HEADER_BYTES = 32
INDEX_ENTRY = struct.Struct("<BBBBI")  # shared, suffixLen, flags, reserved, locator
REDIRECT = 0x01


class PackError(Exception):
    pass


Entry = collections.namedtuple("Entry", "title redirect locator tier")
Article = collections.namedtuple("Article", "title headings xhtml")


def locator_of(block, slot):
    return (block << 12) | slot


def split_locator(locator):
    return locator >> 12, locator & 0xFFF


def heading_bytes(text):
    """A heading as the article header stores it: at most 255 bytes, cut on
    a character boundary."""
    b = text.encode("utf-8")
    if len(b) <= HEADING_MAX_BYTES:
        return b
    b = b[:HEADING_MAX_BYTES]
    while b and (b[-1] & 0xC0) == 0x80:
        b = b[:-1]
    return b[:-1] if b and b[-1] >= 0xC0 else b


# --- records -----------------------------------------------------------------
def encode_article(title, headings, xhtml):
    t = title.encode("utf-8")
    if not 0 < len(t) <= 0xFFFF:
        raise PackError(f"title of {len(t)} bytes")
    if len(headings) > 0xFFFF:
        raise PackError("too many headings")
    parts = [struct.pack("<H", len(t)), t, struct.pack("<H", len(headings))]
    for h in headings:
        hb = heading_bytes(h)
        parts.append(bytes([len(hb)]))
        parts.append(hb)
    parts.append(struct.pack("<I", len(xhtml)))
    parts.append(bytes(xhtml))
    return b"".join(parts)


def decode_article(buf):
    (tl,) = struct.unpack_from("<H", buf, 0)
    o = 2
    title = buf[o : o + tl].decode("utf-8")
    o += tl
    (n,) = struct.unpack_from("<H", buf, o)
    o += 2
    headings = []
    for _ in range(n):
        ln = buf[o]
        o += 1
        headings.append(buf[o : o + ln].decode("utf-8"))
        o += ln
    (xl,) = struct.unpack_from("<I", buf, o)
    o += 4
    xhtml = bytes(buf[o : o + xl])
    if len(xhtml) != xl:
        raise PackError("article record truncated")
    return Article(title, headings, xhtml)


def block_header_bytes(slots):
    return 4 + 4 * (slots + 1)


def encode_block(records):
    slots = len(records)
    if not 0 < slots <= MAX_SLOTS:
        raise PackError(f"{slots} slots in a block")
    off = block_header_bytes(slots)
    starts = []
    for r in records:
        starts.append(off)
        off += len(r)
    starts.append(off)
    return (
        struct.pack("<HH", slots, 0)
        + struct.pack(f"<{slots + 1}I", *starts)
        + b"".join(records)
    )


def decode_block(buf):
    slots, _ = struct.unpack_from("<HH", buf, 0)
    starts = struct.unpack_from(f"<{slots + 1}I", buf, 4)
    if starts[-1] != len(buf):
        raise PackError(f"block sentinel {starts[-1]} != {len(buf)} bytes")
    return [buf[starts[k] : starts[k + 1]] for k in range(slots)]


# --- zstd --------------------------------------------------------------------
def zstd_bin():
    z = shutil.which("zstd")
    if not z:
        raise PackError("zstd is not on PATH")
    return z


def train_dictionary(samples, out_path, dict_bytes=DICT_BYTES):
    """samples: iterable of bytes (article records). Writes out_path."""
    with tempfile.TemporaryDirectory(prefix="wkdict-") as d:
        n = 0
        for i, s in enumerate(samples):
            with open(os.path.join(d, f"{i:08d}"), "wb") as f:
                f.write(s)
            n += 1
        if n < 8:
            raise PackError(f"{n} training samples; zstd needs more")
        r = subprocess.run(
            [
                zstd_bin(),
                "--train",
                "-r",
                d,
                f"--maxdict={dict_bytes}",
                "-o",
                out_path,
                "-q",
                "-f",
            ],
            capture_output=True,
            text=True,
        )
        if r.returncode != 0:
            raise PackError(f"zstd --train failed: {r.stderr.strip()}")
    return os.path.getsize(out_path)


def compress_frames(raw_blocks, dict_path, level=ZSTD_LEVEL, workers=None):
    """One zstd frame per raw block, in order, compressed with the
    dictionary. Batched through the CLI: each worker gets a directory of
    inputs and one zstd process, which is 50x faster than a process per
    frame."""
    if not raw_blocks:
        return []
    workers = workers or max(1, min(8, os.cpu_count() or 1))
    with tempfile.TemporaryDirectory(prefix="wkblk-") as d:
        paths = []
        for i, raw in enumerate(raw_blocks):
            p = os.path.join(d, f"{i:08d}.raw")
            with open(p, "wb") as f:
                f.write(raw)
            paths.append(p)
        chunks = [paths[i::workers] for i in range(workers)]
        chunks = [c for c in chunks if c]

        def run(chunk):
            r = subprocess.run(
                [zstd_bin(), f"-{level}", "-D", dict_path, "-q", "-f"] + chunk,
                capture_output=True,
                text=True,
            )
            if r.returncode != 0:
                raise PackError(f"zstd failed: {r.stderr.strip()}")

        with ThreadPoolExecutor(max_workers=len(chunks)) as ex:
            list(ex.map(run, chunks))
        out = []
        for p in paths:
            with open(p + ".zst", "rb") as f:
                out.append(f.read())
    return out


def decompress_frame(frame, dict_path, usize=None):
    r = subprocess.run(
        [zstd_bin(), "-d", "-D", dict_path, "-q", "-c"],
        input=frame,
        capture_output=True,
    )
    if r.returncode != 0:
        raise PackError(
            f"frame does not decode: {r.stderr.decode(errors='replace').strip()}"
        )
    if usize is not None and len(r.stdout) != usize:
        raise PackError(
            f"frame decoded to {len(r.stdout)} bytes, directory says {usize}"
        )
    return r.stdout


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# --- titles.N.idx ------------------------------------------------------------
def common_prefix(a, b):
    n = min(len(a), len(b))
    i = 0
    while i < n and a[i] == b[i]:
        i += 1
    return i


def write_titles(path, entries, block_bytes=INDEX_BLOCK_BYTES):
    """entries: iterable of (fold_bytes, title_bytes, flags, locator),
    already sorted by (fold, title). Returns (entries, blocks)."""
    blocks = []
    sampler = []
    cur = []
    cur_len = 2
    prev = b""
    count = 0
    last_key = None
    for fb, tb, flags, locator in entries:
        key = (fb, tb)
        if last_key is not None and key <= last_key:
            if key == last_key:
                raise PackError(f"duplicate display title {tb!r}")
            raise PackError("entries are not sorted")
        last_key = key
        if len(tb) > MAX_TITLE_BYTES:
            raise PackError(f"title longer than {MAX_TITLE_BYTES} bytes: {tb[:40]!r}")
        shared = common_prefix(prev, tb) if cur else 0
        suffix = tb[shared:]
        size = INDEX_ENTRY.size + len(suffix)
        if cur and cur_len + size > block_bytes:
            blocks.append(cur)
            cur = []
            cur_len = 2
            shared = 0
            suffix = tb
            size = INDEX_ENTRY.size + len(suffix)
        if not cur:
            sampler.append(fb)
        cur.append(INDEX_ENTRY.pack(shared, len(suffix), flags, 0, locator) + suffix)
        cur_len += size
        prev = tb
        count += 1
    if cur:
        blocks.append(cur)
    sampler_bytes = struct.pack("<I", len(blocks)) + b"".join(
        bytes([len(s)]) + s for s in sampler
    )
    sampler_offset = TITLES_HEADER_BYTES + len(blocks) * block_bytes
    header = TITLES_HEADER.pack(
        TITLES_MAGIC,
        FORMAT,
        0,
        0,
        count,
        len(blocks),
        block_bytes,
        sampler_offset,
        len(sampler_bytes),
    )
    with open(path, "wb") as f:
        f.write(header.ljust(TITLES_HEADER_BYTES, b"\0"))
        for b in blocks:
            body = struct.pack("<H", len(b)) + b"".join(b)
            if len(body) > block_bytes:
                raise PackError("index block overflow")
            f.write(body.ljust(block_bytes, b"\0"))
        f.write(sampler_bytes)
    return count, len(blocks)


class TitlesIndex:
    """One titles.N.idx, open for reading: the sampler in memory, one
    4 KB block read per lookup."""

    def __init__(self, path, tier):
        self.path = path
        self.tier = tier
        self.f = open(path, "rb")
        head = self.f.read(TITLES_HEADER_BYTES)
        (
            magic,
            version,
            _flags,
            _r,
            self.entries,
            self.blocks,
            self.block_bytes,
            so,
            sb,
        ) = TITLES_HEADER.unpack(head[: TITLES_HEADER.size])
        if magic != TITLES_MAGIC:
            raise PackError(f"{path}: not a titles index")
        if version != FORMAT:
            raise PackError(f"{path}: version {version}")
        self.f.seek(so)
        raw = self.f.read(sb)
        (n,) = struct.unpack_from("<I", raw, 0)
        if n != self.blocks:
            raise PackError(f"{path}: sampler has {n} keys for {self.blocks} blocks")
        self.sampler = []
        o = 4
        for _ in range(n):
            ln = raw[o]
            o += 1
            self.sampler.append(raw[o : o + ln])
            o += ln

    def close(self):
        self.f.close()

    def read_block(self, b):
        """[(title_bytes, flags, locator), ...] of index block b."""
        self.f.seek(TITLES_HEADER_BYTES + b * self.block_bytes)
        buf = self.f.read(self.block_bytes)
        (count,) = struct.unpack_from("<H", buf, 0)
        o = 2
        prev = b""
        out = []
        for _ in range(count):
            shared, suffix_len, flags, _r, locator = INDEX_ENTRY.unpack_from(buf, o)
            o += INDEX_ENTRY.size
            title = prev[:shared] + buf[o : o + suffix_len]
            o += suffix_len
            out.append((title, flags, locator))
            prev = title
        return out

    def scan(self, key, prefix):
        """Entries whose fold equals `key` (or starts with it), in order."""
        start = max(0, bisect.bisect_left(self.sampler, key) - 1)
        for b in range(start, self.blocks):
            for tb, flags, locator in self.read_block(b):
                fb = fold_bytes(tb.decode("utf-8"))
                if fb < key:
                    continue
                if fb.startswith(key) if prefix else fb == key:
                    yield Entry(
                        tb.decode("utf-8"), bool(flags & REDIRECT), locator, self.tier
                    )
                else:
                    return

    def iter_entries(self):
        for b in range(self.blocks):
            for tb, flags, locator in self.read_block(b):
                yield Entry(
                    tb.decode("utf-8"), bool(flags & REDIRECT), locator, self.tier
                )


# --- the writer --------------------------------------------------------------
class PackWriter:
    """Articles go in importance order, one add_article() each; redirects
    after their targets; finish() writes the directory, the indexes and the
    manifest. Blocks close at block_target bytes (or one oversize article),
    shards at shard_bytes, and both close at every tier boundary."""

    def __init__(
        self,
        out_dir,
        pack="en",
        snapshot=None,
        tiers=(),
        block_target=BLOCK_TARGET,
        shard_bytes=SHARD_BYTES,
        index_block_bytes=INDEX_BLOCK_BYTES,
        level=ZSTD_LEVEL,
        batch_blocks=512,
        workers=None,
    ):
        self.out_dir = out_dir
        self.pack = pack
        self.snapshot = snapshot
        self.tier_names = [name for name, _ in tiers]
        self.boundaries = [n for _, n in tiers]
        if any(b <= 0 for b in self.boundaries) or self.boundaries != sorted(
            self.boundaries
        ):
            raise PackError("tier boundaries must be positive and increasing")
        self.block_target = block_target
        self.shard_bytes = shard_bytes
        self.index_block_bytes = index_block_bytes
        self.level = level
        self.batch_blocks = batch_blocks
        self.workers = workers
        os.makedirs(os.path.join(out_dir, "shards"), exist_ok=True)
        self.dict_path = os.path.join(out_dir, "dict.zst")
        self.have_dict = False
        self.tier = 0
        self.articles = 0
        self.raw_bytes = 0
        self.block_records = []
        self.block_bytes = 0
        self.next_block = 0
        self.batch = []
        self.dir_records = []
        self.shards = []
        self.shard = None
        self.by_title = {}  # display title -> (locator, tier)
        self.entries = []  # (fold, title_bytes, flags, locator, tier)
        self.finished = False

    def train(self, samples):
        return train_dictionary(samples, self.dict_path)

    def use_dictionary(self, path):
        shutil.copyfile(path, self.dict_path)

    # --- articles ------------------------------------------------------------
    def add_article(self, title, headings, xhtml):
        if self.finished:
            raise PackError("writer is finished")
        if not self.have_dict:
            if not os.path.exists(self.dict_path):
                raise PackError("train() or use_dictionary() before add_article()")
            self.have_dict = True
        if title in self.by_title:
            raise PackError(f"duplicate article {title!r}")
        if len(title.encode("utf-8")) > MAX_TITLE_BYTES:
            raise PackError(
                f"title longer than {MAX_TITLE_BYTES} bytes: {title[:40]!r}"
            )
        if (
            self.tier < len(self.boundaries)
            and self.articles == self.boundaries[self.tier]
        ):
            self._close_tier()
        rec = encode_article(title, headings, xhtml)
        slots = len(self.block_records)
        if slots and (
            block_header_bytes(slots + 1) + self.block_bytes + len(rec)
            > self.block_target
            or slots >= MAX_SLOTS
        ):
            self._close_block()
        slot = len(self.block_records)
        if self.next_block >= MAX_BLOCKS:
            raise PackError("too many blocks for a locator")
        self.block_records.append(rec)
        self.block_bytes += len(rec)
        self.raw_bytes += len(rec)
        locator = locator_of(self.next_block, slot)
        self.by_title[title] = (locator, self.tier)
        self.entries.append(
            (fold_bytes(title), title.encode("utf-8"), 0, locator, self.tier)
        )
        self.articles += 1
        return locator

    def add_redirect(self, title, target):
        """A title that resolves to `target`'s locator. Skipped, and False
        returned, when it would collide with a display title already in
        the pack; refused when the target is not in it."""
        if target not in self.by_title:
            raise PackError(f"redirect target {target!r} is not in the pack")
        if title in self.by_title:
            return False
        if len(title.encode("utf-8")) > MAX_TITLE_BYTES:
            return False
        locator, tier = self.by_title[target]
        self.by_title[title] = (locator, tier)
        self.entries.append(
            (fold_bytes(title), title.encode("utf-8"), REDIRECT, locator, tier)
        )
        return True

    # --- blocks and shards ---------------------------------------------------
    def _close_block(self):
        if not self.block_records:
            return
        self.batch.append(encode_block(self.block_records))
        self.block_records = []
        self.block_bytes = 0
        self.next_block += 1
        if len(self.batch) >= self.batch_blocks:
            self._flush_batch()

    def _flush_batch(self):
        if not self.batch:
            return
        frames = compress_frames(self.batch, self.dict_path, self.level, self.workers)
        for raw, frame in zip(self.batch, frames):
            if (
                self.shard is None
                or self.shard["bytes"] + len(frame) > self.shard_bytes
            ):
                self._close_shard()
                self._open_shard()
            slots, _ = struct.unpack_from("<HH", raw, 0)
            self.dir_records.append(
                (len(self.shards), slots, self.shard["bytes"], len(frame), len(raw))
            )
            self.shard["f"].write(frame)
            self.shard["sha"].update(frame)
            self.shard["bytes"] += len(frame)
            self.shard["blocks"] += 1
        self.batch = []

    def _open_shard(self):
        idx = len(self.shards)
        rel = f"shards/{idx:03d}.blk"
        self.shard = {
            "file": rel,
            "tier": self.tier,
            "f": open(os.path.join(self.out_dir, rel), "wb"),
            "sha": hashlib.sha256(),
            "bytes": 0,
            "firstBlock": len(self.dir_records),
            "blocks": 0,
        }

    def _close_shard(self):
        if self.shard is None:
            return
        self.shard["f"].close()
        self.shards.append(
            {
                "file": self.shard["file"],
                "tier": self.shard["tier"],
                "bytes": self.shard["bytes"],
                "sha256": self.shard["sha"].hexdigest(),
                "firstBlock": self.shard["firstBlock"],
                "blocks": self.shard["blocks"],
            }
        )
        self.shard = None

    def _close_tier(self):
        self._close_block()
        self._flush_batch()
        self._close_shard()
        self.tier += 1

    # --- finish ----------------------------------------------------------------
    def finish(self):
        if self.finished:
            raise PackError("already finished")
        if self.articles == 0:
            raise PackError("no articles")
        self._close_block()
        self._flush_batch()
        self._close_shard()
        self.finished = True
        # A boundary at the last article opens a tier nothing lands in; it is
        # not a tier, and "all" is only a name for what lies past the named ones.
        tiers_used = self.tier + 1
        while tiers_used > 1 and not any(e[4] == tiers_used - 1 for e in self.entries):
            tiers_used -= 1

        path = os.path.join(self.out_dir, "blocks.dir")
        with open(path, "wb") as f:
            f.write(
                BLOCKSDIR_HEADER.pack(
                    BLOCKSDIR_MAGIC, FORMAT, 0, 0, len(self.dir_records)
                )
            )
            for r in self.dir_records:
                f.write(BLOCK_RECORD.pack(*r))
        blocksdir = {
            "file": "blocks.dir",
            "bytes": os.path.getsize(path),
            "sha256": sha256_file(path),
        }
        dict_entry = {
            "file": "dict.zst",
            "bytes": os.path.getsize(self.dict_path),
            "sha256": sha256_file(self.dict_path),
        }

        self.entries.sort(key=lambda e: (e[0], e[1]))
        titles = []
        for t in range(tiers_used):
            rel = f"titles.{t}.idx"
            p = os.path.join(self.out_dir, rel)
            n, _blocks = write_titles(
                p,
                ((e[0], e[1], e[2], e[3]) for e in self.entries if e[4] == t),
                self.index_block_bytes,
            )
            titles.append(
                {
                    "file": rel,
                    "tier": t,
                    "entries": n,
                    "bytes": os.path.getsize(p),
                    "sha256": sha256_file(p),
                }
            )

        tiers = []
        articles_through = 0
        for t in range(tiers_used):
            name = self.tier_names[t] if t < len(self.tier_names) else "all"
            in_tier = sum(1 for e in self.entries if e[4] == t and e[2] == 0)
            articles_through += in_tier
            shards = [s for s in self.shards if s["tier"] <= t]
            size = dict_entry["bytes"] + blocksdir["bytes"]
            size += sum(x["bytes"] for x in titles[: t + 1]) + sum(
                s["bytes"] for s in shards
            )
            tiers.append(
                {
                    "name": name,
                    "shards": len(shards),
                    "articles": articles_through,
                    "bytes": size,
                }
            )

        manifest = {
            "format": FORMAT,
            "pack": self.pack,
            "snapshot": self.snapshot,
            "built": datetime.datetime.now(datetime.timezone.utc).strftime(
                "%Y-%m-%dT%H:%M:%SZ"
            ),
            "articles": self.articles,
            "entries": len(self.entries),
            "blocks": len(self.dir_records),
            "dict": dict_entry,
            "blocksdir": blocksdir,
            "titles": titles,
            "shards": self.shards,
            "tiers": tiers,
        }
        tmp = os.path.join(self.out_dir, "manifest.json.part")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
            f.write("\n")
        os.replace(tmp, os.path.join(self.out_dir, "manifest.json"))
        return manifest


# --- the reader --------------------------------------------------------------
class Pack:
    """A pack directory as the device sees it: what is present is readable,
    what is missing is "not on the card yet"."""

    def __init__(self, dir_path):
        self.dir = dir_path
        with open(os.path.join(dir_path, "manifest.json"), encoding="utf-8") as f:
            self.manifest = json.load(f)
        if self.manifest.get("format") != FORMAT:
            raise PackError(f"pack format {self.manifest.get('format')}")
        self.dict_path = self._present(self.manifest["dict"])
        if not self.dict_path:
            raise PackError("dict.zst missing or wrong size")
        bd = self._present(self.manifest["blocksdir"])
        if not bd:
            raise PackError("blocks.dir missing or wrong size")
        with open(bd, "rb") as f:
            buf = f.read()
        magic, version, _a, _b, count = BLOCKSDIR_HEADER.unpack_from(buf, 0)
        if magic != BLOCKSDIR_MAGIC or version != FORMAT:
            raise PackError("blocks.dir header")
        self.blocks = [
            BLOCK_RECORD.unpack_from(buf, BLOCKSDIR_HEADER.size + 16 * i)
            for i in range(count)
        ]
        if len(self.blocks) != self.manifest["blocks"]:
            raise PackError("blocks.dir count differs from the manifest")
        self.indexes = []
        for t in self.manifest["titles"]:
            p = self._present(t)
            if p:
                self.indexes.append(TitlesIndex(p, t["tier"]))
        self.shard_present = [bool(self._present(s)) for s in self.manifest["shards"]]
        self._block_cache = (None, None)

    def _present(self, entry):
        p = os.path.join(self.dir, entry["file"])
        if os.path.exists(p) and os.path.getsize(p) == entry["bytes"]:
            return p
        return None

    def close(self):
        for i in self.indexes:
            i.close()

    def verify(self):
        """[(file, expected, actual)] for every present file whose sha256
        differs from the manifest; actual is None for a missing file."""
        bad = []
        files = (
            [self.manifest["dict"], self.manifest["blocksdir"]]
            + self.manifest["titles"]
            + self.manifest["shards"]
        )
        for e in files:
            p = self._present(e)
            if not p:
                bad.append((e["file"], e["sha256"], None))
                continue
            actual = sha256_file(p)
            if actual != e["sha256"]:
                bad.append((e["file"], e["sha256"], actual))
        return bad

    # --- titles ------------------------------------------------------------
    def matches(self, title):
        key = fold_bytes(title)
        out = []
        for idx in self.indexes:
            out.extend(idx.scan(key, prefix=False))
        out.sort(key=lambda e: e.title.encode("utf-8"))
        return out

    def lookup(self, title):
        """The best entry for a title: the exact display title, else the
        first article, else the first redirect; None when nothing folds to
        it."""
        m = self.matches(title)
        if not m:
            return None
        for e in m:
            if e.title == title:
                return e
        for e in m:
            if not e.redirect:
                return e
        return m[0]

    def prefix(self, query, limit=8):
        """Up to `limit` matches over every index file, merged in fold order.
        Each file contributes up to `limit` of its own before the merge, so a
        full first tier cannot crowd out the second."""
        key = fold_bytes(query)
        found = []
        for idx in self.indexes:
            taken = 0
            for e in idx.scan(key, prefix=True):
                found.append(e)
                taken += 1
                if taken >= limit:
                    break
        found.sort(key=lambda e: (fold_bytes(e.title), e.title.encode("utf-8")))
        return found[:limit]

    def iter_entries(self):
        for idx in self.indexes:
            yield from idx.iter_entries()

    # --- articles ------------------------------------------------------------
    def has_block(self, block):
        if not 0 <= block < len(self.blocks):
            return False
        return self.shard_present[self.blocks[block][0]]

    def block(self, block):
        if not 0 <= block < len(self.blocks):
            raise PackError(f"block {block} out of range")
        if self._block_cache[0] == block:
            return self._block_cache[1]
        shard, _slots, offset, csize, usize = self.blocks[block]
        if not self.shard_present[shard]:
            raise PackError(f"shard {shard} is not on the card")
        with open(
            os.path.join(self.dir, self.manifest["shards"][shard]["file"]), "rb"
        ) as f:
            f.seek(offset)
            frame = f.read(csize)
        if len(frame) != csize:
            raise PackError("shard truncated")
        records = decode_block(decompress_frame(frame, self.dict_path, usize))
        self._block_cache = (block, records)
        return records

    def article(self, locator):
        b, slot = split_locator(locator)
        records = self.block(b)
        if slot >= len(records):
            raise PackError(f"slot {slot} out of range in block {b}")
        return decode_article(records[slot])

    def iter_articles(self):
        """(locator, Article) for every article in every present block."""
        for b in range(len(self.blocks)):
            if not self.has_block(b):
                continue
            for slot, rec in enumerate(self.block(b)):
                yield locator_of(b, slot), decode_article(rec)


# --- command line --------------------------------------------------------------
def _main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    cmd, d = argv[1], argv[2]
    p = Pack(d)
    if cmd == "info":
        m = p.manifest
        present = sum(p.shard_present)
        print(f"pack {m['pack']} snapshot {m['snapshot']} built {m['built']}")
        print(
            f"{m['articles']:,} articles, {m['entries']:,} entries, {m['blocks']:,} blocks, {len(m['shards'])} shards ({present} present)"
        )
        for t in m["tiers"]:
            print(
                f"  tier {t['name']}: {t['articles']:,} articles, {t['shards']} shards, {t['bytes']:,} bytes"
            )
    elif cmd == "verify":
        bad = p.verify()
        for f, exp, act in bad:
            print(f"{f}: expected {exp[:12]}, got {act[:12] if act else 'missing'}")
        print("ok" if not bad else f"{len(bad)} files differ")
        return 1 if bad else 0
    elif cmd == "lookup":
        e = p.lookup(argv[3])
        print(e)
    elif cmd == "prefix":
        for e in p.prefix(argv[3]):
            print(e)
    elif cmd == "cat":
        e = p.lookup(argv[3])
        if not e:
            sys.exit("not in the pack")
        a = p.article(e.locator)
        print(a.headings, file=sys.stderr)
        sys.stdout.write(a.xhtml.decode("utf-8") + "\n")
    else:
        sys.exit(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
