"""The ZIM path: the reader over a ZIM this test writes, and the converter
over real German articles cut from wikipedia_de_top_nopic_2026-10
(tests/make_zim_fixtures.py), each checked for the rule it exists for.

    python3 -m unittest discover -s tools_local/wikipedia/tests -p 'test_*.py'
"""

import gzip
import os
import struct
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import languages  # noqa: E402
import zim  # noqa: E402
import zim_article  # noqa: E402
import zim_source  # noqa: E402

FIX = os.path.join(HERE, "fixtures", "zim_de")
DE = languages.profile("de")
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


def fixture(name):
    with gzip.open(os.path.join(FIX, name + ".html.gz"), "rt", encoding="utf-8") as f:
        return f.read()


def convert(name, title):
    stats = {}
    t, headings, xhtml = zim_article.zim_xhtml(fixture(name), title, DE, stats)
    return t, headings, xhtml.decode("utf-8"), stats


def text_of(xhtml):
    return "".join(ET.fromstring(xhtml).itertext())


# ------------------------------------------------------------- a ZIM, written


def write_zim(path, entries, clusters, mimes, compress=0):
    """A minimal ZIM 6.1: `entries` sorted by (namespace, path), each
    (ns, path, title, mime index or 0xFFFF, (cluster, blob) or target index).
    `clusters` is a list of blob lists. compress 0 stores, 5 is zstd."""
    mime_list = b"".join(m.encode() + b"\0" for m in mimes) + b"\0"
    dirents = []
    for ns, p, title, mime, ref in entries:
        if mime == 0xFFFF:
            head = struct.pack("<HBcII", mime, 0, ns.encode(), 0, ref)
        else:
            head = struct.pack("<HBcIII", mime, 0, ns.encode(), 0, ref[0], ref[1])
        dirents.append(head + p.encode() + b"\0" + title.encode() + b"\0")
    blobs_out = []
    for blobs in clusters:
        n = len(blobs)
        offs = [4 * (n + 1)]
        for b in blobs:
            offs.append(offs[-1] + len(b))
        raw = struct.pack(f"<{n + 1}I", *offs) + b"".join(blobs)
        if compress == 5:
            from compression import zstd

            raw = zstd.compress(raw)
        blobs_out.append(bytes([compress or 1]) + raw)
    header_len = 80
    mime_pos = header_len
    path_ptr_pos = mime_pos + len(mime_list)
    title_ptr_pos = path_ptr_pos + 8 * len(entries)
    cluster_ptr_pos = title_ptr_pos + 4 * len(entries)
    dirent_pos = cluster_ptr_pos + 8 * len(clusters)
    dirent_ptrs = []
    at = dirent_pos
    for d in dirents:
        dirent_ptrs.append(at)
        at += len(d)
    cluster_ptrs = []
    for c in blobs_out:
        cluster_ptrs.append(at)
        at += len(c)
    checksum_pos = at
    main = next(
        (i for i, e in enumerate(entries) if e[0] == "W" and e[1] == "mainPage"),
        0xFFFFFFFF,
    )
    header = struct.pack(
        "<IHH16sIIQQQQIIQ",
        zim.MAGIC,
        6,
        1,
        b"\0" * 16,
        len(entries),
        len(clusters),
        path_ptr_pos,
        title_ptr_pos,
        cluster_ptr_pos,
        mime_pos,
        main,
        0xFFFFFFFF,
        checksum_pos,
    )
    with open(path, "wb") as f:
        f.write(header + mime_list)
        f.write(struct.pack(f"<{len(entries)}Q", *dirent_ptrs))
        f.write(struct.pack(f"<{len(entries)}I", *range(len(entries))))
        f.write(struct.pack(f"<{len(clusters)}Q", *cluster_ptrs))
        f.write(b"".join(dirents))
        f.write(b"".join(blobs_out))
        f.write(b"\0" * 16)


ARTICLE = (
    '<html><body><div class="mw-parser-output"><section data-mw-section-id="0">'
    "<p><b>Berlin</b> ist die Hauptstadt.</p></section></div><!--htdig_noindex--></body></html>"
)


class ZimReader(unittest.TestCase):
    def build(self, compress):
        d = tempfile.mkdtemp()
        path = os.path.join(d, "t.zim")
        mimes = ["text/html", "text/plain", "application/octet-stream+zimlisting"]
        stub = b'<html><head><meta http-equiv="refresh" content="0;URL=\'./Berlin#Geschichte\'" /></head></html>'
        # sorted by (namespace, path); the listing names the front articles
        entries = [
            ("C", "Berlin", "", 0, (0, 0)),
            ("C", "Groß-Berlin", "Groß-Berlin", 0xFFFF, 0),
            ("C", "Hauptstadt_Deutschlands", "", 0xFFFF, 0),
            ("C", "Stadtgeschichte_Berlins", "", 0, (0, 1)),
            ("M", "Date", "", 1, (1, 0)),
            ("M", "Language", "", 1, (1, 1)),
            ("W", "mainPage", "", 0xFFFF, 0),
            ("X", "listing/titleOrdered/v1", "", 2, (1, 2)),
        ]
        clusters = [
            [ARTICLE.encode(), stub],
            [b"2026-10-10", b"deu", struct.pack("<II", 0, 3)],
        ]
        write_zim(path, entries, clusters, mimes, compress)
        return zim.Zim(path)

    def test_metadata_articles_and_redirects(self):
        for compress in (0, 5):
            z = self.build(compress)
            self.assertEqual(z.metadata("Language"), "deu")
            self.assertEqual(z.metadata("Date"), "2026-10-10")
            fronts = z.front_articles()
            self.assertEqual(
                [e.title for e in fronts], ["Berlin", "Stadtgeschichte Berlins"]
            )
            self.assertEqual(z.content(fronts[0]).decode(), ARTICLE)
            # an empty title is the path with spaces; a redirect resolves
            self.assertEqual(
                sorted(z.redirects()),
                [("Groß-Berlin", "Berlin"), ("Hauptstadt Deutschlands", "Berlin")],
            )
            self.assertEqual(
                z.find("Hauptstadt_Deutschlands").title, "Hauptstadt Deutschlands"
            )
            self.assertIsNone(z.find("Hamburg"))
            self.assertEqual(languages.from_zim_language(z.metadata("Language")), "de")

    def test_section_redirect_stub_names_its_article(self):
        with open(os.path.join(FIX, "section_redirect.html"), "rb") as f:
            stub = f.read()
        m = zim_source._STUB_TARGET.search(stub)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1).decode(), "Pocken")
        with self.assertRaises(ValueError):
            zim_article.zim_xhtml(stub, "Schwarze Blattern", DE)


# ------------------------------------------------------------- the converter


class GermanArticles(unittest.TestCase):
    def test_every_fixture_is_the_pack_subset_and_well_formed(self):
        for name, title in (
            ("strasse", "Straße"),
            ("rotfuchs", "Rotfuchs"),
            ("titanic", "Titanic (1997)"),
            ("bordeaux", "Bordeaux"),
            ("moskau", "Moskau"),
            ("athen", "Athen"),
            ("peking", "Peking"),
            ("borat", "Borat"),
            ("nullstelle", "Nullstelle"),
        ):
            _, headings, xhtml, _ = convert(name, title)
            root = ET.fromstring(xhtml)  # well-formed, or this raises
            tags = {el.tag for el in root.iter()}
            self.assertLessEqual(tags, ALLOWED, name)
            ids = [el.get("id") for el in root.iter("h2")]
            self.assertEqual(ids, ["s%d" % (k + 1) for k in range(len(headings))], name)
            self.assertEqual(
                ["".join(el.itertext()) for el in root.iter("h2")], headings, name
            )
            for el in root.iter():
                allowed = (
                    {"id"} if el.tag == "h2" else {"href"} if el.tag == "a" else set()
                )
                self.assertLessEqual(set(el.attrib), allowed, name)
            body = text_of(xhtml)
            # no reference marks, no edit furniture, no control characters
            self.assertNotRegex(body, r"\[\d+\]", name)
            self.assertNotIn("\x01", body)
            self.assertNotIn("\x02", body)
            self.assertNotRegex(body, r"\(\s*\)|\[\s*\]", name)

    def test_umlauts_and_eszett_reach_the_page_and_apparatus_does_not(self):
        title, headings, xhtml, stats = convert("strasse", "Straße")
        self.assertEqual(title, "Straße")
        self.assertIn("<h1>Straße</h1>", xhtml)
        body = text_of(xhtml)
        for word in ("Straße", "öffentlich", "Fußgänger"):
            self.assertIn(word, body)
        for gone in ("Weblinks", "Einzelnachweise", "Literatur", "Siehe auch"):
            self.assertNotIn(gone, headings)
        self.assertGreater(stats.get("sections_skipped", 0), 0)
        # links keep their display title, umlauts and all, never the path
        self.assertNotIn("%C3", xhtml)
        self.assertNotIn('href="./', xhtml)

    def test_the_infobox_is_a_grid_under_the_facts_heading(self):
        _, headings, xhtml, _ = convert("rotfuchs", "Rotfuchs")
        self.assertEqual(headings[0], "Kurzinfo")
        self.assertIn('<h2 id="s1">Kurzinfo</h2><table>', xhtml)
        self.assertIn("<tr><th>Familie</th><td>Hunde (Canidae)</td></tr>", xhtml)
        # a row of pictures under their two headings is neither a key nor a value
        _, _, moskau, _ = convert("moskau", "Moskau")
        self.assertNotIn("<th>Flagge</th>", moskau)
        self.assertIn("<tr><th>Oberb\u00fcrgermeister</th><td>Sergei Sobjanin</td></tr>", moskau)
        # the facts follow the lead, as the English packs order them
        self.assertLess(xhtml.index("<b>Rotfuchs</b>"), xhtml.index("Kurzinfo"))

    def test_a_nested_comparison_is_not_a_fact_and_an_age_goes(self):
        html = (
            '<div class="mw-parser-output"><section data-mw-section-id="0">'
            '<table class="infobox"><tbody>'
            "<tr><th>Datum</th><td>26. August 1943</td></tr>"
            "<tr><td>Geburtstag:</td><td>21. M\u00e4rz 1992 (34 Jahre)</td></tr>"
            '<tr><td colspan="2"><table><tbody><tr><th colspan="2">Konfliktparteien</th></tr>'
            "<tr><td>Deutsches Reich</td><td>Sowjetunion</td></tr>"
            "<tr><td>Ordnung:</td><td>Raubtiere</td></tr></tbody></table></td></tr>"
            "</tbody></table><p><b>T</b> ist ein Test.</p></section></div>"
        )
        _, _, xhtml = zim_article.zim_xhtml(html, "T", DE)
        xhtml = xhtml.decode("utf-8")
        self.assertIn("<tr><th>Datum</th><td>26. August 1943</td></tr>", xhtml)
        self.assertIn("<tr><th>Geburtstag</th><td>21. M\u00e4rz 1992</td></tr>", xhtml)
        self.assertIn("<tr><th>Ordnung</th><td>Raubtiere</td></tr>", xhtml)
        self.assertNotIn("Deutsches Reich", xhtml)

    def test_captions_under_a_row_of_pictures_are_not_facts(self):
        # Every country's box puts the flag and the arms in one row and
        # "Flagge" | "Wappen" in the row under it: 416 rows of the German
        # pack read as a fact named Flagge whose value was Wappen.
        _, _, xhtml, _ = convert("oesterreich", "\u00d6sterreich")
        self.assertNotIn("<th>Flagge</th>", xhtml)
        self.assertNotIn("<td>Wappen</td>", xhtml)
        self.assertIn('<h2 id="s1">Kurzinfo</h2><table><tr><th>Amtssprache</th>', xhtml)
        self.assertIn("<tr><th>Hauptstadt</th><td>Wien</td></tr>", xhtml)

    def test_a_fact_under_pictures_in_a_nested_box_stays(self):
        # A duchy's box is one nested table: the arms and the flag, then its
        # facts with their keys in bold. A key that says it is one stays.
        html = (
            '<div class="mw-parser-output"><section data-mw-section-id="0">'
            '<table class="infobox"><tbody><tr><td><table><tbody>'
            "<tr><th>Wappen</th><th>Flagge</th></tr>"
            '<tr><td data-had-file="1">a</td><td data-had-file="1">b</td></tr>'
            "<tr><td><b>Landeshauptstadt</b></td><td>Hechingen</td></tr>"
            "<tr><td><b>Regierungsform</b></td><td>Monarchie</td></tr>"
            "</tbody></table></td></tr></tbody></table>"
            "<p><b>T</b> ist ein Test.</p></section></div>"
        )
        _, _, xhtml = zim_article.zim_xhtml(html, "T", DE)
        xhtml = xhtml.decode("utf-8")
        self.assertIn("<tr><th>Landeshauptstadt</th><td>Hechingen</td></tr>", xhtml)
        self.assertIn("<tr><th>Regierungsform</th><td>Monarchie</td></tr>", xhtml)

    def test_infobox_lines_join_as_the_box_reads_them(self):
        _, _, xhtml, _ = convert("oesterreich", "\u00d6sterreich")
        self.assertIn(
            "<tr><th>Amtssprache</th><td>Deutsch \u2013 regionale Amtssprachen: Kroatisch, "
            "Slowenisch, Ungarisch \u2013 anerkannte Minderheitssprachen: u. a. "
            "\u00f6sterreichische Geb\u00e4rdensprache, Romani</td></tr>",
            xhtml,
        )
        facts = xhtml[xhtml.index("Kurzinfo") : xhtml.index("</table>")]
        self.assertNotIn(":,", facts)
        self.assertNotIn(", \u2013", facts)
        join = zim_article.join_items
        # a label runs into its value; a later label opens a group
        self.assertEqual(
            join(["International:", "www.greenpeace.org", "Deutschland:", "www.greenpeace.de"]),
            "International: www.greenpeace.org; Deutschland: www.greenpeace.de",
        )
        # a parenthesis belongs to the line above
        self.assertEqual(
            join(["522.904 Einwohner", "(Stand: 14. Oktober 2010)"]),
            "522.904 Einwohner (Stand: 14. Oktober 2010)",
        )
        # a name that starts with a parenthesis is an item of its own
        self.assertEqual(
            join(["L-Natriumglutamat", "(S)-Natriumglutamat"]),
            "L-Natriumglutamat, (S)-Natriumglutamat",
        )
        # every line a bullet: a plain list
        self.assertEqual(join(["\u2013 A", "\u2013 B"]), "A, B")
        self.assertEqual(join(["\u2013 A", "(a)", "\u2013 B"]), "A (a), B")
        # a dash before a digit is a sign, alone or first
        self.assertEqual(join(["- 0,2 %"]), "- 0,2 %")
        self.assertEqual(join(["- 5 bis 69 m", "(NAP)"]), "- 5 bis 69 m (NAP)")
        # a label over a dashed line: one space, no dash
        self.assertEqual(join(["2008\u20132011:", "\u2013 Es wird"]), "2008\u20132011: Es wird")
        # plain lines stay a comma list
        self.assertEqual(join(["Paramount Pictures", "20th Century Fox"]), "Paramount Pictures, 20th Century Fox")

    def test_an_infobox_line_break_is_a_comma(self):
        _, _, xhtml, _ = convert("titanic", "Titanic (1997)")
        self.assertIn(
            "<tr><th>Produktionsunternehmen</th><td>Paramount Pictures, 20th Century Fox, Lightstorm Entertainment</td></tr>",
            xhtml,
        )
        self.assertIn(
            "<tr><th>Produktion</th><td>James Cameron, Jon Landau</td></tr>", xhtml
        )

    def test_a_transcription_goes_whole(self):
        _, _, xhtml, _ = convert("bordeaux", "Bordeaux")
        lead = xhtml[xhtml.index("<p>") : xhtml.index("</p>")]
        self.assertNotIn("[", lead)
        self.assertNotIn("(,", lead)
        self.assertNotRegex(lead, "[ɐ-ʯ]")
        self.assertIn('<a href="Okzitanische Sprache">okzitanisch</a> Bordèu', lead)

    def test_cyrillic_in_an_audio_button_is_romanised(self):
        _, _, xhtml, _ = convert("moskau", "Moskau")
        self.assertIn(
            '<b>Moskau</b> (<a href="Russische Sprache">russisch</a> Moskvá) ist die',
            xhtml,
        )
        self.assertNotRegex(text_of(xhtml), "[Ѐ-ӿ]")

    def test_greek_gives_way_to_the_sources_own_transliteration(self):
        _, _, xhtml, _ = convert("athen", "Athen")
        self.assertIn(
            '<a href="Altgriechische Sprache">altgriechisch</a> Athênai (f. pl.)', xhtml
        )
        self.assertNotRegex(text_of(xhtml), "[Ͱ-Ͽἀ-῿]")

    def test_chinese_goes_and_the_pinyin_stays_without_its_seams(self):
        _, _, xhtml, _ = convert("peking", "Peking")
        self.assertIn("Wohnsitz (<i>hùkǒu</i>)", xhtml)
        self.assertNotIn("(/", xhtml)
        self.assertNotRegex(text_of(xhtml), "[一-鿿]")

    def test_a_long_title_is_kept_whole(self):
        long = "Borat – Kulturelle Lernung von Amerika, um Benefiz für glorreiche Nation von Kasachstan zu machen"
        title, _, xhtml, _ = convert("borat", long)
        self.assertEqual(title, long)
        self.assertEqual(len(title.encode("utf-8")), 100)
        self.assertIn("<h1>" + long + "</h1>", xhtml)

    def test_formulas_render_and_speak_german(self):
        _, _, xhtml, stats = convert("nullstelle", "Nullstelle")
        self.assertGreater(stats.get("formulas_rendered", 0), 0)
        body = text_of(xhtml)
        self.assertIn("x₀", body)  # subscript digits, not "x_0"
        self.assertIn("Teilmenge von", body)
        self.assertNotIn("subset of", body)

    def test_a_simple_table_stays_a_table_and_a_wide_one_is_listed(self):
        html = (
            '<div class="mw-parser-output"><section data-mw-section-id="0"><p><b>T</b> ist ein Test.</p></section>'
            '<section data-mw-section-id="1"><div class="mw-heading mw-heading2"><h2 id="A">Zahlen</h2></div>'
            '<table class="wikitable"><tbody><tr><th>Jahr</th><th>Einwohner</th></tr>'
            "<tr><td>2000</td><td>1.000</td></tr><tr><td>2010</td><td>1.200</td></tr></tbody></table>"
            '<table class="wikitable"><tbody><tr><th>#</th><th>Name</th><th>Partei</th><th>Von</th><th>Bis</th></tr>'
            "<tr><td>1</td><td>Anna Müller</td><td>SPD</td><td>1990</td><td>1998</td></tr></tbody></table>"
            '<table class="navbox"><tbody><tr><td>Navigation</td></tr></tbody></table>'
            "</section></div>"
        )
        _, headings, xhtml = zim_article.zim_xhtml(html, "T", DE)
        xhtml = xhtml.decode("utf-8")
        self.assertEqual(headings, ["Zahlen"])
        self.assertIn(
            "<table><tr><th>Jahr</th><th>Einwohner</th></tr><tr><td>2000</td><td>1.000</td></tr>",
            xhtml,
        )
        self.assertIn(
            "<p><b>1</b>; Name: Anna Müller; Partei: SPD; Von: 1990; Bis: 1998</p>",
            xhtml,
        )
        self.assertNotIn("Navigation", xhtml)

    def test_hatnotes_navboxes_and_coordinates_never_reach_the_page(self):
        html = (
            '<div class="mw-parser-output"><section data-mw-section-id="0">'
            '<div class="hauptartikel">Hauptartikel: Geschichte</div>'
            '<span class="coordinates">52° N</span>'
            '<p><b>T</b> ist ein Test<sup class="mw-ref reference"><a>[1]</a></sup>.</p></section>'
            '<section data-mw-section-id="1"><div class="mw-heading mw-heading2"><h2 id="W">Weblinks</h2></div>'
            '<ul><li><a rel="mw:ExtLink" href="https://example.org">Offizielle Seite</a></li></ul></section>'
            '<section data-mw-section-id="2"><div class="mw-heading mw-heading2"><h2 id="L">Links</h2></div>'
            '<ul><li><a rel="mw:ExtLink" href="https://example.org">Eine Seite</a></li></ul></section>'
            '<div class="navileiste">Leiste</div></div>'
        )
        _, headings, xhtml = zim_article.zim_xhtml(html, "T", DE)
        xhtml = xhtml.decode("utf-8")
        self.assertEqual(
            xhtml,
            "<html><body><h1>T</h1><p><b>T</b> ist ein Test.</p></body></html>",
        )
        # "Links" is not a name the profile knows; its list of outside links
        # is dropped by shape, and the empty section with it
        self.assertEqual(headings, [])

    def test_a_language_without_a_profile_still_builds(self):
        p = languages.profile("xx")
        self.assertEqual(p["facts"], "Quick facts")
        self.assertEqual(p["name"], "XX")
        _, _, xhtml, _ = convert_with(p)
        self.assertIn('<h2 id="s1">Quick facts</h2>', xhtml)


def convert_with(profile):
    stats = {}
    t, h, x = zim_article.zim_xhtml(fixture("rotfuchs"), "Rotfuchs", profile, stats)
    return t, h, x.decode("utf-8"), stats


if __name__ == "__main__":
    unittest.main()
