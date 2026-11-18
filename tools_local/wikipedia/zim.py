#!/usr/bin/env python3
"""A ZIM file, read: the Kiwix archive every Wikipedia language is published in.

    z = Zim("wikipedia_de_top_nopic_2026-10.zim")
    z.metadata("Language")            -> "deu"
    for entry, html in z.articles():  # every front article, in cluster order
        ...
    for title, target in z.redirects():
        ...

Standard library only (Python 3.14 carries zstd as compression.zstd, and the
older clusters are xz, which lzma reads), like the rest of the pack tools.
The format is openzim's (https://wiki.openzim.org/wiki/ZIM_file_format):
an 80-byte header, a MIME list, a pointer list sorted by path, one sorted
by title, a cluster pointer list, the directory entries and the clusters.
Only what the pack builder needs is here: front articles and redirects in
the content namespace ("C" in the new scheme, "A" in the old), and the
metadata ("M"). Nothing is written.
"""

import lzma
import struct
import sys

try:
    from compression import zstd as _zstd
except ImportError:  # pragma: no cover - Python older than 3.14
    _zstd = None

MAGIC = 0x044D495A
REDIRECT = 0xFFFF
LINKTARGET = 0xFFFE
DELETED = 0xFFFD


class ZimError(Exception):
    pass


class Entry:
    __slots__ = (
        "index",
        "mime",
        "namespace",
        "path",
        "title",
        "cluster",
        "blob",
        "target",
    )

    def __init__(self, index, mime, namespace, path, title, cluster, blob, target):
        self.index = index
        self.mime = mime  # an index into the MIME list, or REDIRECT
        self.namespace = namespace
        self.path = path
        # an empty title means "the same as the path", whose spaces are underscores
        self.title = title or path.replace("_", " ")
        self.cluster = cluster
        self.blob = blob
        self.target = target  # a redirect's target entry index

    @property
    def is_redirect(self):
        return self.mime == REDIRECT

    def __repr__(self):
        return f"Entry({self.index}, {self.namespace}/{self.path!r}, {self.title!r})"


class Zim:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        head = self.f.read(80)
        if len(head) < 80:
            raise ZimError("too short for a ZIM header")
        (
            magic,
            self.major,
            self.minor,
            _uuid,
            self.entry_count,
            self.cluster_count,
            self.path_ptr_pos,
            self.title_ptr_pos,
            self.cluster_ptr_pos,
            self.mime_list_pos,
            self.main_page,
            _layout,
            self.checksum_pos,
        ) = struct.unpack("<IHH16sIIQQQQIIQ", head)
        if magic != MAGIC:
            raise ZimError("not a ZIM file (magic %#x)" % magic)
        self.mimes = self._read_mimes()
        self.f.seek(self.path_ptr_pos)
        self.entry_ptrs = struct.unpack(
            f"<{self.entry_count}Q", self.f.read(8 * self.entry_count)
        )
        self.f.seek(self.cluster_ptr_pos)
        self.cluster_ptrs = struct.unpack(
            f"<{self.cluster_count}Q", self.f.read(8 * self.cluster_count)
        )
        self._entries = None
        self._cluster_cache = (None, None)
        # The new namespace scheme (6.1 on) puts every article under "C"; the
        # old one under "A".
        self.content_ns = "C" if (self.major, self.minor) >= (6, 1) else "A"

    def close(self):
        self.f.close()

    # --- directory ---------------------------------------------------------

    def _read_mimes(self):
        self.f.seek(self.mime_list_pos)
        out = []
        buf = self.f.read(64 * 1024)
        pos = 0
        while True:
            end = buf.index(b"\0", pos)
            if end == pos:
                break
            out.append(buf[pos:end].decode("utf-8"))
            pos = end + 1
        return out

    def _read_entry(self, index):
        self.f.seek(self.entry_ptrs[index])
        raw = self.f.read(16)
        mime, param_len, ns = struct.unpack("<HBc", raw[:4])
        ns = ns.decode("latin-1")
        if mime == REDIRECT:
            (target,) = struct.unpack("<I", raw[8:12])
            cluster = blob = None
            tail_start = 12
        elif mime in (LINKTARGET, DELETED):
            target = cluster = blob = None
            tail_start = 8
        else:
            cluster, blob = struct.unpack("<II", raw[8:16])
            target = None
            tail_start = 16
        # path and title are NUL-terminated and short; read generously
        self.f.seek(self.entry_ptrs[index] + tail_start)
        tail = self.f.read(2048)
        p_end = tail.index(b"\0")
        t_end = tail.index(b"\0", p_end + 1)
        path = tail[:p_end].decode("utf-8", "replace")
        title = tail[p_end + 1 : t_end].decode("utf-8", "replace")
        return Entry(index, mime, ns, path, title, cluster, blob, target)

    def entries(self):
        """Every directory entry, in path order (cached)."""
        if self._entries is None:
            self._entries = [self._read_entry(i) for i in range(self.entry_count)]
        return self._entries

    def entry(self, index):
        return self.entries()[index]

    def mime_of(self, entry):
        if entry.mime >= len(self.mimes):
            return None
        return self.mimes[entry.mime]

    def resolve(self, entry, hops=8):
        """A redirect followed to the entry it ends at; None for a loop."""
        while entry is not None and entry.is_redirect and hops > 0:
            entry = self.entry(entry.target)
            hops -= 1
        return None if entry is None or entry.is_redirect else entry

    # --- clusters ----------------------------------------------------------

    def _cluster_end(self, n):
        return (
            self.cluster_ptrs[n + 1]
            if n + 1 < self.cluster_count
            else self.checksum_pos
        )

    def cluster(self, n):
        """The blobs of cluster n, decompressed, as a list of bytes."""
        if self._cluster_cache[0] == n:
            return self._cluster_cache[1]
        start = self.cluster_ptrs[n]
        self.f.seek(start)
        data = self.f.read(self._cluster_end(n) - start)
        info = data[0]
        comp = info & 0x0F
        wide = bool(info & 0x10)
        body = data[1:]
        if comp in (0, 1):
            raw = body
        elif comp == 4:
            raw = lzma.LZMADecompressor().decompress(body)
        elif comp == 5:
            if _zstd is None:
                raise ZimError("zstd clusters need Python 3.14 (compression.zstd)")
            raw = _zstd.ZstdDecompressor().decompress(body)
        else:
            raise ZimError(f"cluster {n}: compression {comp} is not supported")
        size = 8 if wide else 4
        fmt = "<Q" if wide else "<I"
        (first,) = struct.unpack(fmt, raw[:size])
        count = first // size
        offs = struct.unpack(f"<{count}{fmt[1]}", raw[: count * size])
        blobs = [raw[offs[i] : offs[i + 1]] for i in range(count - 1)]
        self._cluster_cache = (n, blobs)
        return blobs

    def content(self, entry):
        entry = self.resolve(entry)
        if entry is None or entry.cluster is None:
            return None
        return self.cluster(entry.cluster)[entry.blob]

    # --- what the builder reads --------------------------------------------

    def metadata(self, name):
        for e in self.entries():
            if e.namespace == "M" and e.path == name and not e.is_redirect:
                blob = self.content(e)
                return blob.decode("utf-8", "replace") if blob is not None else None
        return None

    def _front_listing(self):
        """Entry indices of the front articles, from the title-ordered listing
        the writer leaves in "X" (6.1 on); None when the file has none."""
        for e in self.entries():
            if (
                e.namespace == "X"
                and e.path == "listing/titleOrdered/v1"
                and not e.is_redirect
            ):
                blob = self.content(e)
                if blob is None:
                    return None
                return struct.unpack(f"<{len(blob) // 4}I", blob)
        return None

    def front_articles(self):
        """The article entries (HTML, not redirects), in cluster order so each
        cluster is decompressed once."""
        listing = self._front_listing()
        entries = self.entries()
        if listing is not None:
            picked = [entries[i] for i in listing]
        else:
            picked = [e for e in entries if e.namespace == self.content_ns]
        out = []
        for e in picked:
            if e.is_redirect or e.cluster is None or e.namespace != self.content_ns:
                continue
            mime = self.mime_of(e) or ""
            if not mime.startswith("text/html"):
                continue
            out.append(e)
        out.sort(key=lambda e: (e.cluster, e.blob))
        return out

    def articles(self, limit=None):
        """(entry, html bytes) for every front article, in cluster order."""
        for n, e in enumerate(self.front_articles()):
            if limit is not None and n >= limit:
                return
            yield e, self.cluster(e.cluster)[e.blob]

    def redirects(self):
        """(redirect title, target title) for every redirect in the content
        namespace that ends at an HTML article."""
        for e in self.entries():
            if e.namespace != self.content_ns or not e.is_redirect:
                continue
            t = self.resolve(e)
            if t is None or t.namespace != self.content_ns:
                continue
            if not (self.mime_of(t) or "").startswith("text/html"):
                continue
            if e.title and t.title and e.title != t.title:
                yield e.title, t.title

    def find(self, path):
        """The content entry at `path`, by binary search over the path list."""
        key = (self.content_ns, path)
        lo, hi = 0, self.entry_count
        while lo < hi:
            mid = (lo + hi) // 2
            e = self._read_entry(mid)
            if (e.namespace, e.path) < key:
                lo = mid + 1
            else:
                hi = mid
        if lo < self.entry_count:
            e = self._read_entry(lo)
            if (e.namespace, e.path) == key:
                return e
        return None


def main(argv):
    if len(argv) < 2:
        sys.exit(
            "usage: zim.py <file.zim> [path]   (metadata and counts, or one article's HTML)"
        )
    z = Zim(argv[1])
    if len(argv) > 2:
        e = z.find(argv[2])
        if e is None:
            sys.exit("no such path")
        sys.stdout.write(z.content(e).decode("utf-8", "replace"))
        return 0
    print(
        f"ZIM {z.major}.{z.minor}, {z.entry_count:,} entries, {z.cluster_count:,} clusters"
    )
    for k in (
        "Name",
        "Title",
        "Language",
        "Date",
        "Flavour",
        "Source",
        "Scraper",
        "Counter",
    ):
        print(f"  {k}: {z.metadata(k)}")
    fronts = z.front_articles()
    print(f"  front articles: {len(fronts):,}")
    print(f"  redirects: {sum(1 for _ in z.redirects()):,}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
