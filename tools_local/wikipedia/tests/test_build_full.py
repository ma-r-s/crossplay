"""build_full.py writes the pack build_pack.py writes, from the fixture,
without holding the articles in memory: same titles, same order, same
redirects and aliases, same article bytes; only the dictionary differs."""

import html
import io
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
sys.path.insert(0, TOOL)

import build_full  # noqa: E402
import build_pack  # noqa: E402
import pack_format as pf  # noqa: E402
from test_article_html import FIXTURE  # noqa: E402


def run(main, argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = main(argv)
    if rc != 0:
        raise AssertionError(err.getvalue())
    return json.loads(out.getvalue())


class Full(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="wkfull-")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def build_both(self, *extra):
        a = os.path.join(self.tmp, "a")
        b = os.path.join(self.tmp, "b")
        sa = run(build_pack.main, ["--rows", FIXTURE, "--out", a, "--cache-dir", self.tmp, "--no-vital"] + list(extra))
        sb = run(
            build_full.main,
            ["--rows", FIXTURE, "--out", b, "--work", os.path.join(self.tmp, "work"), "--cache-dir", self.tmp,
             "--no-vital", "--workers", "2", "--bucket-size", "7", "--train-samples", "16"] + list(extra),
        )
        return a, sa, b, sb

    def test_same_pack_in_three_passes(self):
        a, sa, b, sb = self.build_both()
        self.assertEqual(sb["articles"], sa["articles"])
        self.assertEqual(sb["links_in_pack"], sa["links_in_pack"])
        self.assertEqual(sb["links_outside_pack"], sa["links_outside_pack"])
        self.assertEqual(sb["name_entries"], sa["name_entries"])
        self.assertEqual([t["name"] for t in sb["tiers"]], [t["name"] for t in sa["tiers"]])
        pa, pb = pf.Pack(a), pf.Pack(b)
        try:
            ea = [(e.title, e.redirect, e.locator) for e in pa.iter_entries()]
            eb = [(e.title, e.redirect, e.locator) for e in pb.iter_entries()]
            # Same entries, same locators: the order and the block layout agree.
            self.assertEqual([x[:2] for x in eb], [x[:2] for x in ea])
            self.assertEqual([x[2] for x in eb], [x[2] for x in ea])
            checked = 0
            for e in ea:
                if e[1]:
                    continue
                xa = pa.article(e[2]).xhtml
                xb = pb.article(e[2]).xhtml
                self.assertEqual(xb, xa, e[0])
                checked += 1
            self.assertGreater(checked, 30)
        finally:
            pa.close()
            pb.close()
        self.assertTrue(os.path.exists(os.path.join(self.tmp, "work", "bucket-0000.bin")))

    def test_names_that_clean_to_one_title_are_one_article(self):
        # Two rows whose names differ only in whitespace become one title
        # after cleaning; the first full build died on that at 4.9 million.
        import gzip
        rows = os.path.join(self.tmp, "dup.jsonl.gz")
        with gzip.open(FIXTURE, "rt", encoding="utf-8") as src, gzip.open(rows, "wt", encoding="utf-8") as dst:
            first = None
            for line in src:
                row = json.loads(line)
                if first is None:
                    first = row
                dst.write(json.dumps(row, ensure_ascii=False) + "\n")
            twin = dict(first)
            twin["name"] = "  " + first["name"].replace(" ", "  ", 1) + " "
            twin["date_modified"] = "2030-01-01T00:00:00Z"
            dst.write(json.dumps(twin, ensure_ascii=False) + "\n")
        out = os.path.join(self.tmp, "dup")
        summary = run(
            build_full.main,
            ["--rows", rows, "--out", out, "--work", os.path.join(self.tmp, "dupwork"), "--cache-dir", self.tmp,
             "--no-vital", "--workers", "2", "--bucket-size", "7", "--train-samples", "16"],
        )
        base = run(build_pack.main, ["--rows", FIXTURE, "--out", os.path.join(self.tmp, "base"), "--cache-dir", self.tmp, "--no-vital"])
        self.assertEqual(summary["articles"], base["articles"])
        self.assertEqual(summary["duplicates_dropped"], base["duplicates_dropped"] + 1)
        self.assertEqual(summary["duplicates_at_write"], 0)

    def test_limit_and_tier(self):
        _a, sa, _b, sb = self.build_both("--limit", "12", "--tier", "first=5")
        self.assertEqual(sb["articles"], 12)
        self.assertEqual(sb["articles"], sa["articles"])
        self.assertEqual([t["name"] for t in sb["tiers"]], ["first", "all"])
        self.assertEqual(sb["tiers"][0]["articles"], 5)


if __name__ == "__main__":
    unittest.main()
