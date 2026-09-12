"""The pack: written by PackWriter, read back by Pack, byte for byte.

Built from the committed fixture always, and from the 3,000-row research
sample when this machine has it.
"""

import gzip
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
sys.path.insert(0, TOOL)

import pack_format as pf  # noqa: E402
from article_html import article_xhtml  # noqa: E402
from fold import fold_bytes  # noqa: E402
from test_article_html import FIXTURE, read_rows, sample_path  # noqa: E402


def articles_from(rows):
    """[(title, headings, xhtml)] with duplicate titles dropped (first wins)."""
    out = []
    seen = set()
    for r in rows:
        title, headings, xhtml = article_xhtml(r)
        if title in seen:
            continue
        seen.add(title)
        out.append((title, headings, xhtml))
    return out


def build(articles, out_dir, redirects=(), **kw):
    w = pf.PackWriter(out_dir, pack="test", snapshot="2026-05-13", **kw)
    w.train(pf.encode_article(t, h, x) for t, h, x in articles)
    locators = {}
    for t, h, x in articles:
        locators[t] = w.add_article(t, h, x)
    for title, target in redirects:
        w.add_redirect(title, target)
    return w.finish(), locators


class Fixture(unittest.TestCase):
    tmp = None

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="wkpack-")
        cls.articles = articles_from(read_rows(FIXTURE))
        assert len(cls.articles) >= 30
        cls.titles = [t for t, _, _ in cls.articles]
        cls.redirects = [
            ("NYC test redirect", cls.titles[0]),
            ("Second alias", cls.titles[3]),
        ]
        cls.dir = os.path.join(cls.tmp, "wikipedia")
        cls.manifest, cls.locators = build(
            cls.articles,
            cls.dir,
            cls.redirects,
            tiers=[("essentials", 10)],
            shard_bytes=60_000,
        )
        cls.pack = pf.Pack(cls.dir)

    @classmethod
    def tearDownClass(cls):
        cls.pack.close()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_files_and_manifest_shape(self):
        m = self.manifest
        self.assertEqual(
            list(m),
            [
                "format",
                "pack",
                "snapshot",
                "built",
                "articles",
                "entries",
                "blocks",
                "dict",
                "blocksdir",
                "titles",
                "shards",
                "tiers",
            ],
        )
        self.assertEqual(m["format"], 1)
        self.assertEqual(m["articles"], len(self.articles))
        self.assertEqual(m["entries"], len(self.articles) + len(self.redirects))
        for e in [m["dict"], m["blocksdir"]] + m["titles"] + m["shards"]:
            p = os.path.join(self.dir, e["file"])
            self.assertEqual(os.path.getsize(p), e["bytes"], e["file"])
            self.assertEqual(pf.sha256_file(p), e["sha256"], e["file"])
        self.assertEqual(
            [t["file"] for t in m["titles"]], ["titles.0.idx", "titles.1.idx"]
        )
        self.assertEqual([t["tier"] for t in m["titles"]], [0, 1])
        self.assertEqual(m["dict"]["bytes"], pf.DICT_BYTES)
        self.assertEqual(self.pack.verify(), [])

    def test_every_article_round_trips(self):
        for t, h, x in self.articles:
            e = self.pack.lookup(t)
            self.assertIsNotNone(e, t)
            self.assertEqual(e.title, t)
            self.assertFalse(e.redirect)
            self.assertEqual(e.locator, self.locators[t])
            a = self.pack.article(e.locator)
            self.assertEqual(a.title, t)
            self.assertEqual(
                a.headings, [pf.heading_bytes(s).decode("utf-8") for s in h]
            )
            self.assertEqual(a.xhtml, x)

    def test_iteration_covers_everything(self):
        got = {loc: a.title for loc, a in self.pack.iter_articles()}
        self.assertEqual(got, {loc: t for t, loc in self.locators.items()})
        entries = list(self.pack.iter_entries())
        self.assertEqual(len(entries), self.manifest["entries"])
        self.assertEqual(sum(1 for e in entries if e.redirect), len(self.redirects))

    def test_lookup_is_folded(self):
        t = self.titles[0]
        self.assertEqual(self.pack.lookup(t.upper()).locator, self.locators[t])
        self.assertEqual(
            self.pack.lookup("  " + t.replace(" ", "_") + " ").locator, self.locators[t]
        )
        self.assertIsNone(self.pack.lookup("no such article ever"))

    def test_redirects(self):
        e = self.pack.lookup("nyc TEST redirect")
        self.assertTrue(e.redirect)
        self.assertEqual(e.title, "NYC test redirect")
        self.assertEqual(e.locator, self.locators[self.titles[0]])
        self.assertEqual(self.pack.article(e.locator).title, self.titles[0])
        self.assertEqual(e.tier, 0)
        e2 = self.pack.lookup("second alias")
        self.assertEqual(e2.tier, 0)

    def test_prefix_search(self):
        all_entries = list(self.pack.iter_entries())
        for q in ("s", "S", "2", "list", "m", "zzz", ""):
            key = fold_bytes(q)
            expect = sorted(
                (e for e in all_entries if fold_bytes(e.title).startswith(key)),
                key=lambda e: (fold_bytes(e.title), e.title.encode()),
            )[:8]
            got = self.pack.prefix(q, limit=8)
            self.assertEqual([e.title for e in got], [e.title for e in expect], q)
        self.assertEqual(len(self.pack.prefix("", limit=3)), 3)

    def test_blocks(self):
        recs = self.pack.blocks
        self.assertEqual(len(recs), self.manifest["blocks"])
        self.assertEqual(sum(r[1] for r in recs), len(self.articles))
        big = [r for r in recs if r[4] > pf.BLOCK_TARGET]
        self.assertTrue(big, "the fixture must have an oversize article")
        for r in big:
            self.assertEqual(r[1], 1, "an oversize block holds one article")
        for r in recs:
            self.assertLessEqual(r[1], pf.MAX_SLOTS)
            self.assertGreater(r[3], 0)
        # a block is closed by the NEXT article, so every block but the
        # oversize ones and each tier's last is within target
        self.assertGreater(len(recs), 1)

    def test_shards_never_straddle(self):
        m = self.manifest
        self.assertGreater(len(m["shards"]), 2, "shard_bytes override did not split")
        seen = 0
        for i, s in enumerate(m["shards"]):
            self.assertEqual(s["file"], f"shards/{i:03d}.blk")
            self.assertEqual(s["firstBlock"], seen)
            recs = self.pack.blocks[s["firstBlock"] : s["firstBlock"] + s["blocks"]]
            self.assertTrue(recs)
            for r in recs:
                self.assertEqual(r[0], i)
                self.assertLessEqual(r[2] + r[3], s["bytes"])
            self.assertEqual(recs[-1][2] + recs[-1][3], s["bytes"])
            if s["blocks"] > 1:
                self.assertLessEqual(s["bytes"], 60_000)
            seen += s["blocks"]
        self.assertEqual(seen, m["blocks"])

    def test_boundary_at_the_last_article_opens_no_tier(self):
        # essentials=<every article>: the writer used to open a second tier at
        # that boundary with nothing in it and name it "all".
        d = os.path.join(self.tmp, "whole")
        m, _ = build(
            self.articles,
            d,
            self.redirects,
            tiers=[("essentials", len(self.articles))],
            shard_bytes=60_000,
        )
        self.assertEqual([t["name"] for t in m["tiers"]], ["essentials"])
        self.assertEqual(m["tiers"][0]["articles"], len(self.articles))
        self.assertEqual(m["tiers"][0]["shards"], len(m["shards"]))
        self.assertEqual([t["file"] for t in m["titles"]], ["titles.0.idx"])
        self.assertTrue(all(s["tier"] == 0 for s in m["shards"]))
        self.assertFalse(os.path.exists(os.path.join(d, "titles.1.idx")))

    def test_tiers(self):
        m = self.manifest
        self.assertEqual([t["name"] for t in m["tiers"]], ["essentials", "all"])
        self.assertEqual(m["tiers"][0]["articles"], 10)
        self.assertEqual(m["tiers"][1]["articles"], len(self.articles))
        tier0 = [s for s in m["shards"] if s["tier"] == 0]
        tier1 = [s for s in m["shards"] if s["tier"] == 1]
        self.assertTrue(tier0 and tier1)
        self.assertEqual(m["tiers"][0]["shards"], len(tier0))
        self.assertEqual(m["tiers"][1]["shards"], len(m["shards"]))
        self.assertEqual(
            [s["tier"] for s in m["shards"]], sorted(s["tier"] for s in m["shards"])
        )
        # the first ten articles, and only they, are in tier-0 blocks
        t0_blocks = sum(s["blocks"] for s in tier0)
        for i, t in enumerate(self.titles):
            b, _ = pf.split_locator(self.locators[t])
            self.assertEqual(b < t0_blocks, i < 10, t)
        self.assertEqual(m["titles"][0]["entries"], 10 + len(self.redirects))
        self.assertEqual(m["titles"][1]["entries"], len(self.articles) - 10)
        expect0 = (
            m["dict"]["bytes"]
            + m["blocksdir"]["bytes"]
            + m["titles"][0]["bytes"]
            + sum(s["bytes"] for s in tier0)
        )
        self.assertEqual(m["tiers"][0]["bytes"], expect0)
        expect1 = expect0 + m["titles"][1]["bytes"] + sum(s["bytes"] for s in tier1)
        self.assertEqual(m["tiers"][1]["bytes"], expect1)
        # tier 1 entries are exactly the articles the tier-0 index lacks
        by_tier = {}
        for e in self.pack.iter_entries():
            by_tier.setdefault(e.tier, set()).add(e.title)
        self.assertEqual(
            by_tier[0], set(self.titles[:10]) | {r for r, _ in self.redirects}
        )
        self.assertEqual(by_tier[1], set(self.titles[10:]))

    def test_a_flipped_byte_is_detected(self):
        d = os.path.join(self.tmp, "corrupt")
        shutil.copytree(self.dir, d)
        shard = os.path.join(d, "shards", "000.blk")
        rec = self.pack.blocks[0]
        with open(shard, "r+b") as f:
            f.seek(rec[2] + rec[3] // 2)
            b = f.read(1)
            f.seek(rec[2] + rec[3] // 2)
            f.write(bytes([b[0] ^ 0x55]))
        p = pf.Pack(d)
        bad = p.verify()
        self.assertEqual([x[0] for x in bad], ["shards/000.blk"])
        self.assertEqual(bad[0][1], self.manifest["shards"][0]["sha256"])
        self.assertNotEqual(bad[0][2], bad[0][1])
        with self.assertRaises(pf.PackError):
            p.article(pf.locator_of(0, 0))
        idx = os.path.join(d, "titles.1.idx")
        with open(idx, "r+b") as f:
            f.seek(40)
            b = f.read(1)
            f.seek(40)
            f.write(bytes([b[0] ^ 0x01]))
        self.assertEqual(
            [x[0] for x in pf.Pack(d).verify()], ["titles.1.idx", "shards/000.blk"]
        )
        p.close()

    def test_a_missing_shard_is_not_on_the_card(self):
        d = os.path.join(self.tmp, "partial")
        shutil.copytree(self.dir, d)
        last = self.manifest["shards"][-1]
        os.remove(os.path.join(d, last["file"]))
        p = pf.Pack(d)
        self.assertFalse(p.has_block(last["firstBlock"]))
        self.assertTrue(p.has_block(0))
        with self.assertRaises(pf.PackError):
            p.article(pf.locator_of(last["firstBlock"], 0))
        n = sum(1 for _ in p.iter_articles())
        self.assertEqual(
            n,
            len(self.articles)
            - sum(
                self.pack.blocks[b][1]
                for b in range(last["firstBlock"], last["firstBlock"] + last["blocks"])
            ),
        )
        self.assertEqual([x[0] for x in p.verify()], [last["file"]])
        p.close()


class WriterRules(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="wkrules-")
        self.articles = articles_from(read_rows(FIXTURE))[:12]

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_duplicate_article_refused(self):
        w = pf.PackWriter(os.path.join(self.tmp, "p"))
        w.train(pf.encode_article(t, h, x) for t, h, x in self.articles)
        t, h, x = self.articles[0]
        w.add_article(t, h, x)
        with self.assertRaises(pf.PackError):
            w.add_article(t, h, x)

    def test_redirect_rules(self):
        w = pf.PackWriter(os.path.join(self.tmp, "p"))
        w.train(pf.encode_article(t, h, x) for t, h, x in self.articles)
        for t, h, x in self.articles[:2]:
            w.add_article(t, h, x)
        with self.assertRaises(pf.PackError):
            w.add_redirect("Alias", "not in the pack")
        self.assertFalse(w.add_redirect(self.articles[1][0], self.articles[0][0]))
        self.assertTrue(w.add_redirect("Alias", self.articles[0][0]))
        self.assertFalse(w.add_redirect("Alias", self.articles[1][0]))
        m = w.finish()
        self.assertEqual(m["entries"], 3)
        self.assertEqual(
            m["tiers"],
            [
                {
                    "name": "all",
                    "shards": 1,
                    "articles": 2,
                    "bytes": m["tiers"][0]["bytes"],
                }
            ],
        )

    def test_no_dictionary_refused(self):
        w = pf.PackWriter(os.path.join(self.tmp, "p"))
        with self.assertRaises(pf.PackError):
            w.add_article("A", [], b"<html><body><h1>A</h1></body></html>")

    def test_oversize_article_alone_and_target_respected(self):
        w = pf.PackWriter(os.path.join(self.tmp, "p"), block_target=4096)
        w.train(pf.encode_article(t, h, x) for t, h, x in self.articles)
        small = b"<html><body><h1>x</h1><p>" + b"a" * 1000 + b"</p></body></html>"
        big = b"<html><body><h1>x</h1><p>" + b"b" * 20000 + b"</p></body></html>"
        w.add_article("S1", [], small)
        w.add_article("S2", [], small)
        w.add_article("S3", [], small)
        w.add_article("Big", ["H"], big)
        w.add_article("S4", [], small)
        w.add_article("S5", [], small)
        m = w.finish()
        p = pf.Pack(os.path.join(self.tmp, "p"))
        recs = p.blocks
        self.assertEqual([r[1] for r in recs], [3, 1, 2])
        self.assertLessEqual(recs[0][4], 4096)
        self.assertGreater(recs[1][4], 4096)
        self.assertEqual(p.article(p.lookup("big").locator).xhtml, big)
        self.assertEqual(m["blocks"], 3)
        p.close()


class SamplerBoundaries(unittest.TestCase):
    """Index blocks so small that same-fold entries straddle them."""

    def test_scan_starts_one_block_early(self):
        tmp = tempfile.mkdtemp(prefix="wkidx-")
        try:
            titles = ["Aa", "AB", "Ab", "aB", "ab", "Ac", "B", "b", "C"]
            entries = sorted(
                ((fold_bytes(t), t.encode(), 0, 100 + i) for i, t in enumerate(titles)),
                key=lambda e: (e[0], e[1]),
            )
            path = os.path.join(tmp, "titles.0.idx")
            n, blocks = pf.write_titles(path, entries, block_bytes=2 + 2 * (8 + 2))
            self.assertEqual(n, len(titles))
            self.assertGreaterEqual(blocks, 4)
            idx = pf.TitlesIndex(path, 0)
            self.assertEqual(len(idx.sampler), blocks)
            got = sorted(e.title for e in idx.scan(b"ab", prefix=False))
            self.assertEqual(got, sorted(["AB", "Ab", "aB", "ab"]))
            self.assertEqual(
                [e.title for e in idx.scan(b"b", prefix=False)], ["B", "b"]
            )
            self.assertEqual(
                [e.title for e in idx.scan(b"a", prefix=True)],
                [e[1].decode() for e in entries if e[0].startswith(b"a")],
            )
            self.assertEqual(list(idx.scan(b"zz", prefix=False)), [])
            self.assertEqual(
                [e.title for e in idx.iter_entries()], [e[1].decode() for e in entries]
            )
            for e in entries:
                found = [
                    x for x in idx.scan(e[0], prefix=False) if x.title == e[1].decode()
                ]
                self.assertEqual(len(found), 1, e)
                self.assertEqual(found[0].locator, e[3])
            idx.close()
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_front_coding_and_limits(self):
        tmp = tempfile.mkdtemp(prefix="wkidx-")
        try:
            path = os.path.join(tmp, "t.idx")
            long_title = "x" * 256
            with self.assertRaises(pf.PackError):
                pf.write_titles(
                    path, [(fold_bytes(long_title), long_title.encode(), 0, 1)]
                )
            with self.assertRaises(pf.PackError):
                pf.write_titles(path, [(b"a", b"A", 0, 1), (b"a", b"A", 0, 2)])
            with self.assertRaises(pf.PackError):
                pf.write_titles(path, [(b"b", b"B", 0, 1), (b"a", b"A", 0, 2)])
            titles = ["New York", "New York City", "New Yorker", "Newark"]
            entries = sorted(
                ((fold_bytes(t), t.encode(), 0, i) for i, t in enumerate(titles)),
                key=lambda e: (e[0], e[1]),
            )
            pf.write_titles(path, entries)
            with open(path, "rb") as f:
                raw = f.read()
            self.assertEqual(raw[:4], b"WKTI")
            self.assertEqual(len(raw), 32 + 4096 + 4 + 1 + len(fold_bytes("New York")))
            idx = pf.TitlesIndex(path, 0)
            block = idx.read_block(0)
            self.assertEqual(
                [t.decode() for t, _, _ in block], [e[1].decode() for e in entries]
            )
            # entry 2 ("New York City") shares 8 bytes with "New York"
            body = raw[32:]
            shared = [body[o] for o in (2,)]
            self.assertEqual(shared, [0])
            idx.close()
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


class Sample(unittest.TestCase):
    """The 3,000 real rows, when this machine has them: every article
    looks itself up and comes back byte-identical."""

    def test_round_trip(self):
        path = sample_path()
        if not path:
            print(
                "\n  SAMPLE NOT FOUND: pack round trip ran over the fixture only",
                flush=True,
            )
            return
        articles = articles_from(read_rows(path))
        tmp = tempfile.mkdtemp(prefix="wksample-")
        try:
            d = os.path.join(tmp, "wikipedia")
            m, locators = build(articles, d, tiers=[("essentials", 500)])
            p = pf.Pack(d)
            self.assertEqual(p.verify(), [])
            raw = sum(len(pf.encode_article(t, h, x)) for t, h, x in articles)
            packed = sum(s["bytes"] for s in m["shards"])
            for t, h, x in articles:
                e = p.lookup(t)
                self.assertIsNotNone(e, t)
                self.assertEqual(e.locator, locators[t])
                a = p.article(e.locator)
                self.assertEqual(a.xhtml, x, t)
                self.assertEqual(a.headings, [pf.heading_bytes(s).decode() for s in h])
            print(
                f"\n  pack round trip: {len(articles)} articles, {m['blocks']} blocks, {len(m['shards'])} shards, "
                f"raw {raw:,} -> shards {packed:,} bytes, ratio {raw / packed:.2f}",
                flush=True,
            )
            p.close()
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
