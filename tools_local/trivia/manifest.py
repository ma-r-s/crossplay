#!/usr/bin/env python3
"""What a pack IS, written down so a device can ask without downloading it.

Three artifacts, and it matters which is published where:

    pack.dat        the pack. 6.6 MB. Goes on the card.
    pack.json       the manifest. ~200 bytes. Published BESIDE pack.dat in the
                    same release upload, and fetched by a device deciding
                    whether it is stale.
    pack.idx.tsv    index<TAB>corpus_id for this build. Published too, but the
                    device NEVER fetches it -- it exists so a report naming
                    (pack id, index) can be resolved back to a corpus row.
    pack.meta       written by the DEVICE beside its own pack.dat, recording
                    which build it holds.

--- WHY THE ID IS A HASH OF THE BYTES ---

A build date or a serial would identify a build, and would also be a fact
written down twice: nothing would stop pack.json claiming an id that pack.dat
does not have. Deriving the id FROM the pack means the two cannot disagree,
which is the `derived-facts-written-as-literals` rule applied to the one place
where disagreeing is expensive -- a report resolved against the wrong manifest
deletes a question nobody reported.

--- WHY pack.meta CARRIES count AND bytes, NOT JUST THE ID ---

docs/apps/trivia-pack-format.md's residual: a device cannot tell that its
pack.dat was replaced by a same-count pack, because nothing on the card records
what it holds. pack.meta is that record, and it is only worth having if it can
be caught lying. Copying a pack to a card by hand is a documented case, and a
hand-copy that replaces pack.dat and leaves pack.meta makes every later report
name the WRONG BUILD -- silently, and with no way for the service to tell.

So pack.meta stores count and bytes as well, both free to check (a stat and a
16-byte header read), and a mismatch means the meta is stale. **A stale meta is
DISCARDED, never repaired**: the honest state is "I do not know which build I
hold", which suppresses reporting until the next sync. Guessing an id is the one
outcome worse than having none.
"""

import hashlib
import json
import os
import struct

MANIFEST_VERSION = 1
ID_HEX = 12


def pack_id(path):
    """sha256 of the pack's bytes, first 12 hex. Derived, so it cannot disagree."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()[:ID_HEX]


def pack_count(path):
    """The count out of the header, without reading the body."""
    with open(path, "rb") as f:
        head = f.read(16)
    if len(head) < 16 or head[:8] != b"XTRIVIA\0":
        raise ValueError(f"{path}: not a trivia pack")
    (count,) = struct.unpack_from("<I", head, 12)
    return count


def build(path, built=None):
    return {
        "manifest": MANIFEST_VERSION,
        "id": pack_id(path),
        "count": pack_count(path),
        "bytes": os.path.getsize(path),
        "built": built,
    }


def write(path, out_path=None, built=None):
    man = build(path, built)
    out_path = out_path or os.path.splitext(path)[0] + ".json"
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(man, f, sort_keys=True)
        f.write("\n")
    return man, out_path


def write_index_map(items, out_path, pack_id=None):
    """index<TAB>corpus_id, in pack order.

    Captured AT BUILD TIME on purpose. build_pack.py mints an item's id as a
    sha1 of the normalised clue, so the id moves when the clue is repaired --
    board #146. This file freezes the id as it was on build day, which is the
    only form of it a later repair cannot move, and it is the reason a report
    filed months ago can still be resolved.
    """
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("# index\tid -- frozen at build time; see manifest.py\n")
        # The build this map is FOR. Without it the map is just a list of ids
        # and nothing can tell it apart from another build's, which is how
        # collect_flags.py came to document a pack-id check it could not make.
        if pack_id:
            f.write(f"# pack {pack_id}\n")
        for i, item in enumerate(items):
            qid = item.get("id")
            if not qid:
                raise ValueError(f"item at index {i} has no id; cannot build an index map")
            f.write(f"{i}\t{qid}\n")
    return out_path


# --- the device's own record --------------------------------------------------

META_KEYS = ("id", "count", "bytes")


def meta_path(pack_path):
    return os.path.splitext(pack_path)[0] + ".meta"


def write_meta(pack_path, man, path=None):
    path = path or meta_path(pack_path)
    with open(path, "w", encoding="utf-8") as f:
        for k in META_KEYS:
            f.write(f"{k}\t{man[k]}\n")
    return path


def read_meta(pack_path, path=None):
    """The build this card holds, or None when it cannot be trusted.

    None is a real answer and callers must handle it: it means "I hold a pack
    and do not know which build", which is what a hand-copy produces. Reporting
    is suppressed until a sync re-establishes it. Never guess.
    """
    path = path or meta_path(pack_path)
    if not os.path.exists(path) or not os.path.exists(pack_path):
        return None
    meta = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                if "\t" in line:
                    k, v = line.rstrip("\n").split("\t", 1)
                    meta[k] = v
    except OSError:
        return None
    if not all(k in meta for k in META_KEYS):
        return None
    try:
        count = int(meta["count"])
        size = int(meta["bytes"])
    except ValueError:
        return None
    # The two free checks. Both must hold; either failing means this meta
    # describes a pack that is no longer the pack beside it.
    if size != os.path.getsize(pack_path):
        return None
    try:
        if count != pack_count(pack_path):
            return None
    except (ValueError, OSError):
        return None
    return {"id": meta["id"], "count": count, "bytes": size}
