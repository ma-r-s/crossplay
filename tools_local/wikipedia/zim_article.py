#!/usr/bin/env python3
"""One ZIM article (Kiwix's Parsoid HTML) to the pack's XHTML.

    title, headings, xhtml = zim_xhtml(html, title, profile, stats)

The sibling of article_html.py for every language structured-wikipedia does
not carry. That one works from the dataset's flattened rows and spends most
of its rules repairing what the flattening lost; this one reads the page as
Wikipedia rendered it, so it works from structure instead: Parsoid marks a
reference, a navbox, an infobox, an IPA transcription or a foreign script
with its own class or attribute, and the rule is "drop that element", not a
regex over text. What comes out is exactly the subset of
docs/apps/wikipedia-pack-format.md, "The XHTML", and the same pack writer
takes it.

The words that differ by language (sections to drop, the facts heading, the
omitted-table line) are languages.py's; nothing here is German.

Standard library only.
"""

import html as htmllib
import os
import re
import sys
import unicodedata
import urllib.parse
from html.parser import HTMLParser

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import symbols  # noqa: E402
import tex_text  # noqa: E402
from article_html import _CONTROL, _fold_chars, _runs, cut_words, esc, esc_attr  # noqa: E402
from fold import fold  # noqa: E402

FACT_WORDS = 28  # under the layout engine's 32-word cell cap
TABLE_MAX_COLS = 4
TABLE_CELL_WORDS = 32
TABLE_CELL_BYTES = 512
TABLE_ROWS_LISTED = 60
TABLE_ROW_CELL_WORDS = 60

VOID = frozenset(
    "area base br col embed hr img input link meta param source track wbr".split()
)

# Whole elements that never reach the panel, by tag.
DROP_TAGS = frozenset(
    "style script noscript figure img svg audio video map link meta input button "
    "object iframe canvas picture source track caption wiki-chart".split()
)

# ... and by class. Every one is a Parsoid or a template class counted on the
# German top set; none is language words.
DROP_CLASSES = frozenset(
    (
        # references and their marks
        "reference",
        "mw-ref",
        "mw-references",
        "references",
        "mw-references-wrap",
        "fussnoten-block",
        "fussnoten-marke",
        "fussnoten-etui",
        "mw-cite-backlink",
        # navigation boxes, authority control, categories, sister projects
        "navbox",
        "navileiste",
        "klappleiste",
        "BoxenVerschmelzen",
        "erweiterte-navigationsleiste",
        "navbox-inner",
        "vertical-navbox",
        "catlinks",
        "sisterproject",
        "metadata",
        "authority-control",
        # hatnotes: "Hauptartikel: X", "Siehe auch: X", "For other uses"
        "hauptartikel",
        "sieheauch",
        "hatnote",
        "dablink",
        "rellink",
        "BKL",
        # pictures, maps, galleries and the charts drawn as boxes
        "thumb",
        "gallery",
        "gallerybox",
        "dewiki-gallery",
        "mw-gallery-traditional",
        "mw-gallery-packed",
        "wahldiagramm",
        "noviewer",
        "mw-kartographer-maplink",
        "timeline-wrapper",
        "barbox",
        "legend",
        # coordinates, pronunciations, audio
        "coordinates",
        "geo",
        "geo-default",
        "geo-dms",
        "IPA",
        "ext-phonos-attribution",
        "noexcerpt",
        # editor furniture
        "mw-empty-elt",
        "editoronly",
        "mw-editsection",
        "Z3988",
        "noprint",
        "printfooter",
        "mw-indicators",
        "shortdescription",
    )
)

# Template families whose every member is furniture: "normdaten-typ-p",
# "wahldiagramm-wrapper" (an election chart drawn in boxes), "dewiki-chart-info".
DROP_PREFIXES = ("normdaten", "wahldiagramm", "dewiki-chart", "mw-chart")

# A wikitable that is a navbox in a table's clothes.
NAV_TABLE_CLASSES = frozenset(("navbox", "navigation-box", "toccolours-nav"))

# BCP 47 script subtags the serif cannot draw, and the two it draws that
# Mario does not want on the panel (2026-09-11): Greek and Cyrillic become
# Latin letters, every other script goes.
ROMANISED_SCRIPTS = frozenset(("Grek", "Cyrl"))
LATIN_SCRIPTS = frozenset(("Latn",))
# languages written in a non-Latin script when the tag names no script
NON_LATIN_LANGS = frozenset(
    "ar fa ur ps he yi el grc ru uk be bg sr mk kk ky tg mn zh ja ko th lo km my "
    "hi mr ne sa bn pa gu or ta te kn ml si bo dz am ti ka hy dv syr arc".split()
)

_SUPER = str.maketrans("0123456789+-=()n", "⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁼⁽⁾ⁿ")
_SUB = str.maketrans("0123456789+-=()", "₀₁₂₃₄₅₆₇₈₉₊₋₌₍₎")
_SUPER_OK = re.compile(r"^[0-9+\-=()n−]{1,4}$")
_SUB_OK = re.compile(r"^[0-9+\-=()−]{1,4}$")

_WS = re.compile(r"\s+")
_ITEM = "\x02"  # a line break inside an infobox value, until the value is joined
# The IPA letters (not the schwa, which Azerbaijani names use) and the
# length and stress marks: what tells a transcription from a word.
_IPA = re.compile("[\u0250-\u0258\u025a-\u02af\u02c8\u02cc\u02d0\u02d1]")
_BRACKET_TAG = re.compile(r"^\s*\[[^\[\]]{1,40}\]\s*$")
_MARK = (
    "\x01"  # where a script went with nothing in its place, until the block is tidied
)


# ---------------------------------------------------------------- the tree


class Node:
    __slots__ = ("tag", "attrs", "children", "parent")

    def __init__(self, tag, attrs, parent):
        self.tag = tag
        self.attrs = attrs
        self.children = []
        self.parent = parent

    def classes(self):
        return (self.attrs.get("class") or "").split()

    def has_class(self, *names):
        cls = self.classes()
        return any(n in cls for n in names)

    def text(self):
        out = []
        stack = [self]
        while stack:
            n = stack.pop()
            if isinstance(n, str):
                out.append(n)
            else:
                stack.extend(reversed(n.children))
        return "".join(out)

    def find_all(self, tag):
        out = []
        stack = list(reversed(self.children))
        while stack:
            n = stack.pop()
            if isinstance(n, Node):
                if n.tag == tag:
                    out.append(n)
                stack.extend(reversed(n.children))
        return out

    def first(self, tag):
        found = self.find_all(tag)
        return found[0] if found else None


class _Builder(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("#root", {}, None)
        self.cur = self.root

    def handle_starttag(self, tag, attrs):
        node = Node(tag, {k: (v or "") for k, v in attrs}, self.cur)
        self.cur.children.append(node)
        if tag not in VOID:
            self.cur = node

    def handle_startendtag(self, tag, attrs):
        self.cur.children.append(Node(tag, {k: (v or "") for k, v in attrs}, self.cur))

    def handle_endtag(self, tag):
        n = self.cur
        while n is not None and n.tag != tag:
            n = n.parent
        if n is not None and n.parent is not None:
            self.cur = n.parent

    def handle_data(self, data):
        self.cur.children.append(data)


# Cut and shrink before parsing: the head, the footer and every image's
# inline SVG placeholder are most of a ZIM page's bytes, and MathML is a
# tree per symbol whose TeX the <math> element already carries.
_ATTRS = r"""(?:[^>"']|"[^"]*"|'[^']*')*"""
_IMG = re.compile(r"<img\b" + _ATTRS + ">", re.S)
_STYLE = re.compile(r"<style\b.*?</style>", re.S)
_MATH = re.compile(r"<math\b(" + _ATTRS + r")>.*?</math>", re.S)
_ALTTEXT = re.compile(r'alttext="([^"]*)"')


def _math_marker(m):
    alt = _ALTTEXT.search(m.group(1))
    return '<span data-tex="%s"></span>' % (alt.group(1) if alt else "")


def parse(html_text):
    start = html_text.find("mw-parser-output")
    if start >= 0:
        start = html_text.rfind("<", 0, start)
    end = html_text.find("<!--htdig_noindex-->")
    body = html_text[start if start >= 0 else 0 : end if end > 0 else len(html_text)]
    body = _STYLE.sub("", _IMG.sub("", body))
    body = _MATH.sub(_math_marker, body)
    b = _Builder()
    b.feed(body)
    b.close()
    return b.root


# ---------------------------------------------------------------- pruning


def _script_of(node):
    """ "drop", "romanise", "latin" or None for an element with a lang."""
    lang = node.attrs.get("lang")
    if not lang:
        return None
    parts = lang.split("-")
    script = next((p for p in parts[1:] if len(p) == 4 and p[:1].isupper()), None)
    if script in LATIN_SCRIPTS:
        return "latin"
    if script in ROMANISED_SCRIPTS:
        return "romanise"
    if script:
        return "drop"
    if parts[0].lower() in NON_LATIN_LANGS:
        return (
            "romanise"
            if parts[0].lower()
            in ("el", "grc", "ru", "uk", "be", "bg", "sr", "mk", "kk", "ky", "tg", "mn")
            else "drop"
        )
    return None


def _next_meaningful(siblings, i):
    """The sibling after i that says something: spaces and commas, bare or
    in a span of their own, are passed over."""
    for j in range(i + 1, min(len(siblings), i + 5)):
        s = siblings[j]
        text = s if isinstance(s, str) else s.text()
        if not text.strip(" ,;:\u00a0"):
            continue
        return s
    return None


def _is_transliteration(node):
    if not isinstance(node, Node):
        return False
    if _script_of(node) == "latin":
        return True
    if node.tag == "i":
        inner = [c for c in node.children if isinstance(c, Node)]
        return bool(inner) and _script_of(inner[0]) == "latin"
    return False


def _headed(table):
    """Whether a table's first row is one header cell across it."""
    for tr in table.find_all("tr"):
        cells = [c for c in tr.children if isinstance(c, Node) and c.tag in ("td", "th")]
        return len(cells) == 1 and cells[0].tag == "th"
    return False


def _row_table(tr):
    p = tr.parent
    while p is not None and p.tag != "table":
        p = p.parent
    return p


def _names_a_field(cell):
    """A key cell that says it is one: a header cell, bold, or "Key:"."""
    return (
        cell.tag == "th"
        or cell.first("b") is not None
        or cell.text().rstrip().endswith(":")
    )


def _mark_cell(node):
    """The table cell a picture stood in, so an infobox row that was a picture
    and its caption is not read as a key and a value."""
    while node is not None and node.tag not in ("td", "th", "table"):
        node = node.parent
    if node is not None and node.tag in ("td", "th"):
        node.attrs["data-had-file"] = "1"


_BULLET = re.compile("^[\u2013\u2014-] +(?=[^\\d\\s])")


def _aside(t):
    """A line that is one parenthesis from end to end, "(Stand: 2010)", and not
    a name that starts with one, "(S)-Natriumglutamat"."""
    if not (t.startswith("(") and t.endswith(")")):
        return False
    depth = 0
    for i, ch in enumerate(t):
        depth += ch == "("
        depth -= ch == ")"
        if depth == 0:
            return i == len(t) - 1
    return False


def join_items(items):
    """The lines of an infobox value as one line. A comma between items, as
    the box reads them, except where a line is not an item of its own:

    - a label ("Offizielle Hymne:") runs into what it labels, with a space,
      and one that is not the first opens a group, after a semicolon;
    - a line that starts with a dash is a sub-item ("- regionale
      Amtssprachen:") and follows the line above after a dash, its own dash
      gone; a value whose every line starts with one is a plain list;
    - a parenthesis ("(Stand: 2010)") belongs to the line above.

    Counted over the German pack before it was written: 706 values had
    "label:, item", 41 a ", - " seam, 3,068 a ", (" seam. A single line is
    returned as it came, and a dash before a digit is a sign ("- 0,2 %"),
    never a bullet.
    """
    items = [t for t in items if t]
    if len(items) < 2:
        return items[0] if items else ""
    # a list of bullets, asides between them or not
    bulleted = all(_BULLET.match(t) or _aside(t) for t in items) and any(
        _BULLET.match(t) for t in items
    )
    out = ""
    for i, t in enumerate(items):
        dashed = bool(_BULLET.match(t))
        if dashed and (i > 0 or bulleted):
            t = _BULLET.sub("", t)
        if i == 0:
            out = t
            continue
        prev_label = out.endswith(":")
        if prev_label or _aside(t):
            sep = " "
        elif dashed and not bulleted:
            sep = " \u2013 "
        elif t.endswith(":"):
            sep = "; "
        else:
            sep = ", "
        out += sep + t
    return out


def prune(node, stats):
    """Removes what never reaches the panel, in place."""
    kept = []
    kids = node.children
    for i, c in enumerate(kids):
        if isinstance(c, str):
            kept.append(c)
            continue
        style = c.attrs.get("style", "").replace(" ", "").replace("\n", "")
        # display:none is hidden; position:absolute is a label pinned onto a
        # picture that is not there (a location map's town names)
        # (MathML is display:none beside its picture, and the picture is gone)
        hidden = ("display:none" in style and "mwe-math-mathml-a11y" not in c.attrs.get("class", "")) or (
            "visibility:hidden" in style  # a figure's padding zeros: "{{0}}" in "0,000,12"
        )
        if c.tag in DROP_TAGS or hidden or "position:absolute" in style:
            if c.tag in ("figure", "img") or "mw:File" in c.attrs.get("typeof", ""):
                _mark_cell(node)
            continue
        if "mw:File" in c.attrs.get("typeof", ""):
            # a picture's frame without the picture: its caption alone
            _mark_cell(node)
            continue
        cls = c.classes()
        if any(k in DROP_CLASSES for k in cls) or any(k.startswith(DROP_PREFIXES) for k in cls):
            stats["elements_dropped"] = stats.get("elements_dropped", 0) + 1
            continue
        if c.tag == "table" and any(k in NAV_TABLE_CLASSES for k in cls):
            stats["navboxes_dropped"] = stats.get("navboxes_dropped", 0) + 1
            continue
        if c.tag == "a" and "mw:referencedBy" in c.attrs.get("rel", ""):
            continue
        if "ext-phonos" in cls:
            # An audio button: the word it speaks is its label ("Москва́"),
            # and the word is the content; the button is not.
            labels = [n for n in c.find_all("span") if n.has_class("oo-ui-labelElement-label")]
            if not labels or not labels[0].text().strip():
                continue
            c = labels[0]
        if c.tag == "sup" and _BRACKET_TAG.match(c.text()):
            # an inline maintenance tag: "[veraltet]", "[citation needed]"
            stats["tags_dropped"] = stats.get("tags_dropped", 0) + 1
            continue
        if c.tag in ("a", "span", "i") and _IPA.search(c.text()):
            # a transcription, linked or not: "[bɔʁˈdo]" (Mario, 2026-09-11:
            # pronunciations go whole)
            stats["transcriptions_dropped"] = stats.get("transcriptions_dropped", 0) + 1
            kept.append(_MARK)
            continue
        script = _script_of(c)
        if script in ("drop", "romanise"):
            # The source's own transliteration follows: the native spelling
            # goes and that one stays ("neugriechisch Αθήνα Athína").
            if _is_transliteration(_next_meaningful(kids, i)):
                stats["scripts_with_transliteration"] = (
                    stats.get("scripts_with_transliteration", 0) + 1
                )
                # marked, so the separators it stood between go with it:
                # "(户口 / 戶口, hùkǒu)" reads "(hùkǒu)"
                kept.append(_MARK)
                continue
            if script == "drop":
                stats["scripts_dropped"] = stats.get("scripts_dropped", 0) + 1
                kept.append(_MARK)
                continue
            text = c.text()
            latin = symbols.romanize(text)
            stats["scripts_romanised"] = stats.get("scripts_romanised", 0) + 1
            kept.append(latin if latin.strip() else _MARK)
            continue
        prune(c, stats)
        kept.append(c)
    node.children = kept


# ---------------------------------------------------------------- text


def clean_text(s, stats, words=None):
    """One run of the page's text, drawable: controls and whitespace
    collapsed, symbols spelled, letters the serif lacks folded, Greek and
    Cyrillic romanised, anything still undrawable removed and marked."""
    if not s:
        return s
    # composed, as the reader composes every word before it looks a glyph up
    s = unicodedata.normalize("NFC", s)
    s = _CONTROL.sub(lambda m: m.group(0) if m.group(0) == _MARK else "", s)
    s = _WS.sub(" ", s)
    run_re, _ = _runs()
    if not run_re.search(s):
        return s
    if words:
        for k, v in words.items():
            if k in s:
                s = s.replace(k, v)
                stats["symbols_translated"] = stats.get("symbols_translated", 0) + 1
    s, n = symbols.translate(s)
    if n:
        stats["symbols_translated"] = stats.get("symbols_translated", 0) + n
    s, folded = _fold_chars(s)
    for k, v in folded.items():
        stats[k] = stats.get(k, 0) + v
    if run_re.search(s):

        def sub(m):
            run = m.group(0)
            letters = [c for c in run if c.isalpha()]
            if letters and all(
                "Ͱ" <= c <= "Ͽ" or "ἀ" <= c <= "῿" or "Ѐ" <= c <= "ԯ" for c in letters
            ):
                stats["runs_romanised"] = stats.get("runs_romanised", 0) + 1
                return symbols.romanize(run)
            census = stats.setdefault("removed_chars", {})
            for ch in run:
                if not ch.isspace() and ord(ch) >= 128:
                    census[ch] = census.get(ch, 0) + 1
            stats["runs_removed"] = stats.get("runs_removed", 0) + 1
            return _MARK

        s = run_re.sub(sub, s)
    return s


def link_target(node, profile):
    """The display title an internal link points at, or None."""
    rel = node.attrs.get("rel", "")
    if "mw:WikiLink" not in rel or node.has_class(
        "new", "mw-selflink", "selflink", "mw-selflink-fragment"
    ):
        return None
    href = node.attrs.get("href", "")
    if not href or href.startswith("#") or "://" in href:
        return None
    path = href.split("#", 1)[0].split("?", 1)[0]
    if path.startswith("./"):
        path = path[2:]
    title = _WS.sub(" ", urllib.parse.unquote(path).replace("_", " ")).strip()
    if not title:
        return None
    low = title.lower()
    if any(low.startswith(ns) for ns in profile["ns"]):
        return None
    return title


class _Inline:
    """Builds one block's inline XHTML: text escaped, b/i/a kept."""

    def __init__(self, conv):
        self.conv = conv
        self.out = []

    def node(self, n, in_link=False):
        if isinstance(n, str):
            t = clean_text(n, self.conv.stats, self.conv.profile.get("symbols"))
            if t:
                self.out.append(esc(t).replace(esc(_MARK), _MARK))
            return
        tag = n.tag
        if tag == "br":
            self.out.append(self.conv.line_break)
            return
        if tag in ("li", "p", "div") and self.conv.line_break != " ":
            # an item of a list inside a cell is a line of its own
            self.out.append(self.conv.line_break)
        if "data-tex" in n.attrs:
            self.out.append(esc(self.conv.formula(n.attrs["data-tex"])))
            return
        if tag in ("b", "strong"):
            self.wrap("b", n, in_link)
            return
        if tag in ("i", "em", "cite", "var", "dfn"):
            self.wrap("i", n, in_link)
            return
        if tag == "a" and not in_link:
            target = link_target(n, self.conv.profile)
            if target and target != self.conv.title:
                inner = _Inline(self.conv)
                for c in n.children:
                    inner.node(c, True)
                text = "".join(inner.out)
                if text.strip(" " + _MARK):
                    lead = text[: len(text) - len(text.lstrip())]
                    trail = text[len(text.rstrip()) :]
                    self.out.append(
                        lead
                        + '<a href="'
                        + esc_attr(target)
                        + '">'
                        + text.strip()
                        + "</a>"
                        + trail
                    )
                    self.conv.stats["links_kept"] = (
                        self.conv.stats.get("links_kept", 0) + 1
                    )
                else:
                    self.out.append(text)
                return
        if tag in ("sup", "sub"):
            raw = clean_text(n.text(), self.conv.stats).strip()
            if tag == "sup" and _SUPER_OK.match(raw):
                self.out.append(raw.replace("−", "-").translate(_SUPER))
                return
            if tag == "sub" and _SUB_OK.match(raw):
                self.out.append(raw.replace("−", "-").translate(_SUB))
                return
        for c in n.children:
            self.node(c, in_link)

    def wrap(self, tag, n, in_link):
        inner = _Inline(self.conv)
        for c in n.children:
            inner.node(c, in_link)
        text = "".join(inner.out)
        if not text.strip(" " + _MARK):
            self.out.append(text)
            return
        lead = text[: len(text) - len(text.lstrip())]
        trail = text[len(text.rstrip()) :]
        self.out.append(
            lead + "<" + tag + ">" + text.strip() + "</" + tag + ">" + trail
        )


# The seams a removed element leaves: "Berlin () ist", "(; 1749)", "Wort ,".
_TIDY = (
    (re.compile(r"\s+"), " "),
    (re.compile(r"\s?\(\s*\)"), ""),
    (re.compile(r"\s?\[\s*\]"), ""),
    (re.compile(r"\(\s*[,;:]\s*"), "("),
    (re.compile(r"\s*[,;:]\s*\)"), ")"),
    (re.compile(r"\s?\(\s*\)"), ""),
    (re.compile(r"\(\s+"), "("),
    (re.compile(r"\s+\)"), ")"),
    (re.compile(r"\s+([,;:!?])(?=\s|$|<)"), r"\1"),
    (re.compile(r"\s+\.(?=\s|$|<)"), "."),
    (re.compile(r"(?:[,;]\s*)+([,;])"), r"\1"),
    (re.compile(r"\s{2,}"), " "),
)
_EMPTY_TAG = re.compile(r"<(b|i)>\s*</\1>")
_EMPTY_LINK = re.compile(r'<a href="[^"]*">\s*</a>')
# a bracketed or slashed piece that lost letters is a transcription:
# "[bu<mark>ð<mark>w]" goes whole
_MARKED_GROUP = re.compile(
    r"\[(?:[^\[\]<]|<[^>]*>)*"
    + _MARK
    + r"(?:[^\[\]<]|<[^>]*>)*\]|(?<![\w<])/[^/<>\s]*"
    + _MARK
    + r"[^/<>]*/(?!\w)"
)


def tidy(s, profile):
    if _MARK in s:
        s = _MARKED_GROUP.sub(_MARK, s)
        # "(chinesisch <script>)": the label names a script that went and
        # nothing replaced it, so it goes too
        s = re.sub(
            r"(?:<a href=\"[^\"]*\">)?\b(?:" + profile["labels"] + r")\b(?:</a>)?:?\s*" + _MARK + r"(?=\s*(?:[),;]|$))",
            _MARK,
            s,
        )
        s = re.sub(r"\s*" + _MARK + r"(?:\s*[,;:/]?\s*" + _MARK + r")*\s*", " ", s)
    for rx, rep in _TIDY:
        s = rx.sub(rep, s)
    s = _EMPTY_TAG.sub("", s)
    s = _EMPTY_LINK.sub("", s)
    for rx, rep in _TIDY[1:6]:
        s = rx.sub(rep, s)
    return s.strip()


# ---------------------------------------------------------------- blocks


class Converter:
    def __init__(self, title, profile, stats):
        self.title = title
        self.profile = profile
        self.stats = stats
        self.out = []
        self.headings = []
        self.facts_rows = None
        # what a <br> becomes: a space in prose, an item boundary in a cell
        self.line_break = " "

    # --- small pieces ------------------------------------------------------

    def formula(self, tex):
        tex = htmllib.unescape(tex).strip()
        m = re.match(
            r"^\{\\(?:displaystyle|textstyle|scriptstyle)\s*(.*)\}$", tex, re.S
        )
        if m:
            tex = m.group(1)
        text, complete = tex_text.render(tex)
        if text and complete:
            self.stats["formulas_rendered"] = self.stats.get("formulas_rendered", 0) + 1
            for k, v in (self.profile.get("math") or {}).items():
                text = text.replace(k, v)
            return clean_text(text, self.stats)
        self.stats["formulas_unrendered"] = self.stats.get("formulas_unrendered", 0) + 1
        return clean_text(tex_text.leaves(tex) or text or "", self.stats)

    def inline(self, nodes):
        b = _Inline(self)
        for n in nodes:
            b.node(n)
        return tidy("".join(b.out), self.profile)

    def plain(self, node):
        """A table cell's text: escaped, no markup."""
        b = _Inline(self)
        b.node(node, in_link=True)
        s = tidy("".join(b.out), self.profile)
        s = re.sub(r"</?(?:b|i)>", "", s)
        return s

    def paragraph(self, nodes):
        s = self.inline(nodes)
        if s and s.strip(" .,;:"):
            self.out.append("<p>" + s + "</p>")

    # --- lists -------------------------------------------------------------

    def list_items(self, node, ordered):
        items = [c for c in node.children if isinstance(c, Node) and c.tag == "li"]
        rendered = []
        for li in items:
            inline_nodes = [
                c
                for c in li.children
                if not (
                    isinstance(c, Node) and c.tag in ("ul", "ol", "dl", "table", "div")
                )
            ]
            nested = [
                c
                for c in li.children
                if isinstance(c, Node) and c.tag in ("ul", "ol", "dl", "div")
            ]
            body = self.inline(inline_nodes)
            sub = ""
            if nested:
                saved = self.out
                self.out = []
                for c in nested:
                    self.block(c, 1)
                sub = "".join(self.out)
                self.out = saved
            if body.strip(" .,;:") or sub:
                rendered.append((body, sub))
        if not rendered:
            return
        if ordered:
            for k, (body, sub) in enumerate(rendered, 1):
                if body:
                    self.out.append("<p>%d. %s</p>" % (k, body))
                if sub:
                    self.out.append(sub)
        else:
            self.out.append("<ul>")
            for body, sub in rendered:
                self.out.append("<li>" + body + sub + "</li>")
            self.out.append("</ul>")

    def external_list(self, node):
        """A list whose every item is an outside link (the Weblinks section
        of a language this table has no name for)."""
        items = [c for c in node.children if isinstance(c, Node) and c.tag == "li"]
        if not items:
            return False
        for li in items:
            ext = [
                a for a in li.find_all("a") if "mw:ExtLink" in a.attrs.get("rel", "")
            ]
            if not ext:
                return False
        return True

    # --- tables ------------------------------------------------------------

    def grid(self, table):
        """[(is_header, [cell text])] with colspan and rowspan spread."""
        rows = []
        for tr in table.find_all("tr"):
            # a nested table's rows are its own
            p = tr.parent
            while p is not None and p.tag != "table":
                p = p.parent
            if p is not table:
                continue
            rows.append(tr)
        grid = []
        pending = {}  # column -> (rows left, text)
        for tr in rows:
            cells = [
                c for c in tr.children if isinstance(c, Node) and c.tag in ("td", "th")
            ]
            if not cells:
                continue
            out = []
            header = all(c.tag == "th" for c in cells)
            col = 0
            for c in cells:
                while col in pending:
                    left, text = pending[col]
                    out.append(text)
                    if left <= 1:
                        del pending[col]
                    else:
                        pending[col] = (left - 1, text)
                    col += 1
                text = self.plain(c)
                try:
                    span = max(1, min(20, int(c.attrs.get("colspan", "1") or 1)))
                except ValueError:
                    span = 1
                try:
                    down = max(1, min(200, int(c.attrs.get("rowspan", "1") or 1)))
                except ValueError:
                    down = 1
                for _ in range(span):
                    out.append(text)
                    if down > 1:
                        pending[col] = (down - 1, text)
                    col += 1
            while col in pending:
                left, text = pending[col]
                out.append(text)
                if left <= 1:
                    del pending[col]
                else:
                    pending[col] = (left - 1, text)
                col += 1
            grid.append((header, out))
        return grid

    def table(self, table):
        # A table inside a cell (a climate chart in its frame, a box of
        # results) is a table of its own: lifted out, so the outer cell does
        # not read as one paragraph of every number in it.
        nested = []
        for n in table.find_all("table"):
            p = n.parent
            while p is not None and p.tag != "table":
                p = p.parent
            if p is table:
                nested.append(n)
        for n in nested:
            n.parent.children = [c for c in n.parent.children if c is not n]
        self.table_grid(table)
        for n in nested:
            if n.has_class("navbox"):
                continue
            self.table(n)

    def table_grid(self, table):
        # A phoneme chart, a table of native names: when half its cells are
        # in a script or a transcription the panel cannot show, the grid is
        # holes, and the line saying a table was here is the honest rendering.
        filled = holed = 0
        scratch = {}
        for cell in table.find_all("td") + table.find_all("th"):
            raw = cell.text().strip()
            if not raw:
                continue
            filled += 1
            # a cell is a hole when cleaning it would take letters out
            if _IPA.search(raw) or _MARK in clean_text(raw, scratch, self.profile.get("symbols")):
                holed += 1
        if filled >= 4 and holed * 2 >= filled:
            self.out.append("<p><i>" + esc(self.profile["omitted"]) + "</i></p>")
            self.stats["tables_omitted"] = self.stats.get("tables_omitted", 0) + 1
            return
        grid = self.grid(table)
        grid = [(h, cells) for h, cells in grid if any(c.strip() for c in cells)]
        if not grid:
            return
        # a caption row spanning everything is not column names
        grid = [(h, c) for h, c in grid if not (h and len(c) > 1 and len(set(c)) == 1)]
        if not grid:
            return
        wide = max(len(c) for _, c in grid) > TABLE_MAX_COLS or any(
            len(c.split(" ")) > TABLE_CELL_WORDS
            or len(c.encode("utf-8")) > TABLE_CELL_BYTES
            for _, cells in grid
            for c in cells
        )
        if wide:
            self.table_rows(grid)
            return
        out = ["<table>"]
        last = None
        for is_header, cells in grid:
            tag = "th" if is_header else "td"
            if cells == last:
                continue
            last = cells
            if not is_header:
                cells = [
                    c for j, c in enumerate(cells) if not (j and c == cells[j - 1])
                ]
            out.append(
                "<tr>"
                + "".join(
                    "<%s>%s</%s>"
                    % (tag, self.profile["number"] if c.strip() == "#" else c, tag)
                    for c in cells
                )
                + "</tr>"
            )
        if len(out) == 1:
            return
        out.append("</table>")
        self.out.append("".join(out))
        self.stats["tables_kept"] = self.stats.get("tables_kept", 0) + 1

    def table_rows(self, grid):
        """A table too wide for the panel's grid, one paragraph per row: the
        first cell bold, every other cell labelled by its column header."""
        headers = [cells for is_header, cells in grid if is_header]
        labels = [
            self.profile["number"] if c.strip() == "#" else c
            for c in (headers[-1] if headers else [])
        ]
        body = [cells for is_header, cells in grid if not is_header]
        if not body:
            return
        out = []
        for cells in body[:TABLE_ROWS_LISTED]:
            parts = []
            if len(set(cells)) == 1 and cells[0]:
                cells = cells[:1]
            for j, c in enumerate(cells):
                if j and c == cells[j - 1]:
                    continue
                c = cut_words(c.strip(), TABLE_ROW_CELL_WORDS)
                if not c or c in ("-", "\u2013", "\u2014", "\u2212") or c.endswith(":"):
                    continue
                label = labels[j] if j < len(labels) else ""
                if not parts:
                    parts.append("<b>%s</b>" % c)
                elif label and label != c:
                    parts.append("%s: %s" % (label, c))
                else:
                    parts.append(c)
            if parts:
                para = "<p>" + "; ".join(parts) + "</p>"
                if out and out[-1] == para:
                    continue
                out.append(para)
        if len(body) > TABLE_ROWS_LISTED:
            out.append(
                "<p><i>"
                + esc(self.profile["more_rows"] % (len(body) - TABLE_ROWS_LISTED))
                + "</i></p>"
            )
        if out:
            self.out.extend(out)
            self.stats["tables_listed"] = self.stats.get("tables_listed", 0) + 1

    # --- the infobox -------------------------------------------------------

    def facts(self, box):
        """The infobox as (key, value) rows: two-cell rows only; group
        headers, pictures and captions have no value and go."""
        table = box if box.tag == "table" else box.first("table")
        if table is None:
            return []
        rows = []
        seen = set()
        for _, cells in self._fact_cells(table):
            if len(cells) != 2:
                continue
            key = re.sub(r"\s*:\s*$", "", self.plain(cells[0])).strip()
            value = self.plain_joined(cells[1])
            if not key or not value or len(key) > 80:
                continue
            if key.lower() == value.lower() or (key, value) in seen:
                continue
            seen.add((key, value))
            # an age worked out on the day the page was rendered goes stale on
            # the next birthday ("21. März 1992 (34 Jahre)")
            if self.profile.get("age"):
                value = re.sub(self.profile["age"], "", value).strip()
            if not value:
                continue
            rows.append((key[:1].upper() + key[1:], cut_words(value, FACT_WORDS)))
        return rows

    def _fact_cells(self, table):
        # The row before, per table it belongs to. A table nested in the box
        # that sets pictures side by side puts their captions in the row
        # under them, plain words; in the box's own rows a fact follows the
        # pictures ("Staat:" | "Italien" under a town's arms and map), and so
        # does a key in bold or a header cell in a nested one
        # ("Landeshauptstadt" under a duchy's arms and flag).
        pictures_above = {}
        for tr in table.find_all("tr"):
            cells = [
                c for c in tr.children if isinstance(c, Node) and c.tag in ("td", "th")
            ]
            own = _row_table(tr)
            under_pictures = pictures_above.get(id(own), False)
            pictures_above[id(own)] = (
                own is not table
                and len(cells) >= 2
                and all("data-had-file" in c.attrs for c in cells)
            )
            # a cell spanning the box is a picture, a caption or a group name;
            # a cell that held a picture is a caption, and so is a row under
            # a row of pictures ("Flagge" | "Wappen" under the flag and the
            # arms, in every country's box)
            if any(c.attrs.get("colspan") not in (None, "", "1") for c in cells):
                continue
            if any("data-had-file" in c.attrs for c in cells):
                continue
            if under_pictures and cells and not _names_a_field(cells[0]):
                continue
            # two headers side by side head the pictures under them
            if cells and all(c.tag == "th" for c in cells):
                continue
            # In a table nested inside the box under a heading of its own,
            # two plain cells side by side are two sides compared
            # ("Konfliktparteien": one army | the other), not a key and its
            # value: only a header cell or a "Key:" cell names a field there.
            if len(cells) == 2 and own is not table and _headed(own):
                key = cells[0]
                if key.tag != "th" and not key.text().rstrip().endswith(":"):
                    continue
            yield tr, cells

    def plain_joined(self, cell):
        """A value cell whose line breaks separate items, at any depth:
        "Paramount Pictures, 20th Century Fox, Lightstorm Entertainment"."""
        self.line_break = _ITEM
        try:
            s = self.plain(cell)
        finally:
            self.line_break = " "
        return join_items(t.strip(" ,;") for t in s.split(_ITEM))

    def emit_facts(self):
        if not self.facts_rows:
            return
        name = self.profile["facts"]
        self.headings.append(name)
        self.out.append('<h2 id="s%d">%s</h2>' % (len(self.headings), esc(name)))
        self.out.append(
            "<table>"
            + "".join(
                "<tr><th>%s</th><td>%s</td></tr>" % (k, v) for k, v in self.facts_rows
            )
            + "</table>"
        )
        self.stats["facts"] = self.stats.get("facts", 0) + len(self.facts_rows)

    # --- walking -----------------------------------------------------------

    def heading_text(self, h):
        return re.sub(r"</?(?:b|i)>", "", self.inline(h.children))

    def block(self, n, depth, lead=False):
        """One block-level node into self.out."""
        if isinstance(n, str):
            return
        tag = n.tag
        if tag == "p":
            self.paragraph(n.children)
        elif tag in ("ul", "ol"):
            if self.external_list(n):
                self.stats["link_lists_dropped"] = (
                    self.stats.get("link_lists_dropped", 0) + 1
                )
                return
            self.list_items(n, tag == "ol")
        elif tag == "dl":
            for c in n.children:
                if isinstance(c, Node) and c.tag == "dt":
                    s = self.inline(c.children)
                    if s:
                        self.out.append("<p><b>" + re.sub(r"</?b>", "", s) + "</b></p>")
                elif isinstance(c, Node) and c.tag == "dd":
                    self.container(c, depth)
        elif tag == "table":
            if n.has_class("infobox") and lead and self.facts_rows is None:
                self.facts_rows = self.facts(n)
                return
            if n.has_class("infobox"):
                return
            self.table(n)
        elif tag in ("h2", "h3", "h4", "h5", "h6"):
            self.heading(n, int(tag[1]))
        elif tag == "section":
            self.section(n, depth)
        elif n.has_class("infobox") and lead and self.facts_rows is None:
            self.facts_rows = self.facts(n)
        elif n.has_class("infobox"):
            return
        else:
            self.container(n, depth, lead)

    def container(self, n, depth, lead=False):
        """A div, blockquote or dd: its blocks, and any loose inline run
        between them as a paragraph of its own."""
        run = []
        for c in n.children:
            if isinstance(c, Node) and (c.tag in BLOCK_TAGS or c.has_class("infobox")):
                if run:
                    self.paragraph(run)
                    run = []
                self.block(c, depth, lead)
            else:
                run.append(c)
        if run:
            self.paragraph(run)

    def heading(self, h, level):
        text = self.heading_text(h)
        if not text:
            return
        if level == 2:
            self.headings.append(text)
            self.out.append('<h2 id="s%d">%s</h2>' % (len(self.headings), text))
        else:
            k = min(level, 4)
            self.out.append("<h%d>%s</h%d>" % (k, text, k))

    def section(self, sec, depth):
        heading = None
        for c in sec.children:
            if isinstance(c, Node) and c.has_class("mw-heading"):
                heading = next(
                    (
                        h
                        for h in c.children
                        if isinstance(h, Node)
                        and h.tag in ("h2", "h3", "h4", "h5", "h6")
                    ),
                    None,
                )
                break
            if isinstance(c, Node) and c.tag in ("h2", "h3", "h4", "h5", "h6"):
                heading = c
                break
        if heading is not None:
            name = self.heading_text(heading)
            if fold(name) in self.profile["skip_folded"]:
                self.stats["sections_skipped"] = (
                    self.stats.get("sections_skipped", 0) + 1
                )
                return
        start = len(self.out)
        h2_before = len(self.headings)
        emitted = False
        for c in sec.children:
            if isinstance(c, Node) and c.has_class("mw-heading"):
                for h in c.children:
                    if isinstance(h, Node) and h.tag in ("h2", "h3", "h4", "h5", "h6"):
                        self.heading(h, int(h.tag[1]))
                emitted = len(self.out) > start
                continue
            if isinstance(c, Node) and c.tag in ("h2", "h3", "h4", "h5", "h6") and c is heading:
                self.heading(c, int(c.tag[1]))
                emitted = len(self.out) > start
                continue
            self.block(c, depth + 1)
        # a heading with nothing under it (its section was pictures, a
        # navbox or references) goes with its id
        if emitted and len(self.out) == start + 1:
            self.out.pop()
            if len(self.headings) > h2_before:
                self.headings.pop()
            self.stats["empty_sections_dropped"] = (
                self.stats.get("empty_sections_dropped", 0) + 1
            )


BLOCK_TAGS = frozenset(
    "p ul ol dl table div section blockquote h2 h3 h4 h5 h6 center".split()
)


def zim_xhtml(html_bytes, title, profile, stats=None):
    """(title, headings, xhtml_bytes) for one ZIM article. `title` is the
    entry's own (the ZIM's display title). Raises ValueError for a page that
    is not an article (a redirect stub)."""
    if stats is None:
        stats = {}
    text = (
        html_bytes.decode("utf-8", "replace")
        if isinstance(html_bytes, bytes)
        else html_bytes
    )
    if "mw-parser-output" not in text:
        raise ValueError("not an article page")
    root = parse(text)
    prune(root, stats)
    title = _WS.sub(" ", _CONTROL.sub("", title)).strip()
    conv = Converter(title, profile, stats)
    conv.out.append("<html><body>")
    conv.out.append("<h1>" + esc(clean_text(title, stats)) + "</h1>")
    sections = root.find_all("section")
    top = [
        s for s in sections if s.parent is not None and not _inside(s.parent, "section")
    ]
    lead = next((s for s in top if s.attrs.get("data-mw-section-id") == "0"), None)
    rest = [s for s in top if s is not lead]
    if lead is not None:
        for c in lead.children:
            conv.block(c, 0, lead=True)
    conv.emit_facts()
    for s in rest:
        conv.section(s, 0)
    conv.out.append("</body></html>")
    xhtml = "".join(conv.out)
    xhtml = re.sub(r"<ul></ul>", "", xhtml)
    return title, conv.headings, xhtml.encode("utf-8")


def _inside(node, tag):
    while node is not None:
        if node.tag == tag:
            return True
        node = node.parent
    return False


if __name__ == "__main__":
    import languages
    import zim

    if len(sys.argv) != 3:
        sys.exit("usage: zim_article.py <file.zim> <path>   (prints the XHTML)")
    z = zim.Zim(sys.argv[1])
    e = z.find(sys.argv[2])
    if e is None:
        sys.exit("no such path")
    code = languages.from_zim_language(z.metadata("Language"))
    st = {}
    t, heads, x = zim_xhtml(
        z.content(e), z.resolve(e).title, languages.profile(code), st
    )
    sys.stdout.write(x.decode("utf-8") + "\n")
    print(heads, {k: v for k, v in st.items() if k != "removed_chars"}, file=sys.stderr)
