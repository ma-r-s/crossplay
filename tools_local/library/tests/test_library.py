"""The library tools' invariants, standard library only.

    python3 -m unittest discover -s tools_local/library/tests -p 'test_*.py'

What these exist for: a merge key that folds two works into one or lets one
work through twice changes what a card holds; a form rule that catches a
real book because its record says "Indexes" drops Gibbon; a fill that is
not the greedy prefix breaks the "partial copy is the best subset" promise;
a card path that a FAT volume refuses stops the copy; a shard offset one
header off writes garbage as a book.
"""

import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)

import rank  # noqa: E402
import build_pack  # noqa: E402
import universe2  # noqa: E402


def row(title, author="Austen, Jane", lang="en", downloads=100, size=200_000, locc=None, subjects=None):
    return {"id": 1, "title": title, "creators": [{"name": author}], "languages": [lang], "type": "Text",
            "rights": "Public domain in the USA.", "downloads": downloads, "sizes": {"epub_noimages": size},
            "locc": locc or ["PR"], "subjects": subjects or [], "bookshelves": [], "alternative": []}


class WorkKey(unittest.TestCase):
    def test_edition_dressing_does_not_split_a_work(self):
        whole = rank.work_key(row("The Adventures of Tom Sawyer, Complete"))
        part = rank.work_key(row("The Adventures of Tom Sawyer, Part 1."))
        vol = rank.work_key(row("Oliver Twist, Vol. 2 (of 3)"))
        self.assertEqual(whole, part)
        self.assertEqual(vol, rank.work_key(row("Oliver Twist")))

    def test_subtitle_and_language_and_author_do_split(self):
        self.assertEqual(rank.work_key(row("Emma: A Novel")), rank.work_key(row("Emma")))
        self.assertNotEqual(rank.work_key(row("Hamlet", lang="en")), rank.work_key(row("Hamlet", lang="fi")))
        self.assertNotEqual(rank.work_key(row("Poems", author="Keats, John")), rank.work_key(row("Poems", author="Byron")))

    def test_the_book_named_book_is_not_stripped(self):
        # "book" strips only when followed by a number: The Jungle Book survives.
        self.assertEqual(rank.work_key(row("The Jungle Book"))[1], "jungle book")


class FormRule(unittest.TestCase):
    def test_periodicals_and_dictionaries_are_out(self):
        self.assertEqual(rank.form_rule(row("The Strand Magazine, Vol. 27", locc=["AP"])), "locc:AP")
        self.assertEqual(rank.form_rule(row("Modern English biography", subjects=["Great Britain -- Biography -- Dictionaries"])), "subject")

    def test_a_real_book_with_an_index_stays_in(self):
        # Gibbon carries the subject "Indexes"; the first rule set dropped him.
        self.assertIsNone(rank.form_rule(row("The History of the Decline and Fall of the Roman Empire", subjects=["Indexes"])))
        self.assertIsNone(rank.form_rule(row("Vegetables of the Garden", subjects=["Vegetables"])))


class Fill(unittest.TestCase):
    def test_greedy_prefix_is_within_one_book_of_the_fractional_bound(self):
        import random
        rnd = random.Random(7)
        books = [(rnd.randint(1, 100_000), rnd.randint(50_000, 900_000)) for _ in range(2000)]  # (value, bytes)
        budget = 40_000_000
        order = sorted(books, key=lambda b: -b[0] / b[1])
        used = got = 0
        first_nonfit = None
        for v, s in order:
            if used + s > budget:
                first_nonfit = v
                break
            used += s
            got += v
        # Dantzig: the fractional optimum is the prefix plus a fraction of the next book,
        # so the integer optimum is at most prefix + that book's whole value.
        self.assertGreaterEqual(got + first_nonfit, got)
        self.assertLess(first_nonfit / (got + first_nonfit), 0.01)  # one book is under 1% here


class Blend(unittest.TestCase):
    def test_missing_signals_keep_the_downloads_term_only(self):
        pool = [row("Junk", downloads=62_000), row("Pride and Prejudice", downloads=288_000)]
        pool[0]["id"], pool[1]["id"] = 1, 2
        with tempfile.TemporaryDirectory() as d:
            ol = os.path.join(d, "ol.jsonl")
            open(ol, "w").write(json.dumps({"id": 2, "want": 6000, "reading": 0, "read": 0, "ratings": 0}) + "\n")
            wiki = os.path.join(d, "wiki.jsonl")
            open(wiki, "w").write(json.dumps({"id": 2, "views": 2_000_000}) + "\n")
            rank.blend(pool, ol, wiki)
        self.assertGreater(pool[1]["value"], 10 * pool[0]["value"])

    def test_uncorroborated_views_do_not_count(self):
        magna = row("The Magna Carta", downloads=1_900)
        magna["id"] = 3
        alice = row("Alice", downloads=115_000)
        alice["id"] = 4
        pool = [magna, alice]
        with tempfile.TemporaryDirectory() as d:
            wiki = os.path.join(d, "wiki.jsonl")
            open(wiki, "w").write(json.dumps({"id": 3, "views": 1_330_000}) + "\n")
            rank.blend(pool, None, wiki)
        self.assertGreater(alice["value"], magna["value"])


class CardPath(unittest.TestCase):
    def test_ascii_budgets_and_reserved_names(self):
        p = build_pack.card_path({"lang": "fr", "title": "Les Misérables: Tome I", "creators": ["Hugo, Victor"]})
        self.assertEqual(p, "Library/fr/H/Hugo, Victor/Les Miserables- Tome I.epub")
        long_author = "Hancock, H. Irving (Harrie Irving) and many more words here"
        p = build_pack.card_path({"lang": "en", "title": "X" * 300, "creators": [long_author]})
        parts = p.split("/")
        self.assertLessEqual(len(parts[3].encode()), 34)
        self.assertLessEqual(len(parts[4].encode()), 100)
        self.assertEqual(build_pack.ascii_name("CON", 30), "_CON")
        self.assertEqual(build_pack.ascii_name("  trailing dots... ", 30), "trailing dots")
        self.assertEqual(build_pack.card_path({"lang": "en", "title": "Untitled", "creators": []}).split("/")[3], "Anonymous")


class Shards(unittest.TestCase):
    def test_files_read_back_from_their_recorded_offsets(self):
        with tempfile.TemporaryDirectory() as d:
            epubs = os.path.join(d, "epub")
            ranked = os.path.join(d, "ranked.jsonl")
            with open(ranked, "w") as f:
                for i in range(1, 8):
                    os.makedirs(os.path.join(epubs, str(i)))
                    open(os.path.join(epubs, str(i), f"pg{i}.epub"), "wb").write(b"PK" + bytes([i]) * (1000 * i + 7))
                    f.write(json.dumps({"rank": i, "id": i, "title": f"Book {i}", "creators": ["Author, A"],
                                        "lang": "en", "downloads": 10, "value": 1.0}) + "\n")
            out = os.path.join(d, "pack")
            subprocess.run([sys.executable, os.path.join(TOOLS, "build_pack.py"), "--ranked", ranked, "--epubs", epubs,
                            "--out", out, "--shard-mb", "1"], check=True, capture_output=True)
            m = json.load(open(os.path.join(out, "manifest.json")))
            self.assertEqual(m["books"], 7)
            for f in m["files"]:
                with open(os.path.join(out, "shards", f"{f['shard']:03d}.tar"), "rb") as t:
                    t.seek(f["offset"])
                    data = t.read(f["size"])
                with open(os.path.join(epubs, str(f["id"]), f"pg{f['id']}.epub"), "rb") as src:
                    self.assertEqual(data, src.read())
            # and tar itself agrees with the manifest
            with tarfile.open(os.path.join(out, "shards", "000.tar")) as t:
                names = t.getnames()
            self.assertEqual(names[0], m["files"][0]["path"])
            self.assertEqual(sum(1 for f in m["files"] if f["shard"] == 0), len(names))


class Universe(unittest.TestCase):
    def test_title_key_drops_series_and_dressing(self):
        self.assertEqual(universe2.title_key("Harry Potter and the Sorcerer's Stone (Harry Potter, #1)"),
                         universe2.title_key("Harry Potter and the Sorcerer's Stone"))
        self.assertEqual(universe2.surname("Jane Austen"), "austen")
        self.assertEqual(universe2.surname("Austen, Jane"), "austen")
        self.assertEqual(universe2.AMAZON_DRESSING.sub("", "Where the Crawdads Sing Paperback - 12 March 2019"),
                         "Where the Crawdads Sing")


if __name__ == "__main__":
    unittest.main()
