"""The fold: the Python module, the vectors, and the C++ header, all agreeing.

python3 -m unittest discover -s tools_local/wikipedia/tests -p 'test_*.py'
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(TOOL))
sys.path.insert(0, TOOL)

import fold_table  # noqa: E402
from fold import TABLE, fold, fold_bytes  # noqa: E402

VECTORS = os.path.join(TOOL, "fold_vectors.tsv")
HEADER_DIR = os.path.join(REPO, "src", "apps_local", "wikipedia")


class FoldRules(unittest.TestCase):
    def test_whitespace_and_underscore(self):
        self.assertEqual(fold("New_York__City "), "new york city")
        self.assertEqual(fold("  \t_ "), "")
        self.assertEqual(fold(""), "")
        self.assertEqual(fold("a b"), "a b")  # NBSP is not fold whitespace

    def test_ascii_lowercase_only(self):
        self.assertEqual(fold("ABC xyz 123 !?"), "abc xyz 123 !?")

    def test_table_examples_from_the_spec(self):
        self.assertEqual(fold("É"), "e")
        self.assertEqual(fold("é"), "e")
        self.assertEqual(fold("ß"), "ß")
        self.assertEqual(fold("Ω"), "ω")
        self.assertEqual(fold("ς"), "σ")
        self.assertEqual(fold("Ёлка"), "елка")

    def test_passthrough_outside_the_ranges(self):
        self.assertEqual(fold("東京 Ａ ẞ"), "東京 Ａ ẞ")

    def test_never_longer_in_utf8(self):
        for cp, r in fold_table.build_table():
            self.assertLessEqual(len(chr(r).encode()), len(chr(cp).encode()))

    def test_table_matches_generator(self):
        gen = {chr(a): chr(b) for a, b in fold_table.build_table()}
        self.assertEqual(TABLE, gen, "fold.py is stale: run fold_table.py")

    def test_no_replacement_is_whitespace(self):
        for cp, r in TABLE.items():
            self.assertNotIn(r, fold_table.WHITESPACE)
            self.assertFalse(r.isspace())


class FoldVectors(unittest.TestCase):
    def setUp(self):
        self.vectors = fold_table.load_vectors(VECTORS)

    def test_count(self):
        self.assertEqual(len(self.vectors), fold_table.VECTOR_COUNT)

    def test_python_agrees(self):
        for s, e in self.vectors:
            self.assertEqual(fold(s), e, repr(s))
            self.assertEqual(fold_bytes(s), e.encode("utf-8"))

    def test_vectors_match_generator(self):
        table = fold_table.build_table()
        regenerated = fold_table.make_vectors(table)
        self.assertEqual(
            self.vectors, regenerated, "fold_vectors.tsv is stale: run fold_table.py"
        )

    def test_cover_every_category(self):
        inputs = [s for s, _ in self.vectors]
        self.assertTrue(any("_" in s for s in inputs))
        self.assertTrue(any("  " in s for s in inputs))
        self.assertTrue(any(0x00C0 <= ord(c) <= 0x024F for s in inputs for c in s))
        self.assertTrue(any(0x0370 <= ord(c) <= 0x03FF for s in inputs for c in s))
        self.assertTrue(any(0x0400 <= ord(c) <= 0x04FF for s in inputs for c in s))
        self.assertTrue(any(0x4E00 <= ord(c) <= 0x9FFF for s in inputs for c in s))


class FoldHeader(unittest.TestCase):
    """Compiles WikipediaFold.h on the host and runs it over the vectors.

    Fails, not skips, without a C++ compiler: a fold that only one side
    verified is the bug the vectors exist to catch."""

    def test_header_is_current(self):
        with tempfile.TemporaryDirectory() as d:
            fold_table.emit_cpp(fold_table.build_table(), os.path.join(d, "h.h"))
            with open(os.path.join(d, "h.h"), "rb") as f:
                fresh = f.read()
        with open(os.path.join(HEADER_DIR, "WikipediaFold.h"), "rb") as f:
            committed = f.read()
        self.assertEqual(
            committed, fresh, "WikipediaFold.h is stale: run fold_table.py"
        )

    def test_cpp_agrees_with_vectors(self):
        cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(
            cxx, "no C++ compiler on PATH; the header cannot be checked"
        )
        with tempfile.TemporaryDirectory() as d:
            exe = os.path.join(d, "fold_check")
            build = subprocess.run(
                [
                    cxx,
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    HEADER_DIR,
                    os.path.join(HERE, "fold_check.cpp"),
                    "-o",
                    exe,
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([exe, VECTORS], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr + run.stdout)
            self.assertIn(
                f"{fold_table.VECTOR_COUNT + 3} vectors, 0 failed", run.stdout
            )


if __name__ == "__main__":
    unittest.main()
