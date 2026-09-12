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
_CITE_PAGE = re.compile(r"(?<=[.,;!?])\s?:\s?\d+(?:[\u2013-]\d+)?(?=\s|$)")
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
        run = bad + r"(?:\s*" + bad + ")*"
        _run_re = re.compile(run)
        label = r"(?:(?<![A-Za-z])[A-Z][A-Za-z]*(?:[ -][A-Za-z]+){0,2}:\s?)?"
        _labelled_run_re = re.compile(label + run)
    return _run_re, _labelled_run_re


# Source remnants: the dataset already dropped pronunciation spans and native
# scripts from some leads, leaving "(German:; 6 January 1850" and "Fernandel ()",
# and it pads every quotation with spaces: the " beech ", lit. ' uncle '.
_EMPTY_LABEL = re.compile(
    r"(?<![A-Za-z0-9])[A-Za-z][A-Za-z.]*(?:[ -][A-Za-z.]+){0,3}:\s*(?=[;,)])"
)
# the same at the end of a parenthetical's segment ("from Sanskrit: , IPA:")
_EMPTY_LABEL_END = re.compile(
    r"(?<![A-Za-z0-9])[A-Za-z][A-Za-z.]*(?:[ -][A-Za-z.]+){0,3}:\s*(?=[;,)]|$)"
)
# "(listen)": the audio link's text, with no audio to play
_LISTEN = re.compile(r"\s?\(\s*listen\s*\)", re.I)
_FUNCTION_WORDS = frozenset("from or and of the a an in at by to lit also see cf".split())
_EMPTY_PAREN = re.compile(r"\s?\(\s*\)")
# IPA between slashes or brackets, when the serif cannot draw it.
_SLASHED = re.compile(r" ?/[^/]{1,80}/")
_BRACKETED = re.compile(r" ?\[[^\[\]]{1,80}\]")

_TIDY = (
    (re.compile(r"\(\s*[,;:]\s*"), "("),
    (re.compile(r"\s*[,;:]\s*\)"), ")"),
    (re.compile(r"\(\s*\)"), ""),
    (re.compile(r"\[\s*\]"), ""),
    (re.compile(r"\s+([,;:?)]|!(?!=))"), r"\1"),  # "a != 0" keeps its space
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
_SENTENCE_GLUE = re.compile(r"([a-z]{3,}[.!?])([A-Z][a-z]{2,})")
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
    if "listen" in s:
        s = _LISTEN.sub("", s)
    s = _YEAR_GLUE.sub(r"\1 ", s)
    if not s:
        return ""
    s = _CONTROL.sub("", s)
    if "style" in s and _TEX_OPEN.search(s):
        s = _render_tex(s)
    if "{{" in s:
        s = _TEMPLATE.sub("", s)
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
_COLON_GAP = re.compile(r"(?<=[A-Za-z\u00c0-\u024f]) :(?= [A-Za-z\u00c0-\u024f])")
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
_RESPELL_TOKEN = r"[A-Z]{1,6}(?:-[a-z]{1,8})*-?"
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
_UNIT_POWER = re.compile(r"\b(km|m|cm|mm|mi|ft|yd|in|nmi)\s+([23])\b(?!,\d|\.\d| [a-z])")
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
        # follows; the run goes and that one stays
        after = _NEXT_WORD.match(text, m.end())
        if after and _plain(after.group(1)) == _plain(latin):
            return run
        n += 1
        return latin

    out = run_re.sub(sub, text)
    if n >= 2:
        # "\u1f55\u03b2\u03bf\u03c2 or \u1f51\u03b2\u03cc\u03c2": two accentuations, one spelling
        out = _SAME_TWICE.sub(r"\1", out)
    return out, n


_NEXT_WORD = re.compile(r"\s*,\s*([A-Z][A-Za-z\u00c0-\u024f]+)")
_SAME_TWICE = re.compile(r"\b([A-Za-z]+) (?:or|and|/) \1\b")


def _plain(word):
    return "".join(c for c in unicodedata.normalize("NFD", word) if not unicodedata.combining(c)).lower()


_MARK_LABEL = re.compile(r"(?<![A-Za-z])[A-Z][A-Za-z]*(?:[ -][A-Za-z]+){0,2}:\s*" + _MARK + r"\s*,?\s*(?=(?:romani[sz]ed|translit\w*|pinyin|lit\.|literally|IPA)\b)")
_MARK_COMMA = re.compile(_MARK + r"\s*,\s*(?=[A-Z])")
_MARK_COLON = re.compile(r":\s*" + _MARK + r"\s*,\s*")
_MARK_ANY = re.compile(r"\s*" + _MARK + r"\s*")


_MARK_ROMAN = re.compile(r"\s*" + _MARK + r"\s*,?\s*(?:romani[sz]ed|romani[sz]ation|translit\w*|pinyin):\s*", re.I)


def _settle_marks(text):
    """Where a run stood: "Ancient Greek: <run>, romanized: X" reads
    "Ancient Greek: X"; "Greek <run>, Arithmoi" drops the comma the run
    left before its romanisation; "Hebrew: <run>, Bemidbar" keeps the
    label."""
    text = _MARK_ROMAN.sub(" ", text)
    text = _MARK_LABEL.sub("", text)
    text = _MARK_COMMA.sub(" ", text)
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
        if not run_re.search(whole):
            return whole
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
            # "romanized: Theophrastos" is two words and the whole point;
            # "from" alone, or "from or", is what a removal left behind
            if len(rest.split()) >= 2 and not all(w.lower().strip(".,") in _FUNCTION_WORDS for w in rest.split()):
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
                if not ch.isspace():
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
    for rx in (_EMPTY_LABEL, _EMPTY_PAREN):
        text, n = rx.subn("", text)
        if n:
            removed += n
            stats["remnants_removed"] = stats.get("remnants_removed", 0) + n
    text, n = _close_quote_gaps(text)
    if n:
        removed += 1
        stats["quote_gaps_closed"] = stats.get("quote_gaps_closed", 0) + n
    text, n = _close_inline_gaps(text)
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
    (re.compile(r"\s?[(\[{]\s*[)\]}]"), ""),
    (re.compile(r"\(\(([^()]*)\)\)"), r"(\1)"),
    (re.compile(r"\[\[([^\[\]]*)\]\]"), r"[\1]"),
    (re.compile(r"([,;])\s*(?:[,;]\s*)+"), r"\1 "),
    (re.compile(r"(?<=\S)\s+([,;](?=\s|$))"), r"\1"),
    (re.compile(r"\(\s+"), "("),
    (re.compile(r"\s+\)"), ")"),
    (re.compile(r"^\s*[,;]\s*"), ""),
    (re.compile(r"\s*[,;]$"), ""),
    (re.compile(r"[ \t]{2,}"), " "),
)
_SCRUB_HINT = re.compile(r"[()\[\]{}\"\u201c\u2018',;]")


def scrub_artifacts(text):
    """Returns (text, n) with the marks above removed and every parenthesis
    that closes nothing, or opens nothing, dropped."""
    if not text or not _SCRUB_HINT.search(text):
        return text, 0
    n = 0
    for rx, rep in _SCRUB:
        text, k = rx.subn(rep, text)
        n += k
    if text.count("(") != text.count(")"):
        text, k = _balance(text, "(", ")")
        n += k
    if text.count("[") != text.count("]"):
        text, k = _balance(text, "[", "]")
        n += k
    return text.strip(), n


def _balance(text, opener, closer):
    """Drops closers with no opener before them and openers never closed."""
    out = []
    stack = []
    drop = set()
    for i, ch in enumerate(text):
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
_DATE_THEN_PLACE = re.compile(
    r"(\b(?:\d{1,2} [A-Z][a-z]+ \d{4}|[A-Z][a-z]+ \d{1,2}, \d{4}|\d{4}))\s+(?=[A-Z])"
)
_PAREN_THEN_ITEM = re.compile(r"\)\s+(?=[A-Z])")
_DATE_KEYS = frozenset(("born", "died", "birth date", "death date"))


_FACT_SKIP_NAMES = frozenset(("Imperial conversion", "Metric conversion", "NFPA 704 (fire diamond)", "NFPA 704"))
_FACT_JUNK_VALUE = re.compile(r"^[\W_]*$|^\* ")
_NAMELESS_FACT = re.compile(r"\d|^In office|^Term|^Reign")
# "C 20 H 8 Br 2" in a formula row: the subscripts the dump spaced out
_FORMULA_ROW = re.compile(r"formula", re.I)
_ELEMENT_COUNT = re.compile(r"(?<=[A-Za-z\)\]]) (\d{1,3})(?=[A-Z(\[\s]|$)")
_FORMULA_GAP = re.compile(r"(?<=[A-Za-z\u2080-\u2089)\]]) (?=[A-Z(\[])")
# "g·mol −1", "m s −2": a unit's exponent
_UNIT_EXPONENT = re.compile(r"(?<=[a-zA-Z]) ([\u2212-]?\d)(?=\b)")
_UNIT_BEFORE = re.compile(r"(?:mol|kg|g|m|cm|mm|km|s|K|J|Hz|Pa|N|V|A|W|C|L|dm|cd|sr|rad|h|min|yr|Bq|Gy|Sv|T|H|F|S|Wb|lm|lx)$")
# "Zn 2+", "S 2−": an ion's charge
_ION = re.compile(r"(?<=[A-Za-z]) (\d?[+\u2212-])(?=[\s),]|$)")
_DEGREE_GAP = re.compile(r"(?<=\d) \u00b0")
_SUPER = str.maketrans("0123456789+-\u2212", "\u2070\u00b9\u00b2\u00b3\u2074\u2075\u2076\u2077\u2078\u2079\u207a\u207b\u207b")
_SUB = str.maketrans("0123456789", "\u2080\u2081\u2082\u2083\u2084\u2085\u2086\u2087\u2088\u2089")
_FACT_SKIP_VALUE = re.compile(r"^(?:[JFMASOND] ){11}[JFMASOND]$")  # a climate table's month row
_FACT_LABELS = frozenset(("Preceded by", "Succeeded by", "In office"))
_GENERIC_FIELDS = frozenset((
    "total", "rank", "density", "land", "water", "urban", "metro", "estimate", "census", "preceded by",
    "succeeded by", "in office", "term", "chancellor", "vice-chancellor", "president", "prime minister",
    "monarch", "governor", "deputy", "leader", "members", "seats",
))
_PRONUNCIATION_FIELD = re.compile(r"pronunciation|\bIPA\b|pronounced", re.I)
# "26 May 1564: 90 /1563": the page number of a citation the source stripped
_YEAR_PAGE = re.compile(r"(?<=\d{4}):\s?\d{1,4}\b(?=\s*[/,;.]|\s+[A-Z(]|$)")


def _unit_exponent(m):
    head = m.string[: m.start()]
    unit = re.search(r"[A-Za-z]+$", head)
    if unit and _UNIT_BEFORE.search(unit.group(0)):
        return m.group(1).translate(_SUPER)
    return m.group(0)


def fact_value(name, value):
    if not value:
        return value
    value = _AGE.sub("", value)
    value = _YEAR_PAGE.sub("", value)
    if _FORMULA_ROW.search(name):
        value = _ELEMENT_COUNT.sub(lambda m: m.group(1).translate(_SUB), value)
        value = _FORMULA_GAP.sub("", value)
    value = _ION.sub(lambda m: m.group(1).translate(_SUPER), value)
    value = _UNIT_EXPONENT.sub(_unit_exponent, value)
    value = _DEGREE_GAP.sub("\u00b0", value)
    if name.lower() in _DATE_KEYS:
        value = _DATE_THEN_PLACE.sub(r"\1, ", value)
    value = _PAREN_THEN_ITEM.sub("), ", value)
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
                cells.append(strip_undrawable(raw, self.stats))
            grid.append((is_header, cells))
        if filled and with_runs * 2 >= filled:
            # a phoneme chart, a table of native names: without its script it
            # is a grid of holes, so the notice is the honest rendering
            self.stats["script_tables_omitted"] = self.stats.get("script_tables_omitted", 0) + 1
            return TABLE_OMITTED
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
        for is_header, cells in grid:
            tag = "th" if is_header else "td"
            out.append(
                "<tr>"
                + "".join("<%s>%s</%s>" % (tag, esc(c), tag) for c in cells)
                + "</tr>"
            )
        out.append("</table>")
        return "".join(out)

    def table_rows(self, grid):
        """A table too wide for the panel's grid, as one paragraph per row:
        the first cell in bold, every other cell labelled by its column
        header, empty cells and reference columns left out. A reader gets
        "1984; Category: Best Comedy Recording; Work: Eat It; Result: Won"
        instead of a notice that a table stood here."""
        headers = [cells for is_header, cells in grid if is_header]
        labels = headers[-1] if headers else []
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
                c = cut_words(c, TABLE_ROW_CELL_WORDS)
                if not c:
                    continue
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
                out.append("<p>" + "; ".join(parts) + "</p>")
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
            value = cut_words(
                fact_value(name, strip_undrawable(clean_text(value), self.stats)),
                FACT_WORDS,
            )
            if "coordinates" in name.lower() and " / " in value:
                value = value.split(" / ")[0].strip()
            if value in _FACT_LABELS:
                return  # "Preceded by: Succeeded by": both values were flags
            if _FACT_JUNK_VALUE.match(value) or sum(1 for c in value if c.isalpha()) < 2 and not any(c.isdigit() for c in value):
                return  # "* R ij’ kr -s": a reconstruction whose marks all went
            if name in _FACT_SKIP_NAMES:
                return
            if name and value and value != name and (name, value) not in seen:
                seen.add((name, value))
                fields.append((name, value, group))
                groups.setdefault(name, set()).add(group)

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
                name = clean_text(str(p.get("name") or ""))
                if name and any(isinstance(c, dict) and c.get("type") == "image" for c in p.get("has_parts") or []):
                    has_image[name] = True
                # a group is a short label; a "section" whose name is a whole
                # medal table is the table, not a group
                if name and name != self.title and len(name.split()) <= 8 and not name.lower().startswith("infobox"):
                    group = name
            if t == "field" and isinstance(p.get("value"), str):
                fname = p.get("name")
                if not fname:
                    # "In office 1945 - 1950" under its office; a caption in a
                    # section that holds an image is the image's, not a fact
                    if group and not has_image.get(group) and _NAMELESS_FACT.search(p["value"]):
                        add(group, p["value"])
                elif group and fname.strip().lower() in _GENERIC_FIELDS:
                    add(group + ", " + fname.strip().lower(), p["value"])
                else:
                    add(fname, p["value"], group)
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
                walk(c, group)

        walk(infoboxes)
        if not fields:
            return
        # the same name under two groups ("Literal meaning" under the Korean
        # and the Japanese name) carries its group
        dup = {name for name, gs in groups.items() if len(gs) > 1}
        fields = [
            (group + ", " + name[:1].lower() + name[1:] if name in dup and group else name, value)
            for name, value, group in fields
        ]
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


def article_xhtml(row, stats=None):
    """(title, headings, xhtml_bytes). `stats`, if given, is a dict the
    counters (runs_removed, tables_omitted, ...) are added into."""
    if stats is None:
        stats = {}
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
