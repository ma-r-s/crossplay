"""The row-to-XHTML converter: the spec's subset, on every row we have.

Runs over the committed fixture always, and over the 3,000-row research
sample when it is present on this machine (the path is printed either way,
so a run that only saw the fixture says so).
"""

import gzip
import json
import os
import re
import sys
import unittest
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
sys.path.insert(0, TOOL)

import article_html as ah  # noqa: E402
from article_html import TABLE_OMITTED, article_xhtml, heading_bytes, link_target  # noqa: E402

FIXTURE = os.path.join(HERE, "fixtures", "rows.jsonl.gz")
ALLOWED = {
    "html",
    "body",
    "h1",
    "h2",
    "h3",
    "h4",
    "p",
    "b",
    "i",
    "a",
    "ul",
    "li",
    "table",
    "tr",
    "th",
    "td",
}
WORKSPACE = os.path.dirname(os.path.dirname(os.path.dirname(TOOL)))
SAMPLE_CANDIDATES = [
    os.environ.get("WIKIPEDIA_SAMPLE_ROWS", ""),
    os.path.join(
        WORKSPACE,
        "wikipedia-research",
        "measurements",
        "structured_wikipedia_3000_rows.jsonl.gz",
    ),
    os.path.join(
        os.path.dirname(WORKSPACE),
        "wikipedia-research",
        "measurements",
        "structured_wikipedia_3000_rows.jsonl.gz",
    ),
]


def read_rows(path):
    with gzip.open(path, "rt", encoding="utf-8") as f:
        return [json.loads(line) for line in f]


def sample_path():
    for p in SAMPLE_CANDIDATES:
        if p and os.path.exists(p):
            return p
    return None


def check_document(tc, row, title, headings, xhtml):
    root = ET.fromstring(xhtml)  # well-formed, or this raises
    tc.assertEqual(root.tag, "html")
    tc.assertEqual([c.tag for c in root], ["body"])
    body = root[0]
    tc.assertEqual(body[0].tag, "h1")
    tc.assertEqual(body[0].text, title)
    h2s = []
    for el in root.iter():
        tc.assertIn(el.tag, ALLOWED, f"{title}: element {el.tag}")
        attrs = set(el.attrib)
        if el.tag == "h2":
            tc.assertEqual(attrs, {"id"}, f"{title}: h2 attributes {attrs}")
            h2s.append(el)
        elif el.tag == "a":
            tc.assertEqual(attrs, {"href"}, f"{title}: a attributes {attrs}")
            href = el.attrib["href"]
            tc.assertTrue(href, f"{title}: empty href")
            tc.assertNotIn("#", href)
            tc.assertNotIn("_", href)
            tc.assertIsNone(re.search(r"%[0-9A-Fa-f]{2}", href), f"{title}: encoded href {href}")
            tc.assertEqual(href, href.strip())
        else:
            tc.assertEqual(attrs, set(), f"{title}: {el.tag} has attributes {attrs}")
    ids = [h.attrib["id"] for h in h2s]
    tc.assertEqual(
        ids, [f"s{k}" for k in range(1, len(h2s) + 1)], f"{title}: ids {ids}"
    )
    tc.assertEqual(["".join(h.itertext()) for h in h2s], headings, f"{title}: headings")
    top = list(body)
    if headings and headings[0] == ah.QUICK_FACTS:
        first_h2 = next(i for i, el in enumerate(top) if el.tag == "h2")
        tc.assertEqual(top[first_h2].text, ah.QUICK_FACTS)
        for el in top[1:first_h2]:
            tc.assertNotIn(el.tag, ("h2", "h3", "h4"), f"{title}: {el.tag} before Quick facts")
        facts = top[first_h2 + 1]
        tc.assertEqual(facts.tag, "table", f"{title}: Quick facts is a grid")
        for tr in facts:
            tc.assertEqual(tr.tag, "tr")
            tc.assertEqual([c.tag for c in tr], ["th", "td"], f"{title}: a fact is key then value")
        for el in top[first_h2 + 2 :]:
            if el.tag == "h2":
                break
            tc.assertNotEqual(el.tag, "table", f"{title}: one grid of facts, then the article")
    for el in root.iter("li"):
        tc.assertIsNotNone(el)
    for el in root.iter("table"):
        rows = list(el)
        tc.assertTrue(all(r.tag == "tr" for r in rows))
        tc.assertLessEqual(max(len(r) for r in rows), ah.TABLE_MAX_COLS)
        for r in rows:
            for c in r:
                txt = "".join(c.itertext())
                tc.assertLessEqual(len(txt.split(" ")), ah.TABLE_CELL_WORDS)
                tc.assertLessEqual(len(txt.encode()), ah.TABLE_CELL_BYTES)
    text = xhtml.decode("utf-8")
    tc.assertNotIn("<div", text)
    tc.assertNotIn("<ol", text)
    tc.assertNotIn("<dl", text)
    tc.assertNotIn("style=", text)
    tc.assertNotIn("&#", text, f"{title}: numeric character reference")
    tc.assertIsNone(
        re.search(r"\[\d+\]", "".join(body.itertext())),
        f"{title}: citation mark survived",
    )
    for h in headings:
        tc.assertLessEqual(len(heading_bytes(h)), 255)
    undrawable = re.compile("[^" + ah.drawable_class() + "]")
    for el in root.iter():
        if el.tag in ("h1",):
            continue
        for s in (el.text or "", el.tail or ""):
            m = undrawable.search(s)
            tc.assertIsNone(
                m,
                f"{title}: undrawable U+{ord(m.group(0)):04X} in <{el.tag}>: {s[:80]!r}"
                if m
                else "",
            )


class Fixture(unittest.TestCase):
    rows = None

    @classmethod
    def setUpClass(cls):
        cls.rows = read_rows(FIXTURE)

    def test_fixture_is_not_tiny(self):
        self.assertGreaterEqual(len(self.rows), 30)

    def test_every_fixture_row(self):
        stats = {}
        for row in self.rows:
            title, headings, xhtml = article_xhtml(row, stats)
            check_document(self, row, title, headings, xhtml)
        self.assertGreater(stats.get("tables_kept", 0), 0)
        self.assertGreater(stats.get("tables_omitted", 0), 0)
        self.assertGreater(stats.get("runs_removed", 0), 0)
        self.assertGreater(stats.get("parentheticals_removed", 0), 0)
        self.assertGreater(stats.get("facts", 0), 0)
        self.assertGreater(stats.get("links_kept", 0), 0)

    def test_fixture_has_an_oversize_article(self):
        self.assertTrue(any(len(article_xhtml(r)[2]) > 65536 for r in self.rows))


class Sample(unittest.TestCase):
    """The 3,000 real rows, when this machine has them."""

    def test_every_sample_row(self):
        path = sample_path()
        if not path:
            print(
                "\n  SAMPLE NOT FOUND: article_html ran over the fixture only",
                flush=True,
            )
            return
        rows = read_rows(path)
        stats = {}
        for row in rows:
            title, headings, xhtml = article_xhtml(row, stats)
            check_document(self, row, title, headings, xhtml)
        print(f"\n  article_html: {len(rows)} sample rows ok; {stats}", flush=True)
        self.assertEqual(len(rows), 3000)


class Rules(unittest.TestCase):
    def convert(self, **row):
        row.setdefault("name", "Test")
        stats = {}
        t, h, x = article_xhtml(row, stats)
        ET.fromstring(x)
        return t, h, x.decode("utf-8"), stats

    def lead(self, *paragraphs, name="Test", **extra):
        secs = [{"type": "section", "name": "Abstract", "has_parts": list(paragraphs)}]
        secs += extra.pop("sections", [])
        return self.convert(name=name, sections=json.dumps(secs), **extra)

    def test_subject_is_bold(self):
        _, _, x, _ = self.lead(
            {"type": "paragraph", "value": "Earth is the third planet."}, name="Earth"
        )
        self.assertIn("<p><b>Earth</b> is the third planet.</p>", x)

    def test_subject_without_disambiguator_then_first_word(self):
        _, _, x, _ = self.lead(
            {"type": "paragraph", "value": "Mercury is the first planet."},
            name="Mercury (planet)",
        )
        self.assertIn("<b>Mercury</b> is", x)
        _, _, x, _ = self.lead(
            {"type": "paragraph", "value": "The Beatles were a band."},
            name="The Beatles",
        )
        self.assertIn("<b>The Beatles</b>", x)
        _, _, x, _ = self.lead(
            {"type": "paragraph", "value": "This is a list of films."},
            name="List of films: B",
        )
        self.assertNotIn("<b>", x)

    def test_subject_only_in_first_paragraph(self):
        _, _, x, _ = self.lead(
            {"type": "paragraph", "value": "Something else."},
            {"type": "paragraph", "value": "Earth is the third planet."},
            name="Earth",
        )
        self.assertNotIn("<b>", x)

    def test_subject_yields_to_a_straddling_link(self):
        p = {
            "type": "paragraph",
            "value": "New York City is big.",
            "links": [
                {"url": "https://en.wikipedia.org/wiki/City", "text": "York City"}
            ],
        }
        _, _, x, _ = self.lead(p, name="New York")
        self.assertNotIn("<b>", x)
        self.assertIn('<a href="City">York City</a>', x)

    def test_subject_may_wrap_a_link(self):
        p = {
            "type": "paragraph",
            "value": "New York City is big.",
            "links": [{"url": "https://en.wikipedia.org/wiki/York", "text": "York"}],
        }
        _, _, x, _ = self.lead(p, name="New York City")
        self.assertIn('<b>New <a href="York">York</a> City</b>', x)

    def test_links(self):
        p = {
            "type": "paragraph",
            "value": "See Tokyo, Tokyo Tower, a file, a category and a note.",
            "links": [
                {
                    "url": "https://en.wikipedia.org/wiki/T%C5%8Dky%C5%8D_Metropolis#History",
                    "text": "Tokyo",
                },
                {
                    "url": "https://en.wikipedia.org/wiki/Tokyo_Tower",
                    "text": "Tokyo Tower",
                },
                {"url": "https://en.wikipedia.org/wiki/File:X.jpg", "text": "file"},
                {
                    "url": "https://en.wikipedia.org/wiki/Category:Cities",
                    "text": "category",
                },
                {"url": "https://en.wikipedia.org/wiki/Test#cite_note-1"},
                {"url": "https://fr.wikipedia.org/wiki/Note", "text": "note"},
            ],
        }
        _, _, x, st = self.lead(p)
        self.assertIn('<a href="Tōkyō Metropolis">Tokyo</a>', x)
        self.assertIn('<a href="Tokyo Tower">Tokyo Tower</a>', x)
        self.assertNotIn('href="File', x)
        self.assertNotIn('href="Category', x)
        self.assertNotIn(">note</a>", x)
        self.assertEqual(st["links_kept"], 2)

    def test_link_target_rules(self):
        self.assertEqual(
            link_target("https://en.wikipedia.org/wiki/New_York_City"), "New York City"
        )
        self.assertIsNone(link_target("https://en.wikipedia.org/wiki/Special:Random"))
        self.assertIsNone(link_target("https://en.wikipedia.org/wiki/Help:Contents"))
        self.assertIsNone(link_target("https://en.wikipedia.org/wiki/Wikipedia:About"))
        self.assertIsNone(link_target("https://en.wikipedia.org/wiki/Template:Cite"))
        self.assertIsNone(link_target("https://en.wikipedia.org/wiki/X#cite_note-3"))
        self.assertIsNone(link_target("https://en.wiktionary.org/wiki/word"))
        self.assertEqual(link_target("https://en.wikipedia.org/wiki/A%26B"), "A&B")

    def test_self_link_dropped(self):
        p = {
            "type": "paragraph",
            "value": "Test links Test.",
            "links": [{"url": "https://en.wikipedia.org/wiki/Test", "text": "Test"}],
        }
        _, _, x, _ = self.lead(p)
        self.assertNotIn("<a", x)

    def test_link_anchors_to_first_occurrence(self):
        p = {
            "type": "paragraph",
            "value": "cat and cat",
            "links": [{"url": "https://en.wikipedia.org/wiki/Cat", "text": "cat"}],
        }
        _, _, x, _ = self.lead(p)
        self.assertIn('<p><a href="Cat">cat</a> and cat</p>', x)

    def test_citation_marks_and_escaping(self):
        p = {"type": "paragraph", "value": "Fish & chips[1] are <good>[23]."}
        _, _, x, _ = self.lead(p)
        self.assertIn("<p>Fish &amp; chips are &lt;good&gt;.</p>", x)

    def test_quick_facts_placement_and_cut(self):
        long_value = " ".join(f"w{i}" for i in range(50))
        box = [
            {
                "type": "infobox",
                "name": "Infobox x",
                "has_parts": [
                    {"type": "field", "name": "Born", "value": "1900"},
                    {
                        "type": "list",
                        "name": "Members",
                        "has_parts": [
                            {"type": "list_item", "value": "A"},
                            {"type": "list_item", "value": "B"},
                        ],
                    },
                    {"type": "field", "name": "Long", "value": long_value},
                    {"type": "field", "name": "Empty", "value": ""},
                ],
            }
        ]
        secs = [
            {
                "type": "section",
                "name": "History",
                "has_parts": [{"type": "paragraph", "value": "Then."}],
            }
        ]
        _, h, x, _ = self.lead(
            {"type": "paragraph", "value": "Test is a test."},
            infoboxes=json.dumps(box),
            sections=secs,
        )
        self.assertEqual(h, ["Quick facts", "History"])
        self.assertIn(
            '<h2 id="s1">Quick facts</h2><table><tr><th>Born</th><td>1900</td></tr>'
            "<tr><th>Members</th><td>A; B</td></tr><tr><th>Long</th><td>",
            x,
        )
        self.assertIn(" ".join(f"w{i}" for i in range(28)) + "...</td></tr></table>", x)
        self.assertIn('<h2 id="s2">History</h2>', x)
        self.assertNotIn("Empty", x)
        self.assertLess(x.index("Quick facts"), x.index("History"))
        self.assertLess(x.index("is a test"), x.index("Quick facts"))

    def test_no_infobox_no_facts_heading(self):
        _, h, x, _ = self.lead({"type": "paragraph", "value": "Test."}, infoboxes=None)
        self.assertEqual(h, [])
        self.assertNotIn("Quick facts", x)

    def test_sections_headings_and_skips(self):
        secs = [
            {
                "type": "section",
                "name": "History",
                "has_parts": [
                    {"type": "paragraph", "value": "Then."},
                    {
                        "type": "section",
                        "name": "Early",
                        "has_parts": [
                            {"type": "paragraph", "value": "Earlier."},
                            {
                                "type": "section",
                                "name": "Deep",
                                "has_parts": [
                                    {
                                        "type": "section",
                                        "name": "Deeper",
                                        "has_parts": [
                                            {"type": "paragraph", "value": "Deepest."}
                                        ],
                                    },
                                ],
                            },
                        ],
                    },
                ],
            },
            {
                "type": "section",
                "name": "References",
                "has_parts": [{"type": "paragraph", "value": "Ref."}],
            },
            {
                "type": "section",
                "name": "See also",
                "has_parts": [{"type": "paragraph", "value": "Also."}],
            },
            {
                "type": "section",
                "name": "Legacy",
                "has_parts": [{"type": "paragraph", "value": "Later."}],
            },
        ]
        _, h, x, st = self.lead({"type": "paragraph", "value": "Test."}, sections=secs)
        self.assertEqual(h, ["History", "Legacy"])
        self.assertIn(
            '<h2 id="s1">History</h2><p>Then.</p><h3>Early</h3><p>Earlier.</p><h4>Deep</h4><h4>Deeper</h4><p>Deepest.</p><h2 id="s2">Legacy</h2>',
            x,
        )
        self.assertNotIn("Ref.", x)
        self.assertNotIn("Also.", x)
        self.assertEqual(st["sections_skipped"], 2)

    def test_lists(self):
        secs = [
            {
                "type": "section",
                "name": "Lists",
                "has_parts": [
                    {
                        "type": "list",
                        "has_parts": [
                            {"type": "list_item", "value": "one"},
                            {
                                "type": "list_item",
                                "value": "two",
                                "has_parts": [
                                    {
                                        "type": "list",
                                        "has_parts": [
                                            {"type": "list_item", "value": "nested"}
                                        ],
                                    }
                                ],
                            },
                        ],
                    },
                    {
                        "type": "ordered_list",
                        "has_parts": [
                            {"type": "list_item", "value": "first"},
                            {"type": "list_item", "value": "second"},
                        ],
                    },
                    {"type": "list_item", "value": "loose a"},
                    {"type": "list_item", "value": "loose b"},
                    {
                        "type": "definition_list",
                        "has_parts": [
                            {"type": "definition_term", "value": "Term"},
                            {"type": "definition", "value": "Meaning"},
                        ],
                    },
                ],
            }
        ]
        _, _, x, _ = self.lead({"type": "paragraph", "value": "Test."}, sections=secs)
        self.assertIn("<ul><li>one</li><li>two<ul><li>nested</li></ul></li></ul>", x)
        self.assertIn("<p>1. first</p><p>2. second</p>", x)
        self.assertIn("<ul><li>loose a</li><li>loose b</li></ul>", x)
        self.assertIn("<p><b>Term</b></p><p>Meaning</p>", x)

    def test_tables(self):
        tables = [
            {
                "identifier": "t1",
                "headers": [[{"value": "A"}, {"value": "B"}]],
                "rows": [[{"value": "1"}, {"value": "2 & 3"}]],
            },
            {
                "identifier": "t2",
                "headers": [
                    [
                        {"value": "A"},
                        {"value": "B"},
                        {"value": "C"},
                        {"value": "D"},
                        {"value": "E"},
                    ]
                ],
                "rows": [],
            },
            {"identifier": "t3", "rows": [[{"value": " ".join(["w"] * 33)}]]},
            {"identifier": "t4", "rows": [[{"value": "x" * 513}]]},
            {"identifier": "t5", "rows": []},
        ]
        secs = [
            {
                "type": "section",
                "name": "T",
                "has_parts": [
                    {"type": "table", "table_references": [{"identifier": "t1"}]},
                    {"type": "table", "table_references": [{"identifier": "t2"}]},
                    {"type": "table", "table_references": [{"identifier": "t3"}]},
                    {"type": "table", "table_references": [{"identifier": "t4"}]},
                    {"type": "table", "table_references": [{"identifier": "t5"}]},
                    {"type": "table", "table_references": [{"identifier": "missing"}]},
                ],
            }
        ]
        _, _, x, st = self.lead(
            {"type": "paragraph", "value": "Test."},
            sections=secs,
            tables=json.dumps(tables),
        )
        self.assertIn(
            "<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2 &amp; 3</td></tr></table>",
            x,
        )
        self.assertEqual(x.count(TABLE_OMITTED), 4)
        self.assertEqual(st["tables_kept"], 1)
        self.assertEqual(st["tables_omitted"], 4)

    def test_undrawable_runs(self):
        _, _, x, st = self.lead(
            {
                "type": "paragraph",
                "value": "Suk Suk (Chinese: 叔．叔; lit. 'uncle') is a film.",
            },
            name="Suk Suk",
        )
        self.assertIn("<p><b>Suk Suk</b> (lit. 'uncle') is a film.</p>", x)
        self.assertEqual(st["parentheticals_removed"], 1)
        secs = [
            {
                "type": "section",
                "name": "Name",
                "has_parts": [
                    {
                        "type": "paragraph",
                        "value": "Tokyo (東京, Tōkyō) is written Japanese: 東京 in kanji.",
                    },
                    {
                        "type": "paragraph",
                        "value": "Ancient Greek: Ἀθῆναι was the name.",
                    },
                ],
            }
        ]
        _, _, x, st = self.lead({"type": "paragraph", "value": "Test."}, sections=secs)
        self.assertIn("<p>Tokyo (Tōkyō) is written in kanji.</p>", x)
        self.assertIn("<p>was the name.</p>", x)
        self.assertEqual(st["runs_removed"], 3)

    def test_lead_parenthetical_keeps_its_dates(self):
        st = {}
        got = ah.strip_undrawable(
            "Mahmud II (Ottoman Turkish: \u0645\u062d\u0645\u0648\u062f, romanized: X; "
            "20 July 1785 - 1 July 1839) was the sultan",
            st,
            lead=True,
        )
        self.assertEqual(got, "Mahmud II (20 July 1785 - 1 July 1839) was the sultan")
        self.assertEqual(st["parentheticals_removed"], 1)

    def test_source_remnants(self):
        cases = [
            (
                "Sobhuza II KBE (Swazi:; also known as Mona; 22 July 1899) was",
                "Sobhuza II KBE (also known as Mona; 22 July 1899) was",
            ),
            (
                "Contandin (French pronunciation:; 8 May 1903), known as Fernandel (), was",
                "Contandin (8 May 1903), known as Fernandel, was",
            ),
            ("Paju (Korean pronunciation:) is a city", "Paju is a city"),
            ('the " Peter the Great of Turkey", Mahmud', 'the "Peter the Great of Turkey", Mahmud'),
            ("meaning \" beech \". The ' Right2Water ' campaign", "meaning \"beech\". The 'Right2Water' campaign"),
            ("' Ali-Shir Nava'i (9 February 1441)", "'Ali-Shir Nava'i (9 February 1441)"),
            ("Harvey McGregor 's \" Contract Code \", a Law", "Harvey McGregor 's \"Contract Code\", a Law"),
            (
                "A pinata (/ p \u026a n j a t a /, Spanish pronunciation:) is a container",
                "A pinata is a container",
            ),
            (
                "Oceania (UK: OH-s(h)ee-AH-nee-\u0259, -AY-, US: OH-shee-A(H)N-ee-\u0259) is a region",
                "Oceania is a region",
            ),
        ]
        for src, want in cases:
            self.assertEqual(ah.strip_undrawable(src, {}, lead=True), want, src)
        # Left alone: a period that starts a word, an inch mark, an apostrophe,
        # a clock time, a label with a value.
        for src in [
            "The .NET Framework (.NET) is a thing; rock 'n' roll, a 12\" single.",
            "Ratio 3:1, at 10:30, see Note: this. It's the dog's 'best' day.",
        ]:
            self.assertEqual(ah.strip_undrawable(src, {}, lead=True), src)

    def test_citation_page_remnant(self):
        self.assertEqual(ah.clean_text("principles.: 6 The scope of"), "principles. The scope of")
        self.assertEqual(ah.clean_text("in 1990,: 12-14 and later"), "in 1990, and later")
        self.assertEqual(ah.clean_text("ratio of 3:1 and at 10:30"), "ratio of 3:1 and at 10:30")

    def test_fact_value(self):
        self.assertEqual(
            ah.fact_value("Died", "13 June 1645 (aged 60\u201361) Higo Province, Japan"),
            "13 June 1645, Higo Province, Japan",
        )
        self.assertEqual(
            ah.fact_value("Born", "Mala Helfgott 1930 (age 95 \u2013 96) Piotrkow Trybunalski"),
            "Mala Helfgott 1930, Piotrkow Trybunalski",
        )
        self.assertEqual(
            ah.fact_value("Children", "Mikinosuke (adopted) Kurotaro (adopted) Iori (adopted)"),
            "Mikinosuke (adopted), Kurotaro (adopted), Iori (adopted)",
        )
        self.assertEqual(ah.fact_value("Notable work", "1990 World Cup"), "1990 World Cup")
        self.assertEqual(
            ah.fact_value("Education", "Graz University of Technology (dropped out)"),
            "Graz University of Technology (dropped out)",
        )

    def test_link_text_with_undrawable_run(self):
        p = {
            "type": "paragraph",
            "value": "Go to 東京 now.",
            "links": [{"url": "https://en.wikipedia.org/wiki/Tokyo", "text": "東京"}],
        }
        _, _, x, _ = self.lead(p)
        self.assertEqual(x, "<html><body><h1>Test</h1><p>Go to now.</p></body></html>")

    def test_abstract_column_fallback(self):
        secs = [
            {
                "type": "section",
                "name": "History",
                "has_parts": [{"type": "paragraph", "value": "Then."}],
            }
        ]
        _, h, x, _ = self.convert(
            name="Test", abstract="Test is short.", sections=json.dumps(secs)
        )
        self.assertIn("<h1>Test</h1><p><b>Test</b> is short.</p>", x)

    def test_heading_bytes_cut_on_boundary(self):
        h = "é" * 200
        b = heading_bytes(h)
        self.assertLessEqual(len(b), 255)
        b.decode("utf-8")
        self.assertEqual(heading_bytes("short"), b"short")

    def test_row_without_name_refused(self):
        with self.assertRaises(ValueError):
            article_xhtml({"name": "", "sections": "[]"})


if __name__ == "__main__":
    unittest.main()
