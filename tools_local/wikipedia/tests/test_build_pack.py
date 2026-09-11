"""build_pack.py end to end on the fixture: ordering, tiers, redirects,
the summary, and a pack the reader opens."""

import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
sys.path.insert(0, TOOL)

import build_pack  # noqa: E402
import pack_format as pf  # noqa: E402
import vital  # noqa: E402
from fold import fold_bytes  # noqa: E402
from test_article_html import FIXTURE, read_rows  # noqa: E402


class Build(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="wkbuild-")
        self.rows = read_rows(FIXTURE)
        self.names = []
        for r in self.rows:
            if r["name"] not in self.names:
                self.names.append(r["name"])

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_build(self, *extra):
        out = os.path.join(self.tmp, "wikipedia")
        summary_path = os.path.join(self.tmp, "summary.json")
        argv = [
            "--rows",
            FIXTURE,
            "--out",
            out,
            "--summary-json",
            summary_path,
            "--cache-dir",
            self.tmp,
        ] + list(extra)
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            rc = build_pack.main(argv)
        self.assertEqual(rc, 0, stderr.getvalue())
        with open(summary_path, encoding="utf-8") as f:
            summary = json.load(f)
        self.assertEqual(json.loads(stdout.getvalue()), summary)
        return out, summary, stderr.getvalue()

    def test_title_order_without_vital(self):
        out, summary, log = self.run_build("--no-vital")
        self.assertEqual(summary["articles"], len(self.names))
        self.assertEqual(
            summary["duplicates_dropped"], len(self.rows) - len(self.names)
        )
        self.assertEqual(summary["vital_matched"], 0)
        self.assertEqual([t["name"] for t in summary["tiers"]], ["all"])
        p = pf.Pack(out)
        order = [a.title for _, a in p.iter_articles()]
        self.assertEqual(
            order, sorted(self.names, key=lambda t: (fold_bytes(t), t.encode()))
        )
        self.assertEqual(p.verify(), [])
        self.assertEqual(p.manifest["snapshot"], summary["snapshot"])
        self.assertRegex(summary["snapshot"], r"^\d{4}-\d{2}-\d{2}$")
        self.assertGreater(summary["ratio"], 2.0)
        self.assertIn("articles", log)
        p.close()

    def test_vital_first_then_tier(self):
        vital_path = os.path.join(self.tmp, "vital.json")
        picked = {
            self.names[5]: 3,
            self.names[20]: 1,
            self.names[9].upper(): 5,
        }  # one matches by fold
        with open(vital_path, "w", encoding="utf-8") as f:
            json.dump({t: {"level": lv} for t, lv in picked.items()}, f)
        out, summary, _ = self.run_build("--vital", vital_path)
        self.assertEqual(summary["vital_matched"], 3)
        self.assertEqual([t["name"] for t in summary["tiers"]], ["essentials", "all"])
        self.assertEqual(summary["tiers"][0]["articles"], 3)
        p = pf.Pack(out)
        order = [a.title for _, a in p.iter_articles()]
        self.assertEqual(order[:3], [self.names[20], self.names[5], self.names[9]])
        self.assertEqual(
            order[3:],
            sorted(
                set(self.names) - set(order[:3]),
                key=lambda t: (fold_bytes(t), t.encode()),
            ),
        )
        self.assertEqual(p.manifest["tiers"][0]["articles"], 3)
        self.assertEqual(len(p.manifest["titles"]), 2)
        p.close()

    def test_explicit_tier_redirects_and_snapshot(self):
        redirects = os.path.join(self.tmp, "redirects.tsv")
        with open(redirects, "w", encoding="utf-8") as f:
            f.write("# comment\n")
            f.write(f"Alias one\t{self.names[0]}\n")
            f.write(f"{self.names[1]}\t{self.names[0]}\n")  # collides with an article
            f.write("Nowhere\tNot in the pack\n")
        out, summary, _ = self.run_build(
            "--no-vital",
            "--tier",
            "essentials=7",
            "--redirects",
            redirects,
            "--snapshot",
            "2026-05-13",
            "--shard-bytes",
            "70000",
        )
        self.assertEqual(summary["redirects_kept"], 1)
        self.assertEqual(summary["redirects_dropped"], 2)
        # One redirect kept, plus a "Surname, Given" entry for every row whose
        # infobox says it is a person.
        self.assertGreaterEqual(summary["name_entries"], 1)
        self.assertEqual(summary["entries"], len(self.names) + 1 + summary["name_entries"])
        self.assertEqual(summary["tiers"][0]["articles"], 7)
        self.assertGreater(summary["shards"], 2)
        p = pf.Pack(out)
        self.assertEqual(p.manifest["snapshot"], "2026-05-13")
        e = p.lookup("alias one")
        self.assertTrue(e.redirect)
        self.assertEqual(p.article(e.locator).title, self.names[0])
        p.close()

    def test_limit(self):
        out, summary, _ = self.run_build("--no-vital", "--limit", "12")
        self.assertEqual(summary["articles"], 12)

    def test_vital_load_accepts_both_shapes(self):
        p = os.path.join(self.tmp, "v.json")
        with open(p, "w") as f:
            json.dump(
                {"A": {"level": 2}, "B": 4, "C": {"level": 9}, "D": {"topic": "x"}}, f
            )
        self.assertEqual(vital.load(p), {"A": 2, "B": 4})


if __name__ == "__main__":
    unittest.main()
