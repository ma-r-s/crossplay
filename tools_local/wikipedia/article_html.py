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
import html
import re
import unicodedata

import symbols
import tex_text
import urllib.parse

from fold import fold
from glyphs import drawable_class

WIKI = "https://en.wikipedia.org/wiki/"
QUICK_FACTS = "Quick facts"
FACT_WORDS = 28  # under the layout engine's 32-word cell cap, so the grid never stacks
TABLE_MAX_COLS = 4
TABLE_CELL_WORDS = 32
TABLE_CELL_BYTES = 512
TABLE_ROWS_LISTED = 60  # a wide table becomes at most this many row paragraphs
TABLE_ROW_CELL_WORDS = 60  # a cell in such a row is a phrase, not a grid cell
_REF_COLUMN = re.compile(r"^(?:ref\.?s?|refs?\.|notes?|sources?|citations?)$", re.I)
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

# Controls, and the invisible marks the serif has slots for but draws as a
# smudge or a gap (measured on the simulator): zero-width space and joiners,
# bidi marks and embeddings, word joiner, byte order mark, soft hyphen.
_CONTROL = re.compile(
    "[\x00-\x08\x0b\x0c\x0e-\x1f\x7f-\x9f\ufffe\uffff\ud800-\udfff"
    "\u200b-\u200f\u202a-\u202e\u2060-\u2064\ufeff\u00ad]"
)
_CITE = re.compile(r"\[\d+\]")
# The page number a citation carried, left behind once the mark went:
# "principles.: 6 The scope".
# also "phone.: S643 : S643 : 8" and ". : 32, 33, 105 : 184" (rp templates in a row)
_CITE_PAGE = re.compile(r"(?<=[.,;!?])(?:\s?:\s?[A-Z]?\d+(?:[\u2013-]\d+)?(?:,\s?\d+(?:[\u2013-]\d+)?)*)+(?=\s|$)")
# "invasion,the territory": a comma the dump left without its space
_COMMA_GLUE = re.compile(r"(?<=[a-z)]),(?=[A-Za-z]{2,})")
# "A_{1}^{\\complement }\\quad": TeX the dump left outside any block
_TEX_LOOSE = re.compile(r"(?:\\[A-Za-z]+\s*|[A-Za-z]?[_^]\{[^{}]*\}\s*){2,}")
_TEX_ENV = re.compile(r"\{?\\begin\{([a-z*]+)\}.*?\\end\{\1\}\}?\s*(?:\\right\.)?", re.S)
# ": p.45–78 : p.1–46 : p.111–157": page ranges of citations in a row
_CITE_PAGES = re.compile(r"(?:\s*:\s*p{1,2}\.\s?\d+(?:[\u2013-]\d+)?\b)+")
# "## x:", "#; Key": a note marker the dump kept at a line's start
_NOTE_MARKER = re.compile(r"^#+;?\s+")
# a raw reference tag the dump left in the prose
_REF_TAG = re.compile(r"<ref\b[^>]*/>|<ref\b[^>]*>.*?</ref>|<ref\b[^>]*>|<ref\b[^<>]{0,160}$", re.S | re.I)
# Parsoid's protection markers, leaked into a few articles: "\ufffdPROT139\ufffd"
# stands where a reference or template was, and one can end an unclosed
# template ("{{Pie chart| caption = ...\ufffdPROT199\ufffd Roughly ...")
_PROT = re.compile(r"\{\{[^{}]*?\ufffdPROT\d+\ufffd\s*|\s?\ufffdPROT\d+\ufffd")
# a footnote the dump left in the prose, and a template opener never closed
# ("{{block indent| sigma: F -> F'"): the note goes, the opener alone goes
_NOTE_TEMPLATE = re.compile(r"\s?\{\{(?:efn|sfn|refn|notetag|note)\|[^{}]{0,800}\}\}")
_TEMPLATE_OPENER = re.compile(r"\{\{[A-Za-z][A-Za-z ]{0,30}\|\s*")
_BRACE_OPENER = re.compile(r"\{\{(?=\s|$)")
# wikitext marks the dump left: ''italic'' and '''bold'''; "f''(x)" is a
# second derivative and stays, so a mark must open before a letter
_WIKI_BOLD = re.compile(r"'''([A-Za-z][^'\n]{0,120}?)'''")
_WIKI_ITALIC = re.compile(r"''([A-Za-z][^'\n]{0,120}?)''")
_WIKI_QUOTE_LEFT = re.compile(r"'''(?=[A-Za-z])")
_CULTIVAR_QUOTE = re.compile(r"(?<=[a-z])'(?=[A-Z][a-z])")
# "(-infinity,infinity)": a comma between spelled words gets its space;
# "epsilon,gamma-carotene" is a chemical name and stays tight
_COMMA_WORDS = re.compile(r"\b([a-z]{2,}),(?=[A-Za-z]{2,})")


def _comma_words(m):
    return m.group(0) if m.group(1) in _GREEK_WORDS else m.group(1) + ", "
_GREEK_WORDS = frozenset(symbols.GREEK.values())
# "{{rp}}" page references after a period: ".: ii. 161 : I.68 However"
# the same with plain pages: "speciosa.: 86-95, 137)"
_CITE_NUMERIC = re.compile(r"(?<=[a-z]{4}[.!?])(?::\s?\d{1,4}(?:[\u2013-]\d{1,4})?(?:, \d{1,4}(?:[\u2013-]\d{1,4})?)*)(?=[\s)]|$)")
_CITE_ROMAN = re.compile(r"(?<=[.,;!?])(?:\s?:\s?[IVXLCivxlc]{1,7}\.\s?\d{1,4}(?:[\u2013-]\d{1,4})?)+(?=\s|$)")
# what the residue scanner steps over: TeX spacing, align marks, empty groups
_TEX_LEFTOVER = re.compile(r"\\[,;:!>]|\{\s*\}")
_TEX_ALIGN = re.compile(r"\s*&=|(?<=\s)&(?=\s)")
# "z w z^{w}": the dump's words for a formula, then the TeX of them
_FLAT_THEN_TEX = re.compile(r"\b(\w) (\w) (?=\1\^\{\2\})")
_SCRIPT_BRACES = re.compile(r"(\w)\^\{(\w)\}")
# a formula the dump lost the middle of: "sigma = sigma_ij = = == ==,"
_REPEATED_EQUALS = re.compile(r"([=\u2261]=?)(?:\s+[=\u2261]=?)+(?!\S)")
# one greedy run per group, groups split by whitespace: "(?:\\s*=?=)+" was
# ambiguous on "=====" and took exponential time on a rule of 36 of them,
# which stalled a full build (2026-09-12)
_TRAILING_EQUALS = re.compile(r"\s*[=\u2261]+(?:\s+[=\u2261]+)*(?=\s*[,.;]?\s*$)")
# two quoted lines the dump joined: "her.'""'Did you" gets its space back
_QUOTE_GLUE = re.compile(r"([.!?][\"']{1,2})([\"']{1,2}[A-Z])")
_TEX_START = re.compile(r"\\(?:\\|[A-Za-z]+)")
_TEX_SCRIPT_GROUP = re.compile(r"\s?[A-Za-z]?(?:[_^]\{[^{}]*\}\s*)+")


def _strip_tex_residue(s):
    """TeX the dump left outside any block, in any shape: from a backslash
    command on, over braces (balanced), scripts, operators and letters, up to
    the end of the run. "nabla v = R nabla u \\nabla v=R\\nabla u where R is"
    keeps its words and loses the TeX."""
    out = []
    i = 0
    n = len(s)
    while True:
        m = _TEX_START.search(s, i)
        if not m:
            out.append(s[i:])
            break
        j = m.start()
        k = m.end()
        depth = 0
        while k < n:
            c = s[k]
            if c == "{":
                depth += 1
            elif c == "}":
                if depth == 0:
                    break
                depth -= 1
            elif depth == 0:
                if c == "\\":
                    k += 1  # a command: its letters belong to the run
                    while k < n and s[k].isalpha():
                        k += 1
                    continue
                if c.isalpha():
                    # a word of two or more letters is prose again; a single
                    # letter is a variable and stays in the run
                    if k + 1 < n and s[k + 1].isalpha():
                        break
                elif c == "." and k + 1 < n and s[k + 1] == " ":
                    break
                elif not (c in "^_&=+*/,;()[]| \t" or c.isdigit() or c in "\u2212-'"):
                    break
            k += 1
        # trim to the last TeX-looking char
        run = s[j:k].rstrip(" ,;")
        out.append(s[i:j].rstrip())
        out.append(" ")
        i = j + len(run)
    return re.sub(r"  +", " ", "".join(out))
_TEMPLATE_ERROR = re.compile(r"\s*(?::\s*)?(?:ISBN / Date incompatibility|Check date values in: [^()]*|Cite \w+ requires [^()]*)\s*\(help\)")
_WS = re.compile(r"\s+")
# A Greek letter standing alone is a symbol ("frequency \u03bd"), and the
# reader's serif has no Greek; a Greek word beside other Greek is a run the
# stripper handles. Names, not transliteration: "h nu" reads as the physics.
_GREEK_NAMES = {
    "\u03b1": "alpha", "\u03b2": "beta", "\u03b3": "gamma", "\u03b4": "delta",
    "\u03b5": "epsilon", "\u03b6": "zeta", "\u03b7": "eta", "\u03b8": "theta",
    "\u03b9": "iota", "\u03ba": "kappa", "\u03bb": "lambda", "\u03bc": "mu",
    "\u03bd": "nu", "\u03be": "xi", "\u03c0": "pi", "\u03c1": "rho",
    "\u03c3": "sigma", "\u03c4": "tau", "\u03c5": "upsilon", "\u03c6": "phi",
    "\u03c7": "chi", "\u03c8": "psi", "\u03c9": "omega",
    "\u0393": "Gamma", "\u0394": "Delta", "\u0398": "Theta", "\u039b": "Lambda",
    "\u039e": "Xi", "\u03a0": "Pi", "\u03a3": "Sigma", "\u03a6": "Phi",
    "\u03a8": "Psi", "\u03a9": "Omega",
}
_GREEK_ALONE = re.compile(
    "(?<![A-Za-z\u0370-\u03ff\u1f00-\u1fff])([\u0391-\u03a9\u03b1-\u03c9])"
    "(?![A-Za-z\u0370-\u03ff\u1f00-\u1fff])"
)


_TEX_OPEN = re.compile(r"\{\\(?:displaystyle|textstyle|scriptstyle|scriptscriptstyle)\b")
# Mario, 2026-09-11: maths needs delicate care; route 3. Counted per build.
tex_stats = {"formulas_rendered": 0, "formulas_words_kept": 0, "formulas_unmatched": 0}


def _render_tex(s):
    """Every "{\\displaystyle TEX}" block, with the flattened words the dump
    wrote before it, becomes the TeX rendered as linear text (tex_text). The
    words are found by matching what the dump would have written for that
    TeX; when they are not there, or the renderer met a command it does not
    know, the words stay and the TeX goes, as before."""
    out = []
    i = 0
    n = len(s)
    while True:
        m = _TEX_OPEN.search(s, i)
        if not m:
            out.append(s[i:])
            break
        j = m.start()
        k = j
        depth = 0
        while k < n:
            if s[k] == "{":
                depth += 1
            elif s[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        if k >= n:
            out.append(s[i:])
            break
        head = s[i:j]
        tex = s[m.end() : k]
        rendered = _replace_words(head, tex)
        if rendered is None:
            if head.endswith(" "):
                head = head[:-1]
            out.append(head)
        else:
            out.append(rendered)
        i = k + 1
    return "".join(out)


def _replace_words(head, tex):
    """head with its trailing formula words replaced by the rendering, or
    None when the words are not found or the rendering is not complete."""
    text, complete = tex_text.render(tex)
    if not text:
        return None
    for skip in (False, True):
        target = tex_text.loose(tex_text.leaves(tex, skip_matrices=skip))
        if not target:
            continue
        # walk back over the head until its loose form ends with the target
        j = len(head)
        got = ""
        while j > 0 and len(got) < len(target):
            j -= 1
            if not head[j].isspace():
                got = tex_text.loose(head[j]) + got
        if got != target or (j > 0 and not head[j - 1].isspace() and head[j - 1] not in "(["):
            continue
        if not complete:
            tex_stats["formulas_words_kept"] += 1
            return None
        tex_stats["formulas_rendered"] += 1
        return head[:j] + text
    tex_stats["formulas_unmatched"] += 1
    return None


def _strip_tex(s):
    """The dataset writes every formula twice: its words, then the TeX in
    "{\\displaystyle ...}" (or textstyle, scriptstyle). The TeX goes, braces
    balanced."""
    out = []
    i = 0
    n = len(s)
    while True:
        m = _TEX_OPEN.search(s, i)
        j = m.start() if m else -1
        if j < 0:
            out.append(s[i:])
            break
        k = j
        depth = 0
        while k < n:
            if s[k] == "{":
                depth += 1
            elif s[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        if k >= n:
            out.append(s[i:])
            break
        head = s[i:j]
        if head.endswith(" "):
            head = head[:-1]
        out.append(head)
        i = k + 1
    return "".join(out)

# A hatnote points at another page; on a device with no other page to point
# at it is noise: "Main article: X", "For other uses, see X (disambiguation)".
_HATNOTE = re.compile(
    r"(?:Main articles?:|See also:|Further information:|For other uses\b|For the [^.]{0,80}, see\b|"
    r"Not to be confused with\b|\"[^\"]{1,80}\" redirects here\b|[^.\n]{1,80} redirects here\.)"
)
_NAME_DISAMBIG = re.compile(r" \((?:band|album|singer|group|musician|rapper|artist|film|TV series|series)\)(?= (?:chronology|discography|singles))")
_NAVBOX = re.compile(r"This box:|\bview\s+talk\s+edit\b|\bv\s*[\u00b7.]\s*t\s*[\u00b7.]\s*e\b")
# One level of nesting, so "(UK: OH-s(h)ee-AH-nee-<schwa>)" is one parenthetical.
_PAREN = re.compile(r" ?\((?:[^()]|\([^()]*\))*\)")

_run_re = None
_labelled_run_re = None
REMOVED_SCRIPTS = "\u0400-\u052f"  # Cyrillic and its supplement
_ROMAN_LETTER = re.compile("[\u0370-\u03ff\u1f00-\u1fff\u0400-\u052f]")
# a romanisation stands right after the run: the run goes, the romanisation stays
_ROMANISED_NEXT = re.compile(
    r"\s*[,;:]?\s*(?:\(|\b)(?:romani[sz]ed|romani[sz]ation|translit\w*|pinyin|lit\.|literally|IPA)\b", re.I
)
_MARK = "\x01"  # where a run stood, until the tidy has looked at it
_PRONUNCIATION_SEG = re.compile(r"\s*(?:UK|US|GB|AU|NZ|IPA|pronounced|pronunciation|respelled)\b", re.I)


def _runs():
    """A run: a code point the serif cannot draw, extended over whitespace
    to the next such code point; optionally preceded by a "Script:" label."""
    global _run_re, _labelled_run_re
    if _run_re is None:
        # what the serif lacks, plus Cyrillic, which it draws and Mario does
        # not want on the panel (2026-09-11): a Cyrillic word in prose is
        # romanised, in a labelled aside it goes with its label
        bad = "(?:[^" + drawable_class() + "]|[" + REMOVED_SCRIPTS + "])"
        # a run carries the quotes around it and the commas, colons and
        # quotes inside it, so a quoted Hebrew word, a list of Devanagari
        # titles or a Chinese sentence with its commas goes whole, not as
        # (") and (,)
        quote = "[" + _RUN_QUOTES + "]"
        run = "(?:" + quote + r"\s*)?" + bad + r"(?:[\s,;:\"'\u201c\u201d\u2018\u2019]*" + bad + r")*(?:\s*" + quote + ")?"
        _run_re = re.compile(run)
        label = r"(?:(?<![A-Za-z])(?:(?:simplified|traditional|literally|romani[sz]ed|born|modern|classical|standard|colloquial|formal|archaic|native|also|formerly|abbreviated|pinyin) )?[A-Z][A-Za-z]*(?:[ -][A-Za-z]+){0,2}:\s?)?"
        _labelled_run_re = re.compile(label + run)
    return _run_re, _labelled_run_re


# Source remnants: the dataset already dropped pronunciation spans and native
# scripts from some leads, leaving "(German:; 6 January 1850" and "Fernandel ()",
# and it pads every quotation with spaces: the " beech ", lit. ' uncle '.
_RUN_QUOTES = "\"'\u201c\u201d\u2018\u2019"
# a label inside quotes ("House of the Mahdi:) is quoted text, not a label
# a label opens with a capital or a word labels use ("lit.", "born",
# "romanized"); "of the Mahdi:" inside a quotation is not one
_LABEL_HEAD = r"(?:[A-Z][A-Za-z.]*|lit\\.|pl\\.|romani[sz]ed|pinyin|born|n\\u00e9e|also|abbreviated|simplified|traditional|literally|translit\\.|transliterated|from|or|in|meaning|formerly|native|modern|classical|standard|colloquial|formal|archaic)"
# ... and stands at the start of its segment: after "(", ";" or ","
_SEGMENT_START = r"(?:^|(?<=[(\[;,])|(?<=[(\[;,] ))"
_EMPTY_LABEL = re.compile(
    _SEGMENT_START + _LABEL_HEAD + r"(?:[ -][A-Za-z.]+){0,3}:\s*(?=[;,)]|[A-Z][a-z]+(?:[ -][A-Za-z]+){0,2}:\s|lit\.\s|literally\s|[Rr]omani[sz]ed[: ]|pinyin:|IPA:|translit|also [Rr]omani[sz]ed)"
)
# the same at the end of a parenthetical's segment ("from Sanskrit: , IPA:")
_EMPTY_LABEL_END = re.compile(
    _SEGMENT_START + _LABEL_HEAD + r"(?:[ -][A-Za-z.]+){0,3}:\s*(?=[;,)]|$)"
)
# "(listen)": the audio link's text, with no audio to play
_LISTEN = re.compile(r"\s?\(\s*(?:listen|more)\s*\)", re.I)
# "April 26, 1994 (1994-04-26)": the start-date template's hidden ISO copy
_ISO_DATE_DUP = re.compile(r"(?<=[a-z0-9])\s?\(\d{4}-\d{2}(?:-\d{2})?\)")
# "(Pub. L. Tooltip Public Law (United States)107-252 (text) (PDF))": an
# abbreviation's tooltip and the law template's link labels
_TOOLTIP = re.compile(r"\s?\bTooltip [A-Z][A-Za-z .]{0,60}(?:\([^()]{0,40}\))? ?(?=\d|$)")
_TEXT_PDF = re.compile(r"\s?\(text\)(?:\s?\(PDF\))?")
# "Team v t e": a navbox's view-talk-edit inside a table header
_VTE = re.compile(r"\s?\bv\s+t\s+e\b")
# a coordinate pair the infobox left at the front of a paragraph
_LEAD_COORDS = re.compile(
    r"^\s*\d{1,3}\u00b0[\d\u2032\u2033'\"\s.]*[NS]\s*\d{1,3}\u00b0[\d\u2032\u2033'\"\s.]*[EW]"
    r"(?:\s*/\s*[\d.\u2212-]+\u00b0[NS]\s*[\d.\u2212-]+\u00b0[EW])?(?:\s*/\s*[\d.\u2212-]+;\s*[\d.\u2212-]+)?\s*(?=[A-Z])"
)
_FUNCTION_WORDS = frozenset("from or and of the a an in at by to lit also see cf".split())
_EMPTY_PAREN = re.compile(r"\s?\(\s*[,;:\s]*\)")
# "(pronounced French pronunciation:)": the IPA went with its slashes
_EMPTY_PRON = re.compile(r"(?:pronounced\s+)?(?:[A-Z][a-z]+\s+)?(?:pronunciation|IPA):\s*(?=[;,)]|$)")
# "=== Neural is a discipline": heading marks the dump left on a line
_HEADING_MARKS = re.compile(r"^\s*={2,}\s*|\s*={2,}\s*$")
# IPA between slashes or brackets, when the serif cannot draw it.
_SLASHED = re.compile(r" ?/[^/]{1,80}/")
# what a pronunciation carries that prose never does: IPA letters, modifier
# letters, tone bars, or the spaced single letters of the dump's IPA
_IPA_CHAR = re.compile(r"[\u0250-\u02ff\u1d00-\u1dbf]|(?: [a-z\u00e6\u00f0\u00f8\u03b8]){3}")
_BRACKETED = re.compile(r" ?\[[^\[\]]{1,80}\]")

_TIDY = (
    (re.compile(r"\(\s*[,;:]\s*"), "("),
    (re.compile(r"(?<![\s(]\")(?<!^\")\s*[,;:]\s*\)"), ")"),  # not the face of ":)"
    (re.compile(r"\(\s*\)"), ""),
    (re.compile(r"\[\s*\]"), ""),
    (re.compile(r"\s+([,;?)]|!(?!=)|:(?!\s?\d))"), r"\1"),  # "a != 0" and "3 : 1" keep their spaces
    (re.compile(r"\s+\.(?![A-Za-z0-9])"), "."),
    (re.compile(r"\(\s+"), "("),
    (re.compile(r"(?:[,;:]\s*)+([,;:])"), r"\1"),
    (re.compile(r"\s{2,}"), " "),
)


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def esc_attr(s):
    return esc(s).replace('"', "&quot;")


# "lasted 28 days.The truce": the source lost the space where a citation
# stood. Three letters before the period so "e.g.The" and initials stay.
_SENTENCE_GLUE = re.compile(r"([a-z]{3,}[.!?]|\d{4}\.)([A-Z][a-z]{2,})")
# "Bonaparte, 1835Tribe Acanthurini": nested list items the source ran together
_YEAR_GLUE = re.compile(r"\b((?:1[5-9]|20)\d\d)(?=[A-Z][a-z]{2,})")
# "{{ cite journal }}: CS1 maint: DOI inactive as of June 2024 (link)": a
# citation template the source left in the prose
_TEMPLATE = re.compile(r"\s*\{\{[^{}]{0,120}\}\}(?::\s*CS1 maint:[^.()]*(?:\([a-z ]+\))?\.?)?")
# "10 5 M": a power of ten whose exponent the source flattened into a spaced
# digit; the serif has superscript digits and the superscript minus.
_TEN_POWER = re.compile(r"(?<![\d.,\u2212-])\b10 (-?)(\d{1,2})\b(?![.,:]\d|%| ?[-\u2013]\d)")
_SUPERSCRIPT = str.maketrans("0123456789-", "\u2070\u00b9\u00b2\u00b3\u2074\u2075\u2076\u2077\u2078\u2079\u207b")


def clean_text(s):
    """Controls out, citation marks out, whitespace collapsed."""
    if "&" in s:
        s = html.unescape(s)  # "22 &amp;amp; 23 Geo. 5": the source escaped it twice
    if "." in s:
        s = _SENTENCE_GLUE.sub(r"\1 \2", s)
    if "(" in s:
        s = _LISTEN.sub("", s)
        s = _ISO_DATE_DUP.sub("", s)
        s = _TEXT_PDF.sub("", s)
    if "Tooltip" in s:
        s = _TOOLTIP.sub(" ", s)
    if "v" in s:
        s = _VTE.sub("", s)
    if "\u00b0" in s and _LEAD_COORDS.match(s):
        s = _LEAD_COORDS.sub("", s, count=1)
    if "," in s:
        s = _COMMA_GLUE.sub(", ", s)
    s = _YEAR_GLUE.sub(r"\1 ", s)
    s = _QUOTE_GLUE.sub(r"\1 \2", s)
    if not s:
        return ""
    s = _CONTROL.sub("", s)
    if "," in s:
        s = _COMMA_GLUE.sub(", ", s)  # again: a zero-width space after the comma just went
    if "style" in s and _TEX_OPEN.search(s):
        s = _render_tex(s)
    if "\\" in s:
        s = _TEX_ENV.sub("", s)
        s = _strip_tex_residue(s)  # TeX the dump left outside any block
        s = _TEX_SCRIPT_GROUP.sub(" ", s)  # "A_{1}^{ }" left beside it
        s = _TEX_LEFTOVER.sub(" ", _TEX_LEFTOVER.sub(" ", s))
        s = _TEX_ALIGN.sub(lambda m: " =" if "=" in m.group(0) else " ", s)
        s = re.sub(r"  +", " ", _TEX_SCRIPT_GROUP.sub(" ", s))
        s = re.sub(r"\s[_^](?=\s|$)", "", s)
        if s.count("{") != s.count("}"):
            s, _ = _balance(s, "{", "}")
    if "^{" in s:
        s = _SCRIPT_BRACES.sub(r"\1^\2", _FLAT_THEN_TEX.sub("", s))
    if "=" in s or "\u2261" in s:
        s = _TRAILING_EQUALS.sub("", _REPEATED_EQUALS.sub(r"\1", s))
    if "(help)" in s:
        s = _TEMPLATE_ERROR.sub("", s)
    if "<ref" in s:
        s = _REF_TAG.sub("", s)
    if ": p" in s or ":p" in s:
        s = _CITE_PAGES.sub("", s)
    if ":" in s:
        s = _CITE_NUMERIC.sub("", _CITE_ROMAN.sub("", s))
    if s.startswith("#"):
        s = _NOTE_MARKER.sub("", s)
    if "==" in s:
        s = _HEADING_MARKS.sub("", s)
    if "PROT" in s:
        s = _PROT.sub("", s)
    if "{{" in s:
        s = _TEMPLATE.sub("", s)
        s = _NOTE_TEMPLATE.sub("", s)
        s = _TEMPLATE_OPENER.sub(" ", s)
        surplus = s.count("{") - s.count("}")
        if surplus > 0:
            s = _BRACE_OPENER.sub("", s, count=surplus)
    if "''" in s:
        s = _WIKI_ITALIC.sub(r"\1", _WIKI_BOLD.sub(r"\1", s))
        s = _CULTIVAR_QUOTE.sub(" '", _WIKI_QUOTE_LEFT.sub("'", s))
    s = _CITE.sub("", s)
    s = _CITE_PAGE.sub("", s)
    s = _GREEK_ALONE.sub(lambda m: _GREEK_NAMES.get(m.group(1), m.group(1)), s)
    s = _WS.sub(" ", s).strip()
    if "10 " in s:
        s = _TEN_POWER.sub(lambda m: "10" + (m.group(1) + m.group(2)).translate(_SUPERSCRIPT), s)
    return s


_CLITIC_AFTER = re.compile(r"(?:s|d|ll|re|ve|m|t)(?![A-Za-z])")


def _close_quote_gaps(text):
    """"the \" beech \"." to "the \"beech\".": a quote after whitespace opens
    and the gap after it goes; a quote after a word closes and the gap before
    it goes. Which it is comes from what touches it, not from the count,
    because the source pads its quotes unevenly ("' colouring', 'tingeing '
    or ' dyeing '"). A padded possessive ("Athens ' City") returns to its
    word. An inch mark ("a 12\" single") follows a digit and is left alone.
    Returns (text, gaps closed)."""
    out = []
    n = 0
    i = 0
    inside = None
    length = len(text)
    while i < length:
        c = text[i]
        if c in "\"'":
            before = text[i - 1] if i else " "
            after = text[i + 1] if i + 1 < length else " "
            word_before = before.isalnum() or before in ".,!?)]"
            word_after = after.isalnum() or after in "([\u2018\u201c"
            if c == "'" and before.isspace() and after.isspace():
                # "Athens ' City": the possessive of the word before
                k = len(out) - 1
                while k >= 0 and out[k].isspace():
                    k -= 1
                if inside is None and k >= 0 and out[k] in "sS":
                    del out[k + 1 :]
                    out.append(c)
                    n += 1
                    i += 1
                    continue
            if c == "'" and (word_before and word_after or _CLITIC_AFTER.match(text, i + 1)):
                out.append(c)  # an apostrophe inside a word, or a padded clitic: McGregor 's
                i += 1
                continue
            if c == '"' and before in "'\u2019" and after.isspace() and text[i + 2 : i + 3] in ('"', "'"):
                out.append(c)  # a closing quote before the next line's opening one: her.'" "'Did
                i += 1
                continue
            closes = inside == c and (word_before or not word_after)
            opens = inside is None and not word_before and not before.isdigit()
            if closes:
                while out and out[-1].isspace():
                    out.pop()
                    n += 1
                out.append(c)
                inside = None
                i += 1
                continue
            if opens:
                out.append(c)
                i += 1
                while i < length and text[i].isspace():
                    i += 1
                    n += 1
                inside = c
                continue
        out.append(c)
        i += 1
    return "".join(out), n


# The source pads every inline element, so a possessive after an italic
# title or a link arrives as "Zelda 's" and a comma after one as "Hyrule ,":
# 2.5 of the former per article in the essentials. An apostrophe and its
# clitic close up against the word before; a comma, full stop, semicolon or
# question mark closes up when followed by space or the end. A colon does not
# ("3 : 1" is a ratio), nor an inch mark after a digit.
_CLITIC_GAP = re.compile(r"(?<=[\w)\]\"\u201d])[ \u00a0]+(['\u2019])(s|d|ll|re|ve|m|t)\b")
_PUNCT_GAP = re.compile(r"(?<=\S)[ \u00a0]+([,.;!?])(?=\s|$)")
# "epsilon : Permittivity": a colon padded between two words (a ratio,
# "3 : 1", keeps its spaces)
_COLON_GAP = re.compile(r"(?<=[A-Za-z\u00c0-\u024f)]) :(?=\s|$)|(?<=[A-Za-z\u00c0-\u024f]) :(?= [A-Za-z\u00c0-\u024f])")
# "Protestant -led", "post- Civil War": a hyphen padded on one side after a
# link or an italic (0.8 and 0.5 per article). "pre- and post-war" is the
# one idiom that keeps its space, so a hyphen before "and" or "or" stays.
# The same for a dash the source padded on one side only ("Goudreau \u2014on
# backup vocals": 2,372 em dashes and 624 en dashes in the essentials).
# A dash spaced on both sides is a style and stays.
_HYPHEN_BEFORE = re.compile(r"(?<=\w)[ \u00a0]+([-\u2013\u2014])(?=\w)")
# A suffix or ending written on its own keeps its space: "Final -m was
# dropped", "the suffix -ing", "-am, -em, -um". One or two letters after the
# hyphen, or a word of grammar before it, is that and not a padded link.
_SUFFIX_TAIL = re.compile(r"[A-Za-z]{1,2}(?![A-Za-z])")
_SUFFIX_HEADS = frozenset(
    "suffix suffixes prefix prefixes ending endings final initial affix affixes infix marker "
    "markers particle particles morpheme morphemes form forms clitic clitics stem stems".split()
)


def _hyphen_before(m):
    text = m.string
    if m.group(1) != "-":
        return m.group(1)
    if _SUFFIX_TAIL.match(text, m.end()):
        return m.group(0)
    head = text[: m.start()].split()
    if head and head[-1].lower().strip(",;:") in _SUFFIX_HEADS:
        return m.group(0)
    return m.group(1)
_HYPHEN_AFTER = re.compile(r"(?<=\w)([-\u2013\u2014])[ \u00a0]+(?=(?!(?:and|or)\b)\w)")
# Wikipedia's respelling, "(TAM-ilz, TAHM-)": syllables in capitals joined by
# hyphens, two of them or one ending in a hyphen, and nothing else in the
# parentheses. "(US-based)" is one plain token and stays.
_RESPELL_TOKEN = r"(?:[a-z]{1,4}-)?[A-Z]{1,6}(?:-[a-z]{1,8})*-?"
_RESPELL = re.compile(
    r"[ \u00a0]*\((?:" + _RESPELL_TOKEN + r"(?:,? " + _RESPELL_TOKEN + r")+|[A-Z]{1,6}(?:-[a-z]{1,8})*-)\)"
)


_EMPTY_PAREN_ANY = re.compile(r"\s?\(\s*[,;:\s]*\)")
# "(pronounced; 10 January 1769": the guide went, its word stayed.
_PRONOUNCED = re.compile(r"\(?\s*\bpronounced\b\s*(?=[;,)])")
# "2 + 1 / 4 in": the mixed-number template, once its fraction slash is a slash.
_MIXED_NUMBER = re.compile(r"\b(\d+) \+ (\d+) ?[/\u2044] ?(\d+)\b")
# "1 \u2044 4": the fraction slash the serif draws, spaced by the source.
_FRACTION_GAP = re.compile(r"(?<=\d) ?\u2044 ?(?=\d)")
# "3,855/km 2": the superscript came through as a spaced digit.
_UNIT_POWER = re.compile(r"\b(km|m|cm|mm|mi|ft|yd|in|nmi)\s+([23])\b(?![,.:]\d| [a-z]| ?\d|\u00bd)")
_POWERS = {"2": "\u00b2", "3": "\u00b3"}


def _close_inline_gaps(text):
    # "(,)" and "( ; )": what a parenthetical is after every run inside it
    # went (64 articles in the essentials kept one).
    text, z = _EMPTY_PAREN_ANY.subn("", text)
    text, z2 = _PRONOUNCED.subn(lambda m: "(" if m.group(0).lstrip().startswith("(") else "", text)
    text, z3 = _MIXED_NUMBER.subn(r"\1 \2/\3", text)
    text, z5 = _FRACTION_GAP.subn("\u2044", text)
    z3 += z5
    text, z4 = _UNIT_POWER.subn(lambda m: m.group(1) + _POWERS[m.group(2)], text)
    z += z2 + z3 + z4
    text, a = _CLITIC_GAP.subn(r"\1\2", text)
    text, b = _PUNCT_GAP.subn(r"\1", text)
    text, b2 = _COLON_GAP.subn(":", text)
    b += b2
    text, c = _HYPHEN_BEFORE.subn(_hyphen_before, text)
    text, d = _HYPHEN_AFTER.subn(r"\1", text)
    text, e = _RESPELL.subn("", text)
    return text, a + b + c + d + e + z


_ok_re = None
# Letter plus combining mark draws as a box on the panel today, not because
# the serif lacks the mark (it has U+0300-036F and EpdFont overlays them) but
# because the reader composes every word to NFC before layout (upstream's
# ParsedText::addWord) and then looks up the precomposed letter, which the
# serif lacks. Measured on the simulator 2026-09-11: "Ma\u1e25m\u016bd" as
# "Mah\u0323m\u016bd" showed a box over the h. Until the app draws with a card
# font that carries Latin Extended Additional, the base letter is the lesser
# loss; the census counts every one (diacritics_dropped).
DECOMPOSE = False


def _fold_chars(text):
    """A code point the serif lacks becomes something it draws, losing as
    little as possible, in this order:

    1. its compatibility form when that is drawable: a circled digit is the
       digit, a fullwidth comma a comma, a script or fraktur capital the
       capital, "\u2103" is "\u00b0C" (NFKC);
    2. (off, see DECOMPOSE) its base letter plus the combining mark, which
       the renderer overlays: "\u1e25" as "h" + U+0323, nothing lost;
    3. its base letter alone: "ma\u1e47\u1e0dal\u012b" reads "mandali", not
       "maali" (NFD, marks dropped). This loses the accent and is counted,
       since deleting the letter made a wrong word with no hole in it.

    Anything else stays for the run rules. Returns (text, {counts})."""
    global _ok_re
    run_re, _ = _runs()
    if not run_re.search(text):
        return text, {}
    if _ok_re is None:
        _ok_re = re.compile("[" + drawable_class() + "]")
    ok = _ok_re
    out = []
    counts = {}

    def drawable(s):
        return bool(s) and all(ok.match(c) for c in s)

    for ch in text:
        if ok.match(ch):
            out.append(ch)
            continue
        compat = unicodedata.normalize("NFKC", ch)
        if compat != ch and drawable(compat):
            out.append(compat)
            counts["compat_folded"] = counts.get("compat_folded", 0) + 1
            continue
        decomposed = unicodedata.normalize("NFD", ch)
        if DECOMPOSE and len(decomposed) > 1 and drawable(decomposed):
            out.append(decomposed)
            counts["letters_decomposed"] = counts.get("letters_decomposed", 0) + 1
            continue
        base = "".join(c for c in decomposed if not unicodedata.combining(c))
        if ch.isalpha() and base != ch and drawable(base):
            out.append(base)
            counts["diacritics_dropped"] = counts.get("diacritics_dropped", 0) + 1
            continue
        out.append(ch)
    return "".join(out), counts


_LOOKALIKE_RE = re.compile("[" + "".join(re.escape(c) for c in symbols.LOOKALIKES) + "]")
_LOOKALIKE_TOKEN = re.compile(r"\S+")


def _fold_lookalikes(text):
    """A letter of an orthography the serif lacks becomes the plain letter it
    stands in for (symbols.LOOKALIKES), inside a word only: a token with no
    hyphen and at least two drawable letters besides it. "M\u0259mm\u0259d"
    reads "M\u00e4mm\u00e4d"; the "-\u0259-" of a respelling and a lone IPA symbol
    stay for the run rules. Returns (text, n)."""
    if not _LOOKALIKE_RE.search(text):
        return text, 0
    ok = _ok_re or re.compile("[" + drawable_class() + "]")
    n = 0

    def token(m):
        nonlocal n
        tok = m.group(0)
        if "-" in tok or not _LOOKALIKE_RE.search(tok):
            return tok
        if sum(1 for c in tok if c.isalpha() and ok.match(c)) < 2:
            return tok
        out, k = _LOOKALIKE_RE.subn(lambda mm: symbols.LOOKALIKES[mm.group(0)], tok)
        n += k
        return out

    return _LOOKALIKE_TOKEN.sub(token, text), n


def _romanize_runs(text, run_re):
    """A run that is Greek or Cyrillic letters, with no romanisation beside
    it, is written in Latin letters where it stands. Returns (text, n)."""
    n = 0

    def sub(m):
        nonlocal n
        run = m.group(0)
        letters = [c for c in run if c.isalpha()]
        if not letters or sum(1 for c in letters if _ROMAN_LETTER.match(c)) < len(letters):
            return run
        if _ROMANISED_NEXT.match(text, m.end()):
            return run
        latin = symbols.romanize(run)
        # "\u1f08\u03c1\u03b9\u03b8\u03bc\u03bf\u03af, Arithmoi": the source's own romanisation
        # follows; the run goes and that one stays. "The Neretva (Serbian
        # Cyrillic: \u041d\u0435\u0440\u0435\u0442\u0432\u0430)": the sentence has the word already
        after = _NEXT_WORD.match(text, m.end())
        if after and _plain(after.group(1)) == _plain(latin):
            return run
        if " " not in latin.strip() and re.search(r"(?<![A-Za-z])" + re.escape(_plain(latin)) + r"(?![A-Za-z])", _plain(text[: m.start()])):
            return run
        n += 1
        return latin

    out = run_re.sub(sub, text)
    if n and "(" in out:
        out = _ROMAN_PAREN.sub(lambda m: m.group(2) if _plain(m.group(1)) == _plain(m.group(2)) else m.group(0), out)
    if n >= 2:
        # "\u1f55\u03b2\u03bf\u03c2 or \u1f51\u03b2\u03cc\u03c2": two accentuations, one spelling
        out = _SAME_TWICE.sub(r"\1", out)
    return out, n


_NEXT_WORD = re.compile(r"\s*,\s*([A-Z][A-Za-z\u00c0-\u024f]+)")
_ROMAN_PAREN = re.compile(r"\b([A-Za-z]+) \(([A-Za-z\u00c0-\u024f\u1e00-\u1eff]+)\)")
_SAME_TWICE = re.compile(r"\b([A-Za-z]+) (?:or|and|/) \1\b")


def _plain(word):
    return "".join(c for c in unicodedata.normalize("NFD", word) if not unicodedata.combining(c)).lower()


_MARK_LABEL = re.compile(r"(?<![A-Za-z])[A-Z][A-Za-z]*(?:[ -][A-Za-z]+){0,2}:\s*" + _MARK + r"\s*,?\s*(?=(?:romani[sz]ed|translit\w*|pinyin|lit\.|literally|IPA)\b)")
# "pl. <run>, madhāhib" reads "pl. madhāhib": after an abbreviation the
# romanisation may be lowercase
_MARK_COMMA = re.compile(r"(\b(?:pl|sing|lit|abbr|orig|trans|cf)\.\s*)?" + _MARK + r"\s*,\s*(?=(\S)?)")


def _mark_comma(m):
    nxt = m.group(2) or ""
    if m.group(1) or nxt.isupper():
        return (m.group(1) or "") + " "
    return m.group(0)
_MARK_COLON = re.compile(r":\s*" + _MARK + r"\s*,\s*")
_MARK_ANY = re.compile(r"\s*" + _MARK + r"\s*")


_MARK_ROMAN = re.compile(r":?\s*" + _MARK + r"\s*,?\s*(romani[sz]ed|romani[sz]ation|translit\w*|pinyin):\s*", re.I)


def _settle_marks(text):
    """Where a run stood: "Ancient Greek: <run>, romanized: X" reads
    "Ancient Greek: X"; "Greek <run>, Arithmoi" drops the comma the run
    left before its romanisation; "Hebrew: <run>, Bemidbar" keeps the
    label."""
    text = _MARK_ROMAN.sub(lambda m: ", " + m.group(1).lower() + ": ", text)
    text = _MARK_LABEL.sub("", text)
    text = _MARK_COMMA.sub(_mark_comma, text)
    text = _MARK_COLON.sub(": ", text)
    text = _MARK_ANY.sub(" ", text)
    return text


def strip_undrawable(text, stats, lead=False):
    """Removes what the serif cannot draw, and the remnants the source left
    behind; tidies only when it removed something, so untouched text stays
    byte-for-byte. In a lead, a parenthetical is cut segment by segment
    (";" separated) so "(Ottoman Turkish: <arabic>; 20 July 1785 - 1 July
    1839)" keeps its dates."""
    run_re, labelled_re = _runs()
    removed = 0

    def drop_span(m):
        nonlocal removed
        whole = m.group(0)
        if not run_re.search(whole) or not _IPA_CHAR.search(whole):
            return whole  # "10 μg/dL (10 μg/100 g)" is units, not a pronunciation
        removed += 1
        stats["spans_removed"] = stats.get("spans_removed", 0) + 1
        return ""

    def paren(m):
        nonlocal removed
        whole = m.group(0)
        if not run_re.search(whole):
            return whole
        removed += 1
        stats["parentheticals_removed"] = stats.get("parentheticals_removed", 0) + 1
        open_at = whole.index("(")
        body = whole[open_at + 1 : -1]
        kept = []
        for seg in body.split(";"):
            seg = seg.strip()
            if not run_re.search(seg):
                if seg:
                    kept.append(seg)
                continue
            # A segment that is more than the run: "from the ... Greek words
            # τοξικός (toxikos), "poisonous"" keeps its words and loses the
            # Greek word. A segment whose runs are single symbols (a schwa in
            # a respelling, an IPA vowel) or that is a pronunciation ("UK:",
            # "pronounced") goes whole, as before.
            if any(len(m.group(0)) < 2 for m in run_re.finditer(seg)) or _PRONUNCIATION_SEG.match(seg):
                continue
            rest = _settle_marks(run_re.sub(_MARK, seg))
            rest = _EMPTY_LABEL_END.sub("", rest).strip(" ,:")
            # "romanized: Theophrastos" is two words and the whole point, and
            # "Moskva" alone is the romanisation of the word that went;
            # "from" alone, or "from or", is what a removal left behind
            words = rest.split()
            label = _LEAD_LABEL.match(rest)
            body_words = rest[label.end() :].split() if label else words
            if lead and _TITLE_WORDS and {w.lower().strip(".,") for w in body_words[: len(_TITLE_WORDS)]} == _TITLE_WORDS:
                # "(Japanese: Kitao Masaru, born ...)": the title's own words reordered
                words = " ".join(body_words[len(_TITLE_WORDS) :]).lstrip(", ").split()
                rest = " ".join(words)
                if not words:
                    continue
            if words and (len(words) >= 2 or words[0][:1].isupper()) and not all(w.lower().strip(".,") in _FUNCTION_WORDS for w in words):
                kept.append(re.sub(r"  +", " ", rest))
        if not kept:
            return ""
        return whole[:open_at] + "(" + "; ".join(kept) + ")"

    if run_re.search(text):
        # A pronunciation, slashed or bracketed, goes whole and first: spelling
        # its theta or folding its schwa would leave half of it behind.
        text = _SLASHED.sub(drop_span, text)
        text = _BRACKETED.sub(drop_span, text)
    text, n = symbols.translate(text)
    if n:
        stats["symbols_translated"] = stats.get("symbols_translated", 0) + n
    text, folded = _fold_chars(text)
    if "," in text:
        # a comma between spelled or folded words gets its space: "(-infinity,infinity)",
        # "clan,personal" (a full-width comma folded); "epsilon,gamma-carotene" stays
        text = _COMMA_WORDS.sub(_comma_words, text)
    for k, n in folded.items():
        stats[k] = stats.get(k, 0) + n
    text, n = _fold_lookalikes(text)
    if n:
        stats["letters_lookalike"] = stats.get("letters_lookalike", 0) + n
    if not text:
        return text
    if run_re.search(text):
        text, n = _romanize_runs(text, run_re)
        if n:
            stats["runs_romanized"] = stats.get("runs_romanized", 0) + n
    if run_re.search(text):
        # A census of what goes, so the report can say which characters the
        # pack loses most and the symbol table can grow from evidence.
        census = stats.setdefault("removed_chars", {})
        for m in run_re.finditer(text):
            for ch in m.group(0):
                # the quotes and commas a run carries are its own, not a loss
                if not ch.isspace() and ord(ch) >= 128 and ch not in _RUN_QUOTES:
                    census[ch] = census.get(ch, 0) + 1
        if lead:
            for _ in range(3):
                new = _PAREN.sub(paren, text)
                if new == text:
                    break
                text = new
        if run_re.search(text):
            text, n = labelled_re.subn(_MARK, text)
            removed += n
            stats["runs_removed"] = stats.get("runs_removed", 0) + n
    if _MARK in text:
        text = _settle_marks(text)
    for rx in (_EMPTY_PRON, _EMPTY_LABEL, _EMPTY_PAREN):
        text, n = rx.subn("", text)
        if n:
            removed += n
            stats["remnants_removed"] = stats.get("remnants_removed", 0) + n
    text, n = _close_quote_gaps(text)
    if n:
        removed += 1
        stats["quote_gaps_closed"] = stats.get("quote_gaps_closed", 0) + n
    text, n = _close_inline_gaps(text)
    if "," in text:
        text = _COMMA_WORDS.sub(_comma_words, text)  # a comma a removed run left glued
    if n:
        removed += 1
        stats["inline_gaps_closed"] = stats.get("inline_gaps_closed", 0) + n
    if removed:
        for rx, rep in _TIDY:
            text = rx.sub(rep, text)
        text = text.strip()
    text, n = scrub_artifacts(text)
    if n:
        stats["artifacts_scrubbed"] = stats.get("artifacts_scrubbed", 0) + n
    return text


# Mario, 2026-09-11: "make sure at the end no weird symbols like consecutive
# parenthesis or stuff that would read as an artifact remain". Whatever
# left them, the source or a removal here, they go: an empty pair of
# quotes or brackets, a doubled comma, a space before a comma, a
# parenthesis with nothing to match, "((x))" around one thing. Chemistry's
# "((1R,4R)-bornan-2-one)" keeps its nesting because the inner pair does
# not close the outer one.
_SCRUB = (
    (re.compile(r"\s?(?:\"\s*\"|\u201c\s*\u201d|''|\u2018\s*\u2019)(?=\s|[,.;:)]|$)"), ""),
    (re.compile(r"\s?[(\[{]\s*[,;:\s]*[)\]}]"), ""),
    (re.compile(r"\(\(([^()]*)\)\)"), r"(\1)"),
    (re.compile(r"\[\[([^\[\]]*)\]\]"), r"[\1]"),
    (re.compile(r"([,;])\s*(?:[,;]\s*)+"), r"\1 "),
    (re.compile(r"(?<=\S)\s+([,;](?=\s|$))"), r"\1"),
    (re.compile(r"\(\s+"), "("),
    (re.compile(r"\s+\)"), ")"),
    (re.compile(r"^\s*(?:[,;]|:(?=\s|$))\s*"), ""),
    (re.compile(r"\s*[,;]$"), ""),
    (re.compile(r"[ \t\u00a0]{2,}"), " "),
)
_SCRUB_HINT = re.compile(r"[()\[\]{}\"\u201c\u2018',;]|^\s*:")


def scrub_artifacts(text):
    """Returns (text, n) with the marks above removed and every parenthesis
    that closes nothing, or opens nothing, dropped."""
    if not text or not _SCRUB_HINT.search(text):
        return text, 0
    n = 0
    for _ in range(3):
        k_all = 0
        for rx, rep in _SCRUB:
            text, k = rx.subn(rep, text)
            k_all += k
        n += k_all
        if not k_all:
            break
    if text.count("(") != text.count(")"):
        text, k = _balance(text, "(", ")")
        n += k
    if text.count("[") != text.count("]"):
        text, k = _balance(text, "[", "]")
        n += k
    if n:
        for rx, rep in _SCRUB:
            text, k = rx.subn(rep, text)  # what a dropped mark left: (as in " ")
            n += k
    return text.strip(), n


def _is_face(text, i):
    """text[i] is the mouth of ":)" or ";-(": an emoticon, not a parenthesis.
    The eyes stand at a word boundary; "8)" is "(number 8)" and "=)" is "(P, <=)"
    far more often than a face."""
    j = i - 1
    if j >= 0 and text[j] == "-":
        j -= 1
    if j < 0 or text[j] not in ":;":
        return False
    return j == 0 or text[j - 1] in " \t\"'\u201c\u2018"


def _balance(text, opener, closer):
    """Drops closers with no opener before them and openers never closed."""
    out = []
    stack = []
    drop = set()
    for i, ch in enumerate(text):
        if ch in "()" and _is_face(text, i):
            continue  # ":)" and ":-(" are faces, not parentheses
        if ch == opener:
            stack.append(i)
        elif ch == closer:
            if stack:
                stack.pop()
            else:
                drop.add(i)
    drop.update(stack)
    if not drop:
        return text, 0
    for i, ch in enumerate(text):
        if i in drop:
            # a space beside the dropped mark goes with it
            if out and out[-1] == " " and i + 1 < len(text) and text[i + 1] in " ,.;":
                out.pop()
            continue
        out.append(ch)
    text = "".join(out)
    text = re.sub(r"[ \t]{2,}", " ", text)
    text = re.sub(r"\s+([,.;:])", r"\1", text)
    return text, len(drop)


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


# Infobox values arrive with their line breaks already collapsed to spaces:
# "13 June 1645 (aged 60-61) Higo Province". The age is computed against the
# snapshot and wrong from the next day; the place gets its comma back after
# a date, and one item after another gets one after its parenthesis.
_AGE = re.compile(r"\s*\(aged?\s+\d+(?:\s*[\u2013-]\s*\d+)?\)")
# "Preferred IUPAC name Methyl methanesulfonate": the chembox's sub-label
_CHEM_SUBLABEL = re.compile(r"^((?:Preferred |Systematic )?IUPAC name|Other names?|Chemical formula)\s+(?=\S)")
# "Length: 61: 49": a running time the dump spaced
_TIME_FIELD = re.compile(r"^(?:Length|Duration|Running time|Time|Runtime)$", re.I)
_TIME_GAP = re.compile(r"\b(\d{1,2}): (\d{2})\b")
_YEAR_TWICE = re.compile(r"\b(\d{4}) \(\1\)")
_DECIMAL_COORDS = re.compile(r"\s*/\s*[\d.\u2212-]+\u00b0[NS]\s*[\d.\u2212-]+\u00b0[EW]")
_COORDS_AFTER_TEXT = re.compile(r"([a-z]) (?=\d{1,3}\u00b0\d)")  # "Pakistan 25°24′N", not the N of a pair
# "Coordinates: 41°37′N 44°00′E" as a nameless value: the label is the name
_LABELLED_VALUE = re.compile(r"^([A-Z][a-z]+(?: [a-z]+){0,2}): (\S.*)$")
# "team Former teams": a word of the row above leaked into the name
_LEAKED_WORD = re.compile(r"^[a-z]+ (?=[A-Z][a-z]+)")
_WEBSITE_VALUE = re.compile(r"^(?:official )?(?:web ?site|site|homepage|home page)$", re.I)
_NAME_THEN_DATE = re.compile(
    r"([A-Za-z]+)\s+(?=(?:\d{1,2} [A-Z][a-z]+ \d{4}|[A-Z][a-z]+ \d{1,2}, \d{4})\b)"
)
_DATE_LEAD_WORDS = frozenset(
    "born died c ca circa on in since until from to before after about by at the of and or between as".split()
)


def _name_then_date(m):
    word = m.group(1)
    if word.lower() in _DATE_LEAD_WORDS or not word[:1].isupper():
        return m.group(0)
    return word + ", "


_DATE_THEN_PLACE = re.compile(
    r"(\b(?:\d{1,2} [A-Z][a-z]+ \d{4}|[A-Z][a-z]+ \d{1,2}, \d{4}|\d{4}))\s+(?=[A-Z])"
)
# "(aged 45) Chicago" reads "(aged 45), Chicago"; "(Barfod) A.J.Hend.", a
# botanical authority, keeps its shape: only a parenthesis that ends in a
# digit is a date's
_PAREN_THEN_ITEM = re.compile(r"\(([^()]*)\)\s+(?=([A-Z][A-Za-z.]*))")


def _paren_then_item(m):
    inner, nxt = m.group(1), m.group(2)
    # "(Barfod) A.J.Hend.": a capitalised name in the parenthesis followed by
    # an initial is a botanical authority and keeps its shape
    if inner[:1].isupper() and " " not in inner and re.match(r"[A-Z]\.", nxt):
        return m.group(0)
    if _COMPASS.match(nxt):
        return m.group(0)  # "(118 mi) W of Sydney"
    return "(" + inner + "), "
_DATE_KEYS = frozenset(("born", "died", "birth date", "death date"))
_MONTH_WORDS = frozenset(m.lower() for m in ("January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December", "Jan", "Feb", "Mar", "Apr", "Jun", "Jul", "Aug", "Sep", "Sept", "Oct", "Nov", "Dec"))


def _word_then_year(m):
    w = m.group(1)
    if w.lower() in _MONTH_WORDS or w.lower() in _DATE_LEAD_WORDS or not w[:1].isupper():
        return m.group(0)
    return w + ", "


_NAME_GROUP = re.compile(r"\bnames?$", re.I)  # "Korean name", "Chinese name": every row carries it


_LIST_ROWS = re.compile(
    r"characters|members|names|children|relatives|works|genres|labels|occupations|known for|fields|institutions|"
    r"awards|influences|languages|groups|religions|subdivisions|ideas|notable|alumni|students|advisors|parties|"
    r"predecessor|successor|founders|owners|products|services|divisions|subsidiaries|partners|spouses|parents",
    re.I,
)


def _split_at_links(value, links, name=""):
    """"Atossa Messenger Ghost of Darius Xerxes" with links for Atossa, Ghost
    of Darius and Xerxes is three items the dump glued; where one link's
    text starts right after another ends, a "; " goes between them. Two
    adjacent links in a row that is not a list ("Tortricoidea Latreille,
    1803", a taxon and its authority) stay as they are."""
    if not links or len(links) < 2:
        return value
    if _TAXON_RANK.match((name or "").strip()):
        return value  # "Helonias L.": a genus and its authority
    sep = ", " if not name or _ADDRESS_FIELD.search(name) else "; "
    texts = [lk.get("text") for lk in links if isinstance(lk, dict) and isinstance(lk.get("text"), str) and lk.get("text")]
    if len(texts) >= 2 and value.strip() == " ".join(texts) and all(t[:1].isupper() or t[:1].isdigit() for t in texts):
        return sep.join(texts)  # "Mark Waid Alex Ross": the links are the whole value
    if len(links) < 3 and not _LIST_ROWS.search(name or ""):
        return value
    if len(texts) < 2:
        return value
    out = value
    pos = 0
    prev_end = None
    for t in texts:
        i = out.find(t, pos)
        if i < 0:
            continue
        if prev_end is not None and out[prev_end:i].strip() == "" and i - prev_end <= 1:
            out = out[:prev_end] + sep + out[i:]
            i = prev_end + len(sep)
        prev_end = i + len(t)
        pos = prev_end
    return out


_TAXON_RANK = re.compile(r"^(?:Genus|Species|Family|Order|Class|Kingdom|Phylum|Division|Tribe|Subfamily|Subgenus|Variety|Subspecies|Binomial name|Trinomial name|Synonyms|Authority)$", re.I)
_ADDRESS_FIELD = re.compile(r"location|address|headquarters|residence|place|origin|coordinates", re.I)
_COMPASS = re.compile(r"(?:N|S|E|W|NE|NW|SE|SW|NNE|ENE|ESE|SSE|SSW|WSW|WNW|NNW)$")
_ISO_DATE = re.compile(r"^(\d{4})-(\d{2})-(\d{2})$")
_MONTHS = ("", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December")


def _spell_iso_date(s):
    """A cell or value that is exactly a machine date reads "15 October 2014"."""
    m = _ISO_DATE.match(s.strip())
    if not m or not 1 <= int(m.group(2)) <= 12 or not 1 <= int(m.group(3)) <= 31:
        return s
    return "%d %s %s" % (int(m.group(3)), _MONTHS[int(m.group(2))], m.group(1))


_FACT_SKIP_NAMES = frozenset(("Imperial conversion", "Metric conversion", "NFPA 704 (fire diamond)", "NFPA 704"))
_FACT_JUNK_VALUE = re.compile(r"^[\W_]*$|^\* ")
_NAMELESS_FACT = re.compile(r"\d|^In office|^Term|^Reign")
_FOOTNOTE_VALUE = re.compile(r"^\d [A-Z][a-z]+ [a-z]")  # "1 Playing statistics correct to ..."
# "C 20 H 8 Br 2" in a formula row: the subscripts the dump spaced out
_FORMULA_ROW = re.compile(r"formula", re.I)
_ELEMENT_COUNT = re.compile(r"(?<=[A-Za-z\)\]]) (\d{1,3})(?=[A-Z(\[\s]|$)")
_FORMULA_GAP = re.compile(r"(?<=[A-Za-z\u2080-\u2089)\]]) (?=[A-Z(\[])")
# "g·mol −1", "m s −2": a unit's exponent
_UNIT_EXPONENT = re.compile(r"(?<=[a-zA-Z]) ([\u2212-]?\d)(?=\b)(?![\u00b0\u2032\u2033\d])")
_UNIT_BEFORE = re.compile(r"^(?:mol|kg|g|m|cm|mm|km|s|K|J|Hz|Pa|N|V|A|W|C|L|dm|cd|sr|rad|h|min|yr|Bq|Gy|Sv|T|H|F|S|Wb|lm|lx)$")
_ELEMENT_BEFORE = re.compile(r"(?:^|[\s(])([A-Z][a-z]?)$")
# "Zn 2+", "S 2−": an ion's charge
_ION = re.compile(r"(?<=[A-Za-z]) (\d?[+\u2212-])(?=[\s),]|$)")
_DEGREE_GAP = re.compile(r"(?<=\d) \u00b0")
_SUPER = str.maketrans("0123456789+-\u2212", "\u2070\u00b9\u00b2\u00b3\u2074\u2075\u2076\u2077\u2078\u2079\u207a\u207b\u207b")
_SUB = str.maketrans("0123456789", "\u2080\u2081\u2082\u2083\u2084\u2085\u2086\u2087\u2088\u2089")
# a climate table's month row; a link's caption with no link to follow
_FACT_SKIP_VALUE = re.compile(r"^(?:[JFMASOND] ){11}[JFMASOND]$|^(?:(?:Listen live|Public file(?:; LMS)?|LMS|Website|Official website)(?: \([^()]*\))?(?:[,;] )?)+$", re.I)
_DEAD_CELL = re.compile(r"^(?:Report|Match report|Highlights|Video|Stats|Box score)$", re.I)
_NAME_FOOTNOTE = re.compile(r"(?<=[A-Za-z)]) \d$")
_SLASH_GAP = re.compile(r"(?<=\S) /(?=[A-Za-z0-9])(?!\d{4}\b)")  # not "1564 /1563", a year either way
_FACT_LABELS = frozenset(("Preceded by", "Succeeded by", "In office"))
_GENERIC_FIELDS = frozenset((
    "total", "rank", "density", "land", "water", "urban", "metro", "estimate", "census", "preceded by",
    "succeeded by", "in office", "term", "chancellor", "vice-chancellor", "president", "prime minister",
    "monarch", "governor", "deputy", "leader", "members", "seats",
))
_PRONUNCIATION_FIELD = re.compile(r"pronunciation|\bIPA\b|pronounced", re.I)
# "26 May 1564: 90 /1563": the page number of a citation the source stripped
_YEAR_PAGE = re.compile(r"(?<=\d{4}):\s?\d{1,4}\b(?=\s*[/,;.]|\s+[A-Z(]|$)")


def _ion_charge(m):
    head = m.string[: m.start()]
    if _ELEMENT_BEFORE.search(head):
        return m.group(1).translate(_SUPER)
    return m.group(0)


def _unit_exponent(m):
    head = m.string[: m.start()]
    unit = re.search(r"(?:^|(?<=[\s(\u00b7/]))([A-Za-z]+)$", head)  # a unit stands alone: "5 m 2", "g\u00b7mol -1"; not "50s 0"
    if unit and _UNIT_BEFORE.search(unit.group(1)) and not m.string.startswith("/", m.end()):
        return m.group(1).translate(_SUPER)
    return m.group(0)


_WRAPPED = re.compile(r"^\(([^()]+)\)$")
_SHELL = re.compile(r"\b(\d[spdf]) (\d{1,2})\b")


def fact_value(name, value):
    if not value:
        return value
    value = _AGE.sub("", value)
    value = _WRAPPED.sub(r"\1", value.strip())
    value = _CHEM_SUBLABEL.sub(r"\1: ", value)
    if _TIME_FIELD.search(name):
        value = _TIME_GAP.sub(r"\1:\2", value)
    if "configuration" in name.lower() or "shell" in name.lower():
        value = _SHELL.sub(lambda m: m.group(1) + m.group(2).translate(_SUPER), value)
    value = _YEAR_PAGE.sub("", value)
    value = _NAME_THEN_DATE.sub(_name_then_date, value)
    value = _YEAR_TWICE.sub(r"\1", value)
    value = _SLASH_GAP.sub(" / ", value)
    value = _spell_iso_date(value)
    if value.startswith("-> "):
        value = "to " + value[3:]  # a loan arrow at the front of a cell
    value = re.sub(r"\b([A-Z]):(?=\d)", r"\1: ", value)
    if "\u00b0" in value:
        value = _DECIMAL_COORDS.sub("", value)
        value = _COORDS_AFTER_TEXT.sub(r"\1; ", value)
    value = re.sub(r"(\d{4}) /(\d{4})\b", r"\1/\2", value)
    if _FORMULA_ROW.search(name):
        value = _ELEMENT_COUNT.sub(lambda m: m.group(1).translate(_SUB), value)
        value = _FORMULA_GAP.sub("", value)
    value = _ION.sub(_ion_charge, value)
    value = _UNIT_EXPONENT.sub(_unit_exponent, value)
    value = _DEGREE_GAP.sub("\u00b0", value)
    if name.lower() in _DATE_KEYS:
        value = re.sub(r"\b([A-Za-z]+) (?=(?:c\. )?\d{4}\b)", _word_then_year, value)  # "Kirkpatrick III 1951"
        value = _DATE_THEN_PLACE.sub(r"\1, ", value)
    value = _PAREN_THEN_ITEM.sub(_paren_then_item, value)
    return value.strip()


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
            if not t.strip():
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
        return _scrub_inline(self._emit(text, 0, len(text), links))

    @staticmethod
    def _emit(text, lo, hi, links):
        out = []
        i = lo
        for s, e, href in links:
            if s < lo or e > hi:
                continue
            # the dump's link text can carry the space that stood beside a
            # lost icon ("China "): the space stays outside the anchor
            inner = text[s:e]
            if not inner.strip():
                out.append(esc(text[i:e]))
                i = e
                continue
            lead = inner[: len(inner) - len(inner.lstrip())]
            trail = inner[len(inner.rstrip()) :]
            out.append(esc(text[i:s] + lead))
            out.append('<a href="' + esc_attr(href) + '">' + esc(inner.strip()) + "</a>")
            out.append(esc(trail))
            i = e
        out.append(esc(text[i:hi]))
        return "".join(out)

    def plain(self, s):
        return esc(strip_undrawable(clean_text(s), self.stats))


    # --- blocks ------------------------------------------------------------
    def paragraph(self, part, lead=False):
        raw = part.get("value") if isinstance(part, dict) else None
        if isinstance(raw, str) and _HATNOTE.match(raw.lstrip()):
            self.stats["hatnotes_dropped"] = self.stats.get("hatnotes_dropped", 0) + 1
            return
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
            elif html.startswith("<table>"):
                self.stats["tables_kept"] = self.stats.get("tables_kept", 0) + 1
            if html:
                self.out.append(html)

    def table_html(self, tb):
        rows = [(True, r) for r in (tb.get("headers") or []) if isinstance(r, list)]
        rows = rows[-1:]  # of stacked header rows, the lowest names the columns
        rows += [(False, r) for r in (tb.get("rows") or []) if isinstance(r, list)]
        rows = [(h, r) for h, r in rows if r]
        if not rows:
            return ""
        grid = []
        run_re, _ = _runs()
        filled = with_runs = 0
        for is_header, r in rows:
            cells = []
            for c in r:
                v = c.get("value") if isinstance(c, dict) else c
                raw = clean_text(v if isinstance(v, str) else "")
                if raw.strip():
                    filled += 1
                    if run_re.search(raw):
                        with_runs += 1
                cells.append(_spell_iso_date(re.sub(r"\s#$", "", strip_undrawable(raw, self.stats))))
            if is_header and len(cells) > 1 and len(set(cells)) == 1:
                continue  # a caption spanning the row ("Key (expand for notes)"), not column names
            grid.append((is_header, cells))
        if filled and with_runs * 2 >= filled:
            # a phoneme chart, a table of native names: without its script it
            # is a grid of holes, so the notice is the honest rendering
            self.stats["script_tables_omitted"] = self.stats.get("script_tables_omitted", 0) + 1
            return TABLE_OMITTED
        if not grid:
            return ""  # a caption and nothing under it
        # A navbox is a table of links to other pages with its own controls in
        # it; on this device it is three columns of "This box: view talk edit".
        # It is navigation, not content, so it leaves no notice behind.
        for _, cells in grid:
            for c in cells:
                if _NAVBOX.search(c):
                    self.stats["navboxes_dropped"] = self.stats.get("navboxes_dropped", 0) + 1
                    return ""
        wide = max(len(c) for _, c in grid) > TABLE_MAX_COLS or any(
            len(c.split(" ")) > TABLE_CELL_WORDS or len(c.encode("utf-8")) > TABLE_CELL_BYTES
            for _, cells in grid
            for c in cells
        )
        if wide:
            return self.table_rows(grid)
        out = ["<table>"]
        last = None
        for is_header, cells in grid:
            tag = "th" if is_header else "td"
            cells = ["" if _DEAD_CELL.match(c.strip()) else c for c in cells]  # "Report": a link's caption
            if all(not c.strip() or c.rstrip().endswith(":") for c in cells):
                continue  # "Source:" with nothing after it
            if cells == last:
                continue  # the same row twice ("Source: INSEE")
            last = cells
            if not is_header:
                cells = [c for j, c in enumerate(cells) if not (j and c == cells[j - 1])]  # a spanning cell, once
            out.append(
                "<tr>"
                + "".join("<%s>%s</%s>" % (tag, esc("Number" if c.strip() == "#" else c), tag) for c in cells)
                + "</tr>"
            )
        if len(out) == 1:
            return ""  # every row was a label with nothing after it
        out.append("</table>")
        return "".join(out)

    def table_rows(self, grid):
        """A table too wide for the panel's grid, as one paragraph per row:
        the first cell in bold, every other cell labelled by its column
        header, empty cells and reference columns left out. A reader gets
        "1984; Category: Best Comedy Recording; Work: Eat It; Result: Won"
        instead of a notice that a table stood here."""
        headers = [cells for is_header, cells in grid if is_header]
        labels = ["Number" if c.strip() == "#" else c for c in (headers[-1] if headers else [])]
        body = [cells for is_header, cells in grid if not is_header]
        if not body:
            return ""
        out = []
        for cells in body[:TABLE_ROWS_LISTED]:
            parts = []
            if len(set(cells)) == 1 and cells[0]:
                # a spanning row ("Source: Agencia Estatal de Meteorología")
                # arrives as the same text in every column: say it once
                cells = cells[:1]
            for j, c in enumerate(cells):
                if j and c == cells[j - 1]:
                    continue  # a spanning cell ("did not advance"), once per column it covered
                c = cut_words(c.strip(), TABLE_ROW_CELL_WORDS)
                c = re.sub(r"\s#$", "", c)  # a footnote marker
                if len(c.split(" ")) >= TABLE_ROW_CELL_WORDS:
                    c, _ = scrub_artifacts(c)  # a cut can leave a parenthesis open
                c = re.sub(r"  +", " ", c).strip()
                if c == "#":
                    c = "Number"
                if not parts and re.match(r"^\d+\.$", c):
                    c = c[:-1]  # "1." then "; Date: ..." read as "1.;"
                c = _spell_iso_date(c)
                if c.startswith("-> "):
                    c = "to " + c[3:]
                if not c or c.endswith(":"):
                    continue  # empty, or a label whose script went
                label = labels[j] if j < len(labels) else ""
                if _REF_COLUMN.match(label):
                    continue
                if not parts:
                    parts.append("<b>%s</b>" % esc(c))
                elif label and label != c:
                    parts.append("%s: %s" % (esc(label), esc(c)))
                else:
                    parts.append(esc(c))
            if parts:
                para = "<p>" + re.sub(r"  +", " ", "; ".join(parts)) + "</p>"
                if out and out[-1] == para:
                    continue  # "Source: INSEE" once per table, not per row
                out.append(para)
        if len(body) > TABLE_ROWS_LISTED:
            out.append("<p><i>(%d more rows)</i></p>" % (len(body) - TABLE_ROWS_LISTED))
        if out:
            self.stats["tables_listed"] = self.stats.get("tables_listed", 0) + 1
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
                # Render the body first: a section whose parts were all
                # images, navboxes or empty subsections has no heading.
                body = self.capture(p.get("has_parts"), depth + 1)
                body, n = _COLON_AT_END.subn(_colon_alone, body)
                if n:
                    self.stats["list_intros_dropped"] = self.stats.get("list_intros_dropped", 0) + n
                if not body:
                    self.stats["empty_sections_dropped"] = self.stats.get("empty_sections_dropped", 0) + 1
                    continue
                self.heading(name, depth)
                self.out.append(body)
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
        has_image = {}
        groups = {}  # a field's group, to tell "Literal meaning" under Korean from under Japanese

        def add(name, value, group=""):
            if name in _FACT_SKIP_NAMES or _FACT_SKIP_VALUE.match(value or ""):
                return
            if _PRONUNCIATION_FIELD.search(name):
                self.stats["pronunciation_facts_dropped"] = self.stats.get("pronunciation_facts_dropped", 0) + 1
                return
            name = strip_undrawable(clean_text(name), self.stats)
            name = _NAME_DISAMBIG.sub("", name)
            name = _SLASH_GAP.sub(" / ", _NAME_FOOTNOTE.sub("", name))
            value = cut_words(
                fact_value(name, strip_undrawable(clean_text(value), self.stats)),
                FACT_WORDS,
            )
            if "coordinates" in name.lower() and " / " in value:
                value = value.split(" / ")[0].strip()
            if value in _FACT_LABELS or value in (self.title, self.title.split(",")[0].strip(), base):
                return  # "Preceded by: Succeeded by": both values were flags; "Azerbaijani: <title>"
            name = name[:1].upper() + name[1:]
            if _FACT_JUNK_VALUE.match(value) or sum(1 for c in value if c.isalpha()) < 2 and not any(c.isdigit() for c in value):
                return  # "* R ij’ kr -s": a reconstruction whose marks all went
            if name in _FACT_SKIP_NAMES:
                return
            if name and value and value != name and (name, value) not in seen:
                seen.add((name, value))
                fields.append((name, value, group))
                groups.setdefault(name, set()).add(group)

        base = re.sub(r"\s*\([^()]*\)\s*$", "", self.title)
        title_words = {w.lower() for w in base.split()}
        recent = []  # named fields seen so far, for a generic one to hang on

        def walk(p, group=""):
            if isinstance(p, list):
                for c in p:
                    walk(c, group)
                return
            if not isinstance(p, dict):
                return
            t = p.get("type")
            if t == "section":
                # "President of Austria", "Area", "Population": the group a
                # field belongs to, which the flat grid would otherwise lose
                name = _NAME_FOOTNOTE.sub("", clean_text(str(p.get("name") or "")))
                if name and any(isinstance(c, dict) and c.get("type") == "image" for c in p.get("has_parts") or []):
                    has_image[name] = True
                # a group is a short label; a "section" whose name is a whole
                # medal table is the table, not a group
                if name and name != self.title and (not base or base not in name) and not (len(title_words) >= 2 and title_words <= {w.lower().strip(",") for w in name.split()}) and len(name.split()) <= 12 and not name.lower().startswith("infobox"):
                    # "Transcriptions" under "Chinese name" keeps the group
                    # that says which language the rows belong to
                    if not (group and _NAME_GROUP.search(group)):
                        group = name
            if t == "field" and isinstance(p.get("value"), str):
                fname = p.get("name")
                if p.get("images"):
                    return  # the field is an image and its value is the caption
                if fname:
                    fname = _LEAKED_WORD.sub("", fname)
                value = _split_at_links(p["value"], p.get("links"), fname or "")
                if fname and _WEBSITE_VALUE.match(fname.strip()) and _WEBSITE_VALUE.match(value.strip()):
                    # "Official website" is a link's text; the reader has no link, so the address
                    value = _website_host(p.get("links")) or value
                if not fname:
                    labelled = _LABELLED_VALUE.match(value)
                    if labelled:
                        add(labelled.group(1), labelled.group(2), group)
                        return
                if group and _NAME_GROUP.search(group) and fname:
                    add(group + ", " + fname.strip(), value, group)
                    return
                if not fname:
                    # "In office 1945 - 1950" under its office; a caption in a
                    # section that holds an image is the image's, not a fact
                    if _FOOTNOTE_VALUE.match(value):
                        return
                    if group and (not has_image.get(group) or any(c.isdigit() for c in value)) and _NAMELESS_FACT.search(value):
                        add(group, value)
                elif group and fname.strip().lower() in _GENERIC_FIELDS:
                    key = fname.strip().lower()
                    g = group
                    if key in ("density", "total", "estimate", "census", "urban", "metro", "land", "water"):
                        for prev in reversed(recent):
                            if prev.lower().startswith(("population", "pop.", "area")):
                                g = _NAME_FOOTNOTE.sub("", prev)
                                break
                    add(g if key == "total" and g.lower().startswith(("population", "pop.")) else g + ", " + key, value)
                else:
                    recent.append(fname.strip())
                    add(fname, value, group)
            elif t == "list" and p.get("name"):
                items = [
                    _item_text(it)
                    for it in p.get("has_parts") or []
                    if isinstance(it, dict) and isinstance(it.get("value"), str)
                ]
                items = [i for i in items if i]
                if items:
                    add(p["name"], "; ".join(items))
                return
            for c in p.get("has_parts") or []:
                walk(c, group)

        walk(infoboxes)
        if not fields:
            return
        # the same name under two groups ("Literal meaning" under the Korean
        # and the Japanese name) carries its group
        dup = {name for name, gs in groups.items() if len(gs) > 1}
        fields = [
            (group + ", " + name if name in dup and group else name, value)
            for name, value, group in fields
        ]
        merged = []
        for name, value in fields:
            if merged and merged[-1][0] == name:
                merged[-1] = (name, cut_words(merged[-1][1] + "; " + value, FACT_WORDS))
            else:
                merged.append((name, value))
        fields = merged
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


def _website_host(links):
    for lk in links or []:
        url = lk.get("url") if isinstance(lk, dict) else None
        if isinstance(url, str) and "://" in url:
            host = url.split("://", 1)[1].split("/", 1)[0]
            return host[4:] if host.startswith("www.") else host
    return ""


def _item_text(it):
    """A list item's text; a definition term carries its definitions
    ("Gold 0"), which the dump nests under it."""
    v = clean_text(it.get("value") or "")
    if it.get("type") == "definition_term":
        defs = [clean_text(d.get("value") or "") for d in it.get("has_parts") or [] if isinstance(d, dict)]
        defs = [d for d in defs if d]
        if defs:
            v = v + " " + ", ".join(defs)
    return v


# "Wolfgang Amadeus Mozart" is found by "mozart" only through an index entry
# that starts with the surname. The pack has no redirect list, so the builder
# makes the one every printed index has: "Mozart, Wolfgang Amadeus".
_NAME_WORD = re.compile(r"^[A-Z\u00c0-\u024f][A-Za-z\u00c0-\u024f'\u2019.-]*$")
_PARTICLES = frozenset(
    ("van", "von", "de", "da", "del", "della", "der", "di", "du", "la", "le",
     "of", "the", "al", "bin", "ibn", "y", "e", "af", "zu", "ter", "ten", "den")
)
_ROMAN = re.compile(r"^[IVXLC]+$")
_PERSON_FIELDS = frozenset(("born", "died", "birth name", "birth date", "date of birth"))


_LINK = re.compile(rb'<a href="([^"]*)">(.*?)</a>', re.S)


def strip_unknown_links(xhtml, known, stats=None):
    """Links whose target is not in the pack become their own text.

    A pack is a subset of Wikipedia, so most of an article's links point at
    pages it does not carry; underlined, they read as a promise the reader
    cannot keep (every one of them ended in a NOT FOUND notice on the panel).
    `known` is every title the pack answers to: its articles and its
    redirects. Called once all of them are known, so it is the pack, not the
    row, that decides. Bytes in, bytes out: the article body is already
    encoded by then.
    """

    def keep(m):
        target = html.unescape(m.group(1).decode("utf-8"))
        if target in known:
            if stats is not None:
                stats["links_in_pack"] = stats.get("links_in_pack", 0) + 1
            return m.group(0)
        if stats is not None:
            stats["links_outside_pack"] = stats.get("links_outside_pack", 0) + 1
        return m.group(2)

    return _LINK.sub(keep, xhtml)


def person_alias(row):
    """"Surname, Given names" for a row whose infobox says it is a person
    (a Born or Died field) and whose title is a plain two- to four-word
    name; None otherwise."""
    title = clean_text(row.get("name") or "")
    words = title.split(" ")
    if not 2 <= len(words) <= 4:
        return None
    if any(ch in title for ch in "(),0123456789:/"):
        return None
    last = words[-1]
    if not _NAME_WORD.match(last) or len(last) < 3 or _ROMAN.match(last):
        return None
    if last.rstrip(".").lower() in ("jr", "sr"):
        return None
    if not _NAME_WORD.match(words[0]):
        return None
    for w in words[1:-1]:
        if not (_NAME_WORD.match(w) or w.lower() in _PARTICLES):
            return None
    found = False

    def walk(p):
        nonlocal found
        if found:
            return
        if isinstance(p, list):
            for c in p:
                walk(c)
        elif isinstance(p, dict):
            name = p.get("name")
            if p.get("type") in ("field", "list") and isinstance(name, str):
                if name.strip().lower() in _PERSON_FIELDS:
                    found = True
                    return
            for c in p.get("has_parts") or []:
                walk(c)

    walk(_parse(row.get("infoboxes")))
    if not found:
        return None
    return last + ", " + " ".join(words[:-1])


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



_INLINE_DOUBLE_SPACE = re.compile(r"(?<=\S)[ \u00a0]{2,}(?=\S)")
# the label may sit in a link ("Chinese</a>: ,"); the closing tag stays.
# The piece ends at a block's closing tag, never at the link's own ("Vizing's
# Theorem:</a> A graph" is a label with its content after it)
_INLINE_EMPTY_LABEL = re.compile(
    r"(?<![A-Za-z0-9>\"'\u201c\u2018])[A-Z][A-Za-z.]*(?:[ -][A-Za-z.]+){0,3}(</a>)?:\s*(</a>)?\s*(?=[;,)]|</(?:p|li|td|th|dd|dt|h[1-6])>)"
)
_EMPTY_ANCHOR = re.compile(r'<a href="[^"]*">\s*</a>')
_INLINE_TIDY = (
    (re.compile(r"\(\s*[;,]\s*"), "("),
    (re.compile(r"\s*[;,]\s*\)"), ")"),
    (re.compile(r"\s?\(\s*\)"), ""),
    (re.compile(r"\s+([,;)])"), r"\1"),
)


def _scrub_inline(html_text):
    """The text was stripped in pieces around its links, so a label whose
    content went could not see the ")" after it and a removed character
    left two spaces at a piece boundary. One pass over the assembled line."""
    if "  " in html_text or ":" in html_text:
        html_text = _INLINE_DOUBLE_SPACE.sub(" ", html_text)
        html_text = _INLINE_EMPTY_LABEL.sub(lambda m: (m.group(1) or "") + (m.group(2) or ""), html_text)
        html_text = _EMPTY_ANCHOR.sub("", html_text)
        for rx, rep_ in _INLINE_TIDY:
            html_text = rx.sub(rep_, html_text)
    return html_text


_TITLE_WORDS = frozenset()
_LEAD_LABEL = re.compile(r"[A-Z][A-Za-z -]{0,24}:\s*")


def article_xhtml(row, stats=None):
    """(title, headings, xhtml_bytes). `stats`, if given, is a dict the
    counters (runs_removed, tables_omitted, ...) are added into."""
    global _TITLE_WORDS
    if stats is None:
        stats = {}
    base = re.sub(r"\s*\([^()]*\)\s*$", "", str(row.get("name") or ""))
    _TITLE_WORDS = frozenset(w.lower().strip(".,") for w in base.split()) if len(base.split()) >= 2 else frozenset()
    before = dict(tex_stats)
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

    for p in lead[:2]:
        v = p.get("value") if isinstance(p, dict) else None
        if isinstance(v, str) and v.lstrip().startswith("#REDIRECT"):
            raise ValueError("redirect page")
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
    for k, v in tex_stats.items():  # the formula counters reach both builders' summaries
        if v != before[k]:
            stats[k] = stats.get(k, 0) + v - before[k]
    xhtml, n = _COLON_ALONE.subn(_colon_alone, "".join(doc.out))
    if n:
        stats["list_intros_dropped"] = stats.get("list_intros_dropped", 0) + n
    return doc.title, doc.headings, xhtml.encode("utf-8")


# "Typical fashions in the 1930s:" and then a heading: the gallery it
# introduced was images, and went. A short colon line goes with it; a real
# paragraph that happened to introduce an image ("...is a canon in which
# the right hand is imitated at one beat's distance:") keeps every word and
# ends with a period instead. A disambiguation page's "X may refer to:"
# stays, so the builder can still tell the page for what it is.
_COLON_ALONE = re.compile(r"<p>((?:[^<]|<(?!/p>))*):</p>(?=<h[1-6]|</body>)")
_COLON_AT_END = re.compile(r"<p>((?:[^<]|<(?!/p>))*):</p>$")
COLON_LINE_WORDS = 12


def _colon_alone(m):
    inner = m.group(1)
    if "may refer to" in inner:
        return m.group(0)
    if len(re.sub(r"<[^>]+>", "", inner).split()) <= COLON_LINE_WORDS:
        return ""
    return "<p>" + inner + ".</p>"



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
