#!/usr/bin/env python3
"""One structured-wikipedia row to the pack's XHTML.

    title, headings, xhtml = article_xhtml(row)

produces exactly the subset of docs/apps/wikipedia-pack-format.md, "The
XHTML": elements html body h1 h2 h3 h4 p b i a ul li table tr th td; id on
h2 only (s1, s2, ... in document order); href on a, holding a display title.
`headings` lists the h2 texts in order, so heading k is the element with
id="s<k>"; "Quick facts" is the first when the row has an infobox.

A row is a dict as the dataset's parquet or jsonl gives it: name, abstract,
sections (a JSON string or list of part trees), infoboxes (JSON string or
list), tables (JSON string or list). Everything the reader cannot show is
dropped here, once, at build time: images, galleries, citations, navboxes,
references sections, complex tables, and any run of code points the
reader's serif has no glyph for (glyphs.py reads them out of the font).

Standard library only; the pack tool has no dependencies.
"""

import json
import re
import urllib.parse

from fold import fold
from glyphs import drawable_class

WIKI = "https://en.wikipedia.org/wiki/"
QUICK_FACTS = "Quick facts"
FACT_WORDS = 28  # under the layout engine's 32-word cell cap, so the grid never stacks
TABLE_MAX_COLS = 4
TABLE_CELL_WORDS = 32
TABLE_CELL_BYTES = 512
HEADING_MAX_BYTES = 255
TABLE_OMITTED = "<p><i>(a table was omitted)</i></p>"

# Sections dropped by name (compared folded, so "Notes" and "notes" match).
SKIP_SECTIONS = frozenset(
    fold(s)
    for s in (
        "References",
        "External links",
        "See also",
        "Notes",
        "Further reading",
        "Bibliography",
        "Sources",
        "Notes and references",
        "References and notes",
        "Footnotes",
        "Citations",
        "Gallery",
    )
)

# A link whose target starts with one of these is not an article.
SKIP_NAMESPACES = (
    "special:",
    "file:",
    "image:",
    "media:",
    "category:",
    "help:",
    "wikipedia:",
    "wp:",
    "template:",
    "talk:",
    "portal:",
    "draft:",
    "user:",
    "module:",
    "mediawiki:",
    "timedtext:",
    "book:",
)

# The first-word fallback for the bold subject skips these: "<b>The</b>
# Beatles" and "This is a <b>list</b> of" are worse than no bold at all.
SUBJECT_STOPWORDS = frozenset(("the", "a", "an", "list", "of", "in", "on", "and"))

_CONTROL = re.compile("[\x00-\x08\x0b\x0c\x0e-\x1f\x7f-\x9f￾￿\ud800-\udfff]")
_CITE = re.compile(r"\[\d+\]")
_WS = re.compile(r"\s+")
_PAREN = re.compile(r" ?\([^()]*\)")

_run_re = None
_labelled_run_re = None


def _runs():
    """A run: a code point the serif cannot draw, extended over whitespace
    to the next such code point; optionally preceded by a "Script:" label."""
    global _run_re, _labelled_run_re
    if _run_re is None:
        bad = "[^" + drawable_class() + "]"
        run = bad + r"(?:\s*" + bad + ")*"
        _run_re = re.compile(run)
        label = r"(?:(?<![A-Za-z])[A-Z][A-Za-z]*(?:[ -][A-Za-z]+){0,2}:\s?)?"
        _labelled_run_re = re.compile(label + run)
    return _run_re, _labelled_run_re


_TIDY = (
    (re.compile(r"\(\s*[,;:.]\s*"), "("),
    (re.compile(r"\s*[,;:]\s*\)"), ")"),
    (re.compile(r"\(\s*\)"), ""),
    (re.compile(r"\[\s*\]"), ""),
    (re.compile(r"\s+([,;:.!?)])"), r"\1"),
    (re.compile(r"\(\s+"), "("),
    (re.compile(r"([,;:])(?:\s*[,;:])+"), r"\1"),
    (re.compile(r"\s{2,}"), " "),
)


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def esc_attr(s):
    return esc(s).replace('"', "&quot;")


def clean_text(s):
    """Controls out, citation marks out, whitespace collapsed."""
    if not s:
        return ""
    s = _CONTROL.sub("", s)
    s = _CITE.sub("", s)
    return _WS.sub(" ", s).strip()


def strip_undrawable(text, stats, lead=False):
    """Removes what the serif cannot draw; tidies only when it removed
    something, so untouched text stays byte-for-byte."""
    if not text:
        return text
    run_re, labelled_re = _runs()
    if not run_re.search(text):
        return text
    removed = 0
    if lead:

        def paren(m):
            nonlocal removed
            if run_re.search(m.group(0)):
                removed += 1
                stats["parentheticals_removed"] = (
                    stats.get("parentheticals_removed", 0) + 1
                )
                return ""
            return m.group(0)

        for _ in range(3):
            new = _PAREN.sub(paren, text)
            if new == text:
                break
            text = new
    text, n = labelled_re.subn("", text)
    removed += n
    stats["runs_removed"] = stats.get("runs_removed", 0) + n
    if removed:
        for rx, rep in _TIDY:
            text = rx.sub(rep, text)
        text = text.strip()
    return text


def _parse(v):
    if v is None:
        return []
    if isinstance(v, str):
        try:
            v = json.loads(v)
        except ValueError:
            return []
    return v if isinstance(v, list) else [v]


def link_target(url):
    """The display title an article link points at, or None."""
    if not url or not url.startswith(WIKI):
        return None
    path, _, frag = url[len(WIKI) :].partition("#")
    if frag.startswith("cite_note") or frag.startswith("cite_ref"):
        return None
    path = path.split("?", 1)[0]
    title = clean_text(_WS.sub(" ", urllib.parse.unquote(path).replace("_", " "))).strip()
    if not title:
        return None
    low = title.lower()
    if low.startswith(SKIP_NAMESPACES):
        return None
    return title


def cut_words(text, n):
    words = text.split(" ")
    if len(words) <= n:
        return text
    return " ".join(words[:n]) + "..."


class _Doc:
    def __init__(self, row, stats):
        self.title = clean_text(row.get("name") or "")
        self.stats = stats
        self.headings = []
        self.out = []
        self.first_paragraph_done = False
        self.tables = {}
        for t in _parse(row.get("tables")):
            if isinstance(t, dict) and t.get("identifier"):
                self.tables[t["identifier"]] = t

    # --- inline text -----------------------------------------------------
    def _place_links(self, text, links, lead):
        spans = []
        for link in links or []:
            if not isinstance(link, dict):
                continue
            raw = link.get("text")
            href = link_target(link.get("url"))
            if not raw or not href or href == self.title:
                self.stats["links_dropped"] = self.stats.get("links_dropped", 0) + 1
                continue
            t = strip_undrawable(clean_text(raw), {}, lead=False)
            if not t:
                self.stats["links_dropped"] = self.stats.get("links_dropped", 0) + 1
                continue
            i = text.find(t)
            while i >= 0 and any(s < i + len(t) and e > i for s, e, _ in spans):
                i = text.find(t, i + 1)
            if i < 0:
                self.stats["links_unanchored"] = (
                    self.stats.get("links_unanchored", 0) + 1
                )
                continue
            spans.append((i, i + len(t), href))
            self.stats["links_kept"] = self.stats.get("links_kept", 0) + 1
        spans.sort()
        return spans

    def _subject(self, text):
        base = re.sub(r"\s*\([^()]*\)\s*$", "", self.title)
        cands = [self.title]
        if base and base != self.title:
            cands.append(base)
        first = (base or self.title).split(" ")[0]
        if (
            first
            and first not in cands
            and first.lower() not in SUBJECT_STOPWORDS
            and len(first) >= 3
        ):
            cands.append(first)
        for c in cands:
            for flags in (0, re.IGNORECASE):
                m = re.search(r"(?<!\w)" + re.escape(c) + r"(?!\w)", text, flags)
                if m:
                    return (m.start(), m.end())
        return None

    def inline(self, part, lead=False, subject=False):
        text = strip_undrawable(
            clean_text(part.get("value") or ""), self.stats, lead=lead
        )
        if not text:
            return ""
        links = self._place_links(text, part.get("links"), lead)
        bold = self._subject(text) if subject else None
        if bold:
            bs, be = bold
            for s, e, _ in links:
                if s < be and e > bs and not (bs <= s and e <= be):
                    bold = None  # a link straddles the subject; the link wins
                    break
        if bold:
            bs, be = bold
            return (
                self._emit(text, 0, bs, links)
                + "<b>"
                + self._emit(text, bs, be, links)
                + "</b>"
                + self._emit(text, be, len(text), links)
            )
        return self._emit(text, 0, len(text), links)

    @staticmethod
    def _emit(text, lo, hi, links):
        out = []
        i = lo
        for s, e, href in links:
            if s < lo or e > hi:
                continue
            out.append(esc(text[i:s]))
            out.append('<a href="' + esc_attr(href) + '">' + esc(text[s:e]) + "</a>")
            i = e
        out.append(esc(text[i:hi]))
        return "".join(out)

    def plain(self, s):
        return esc(strip_undrawable(clean_text(s), self.stats))

    # --- blocks ------------------------------------------------------------
    def paragraph(self, part, lead=False):
        subject = lead and not self.first_paragraph_done
        s = self.inline(part, lead=lead, subject=subject)
        if s:
            self.out.append("<p>" + s + "</p>")
            if lead:
                self.first_paragraph_done = True

    def heading(self, name, depth):
        text = strip_undrawable(clean_text(name), self.stats)
        if not text:
            return
        if depth == 0:
            self.headings.append(text)
            self.out.append('<h2 id="s%d">%s</h2>' % (len(self.headings), esc(text)))
        else:
            self.out.append(
                "<h%d>%s</h%d>" % (min(2 + depth, 4), esc(text), min(2 + depth, 4))
            )

    def list_items(self, items, ordered, depth):
        rendered = []
        for it in items:
            if not isinstance(it, dict):
                continue
            body = self.inline(it) if it.get("value") else ""
            # A section nested inside an item is not a top-level section: one
            # level down, so it draws as a lower heading and never as an h2
            # with an id the CONTENTS list would name.
            nested = self.capture(it.get("has_parts"), depth + 1)
            if body or nested:
                rendered.append((body, nested))
        if not rendered:
            return
        if ordered:
            for n, (body, nested) in enumerate(rendered, 1):
                if body:
                    self.out.append("<p>%d. %s</p>" % (n, body))
                self.out.append(nested)
        else:
            self.out.append("<ul>")
            for body, nested in rendered:
                self.out.append("<li>" + body + nested + "</li>")
            self.out.append("</ul>")

    def definition_list(self, items, depth):
        for it in items:
            if not isinstance(it, dict):
                continue
            body = self.inline(it) if it.get("value") else ""
            if body:
                if it.get("type") == "definition_term":
                    self.out.append("<p><b>" + body + "</b></p>")
                else:
                    self.out.append("<p>" + body + "</p>")
            self.parts(it.get("has_parts"), depth + 1)

    def table(self, part):
        refs = part.get("table_references") or []
        if not refs:
            self.out.append(TABLE_OMITTED)
            self.stats["tables_omitted"] = self.stats.get("tables_omitted", 0) + 1
            return
        for ref in refs:
            tb = (
                self.tables.get((ref or {}).get("identifier"))
                if isinstance(ref, dict)
                else None
            )
            html = self.table_html(tb) if tb else TABLE_OMITTED
            if html == TABLE_OMITTED:
                self.stats["tables_omitted"] = self.stats.get("tables_omitted", 0) + 1
            elif html:
                self.stats["tables_kept"] = self.stats.get("tables_kept", 0) + 1
            if html:
                self.out.append(html)

    def table_html(self, tb):
        rows = [(True, r) for r in (tb.get("headers") or []) if isinstance(r, list)]
        rows += [(False, r) for r in (tb.get("rows") or []) if isinstance(r, list)]
        rows = [(h, r) for h, r in rows if r]
        if not rows:
            return ""
        grid = []
        for is_header, r in rows:
            cells = []
            for c in r:
                v = c.get("value") if isinstance(c, dict) else c
                cells.append(
                    strip_undrawable(
                        clean_text(v if isinstance(v, str) else ""), self.stats
                    )
                )
            grid.append((is_header, cells))
        if max(len(c) for _, c in grid) > TABLE_MAX_COLS:
            return TABLE_OMITTED
        for _, cells in grid:
            for c in cells:
                if (
                    len(c.split(" ")) > TABLE_CELL_WORDS
                    or len(c.encode("utf-8")) > TABLE_CELL_BYTES
                ):
                    return TABLE_OMITTED
        out = ["<table>"]
        for is_header, cells in grid:
            tag = "th" if is_header else "td"
            out.append(
                "<tr>"
                + "".join("<%s>%s</%s>" % (tag, esc(c), tag) for c in cells)
                + "</tr>"
            )
        out.append("</table>")
        return "".join(out)

    def capture(self, parts, depth):
        """Renders parts into a string instead of self.out (nested lists)."""
        if not parts:
            return ""
        saved = self.out
        self.out = []
        self.parts(parts, depth)
        s = "".join(self.out)
        self.out = saved
        return s

    def parts(self, parts, depth, lead=False):
        loose = []  # consecutive bare list_items become one <ul>

        def flush_loose():
            if loose:
                self.list_items(loose, False, depth)
                loose.clear()

        for p in parts or []:
            if not isinstance(p, dict):
                continue
            t = p.get("type")
            if t == "list_item":
                loose.append(p)
                continue
            flush_loose()
            if t == "section":
                name = str(p.get("name") or "")
                if fold(name) in SKIP_SECTIONS:
                    self.stats["sections_skipped"] = (
                        self.stats.get("sections_skipped", 0) + 1
                    )
                    continue
                self.heading(name, depth)
                self.parts(p.get("has_parts"), depth + 1)
            elif t == "paragraph":
                self.paragraph(p, lead=lead)
            elif t == "list":
                items = p.get("has_parts")
                if items:
                    self.list_items(items, False, depth)
                elif p.get("value"):
                    lines = [
                        {"type": "list_item", "value": x}
                        for x in str(p["value"]).split("\n")
                        if x.strip()
                    ]
                    self.list_items(lines, False, depth)
            elif t == "ordered_list":
                self.list_items(p.get("has_parts") or [], True, depth)
            elif t == "definition_list":
                self.definition_list(p.get("has_parts") or [], depth)
            elif t == "table":
                self.table(p)
            # image, gallery, anything unknown: dropped
        flush_loose()

    # --- the infobox ---------------------------------------------------------
    def facts(self, infoboxes):
        fields = []
        seen = set()

        def add(name, value):
            name = strip_undrawable(clean_text(name), self.stats)
            value = cut_words(
                strip_undrawable(clean_text(value), self.stats), FACT_WORDS
            )
            if name and value and (name, value) not in seen:
                seen.add((name, value))
                fields.append((name, value))

        def walk(p):
            if isinstance(p, list):
                for c in p:
                    walk(c)
                return
            if not isinstance(p, dict):
                return
            t = p.get("type")
            if t == "field" and p.get("name") and isinstance(p.get("value"), str):
                add(p["name"], p["value"])
            elif t == "list" and p.get("name"):
                items = [
                    clean_text(it.get("value"))
                    for it in p.get("has_parts") or []
                    if isinstance(it, dict) and isinstance(it.get("value"), str)
                ]
                items = [i for i in items if i]
                if items:
                    add(p["name"], "; ".join(items))
                return
            for c in p.get("has_parts") or []:
                walk(c)

        walk(infoboxes)
        if not fields:
            return
        self.stats["facts"] = self.stats.get("facts", 0) + len(fields)
        self.headings.append(QUICK_FACTS)
        self.out.append('<h2 id="s%d">%s</h2>' % (len(self.headings), QUICK_FACTS))
        # A two-column grid rather than "<p><b>Key</b> value</p>" rows: the
        # engine justifies a paragraph, and a key and a two-word value pulled
        # to opposite margins read as a broken line. In a grid each cell wraps
        # on its own.
        self.out.append("<table>")
        for name, value in fields:
            self.out.append("<tr><th>" + esc(name) + "</th><td>" + esc(value) + "</td></tr>")
        self.out.append("</table>")


def heading_bytes(text):
    """A heading as the article header stores it: at most 255 bytes, cut on
    a character boundary."""
    b = text.encode("utf-8")
    if len(b) <= HEADING_MAX_BYTES:
        return b
    b = b[:HEADING_MAX_BYTES]
    while b and (b[-1] & 0xC0) == 0x80:
        b = b[:-1]
    return b[:-1] if b and b[-1] >= 0xC0 else b


def article_xhtml(row, stats=None):
    """(title, headings, xhtml_bytes). `stats`, if given, is a dict the
    counters (runs_removed, tables_omitted, ...) are added into."""
    if stats is None:
        stats = {}
    doc = _Doc(row, stats)
    if not doc.title:
        raise ValueError("row has no name")
    secs = [s for s in _parse(row.get("sections")) if isinstance(s, dict)]
    lead = []
    rest = secs
    if (
        secs
        and secs[0].get("type") == "section"
        and fold(secs[0].get("name") or "") == "abstract"
    ):
        lead = secs[0].get("has_parts") or []
        rest = secs[1:]
    elif isinstance(row.get("abstract"), str) and row["abstract"].strip():
        lead = [{"type": "paragraph", "value": row["abstract"]}]

    doc.out.append("<html><body>")
    doc.out.append("<h1>" + esc(doc.title) + "</h1>")
    lead_blocks = [
        p for p in lead if not (isinstance(p, dict) and p.get("type") == "section")
    ]
    lead_sections = [
        p for p in lead if isinstance(p, dict) and p.get("type") == "section"
    ]
    doc.parts(lead_blocks, 0, lead=True)
    doc.facts(_parse(row.get("infoboxes")))
    doc.parts(lead_sections, 0)
    doc.parts(rest, 0)
    doc.out.append("</body></html>")
    return doc.title, doc.headings, "".join(doc.out).encode("utf-8")


if __name__ == "__main__":
    import gzip
    import sys

    if len(sys.argv) != 3:
        sys.exit(
            "usage: article_html.py <rows.jsonl[.gz]> <title>   (prints the XHTML)"
        )
    opener = gzip.open if sys.argv[1].endswith(".gz") else open
    with opener(sys.argv[1], "rt", encoding="utf-8") as f:
        for line in f:
            r = json.loads(line)
            if r.get("name") == sys.argv[2]:
                st = {}
                _, heads, x = article_xhtml(r, st)
                sys.stdout.write(x.decode("utf-8") + "\n")
                print(heads, st, file=sys.stderr)
                break
        else:
            sys.exit("no such title")
