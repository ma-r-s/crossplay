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
            try:
                title, headings, xhtml = article_xhtml(row, stats)
            except ValueError as e:
                if str(e) == "redirect page":
                    continue  # the builders skip these too
                raise
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
        # t2 has headers and no rows and t5 has nothing: neither leaves a
        # mark. t3 (a 33-word cell) and t4 (a 513-byte cell) are too big for
        # the panel's grid and become one paragraph per row. Only the table
        # the row does not carry is a notice.
        self.assertEqual(x.count(TABLE_OMITTED), 1)
        self.assertEqual(st["tables_kept"], 1)
        self.assertEqual(st["tables_omitted"], 1)
        self.assertEqual(st["tables_listed"], 2)
        self.assertIn("<p><b>" + " ".join(["w"] * 33) + "</b></p>", x)

    def test_wide_table_becomes_row_paragraphs(self):
        tables = [
            {
                "identifier": "t1",
                "headers": [[{"value": "Year"}, {"value": "Category"}, {"value": "Work"}, {"value": "Result"}, {"value": "Ref."}]],
                "rows": [
                    [{"value": "1984"}, {"value": "Best Comedy Recording"}, {"value": "Eat It"}, {"value": "Won"}, {}],
                    [{"value": "1985"}, {"value": "Best Comedy Recording"}, {"value": ""}, {"value": "Nominated"}, {"value": "[3]"}],
                ],
            }
        ]
        secs = [{"type": "section", "name": "Awards", "has_parts": [{"type": "table", "table_references": [{"identifier": "t1"}]}]}]
        _, _, x, st = self.lead({"type": "paragraph", "value": "Test."}, sections=secs, tables=json.dumps(tables))
        self.assertIn("<p><b>1984</b>; Category: Best Comedy Recording; Work: Eat It; Result: Won</p>", x)
        self.assertIn("<p><b>1985</b>; Category: Best Comedy Recording; Result: Nominated</p>", x)
        self.assertNotIn("Ref", x)
        self.assertEqual(st["tables_listed"], 1)
        self.assertNotIn("tables_kept", st)

    def test_spanning_row_is_said_once(self):
        tables = [{"identifier": "t1", "headers": [[{"value": "Month"}, {"value": "Jan"}, {"value": "Feb"}, {"value": "Mar"}, {"value": "Apr"}]],
                   "rows": [[{"value": "Source: Met Office"}] * 5]}]
        secs = [{"type": "section", "name": "Climate", "has_parts": [{"type": "table", "table_references": [{"identifier": "t1"}]}]}]
        _, _, x, _ = self.lead({"type": "paragraph", "value": "Test."}, sections=secs, tables=json.dumps(tables))
        self.assertIn("<p><b>Source: Met Office</b></p>", x)
        self.assertNotIn("Jan: Source", x)

    def test_colon_paragraph_before_a_lost_image(self):
        long = "Both Bach and Handel featured canons in their works. The final variation of the Chaconne is a canon in which the right hand is imitated at one beat's distance:"
        secs = [
            {"type": "section", "name": "Baroque", "has_parts": [{"type": "paragraph", "value": long}, {"type": "image", "images": []}]},
            {"type": "section", "name": "Fashion", "has_parts": [{"type": "paragraph", "value": "Typical fashions in the 1930s:"}, {"type": "image", "images": []}]},
        ]
        _, heads, x, st = self.lead({"type": "paragraph", "value": "Test."}, sections=secs)
        self.assertIn("at one beat's distance.</p>", x)
        self.assertIn("Baroque", heads)
        self.assertNotIn("Fashion", heads)
        self.assertNotIn("Typical fashions", x)
        self.assertEqual(st["list_intros_dropped"], 2)

    def test_may_refer_to_intro_stays(self):
        secs = [{"type": "section", "name": "Arts", "has_parts": [{"type": "list", "has_parts": [{"type": "list_item", "value": "Art (band)"}]}]}]
        _, _, x, _ = self.lead({"type": "paragraph", "value": "ART may refer to:"}, name="ART", sections=secs)
        self.assertIn("<p><b>ART</b> may refer to:</p><h2", x)

    def test_empty_sections_go_and_ids_renumber(self):
        secs = [
            {"type": "section", "name": "Abstract", "has_parts": [{"type": "paragraph", "value": "Lead."}]},
            {"type": "section", "name": "Pictures", "has_parts": [{"type": "image", "images": []}]},
            {
                "type": "section",
                "name": "Boxes",
                "has_parts": [
                    {"type": "section", "name": "Nav", "has_parts": [{"type": "table", "table_references": [{"identifier": "nav"}]}]},
                ],
            },
            {"type": "section", "name": "Later", "has_parts": [{"type": "paragraph", "value": "More."}]},
        ]
        tables = [{"identifier": "nav", "rows": [[{"value": "This box: view talk edit"}, {"value": "x"}]]}]
        _, heads, x, st = self.convert(name="Test", sections=json.dumps(secs), tables=json.dumps(tables))
        self.assertEqual(heads, ["Later"])
        self.assertEqual(x, '<html><body><h1>Test</h1><p>Lead.</p><h2 id="s1">Later</h2><p>More.</p></body></html>')
        self.assertEqual(st["empty_sections_dropped"], 3)
        self.assertEqual(st["navboxes_dropped"], 1)
        self.assertNotIn("tables_omitted", st)

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
        self.assertIn("<p>Ancient Greek: Athenai was the name.</p>", x)
        self.assertEqual(st["runs_removed"], 2)
        self.assertEqual(st["runs_romanized"], 1)

    def test_lead_parenthetical_keeps_its_dates(self):
        st = {}
        got = ah.strip_undrawable(
            "Mahmud II (Ottoman Turkish: \u0645\u062d\u0645\u0648\u062f, romanized: X; "
            "20 July 1785 - 1 July 1839) was the sultan",
            st,
            lead=True,
        )
        # the Arabic goes with its label; the romanisation is information
        self.assertEqual(got, "Mahmud II (Ottoman Turkish: X; 20 July 1785 - 1 July 1839) was the sultan")
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
            ("Harvey McGregor 's \" Contract Code \", a Law", "Harvey McGregor's \"Contract Code\", a Law"),
            (
                "Breath of the Wild 's world , and Zelda \u2019s conventions ; a 12\" single . Done ?",
                "Breath of the Wild's world, and Zelda\u2019s conventions; a 12\" single. Done?",
            ),
            (
                "a Protestant -led group in the post- Civil War South, pre- and post-war, two- or three-day",
                "a Protestant-led group in the post-Civil War South, pre- and post-war, two- or three-day",
            ),
            (
                "The Tamils (TAM-ilz, TAHM-), also known; Chaos (KAY-oss) is; Foo (US-based) is",
                "The Tamils, also known; Chaos (KAY-oss) is; Foo (US-based) is",
            ),
            # Symbols the serif lacks are spelled, not dropped: "where a 0" said
            # something false. A lone Greek letter is a symbol and gets its
            # name; a Greek word is a run and goes.
            ("where a \u2260 0 and x \u2264 \u22121", "where a != 0 and x <= \u22121"),
            # the tidy after a removal must not glue "!=" to its left operand
            ("where a \u2260 0 (Greek: \u03b1\u03bb\u03c6\u03b1) holds", "where a != 0 (Greek: alpha) holds"),
            ("Goudreau \u2014on backup vocals, 1990\u2013 1995, and a spaced \u2014 dash stays", "Goudreau\u2014on backup vocals, 1990\u20131995, and a spaced \u2014 dash stays"),
            # From a reviewer's read of thirty articles (2026-09-11): a letter whose
            # accented form the serif lacks keeps its base letter; a pronunciation
            # guide's word does not outlive the guide; the mixed-number template
            # reads as a number; a spaced unit power is a power; an entity the
            # source escaped twice is a character.
            ("known as a ma\u1e47\u1e0dal\u012b.", "known as a mandal\u012b."),
            ("Elchingen (pronounced [mi\u0283\u025bl ne]; 10 January 1769) was", "Elchingen (10 January 1769) was"),
            ("6\u201312 cm (2 + 1 \u2044 4 \u2013 4 + 3 \u2044 4 in) long", "6\u201312 cm (2 1/4 \u2013 4 3/4 in) long"),
            ("Density 3,855/km 2 (9,985/sq mi)", "Density 3,855/km\u00b2 (9,985/sq mi)"),
            ("the angle \u03b8 and 10 \u03bcm of \u0394x", "the angle theta and 10 \u00b5m of Delta x"),
            ("(Greek: \u1f08\u03bb\u03ad\u03be\u03b1\u03bd\u03b4\u03c1\u03bf\u03c2) then (\u8f9b\u4ea5, \u53d4) ok", "(Greek: Alexandros) then ok"),
            (
                "A pinata (/ p \u026a n j a t a /, Spanish pronunciation:) is a container",
                "A pinata is a container",
            ),
            (
                "Oceania (UK: OH-s(h)ee-AH-nee-\u0259, -AY-, US: OH-shee-A(H)N-ee-\u0259) is a region",
                "Oceania is a region",
            ),
            # From the character census of the essentials (2026-09-11). A
            # letter whose mark the serif has is drawn as letter plus mark,
            # nothing lost; a compatibility character is its plain form; a
            # flat or sharp is spelled, because "D major" is a different key;
            # a suffix written on its own keeps its space.
            ("Ma\u1e25m\u016bd Mu\u1e63\u1e6daf\u0101", "Mahm\u016bd Mustaf\u0101"),

            ("at 25 \u2103, page \u216b, item \u2460, \U0001d513 4", "at 25 \u00b0C, page XII, item 1, P 4"),
            ("in D \u266d major and F \u266f minor, B\u266e", "in D-flat major and F-sharp minor, B-natural"),
            ("Final -m was dropped; the suffix -ing and -am, -em, -um; a Protestant -led group", "Final -m was dropped; the suffix -ing and -am, -em, -um; a Protestant-led group"),
            ("Mass \u2273 10 5 M\u2609 and \u2205 \u2229 A", "Mass >~ 10 5 M(sun) and empty set intersect A"),
            ("Hawai\u02bbi, \u02bfAl\u012b and the Qur\u02beān", "Hawai\u2018i, \u2018Al\u012b and the Qur\u2019\u0101n"),
            # A letter of an orthography folds inside a word; a pronunciation
            # goes whole before any spelling, so its theta is not "theta" and
            # its schwa is not a letter; a respelling's "-\u0259-" is not a word.
            ("C\u0259lil M\u0259mm\u0259dquluzad\u0259 wrote; laamii\u0257o; Bum\u00efn qa\u0263an; \u01c3Nanseb", "Celil Memmedquluzade wrote; laamiido; Bum\u00efn qa\u011fan; !Nanseb"),
            ("Theophrastus (/ \u02cc \u03b8 i\u02d0. \u0259 /; Ancient Greek: \u0398\u03b5\u03cc\u03c6\u03c1\u03b1\u03c3\u03c4\u03bf\u03c2, romanized: Theophrastos) was", "Theophrastus (Ancient Greek: Theophrastos) was"),
            ("Camogie (/ k \u0259 \u02c8 m o\u028a \u0261 i / k\u0259- MOH -ghee; Irish: cam\u00f3ga\u00edocht) is", "Camogie (Irish: cam\u00f3ga\u00edocht) is"),
            # Mario, 2026-09-11: Greek is defensible, Cyrillic is not, and no
            # removal may butcher the sentence. A Greek or Cyrillic word with
            # no romanisation beside it is romanised where it stands; with
            # one beside it, the word goes and the romanisation stays, label
            # and all; two accentuations of one word are one spelling.
            ("comes from the Greek word \u1f55\u03b2\u03bf\u03c2 or \u1f51\u03b2\u03cc\u03c2 meaning hump and \u1f40\u03b4\u03bf\u03cd\u03c2, meaning tooth.", "comes from the Greek word hybos meaning hump and odous, meaning tooth."),
            ("(from Greek \u1f08\u03c1\u03b9\u03b8\u03bc\u03bf\u03af, Arithmoi, lit. 'numbers'; Biblical Hebrew: \u05d1\u05b0\u05bc\u05de\u05b4\u05d3\u05b0\u05d1\u05b7\u05bc\u05e8, B\u0259m\u012b\u1e0fbar, lit. 'In desert'; Latin: Liber Numeri) is", "(from Greek Arithmoi, lit. 'numbers'; Biblical Hebrew: Bem\u012bdbar, lit. 'In desert'; Latin: Liber Numeri) is"),
            ("Seventeen Moments (Russian: \u0421\u0435\u043c\u043d\u0430\u0434\u0446\u0430\u0442\u044c, romanized: Semnadtsat') is a series about \u041c\u043e\u0441\u043a\u0432\u0430 and (\u0422\u043e\u043b\u0441\u0442\u043e\u0439).", "Seventeen Moments (Russian: Semnadtsat') is a series about Moskva and (Tolstoy)."),
            # the romanisation is the English word itself: the aside says nothing
            ("The pentathlon (Greek: \u03c0\u03ad\u03bd\u03c4\u03b1\u03b8\u03bb\u03bf\u03bd) was", "The pentathlon was"),
            ("The contest (Greek: \u03c0\u03ad\u03bd\u03c4\u03b1\u03b8\u03bb\u03bf\u03bd) was", "The contest (Greek: pentathlon) was"),
        ]
        for src, want in cases:
            self.assertEqual(ah.strip_undrawable(src, {}, lead=True), want, src)
        # clean_text repairs what the source flattened before any rule runs:
        # a lost space after a period, a power of ten as a spaced digit.
        for src, want in [
            ("lasted 28 days.The truce held, e.g.The end, Inc.The", "lasted 28 days. The truce held, e.g.The end, Inc.The"),
            ("Mass 10 5 M and 10 -3 m, the year 10 and 10 5,000 and 10 5.5", "Mass 10\u2075 M and 10\u207b\u00b3 m, the year 10 and 10 5,000 and 10 5.5"),
            # the invisible marks the panel smudges are gone, the words whole
            ("left\u200eto\u200fright, word\u2060joiner, soft\u00adhyphen, zero\u200bwidth\ufeff", "lefttoright, wordjoiner, softhyphen, zerowidth"),
            # every TeX wrapper goes, not just displaystyle; a citation template
            # left in the prose goes with its maintenance note; nested list
            # items the source ran together after a year come apart
            ("log 10 (d + 1 d) {\\textstyle \\log _{10}\\left({\\frac {d+1}{d}}\\right)}. The", "log\u2081\u2080((d + 1)/d). The"),
            ("teach it to me. {{ cite journal }}: CS1 maint: DOI inactive as of June 2024 (link) Next", "teach it to me. Next"),
            ("Fowler & Bean, 1929Genus Naso, 1801 and 1990s", "Fowler & Bean, 1929 Genus Naso, 1801 and 1990s"),
        ]:
            self.assertEqual(ah.clean_text(src), want, src)
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

    def test_math_and_greek(self):
        self.assertEqual(
            ah.clean_text("in which n 2 {\\displaystyle n_{2}} is the density"),
            "in which n\u2082 is the density",
        )
        self.assertEqual(
            ah.clean_text("= h 4 n 2 A 21, {\\displaystyle \\varepsilon ={\\frac {h\\nu }{4\\pi }}n_{2}A_{21},} where"),
            "= h 4 n 2 A 21, where",
        )
        self.assertEqual(ah.clean_text("at frequency \u03bd may"), "at frequency nu may")
        self.assertEqual(ah.clean_text("Ancient Greek: \u1f08\u03b8\u1fc6\u03bd\u03b1\u03b9 was"),
                         "Ancient Greek: \u1f08\u03b8\u1fc6\u03bd\u03b1\u03b9 was")

    def test_navbox_omitted(self):
        tables = [
            {
                "identifier": "t1",
                "rows": [
                    [{"value": "Life timeline"}, {"value": "Life timeline"}],
                    [{"value": "This box: view\ntalk\nedit"}, {"value": "This box: view\ntalk\nedit"}],
                ],
            }
        ]
        secs = [
            {
                "type": "section",
                "name": "Evolution",
                "has_parts": [{"type": "paragraph", "value": "Then."}, {"type": "table", "identifier": "t1"}],
            }
        ]
        _, _, x, st = self.convert(name="Test", sections=json.dumps(secs), tables=json.dumps(tables))
        self.assertNotIn("<table>", x)
        self.assertEqual(st["tables_omitted"], 1)

    def test_person_alias(self):
        born = json.dumps([{"type": "infobox", "has_parts": [{"type": "field", "name": "Born", "value": "1756"}]}])
        self.assertEqual(ah.person_alias({"name": "Wolfgang Amadeus Mozart", "infoboxes": born}),
                         "Mozart, Wolfgang Amadeus")
        self.assertEqual(ah.person_alias({"name": "Ludwig van Beethoven", "infoboxes": born}),
                         "Beethoven, Ludwig van")
        self.assertIsNone(ah.person_alias({"name": "Mahmud II", "infoboxes": born}))
        self.assertIsNone(ah.person_alias({"name": "Einstein coefficients", "infoboxes": "[]"}))
        self.assertIsNone(ah.person_alias({"name": "Tokyo", "infoboxes": born}))
        self.assertIsNone(ah.person_alias({"name": "Battle of Hastings (1066)", "infoboxes": born}))

    def test_redirect_page_refused(self):
        with self.assertRaises(ValueError):
            self.lead(
                {"type": "paragraph", "value": "%5B%5BWikipedia%3ARedirects+for+discussion%5D%5D+debate"},
                {"type": "paragraph", "value": "#REDIRECT Paris"},
                name="PariS",
            )

    def test_hatnotes_and_orphan_list_intro_go(self):
        secs = [
            {"type": "section", "name": "Fashion", "has_parts": [
                {"type": "paragraph", "value": "Main article: 1930s in fashion"},
                {"type": "paragraph", "value": "Typical fashions in the 1930s:"},
                {"type": "section", "name": "Hats", "has_parts": [
                    {"type": "paragraph", "value": "For other uses, see Hat (disambiguation)."},
                    {"type": "paragraph", "value": "Hats were worn. The list of hats is:"},
                    {"type": "list", "has_parts": [{"type": "list_item", "value": "Fedora"}]},
                ]},
            ]},
        ]
        _, heads, x, st = self.lead({"type": "paragraph", "value": "Test."}, sections=secs)
        self.assertNotIn("Main article", x)
        self.assertNotIn("For other uses", x)
        self.assertNotIn("Typical fashions", x)
        self.assertIn("<p>Hats were worn. The list of hats is:</p><ul>", x)
        self.assertEqual(st["hatnotes_dropped"], 2)
        self.assertEqual(st["list_intros_dropped"], 1)

    def test_cold_review_round_five(self):
        # From the cold reviewer's read of the q4 sample (2026-09-11): a label
        # left standing after its content went, a possessive apostrophe that
        # jumped to the next word, quotes paired by count instead of by what
        # touches them, the audio link's "(listen)".
        cases = [
            ("Karma (/ \u02c8 k \u0251\u02d0r m \u0259 /, from Sanskrit: \u0915\u0930\u094d\u092e, IPA:; Pali: kamma) is an ancient", "Karma (Pali: kamma) is an ancient"),
            ("A raga (/ \u02c8 r \u0251\u02d0 \u0261 \u0259 / RAH-g\u0259; IAST: r\u0101ga, Sanskrit:; lit. ' colouring', 'tingeing ' or ' dyeing ') is", "A raga (IAST: r\u0101ga; lit. 'colouring', 'tingeing' or 'dyeing') is"),
            ("won in Athens ' City Dionysia festival in 472 BC. It is Aeschylus' oldest play, the \" best \" one.", "won in Athens' City Dionysia festival in 472 BC. It is Aeschylus' oldest play, the \"best\" one."),
            ("The Persians (Ancient Greek: \u03a0\u03ad\u03c1\u03c3\u03b1\u03b9, romanized: P\u00e9rsai, Latinised as Persae) is", "The Persians (Ancient Greek: P\u00e9rsai, Latinised as Persae) is"),
            ("A samosa (listen) is a fried pastry with epsilon : Permittivity", "A samosa is a fried pastry with epsilon: Permittivity"),
        ]
        for src, want in cases:
            self.assertEqual(ah.strip_undrawable(ah.clean_text(src), {}, lead=True), want, src)
        self.assertEqual(ah.fact_value("Chemical formula", "C 20 H 8 Br 2 Hg Na 2 O 6"), "C\u2082\u2080H\u2088Br\u2082HgNa\u2082O\u2086")
        self.assertEqual(ah.fact_value("Molar mass", "750.658 g\u00b7mol \u22121"), "750.658 g\u00b7mol\u207b\u00b9")
        self.assertEqual(ah.fact_value("Coordination", "Tetrahedral (Zn 2+), Tetrahedral (S 2\u2212)"), "Tetrahedral (Zn\u00b2\u207a), Tetrahedral (S\u00b2\u207b)")
        self.assertEqual(ah.fact_value("Declination", "6.63 \u00b0 to 35.69 \u00b0"), "6.63\u00b0 to 35.69\u00b0")

    def test_cold_review_round_six(self):
        # From the third cold read (2026-09-12): a unit rule that superscripted
        # a date ("March 4" read "March⁴") because "March" ends in h; the
        # whole token must be the unit, and an ion's charge follows an element
        # symbol only. Citation page numbers in a row, a comma without its
        # space, TeX the dump left outside any block, alternative years.
        self.assertEqual(ah.fact_value("Died", "March 4, 1994, Durango"), "March 4, 1994, Durango")
        self.assertEqual(ah.fact_value("Wade", "Li 3-ching 1"), "Li 3-ching 1")
        self.assertEqual(ah.fact_value("Molar mass", "750.658 g\u00b7mol \u22121 and 3 m 2"), "750.658 g\u00b7mol\u207b\u00b9 and 3 m\u00b2")
        self.assertEqual(ah.fact_value("Born", "26 May 1564: 90 /1563 Sirhind"), "26 May 1564/1563, Sirhind")
        self.assertEqual(
            ah.clean_text("over a phone.: S643 : S643 : 8 In adults; driven. : 32, 33, 105 : 184 Next; invasion,the land; A = A 1 A_{1}^{\\complement }\\quad A_{2}^{\\complement } end"),
            "over a phone. In adults; driven. Next; invasion, the land; A = A 1 end",
        )
        # the assembled paragraph is scrubbed across its links: a label the
        # dump left empty before ")" and a removed character at a boundary
        secs = [{"type": "section", "name": "Abstract", "has_parts": [{"type": "paragraph", "value": "The Hara (\"the quarter\"; Arabic:) and U+0374 \u02b9 GREEK sign.", "links": [{"url": "https://en.wikipedia.org/wiki/Hara", "text": "Hara"}, {"url": "https://en.wikipedia.org/wiki/Greek", "text": "GREEK"}]}]}]
        _, _, x, _ = self.convert(name="D", sections=json.dumps(secs))
        self.assertIn('("the quarter") and U+0374 <a href="Greek">GREEK</a> sign.', x)
        self.assertNotIn("Arabic", x)
        self.assertNotIn("  ", x)

    def test_round_ten_from_the_fifth_read(self):
        # a slashed span is a pronunciation only when it holds IPA: "10
        # μg/dL (10 μg/100 g)" is units; a label may open with a lowercase
        # word ("simplified Chinese:"); a romanisation the sentence already
        # has goes with its label; "(more)" is a dead link; a value left as
        # "(muntala)" is its romanisation; electron shells keep their powers;
        # two adjacent links in a taxon row stay as they are.
        cases = [
            ("adults at 10 \u03bcg/dL (10 \u03bcg/100 g) and children at 3.5 \u03bcg/dL. Next", "adults at 10 \u00b5g/dL (10 \u00b5g/100 g) and children at 3.5 \u00b5g/dL. Next"),
            ("\"Southern China\" (simplified Chinese: \u4e2d\u56fd\u5357\u65b9; traditional Chinese: \u4e2d\u570b\u5357\u65b9) is geographically", "\"Southern China\" is geographically"),
            ("The Neretva (Serbian Cyrillic: \u041d\u0435\u0440\u0435\u0442\u0432\u0430), also known as Narenta, is a river", "The Neretva, also known as Narenta, is a river"),
            ("1st: 740 kJ/mol; (more) (all but first estimated)", "1st: 740 kJ/mol; (all but first estimated)"),
            ("Myiasis (/ m a\u026a \u02c8 a\u026a \u0259 s \u0259 s / my- EYE -\u0259-s\u0259ss) is a fly", "Myiasis is a fly"),
        ]
        for src, want in cases:
            self.assertEqual(ah.strip_undrawable(ah.clean_text(src), {}, lead=True), want, src)
        self.assertEqual(ah.fact_value("Nepali", "(muntala)"), "muntala")
        self.assertEqual(ah.fact_value("Electron configuration", "5f 14 6d 5 7s 2"), "5f\u00b9\u2074 6d\u2075 7s\u00b2")
        self.assertEqual(ah._split_at_links("Tortricoidea Latreille, 1803", [{"text": "Tortricoidea"}, {"text": "Latreille"}], "Superfamily"), "Tortricoidea Latreille, 1803")

    def test_round_twelve_from_the_full_pack_sample(self):
        # A cold read of thirty articles from the full pack (stubs with
        # infoboxes, which the essentials rarely are): hidden template spans
        # and navigation words leaking as text, a name glued to its date, a
        # spanning cell repeated per column, stacked header rows, a unit
        # rule that superscripted a longitude.
        cases = [
            ("58°37′25″N 8°55′40″E / 58.623675°N 08.927698°E The church is", "The church is"),
            ("released April 26, 1994 (1994-04-26) by", "released April 26, 1994 by"),
            ("6 April (2001-04-06) – 29 June 2001 (2001-06-29)", "6 April – 29 June 2001"),
            ("(Pub. L. Tooltip Public Law (United States)107–252 (text) (PDF))", "(Pub. L. 107–252)"),
            ("Team v t e", "Team"),
        ]
        for src, want in cases:
            self.assertEqual(ah.strip_undrawable(ah.clean_text(src), {}, lead=True), want, src)
        self.assertEqual(ah.fact_value("Born", "Weslyn Melva Dunford October 2, 1945 Lethbridge, Alberta"), "Weslyn Melva Dunford, October 2, 1945, Lethbridge, Alberta")
        self.assertEqual(ah.fact_value("Born", "Innokenty Mikhailovich Smoktunovich 28 March 1925 Tatyanovka"), "Innokenty Mikhailovich Smoktunovich, 28 March 1925, Tatyanovka")
        self.assertEqual(ah.fact_value("Died", "born 3 May 1959 Chicago"), "born 3 May 1959, Chicago")
        self.assertEqual(ah.fact_value("Coordinates", "58°37′25″N 8°55′40″E"), "58°37′25″N 8°55′40″E")
        tables = [{"identifier": "t1",
                   "headers": [[{"value": "Athlete"}, {"value": "Event"}, {"value": "Final"}, {"value": "Final"}],
                               [{"value": "Athlete"}, {"value": "Event"}, {"value": "Result"}, {"value": "Rank"}]],
                   "rows": [[{"value": "Antoni"}, {"value": "Marathon"}, {"value": "2:55:23"}, {"value": "57"}],
                            [{"value": "Felipo"}, {"value": "100 m"}, {"value": "did not advance"}, {"value": "did not advance"}]]}]
        secs = [{"type": "section", "name": "Athletics", "has_parts": [{"type": "table", "table_references": [{"identifier": "t1"}]}]}]
        _, _, x, st = self.lead({"type": "paragraph", "value": "Test."}, sections=secs, tables=json.dumps(tables))
        self.assertIn("<tr><th>Athlete</th><th>Event</th><th>Result</th><th>Rank</th></tr>", x)
        self.assertNotIn("<th>Final</th>", x)
        wide = [{"identifier": "t2",
                 "headers": [[{"value": "Athlete"}, {"value": "Event"}, {"value": "Heat"}, {"value": "Rank"}, {"value": "Semi"}, {"value": "Rank"}, {"value": "Final"}, {"value": "Rank"}]],
                 "rows": [[{"value": "Felipo"}, {"value": "100 m"}, {"value": "11.2"}, {"value": "7"}, {"value": "did not advance"}, {"value": "did not advance"}, {"value": "did not advance"}, {"value": "did not advance"}]]}]
        secs = [{"type": "section", "name": "Athletics", "has_parts": [{"type": "table", "table_references": [{"identifier": "t2"}]}]}]
        _, _, x, st = self.lead({"type": "paragraph", "value": "Test."}, sections=secs, tables=json.dumps(wide))
        self.assertIn("<p><b>Felipo</b>; Event: 100 m; Heat: 11.2; Rank: 7; Semi: did not advance</p>", x)
        # the same read, traced on the dump's own rows: "v t e" arrives on
        # three lines; a chembox sub-label is glued to its value; a running
        # time is spaced; two links that are the whole value are a list; a
        # definition list keeps its values; a romanisation in parentheses
        # beside the Greek word stands alone once the word is romanised
        self.assertEqual(ah.strip_undrawable(ah.clean_text("Team\nv\nt\ne"), {}), "Team")
        self.assertEqual(ah.fact_value("Names", "Preferred IUPAC name Methyl methanesulfonate"), "Preferred IUPAC name: Methyl methanesulfonate")
        self.assertEqual(ah.fact_value("Length", "61: 49"), "61:49")
        self.assertEqual(ah.fact_value("Score", "61: 49"), "61: 49")
        self.assertEqual(ah._split_at_links("Mark Waid Alex Ross", [{"text": "Mark Waid"}, {"text": "Alex Ross"}], "Created by"), "Mark Waid; Alex Ross")
        self.assertEqual(
            ah.strip_undrawable(ah.clean_text("related to Greek ἄγγελος (ángelos) – \"messenger\". The poets"), {}, lead=True),
            "related to Greek ángelos – \"messenger\". The poets",
        )
        row = {"name": "T", "infoboxes": [{"name": "Infobox", "has_parts": [{"type": "list", "name": "Medals", "has_parts": [
            {"type": "definition_term", "value": "Gold", "has_parts": [{"type": "definition", "value": "0"}]},
            {"type": "definition_term", "value": "Silver", "has_parts": [{"type": "definition", "value": "1"}]}]}]}],
            "sections": [{"type": "section", "name": "Abstract", "has_parts": [{"type": "paragraph", "value": "Test."}]}]}
        self.assertIn("<tr><th>Medals</th><td>Gold 0; Silver 1</td></tr>", ah.article_xhtml(row, {})[2].decode())
        # a second cold read of a fresh full sample: "in 2:11:53" is a time,
        # not square inches; a label before another label is empty; the
        # end-date template's copy of the year; a nameless "Coordinates: ..."
        # value takes its label as its name and keeps one notation; the
        # infobox's own header is not a group; a word leaked into a name;
        # "Official website" becomes the address the reader can type
        self.assertEqual(ah.strip_undrawable(ah.clean_text("who broke the world record in 2:11:53 at"), {}), "who broke the world record in 2:11:53 at")
        self.assertEqual(
            ah.strip_undrawable(ah.clean_text("Namgyal (Tibetan: Wylie: zhabs drung ngag dbang rnam rgyal; 1594) was"), {}, lead=True),
            "Namgyal (Wylie: zhabs drung ngag dbang rnam rgyal; 1594) was",
        )
        self.assertEqual(ah.fact_value("Defunct", "1939 (1939)"), "1939")
        self.assertEqual(ah.fact_value("Founded", "1939 (1940)"), "1939 (1940)")
        row = {"name": "John Bloomfield (British Army officer)", "infoboxes": [{"name": "Infobox military person", "has_parts": [
            {"type": "section", "name": "General Sir John Bloomfield GCB", "has_parts": [
                {"type": "field", "name": "Rank", "value": "General"},
                {"type": "field", "name": "team Former teams", "value": "Retired"},
                {"type": "field", "name": "Website", "value": "Official website", "links": [{"url": "https://www.comune.frascineto.cs.it/", "text": "Official website"}]},
            ]},
            {"type": "section", "name": "Plateau", "has_parts": [
                {"type": "field", "value": "Coordinates: 41°37′00″N 44°00′00″E / 41.61667°N 44.00000°E"},
            ]}]}],
            "sections": [{"type": "section", "name": "Abstract", "has_parts": [{"type": "paragraph", "value": "John Bloomfield was a general."}]}]}
        x = ah.article_xhtml(row, {})[2].decode()
        self.assertIn("<tr><th>Rank</th><td>General</td></tr>", x)
        self.assertIn("<tr><th>Former teams</th><td>Retired</td></tr>", x)
        self.assertIn("<tr><th>Website</th><td>comune.frascineto.cs.it</td></tr>", x)
        self.assertIn("<tr><th>Coordinates</th><td>41°37′00″N 44°00′00″E</td></tr>", x)
        self.assertNotIn("GCB", x.split("Quick facts")[1])
        # a third read: a table row of labels with nothing after them goes
        # ("Source:"), a row repeated goes, a table left with no rows goes; a
        # footnote digit on a fact's name ("Area 1") goes; " /" gets its
        # spaces except between two years; a link's caption with no link
        # ("Listen live") is no fact; an office of nine words is still a group
        row = {"name": "Bergbieten", "infoboxes": [{"name": "Infobox", "has_parts": [
            {"type": "section", "name": "Judge of the Washington Court of Appeals, Division One", "has_parts": [{"type": "field", "name": "Preceded by", "value": "Ronald Cox"}]},
            {"type": "field", "name": "Area 1", "value": "5.2 km2"}, {"type": "field", "name": "INSEE /Postal code", "value": "67030 /67310"},
            {"type": "field", "name": "Webcast", "value": "Listen live"}]}],
            "sections": [{"type": "section", "name": "Abstract", "has_parts": [{"type": "paragraph", "value": "Test."}]},
                {"type": "section", "name": "Population", "has_parts": [{"type": "table", "table_references": [{"identifier": "t1"}, {"identifier": "t2"}]}]}],
            "tables": json.dumps([
                {"identifier": "t1", "headers": [[{"value": "Year"}, {"value": "Pop."}]],
                 "rows": [[{"value": "1968"}, {"value": "394"}], [{"value": "Source: INSEE"}, {"value": "Source: INSEE"}],
                          [{"value": "Source: INSEE"}, {"value": "Source: INSEE"}], [{"value": "Source:"}, {"value": ""}]]},
                {"identifier": "t2", "headers": [[{"value": "Election"}, {"value": "Election"}]], "rows": [[{"value": "Source:"}, {"value": ""}]]}])}
        x = ah.article_xhtml(row, {})[2].decode()
        self.assertIn("<tr><th>Judge of the Washington Court of Appeals, Division One, preceded by</th><td>Ronald Cox</td></tr>", x)
        self.assertIn("<tr><th>Area</th><td>5.2 km2</td></tr>", x)
        self.assertIn("<tr><th>INSEE / Postal code</th><td>67030 / 67310</td></tr>", x)
        self.assertNotIn("Listen live", x)
        self.assertEqual(x.count("Source: INSEE"), 1)
        self.assertNotIn("<td>Source:</td>", x)
        self.assertNotIn("Election", x)
        self.assertEqual(ah.fact_value("Born", "26 May 1564 /1563, Sirhind"), "26 May 1564/1563, Sirhind")
        self.assertEqual(ah.strip_undrawable(ah.clean_text("set (P, ≤) forms and x =(y+1) and a face :) here"), {}), "set (P, <=) forms and x =(y+1) and a face :) here")

    def test_rules_finish_on_long_runs(self):
        # A rule of 36 "=" stalled the full build: the trailing-equals rule
        # was ambiguous and backtracked exponentially. Every rule must finish
        # a long run of one character in well under a second.
        import time
        for ch in "=-_*.'\"()[]{}|/\\#:;,<>~^ ":
            for src in ("x " + ch * 400 + " y", ch * 400, "x " + (ch + " ") * 200 + "y"):
                t = time.time()
                ah.strip_undrawable(ah.clean_text(src), {}, lead=True)
                ah.fact_value("Name", src)
                self.assertLess(time.time() - t, 1.0, repr(ch))

    def test_round_eleven_the_last_classes(self):
        # What round ten's measurement still flagged, each traced to the dump
        # or to a rule: Parsoid's protection markers; an unclosed ref tag; a
        # footnote template and an unclosed opener; a quoted script run and a
        # run with commas inside it go whole, not as (") and (,); a block
        # that starts with a colon; rp page references; a formula whose
        # middle the dump lost; the dump's words beside the TeX of them; the
        # residue of an align block; two quoted lines joined.
        cases = [
            ("1,432,820 \ufffdPROT139\ufffd 1910-2020\ufffdPROT140\ufffd 2024 \ufffdPROT141\ufffd", "1,432,820 1910-2020 2024"),
            ("Religion: {{Pie chart| thumb = right| caption = Religion (2014)\ufffdPROT199\ufffd Roughly one-quarter identify as unaffiliated.", "Religion: Roughly one-quarter identify as unaffiliated."),
            ("the interstellar medium. <ref name=\"abundance of chemical elements3", "the interstellar medium."),
            ("in exceptional circumstances.{{efn|For example, travel was restricted in 2020.}} Federations, and{{block indent| sigma: F -> F are natural", "in exceptional circumstances. Federations, and sigma: F -> F are natural"),
            ("The Hebrew Bible is also known by the name Tanakh (Hebrew: \u05ea\u05e0\"\u05da). This reflects", "The Hebrew Bible is also known by the name Tanakh. This reflects"),
            ("the foundation of \"this mosque\" (Arabic: \"\u0647\u0630\u0627 \u0627\u0644\u0645\u0633\u062c\u062f\") by Dawud", "the foundation of \"this mosque\" by Dawud"),
            ("Two Systems (Chinese: \u201c\u4e00\u56fd\u4e24\u5236\u201d\u6770\u51fa\u8d21\u732e\u8005) national honorary title", "Two Systems national honorary title"),
            ("were for solo vocal ('\u0938\u094d\u0935\u0930\u094d\u0917\u0915\u0940 \u0930\u093e\u0928\u0940', '\u092d\u094b \u092d\u094b') and two were for duet", "were for solo vocal and two were for duet"),
            ("you can not create a new career. (\u6539\u9769\u5f00\u653e\u80c6\u5b50\uff0c\u6562\u4e8e\u8bd5\u9a8c)", "you can not create a new career."),
            (": An offering table with a secondary dedication", "An offering table with a secondary dedication"),
            ("\u0543\u0561\u0576\u0561\u0579\u0565\u056c \u0566\u056b\u0574\u0561\u057d\u057f\u0578\u0582\u0569\u056b\u0582\u0576: \u010cana\u010d\u02bfel zimastut\u02bfiun yev zxrat. To know wisdom", "\u010cana\u010d\u2018el zimastut\u2018iun yev zxrat. To know wisdom"),
            ("secured their submission.: ii. 161 : I.68 However, this", "secured their submission. However, this"),
            ("sigma = sigma_ij = = \u2261 \u2261,", "sigma = sigma_ij"),
            ("many possible values for z w z^{w}. So", "many possible values for z^w. So"),
            ("in her.'\"\"'Did you see", "in her.'\" \"'Did you see"),
        ]
        for src, want in cases:
            self.assertEqual(ah.strip_undrawable(ah.clean_text(src), {}, lead=True), want, src)
        # from the round-eleven essentials gate: an emoticon in quotes keeps
        # its face; a quoted phrase ending in a colon is not a label; the
        # dump's wikitext quotes go, a second derivative stays; a "{{" with
        # nothing after it goes, set-builder braces stay; a comma between
        # spelled words gets its space (a chemical name stays tight); the
        # full-width comma the dump uses folds to a comma with its space; rp
        # page numbers after a period go; "(number 8)" is not a face
        more = [
            ("including 14 instances of \":) \" in Richard", "including 14 instances of \":)\" in Richard"),
            ("the Ahl ad-dār (\"House of the Mahdi:), composed of", "the Ahl ad-dār (\"House of the Mahdi:), composed of"),
            ("cultivars are ''Prunus serrulata'''Grandiflora' A. Wagner and ''Prunus serrulata'''Gioiko' Koidz", "cultivars are Prunus serrulata 'Grandiflora' A. Wagner and Prunus serrulata 'Gioiko' Koidz"),
            ("Notable out-fighters include '''Sydney Greve''', Muhammad Ali", "Notable out-fighters include Sydney Greve, Muhammad Ali"),
            ("the derivative f''(x) and f'''(x) of f", "the derivative f''(x) and f'''(x) of f"),
            ("reduced to one-third of what it was at independence.{{", "reduced to one-third of what it was at independence."),
            ("the filter {{k:k >= N}:N in D} is a filter", "the filter {{k:k >= N}:N in D} is a filter"),
            ("the spatial domain is (−∞,∞). In others", "the spatial domain is (−infinity, infinity). In others"),
            ("7',8'-Dihydro-ε,γ-carotene and the Zheng clan，personal name", "7',8'-Dihydro-epsilon,gamma-carotene and the Zheng clan, personal name"),
            ("hybrids based on Prunus speciosa.: 86–95, 137 and others", "hybrids based on Prunus speciosa. and others"),
            ("H (number 8) means add 8 hours", "H (number 8) means add 8 hours"),
        ]
        for src, want in more:
            self.assertEqual(ah.strip_undrawable(ah.clean_text(src), {}, lead=True), want, src)
        # a link whose text is only a space is no link, and the space stands once
        row = {"name": "T", "sections": [{"type": "section", "name": "Abstract", "has_parts": [
            {"type": "paragraph", "value": "The modern alphabet used by Bashkir.", "links": [{"url": "https://en.wikipedia.org/wiki/Space", "text": " "}]}]}]}
        self.assertIn("<p>The modern alphabet used by Bashkir.</p>", ah.article_xhtml(row, {})[2].decode())
        residue = ah.clean_text("= E.1 { }&= }{ }} _{=\\,1{ }G}1 { }{ } . }}")
        for mark in ("{", "}", "&", "_{", "\\"):
            self.assertNotIn(mark, residue, residue)
        # a link's text keeps its label ("Vizing's Theorem:" is not an empty
        # label because its own closing tag follows), and the space a lost
        # icon left in a link's text stands outside the anchor
        row = {"name": "T", "sections": [{"type": "section", "name": "Abstract", "has_parts": [
            {"type": "paragraph", "value": "Vizing's Theorem: A graph of maximal degree has edge-chromatic number.", "links": [{"url": "https://en.wikipedia.org/wiki/Vizing's_theorem", "text": "Vizing's Theorem:"}]},
            {"type": "paragraph", "value": "China  Merrill's Marauders and OSS Detachment 101.", "links": [{"url": "https://en.wikipedia.org/wiki/China_Burma_India_Theater", "text": "China "}, {"url": "https://en.wikipedia.org/wiki/Merrill's_Marauders", "text": "Merrill's Marauders"}]},
        ]}]}
        x = ah.article_xhtml(row, {})[2].decode()
        self.assertIn("<a href=\"Vizing's theorem\">Vizing's Theorem:</a> A graph", x)
        self.assertIn("<a href=\"China Burma India Theater\">China</a> <a href=\"Merrill's Marauders\">Merrill's Marauders</a>", x)
        # a table header whose cells all say the same thing is a caption, not
        # column names, and "#" is a number (in a table, and in the row
        # paragraphs a wide table becomes)
        tables = [{"identifier": "t1", "headers": [[{"value": "Key (expand for notes)"}, {"value": "Key (expand for notes)"}]],
                   "rows": [[{"value": "Location"}, {"value": "Where the match was played"}], [{"value": "#"}, {"value": "Goal of total goals"}]]}]
        secs = [{"type": "section", "name": "Goals", "has_parts": [{"type": "table", "table_references": [{"identifier": "t1"}]}]}]
        _, _, x, st = self.lead({"type": "paragraph", "value": "Test."}, sections=secs, tables=json.dumps(tables))
        self.assertIn("<tr><td>Location</td><td>Where the match was played</td></tr>", x)
        self.assertIn("<tr><td>Number</td><td>Goal of total goals</td></tr>", x)
        self.assertNotIn("Key (expand", x)

    def test_round_nine_residue(self):
        # TeX the dump left outside any block, in any shape, goes and the
        # prose around it stays; citation page ranges in a row go; a note
        # marker at a line's start goes; a raw ref tag goes; a no-break
        # space beside a removed character is one space.
        cases = [
            ("the equations read nabla v = R nabla u \\nabla v=R\\nabla u where R is the rotation", "the equations read nabla v = R nabla u where R is the rotation"),
            ("R 1 = A 1 A 2 B n \\right. Let us consider", "R 1 = A 1 A 2 B n. Let us consider"),
            ("shear stresses: p.45\u201378 : p.1\u201346 : p.111\u2013157 The normal stress", "shear stresses The normal stress"),
            ("## x: Nickel\u2013Strunz mineral/group number", "x: Nickel\u2013Strunz mineral/group number"),
            ("the medium. <ref name=\"a3\">cite</ref> Next and <ref name=\"x\"/> end", "the medium. Next and end"),
            ("China\u00a0 Merrill's Marauders", "China Merrill's Marauders"),
        ]
        for src, want in cases:
            self.assertEqual(ah.strip_undrawable(ah.clean_text(src), {}, lead=True), want, src)

    def test_round_eight_seams(self):
        # From the fourth cold read: a comma inserted after a botanical
        # authority's parenthesis, and a comma left after "pl." when the
        # Arabic plural went.
        self.assertEqual(ah.fact_value("Species", "Phytelephas tenuicaulis (Barfod) A.J.Hend."), "Phytelephas tenuicaulis (Barfod) A.J.Hend.")
        self.assertEqual(ah.fact_value("Born", "3 May 1959 (aged 45) Chicago"), "3 May 1959, Chicago")
        self.assertEqual(
            ah.strip_undrawable("A madhhab (Arabic: \u0645\u0630\u0647\u0628, romanized: madhhab, lit. 'way to act', pl. \u0645\u0630\u0627\u0647\u0628, madh\u0101hib) refers", {}, lead=True),
            "A madhhab (Arabic: madhhab, lit. 'way to act', pl. madh\u0101hib) refers",
        )

    def test_round_seven_remnants(self):
        # a colon padded at the end of a phrase closes, a ratio keeps its
        # spaces even when the paragraph had gaps to close; "({})" is nothing
        # after two passes; an environment the dump left outside a block
        # goes whole; a citation template's error goes.
        cases = [
            ("The responsibilities include : water. The Mayor : is here, a ratio of 3 : 1 and m/z : 291.0 (Greek: \u03b1\u03bb\u03c6\u03b1)",
             "The responsibilities include: water. The Mayor: is here, a ratio of 3 : 1 and m/z: 291.0 (Greek: alpha)"),
            ("curly brackets ({}) and (,) here", "curly brackets and here"),
            ("B 1 B 2 B n {\\begin{array}{cccc}&&\\dots &\\\\&&\\dots &\\end{array}}\\right. Let us consider", "B 1 B 2 B n Let us consider"),
            (": ISBN / Date incompatibility (help) Next", "Next"),
        ]
        for src, want in cases:
            self.assertEqual(ah.strip_undrawable(ah.clean_text(src), {}, lead=True), want, src)
        # a listed table row: a cell that is only a label goes, spaces settle
        tables = [{"identifier": "t1", "headers": [[{"value": "Letter"}, {"value": "Cyrillic"}, {"value": "Phonemic Value (IPA)"}, {"value": "2021"}, {"value": "2018"}]],
                   "rows": [[{"value": "A"}, {"value": "\u0430"}, {"value": "\u2205"}, {"value": "x"}, {"value": "y"}]]}]
        secs = [{"type": "section", "name": "Letters", "has_parts": [{"type": "table", "table_references": [{"identifier": "t1"}]}]}]
        _, _, x, _ = self.lead({"type": "paragraph", "value": "Test."}, sections=secs, tables=json.dumps(tables))
        self.assertIn("<p><b>A</b>; Cyrillic: a; Phonemic Value (IPA): empty set; 2021: x; 2018: y</p>", x)

    def test_infobox_images_names_and_glued_lists(self):
        boxes = [{"type": "infobox", "name": "Infobox settlement", "has_parts": [
            {"type": "section", "name": "City", "has_parts": [
                {"type": "field", "value": "Suspension bridge Memorial", "images": [{"caption": "x"}]},
                {"type": "field", "name": "Country", "value": "Syria"},
            ]},
            {"type": "section", "name": "Korean name", "has_parts": [{"type": "field", "name": "Revised Romanization", "value": "Yegi"}]},
            {"type": "section", "name": "Play", "has_parts": [{"type": "field", "name": "Characters", "value": "Atossa Ghost of Darius Xerxes", "links": [{"text": "Atossa"}, {"text": "Ghost of Darius"}, {"text": "Xerxes"}]}]},
        ]}]
        _, _, x, _ = self.lead({"type": "paragraph", "value": "Test."}, infoboxes=json.dumps(boxes))
        self.assertNotIn("Suspension", x)
        self.assertIn("<tr><th>Korean name, Revised Romanization</th><td>Yegi</td></tr>", x)
        self.assertIn("<tr><th>Characters</th><td>Atossa; Ghost of Darius; Xerxes</td></tr>", x)

    def test_infobox_captions_are_not_facts(self):
        boxes = [{"type": "infobox", "name": "Infobox settlement", "has_parts": [
            {"type": "section", "name": "City", "has_parts": [
                {"type": "image", "value": "Suspension bridge of Deir ez-Zor", "images": []},
                {"type": "field", "value": "Suspension bridge of Deir ez-Zor Memorial of Armenian genocide"},
                {"type": "field", "value": "Interactive map of Deir ez-Zor"},
                {"type": "field", "name": "Country", "value": "Syria"},
            ]},
            {"type": "section", "name": "Korean name", "has_parts": [{"type": "field", "name": "Literal meaning", "value": "Rites Classic"}]},
            {"type": "section", "name": "Japanese name", "has_parts": [{"type": "field", "name": "Literal meaning", "value": "Book of Rites"}]},
            {"type": "section", "name": "Hazards", "has_parts": [{"type": "field", "name": "NFPA 704 (fire diamond)", "value": "1 0 0"}, {"type": "field", "name": "Reconstruction", "value": "* R ij\u2019 kr -s"}]},
        ]}]
        _, _, x, _ = self.lead({"type": "paragraph", "value": "Test."}, infoboxes=json.dumps(boxes))
        self.assertNotIn("Suspension bridge", x)
        self.assertNotIn("Interactive map", x)
        self.assertIn("<tr><th>Country</th><td>Syria</td></tr>", x)
        self.assertIn("<tr><th>Korean name, Literal meaning</th><td>Rites Classic</td></tr>", x)
        self.assertIn("<tr><th>Japanese name, Literal meaning</th><td>Book of Rites</td></tr>", x)
        self.assertNotIn("NFPA", x)
        self.assertNotIn("Reconstruction", x)

    def test_artifacts_are_scrubbed(self):
        # Mario, 2026-09-11: no consecutive parentheses or stray marks may
        # remain, whoever left them. Chemistry's nesting stays.
        cases = [
            ("the united army captured Wulongshan, Mufushan), Yuhuatai) among others", "the united army captured Wulongshan, Mufushan, Yuhuatai among others"),
            ("a name ((Latin)) and an empty pair ( ) and quotes \"\" here,, twice ; and a space , before", "a name (Latin) and an empty pair and quotes here, twice; and a space, before"),
            ("the (+)-camphor ((1 R,4 R)-bornan-2-one) is rarer", "the (+)-camphor ((1 R,4 R)-bornan-2-one) is rarer"),
            ("(an opener with no close", "an opener with no close"),
            (", a leading comma and a trailing one,", "a leading comma and a trailing one"),
            ("It is the \u201cbest\u201d ( really ) one [ ]", "It is the \u201cbest\u201d (really) one"),
        ]
        for src, want in cases:
            self.assertEqual(ah.strip_undrawable(src, {}, lead=True), want, src)

    def test_fact_groups_and_remnants(self):
        boxes = [{"type": "infobox", "name": "Infobox officeholder", "has_parts": [
            {"type": "section", "name": "Test", "has_parts": [{"type": "image", "value": "Test in 1931"}]},
            {"type": "section", "name": "President of Austria", "has_parts": [
                {"type": "field", "value": "In office 20 December 1945 \u2013 31 December 1950"},
                {"type": "field", "name": "Chancellor", "value": "Leopold Figl"},
                {"type": "field", "name": "Preceded by", "value": "Wilhelm Miklas"},
            ]},
            {"type": "section", "name": "Area", "has_parts": [{"type": "field", "name": "Total", "value": "303 km\u00b2 (117 sq mi)"}]},
            {"type": "section", "name": "Personal life", "has_parts": [
                {"type": "field", "name": "Born", "value": "26 May 1564: 90 /1563 Sirhind"},
                {"type": "field", "name": "Preceded by", "value": "Succeeded by"},
                {"type": "field", "name": "Imperial conversion", "value": "J F M A M J J A S O N D"},
                {"type": "field", "name": "Months", "value": "J F M A M J J A S O N D"},
            ]},
        ]}]
        _, _, x, _ = self.lead({"type": "paragraph", "value": "Test."}, infoboxes=json.dumps(boxes))
        self.assertIn("<tr><th>President of Austria</th><td>In office 20 December 1945 \u2013 31 December 1950</td></tr>", x)
        self.assertIn("<tr><th>President of Austria, chancellor</th><td>Leopold Figl</td></tr>", x)
        self.assertIn("<tr><th>President of Austria, preceded by</th><td>Wilhelm Miklas</td></tr>", x)
        self.assertIn("<tr><th>Area, total</th><td>303 km\u00b2 (117 sq mi)</td></tr>", x)
        self.assertIn("<tr><th>Born</th><td>26 May 1564/1563, Sirhind</td></tr>", x)
        self.assertNotIn("Succeeded by", x)
        self.assertNotIn("Imperial conversion", x)
        self.assertNotIn("J F M A", x)

    def test_fact_repeating_its_name_goes(self):
        boxes = [{"name": "Infobox", "has_parts": [
            {"type": "field", "name": "Works", "value": "Works"},
            {"type": "field", "name": "Coordinates", "value": "34\u00b030\u2032N 109\u00b018\u2032E / 34.500\u00b0N 109.300\u00b0E"},
            {"type": "field", "name": "Born", "value": "1959"},
        ]}]
        _, _, x, _ = self.lead({"type": "paragraph", "value": "Test."}, infoboxes=json.dumps(boxes))
        self.assertNotIn("Works", x)
        self.assertIn("<td>34\u00b030\u2032N 109\u00b018\u2032E</td>", x)
        self.assertIn("1959", x)

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
