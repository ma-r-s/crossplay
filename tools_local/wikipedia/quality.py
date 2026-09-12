#!/usr/bin/env python3
"""What a pack reads like, measured over every article, before it ships.

    quality.py <pack-dir> [--sample N --plain out.md] [--json out.json]

Walks every article of a built pack and counts the marks a cut leaves
behind: empty parentheses, a space before a comma, a sentence that starts
lowercase, a bare "( )", doubled spaces, a letter followed by a bare number
where a symbol stood, near-empty bodies, omitted tables. Each signature gets
a count, a rate per thousand articles and a few examples with their
context, so a rule is written from evidence and its effect is a number on
the next run. --sample writes N random articles as plain text for a
reviewer to read as a reader would; that review is the part no regex does.
"""

import argparse
import collections
import html
import json
import multiprocessing as mp
import os
import random
import re
import sys

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
import pack_format as pf  # noqa: E402

# ---------------------------------------------------------------- detectors
#
# Each detector is (category, name, kind, pattern, note). `kind` says what the
# pattern runs over: "text" is the whole article as plain text, "para" is
# each paragraph or list item, "head" is each heading, "xhtml" is the raw
# markup, "struct" is a named function over the block list. Every hit is
# counted, the articles it touches are counted, and a few examples with
# context are kept, so the report says how common a thing is and shows it,
# and the person reading decides whether it is damage, a source quirk, or
# fine.
#
# The list is deliberately wide. A detector that fires on legitimate text is
# not a bug here: the point is to look at everything odd and decide from
# evidence, not to guess which oddities exist.

_LABELS = (
    r"Chinese|Japanese|Korean|Russian|Arabic|Hebrew|Greek|Hindi|Persian|Urdu|Bengali|Thai|Tamil|"
    r"Ukrainian|Serbian|Armenian|Georgian|Sanskrit|Latin|IPA|romanized|romanised|pinyin|Hangul|Hanja|"
    r"Kanji|Cyrillic|Tibetan|Mongolian|Burmese|Khmer|Sinhala|Malayalam|Telugu|Kannada|Gujarati|Punjabi|"
    r"Marathi|Nepali|Pashto|Kurdish|Turkish|Azerbaijani|Kazakh|Uzbek|Amharic|Yiddish|Coptic|Syriac|"
    r"Aramaic|Akkadian|Sumerian|Egyptian|Ancient Greek|Old English|Old Norse|Gothic|Polish|Czech|"
    r"Hungarian|Finnish|Swedish|Norwegian|Danish|Dutch|German|French|Spanish|Portuguese|Italian|"
    r"Romanian|Vietnamese|Indonesian|Malay|Tagalog|Swahili|Hawaiian|Maori|Irish|Welsh|Scottish Gaelic|"
    r"Icelandic|Lithuanian|Latvian|Estonian|Bulgarian|Croatian|Slovene|Slovak|Macedonian|Albanian|"
    r"Basque|Catalan|Galician|Occitan|Breton|Cornish|Manx|Nahuatl|Quechua|Cherokee|Inuktitut|Wade-Giles|"
    r"simplified|traditional|lit\.|literally|born|n\u00e9e|also known as|abbreviated|pronounced|pronunciation"
)

DETECTORS = [
    # --- structure: what the article is made of
    (
        "structure",
        "no_lead",
        "struct",
        "no_lead",
        "the first block after the title is a heading: the lead was lost",
    ),
    (
        "structure",
        "short_lead",
        "struct",
        "short_lead",
        "the lead paragraph has fewer than 25 words",
    ),
    (
        "structure",
        "tiny_article",
        "struct",
        "tiny",
        "fewer than 60 words of body: a stub, a redirect in disguise, or a gutted article",
    ),
    (
        "structure",
        "huge_article",
        "struct",
        "huge",
        "more than 60,000 words: worth a look for a flattened list or a pasted table",
    ),
    (
        "structure",
        "empty_section",
        "struct",
        "empty_section",
        "a heading with nothing under it before the next heading at its level or above",
    ),
    (
        "structure",
        "section_only_omitted_table",
        "struct",
        "only_table",
        "a section whose only content is the omitted-table notice",
    ),
    (
        "structure",
        "heading_level_jump",
        "struct",
        "level_jump",
        "h2 straight to h4: a level was lost",
    ),
    (
        "structure",
        "duplicate_heading",
        "struct",
        "dup_heading",
        "the same heading text twice in one article",
    ),
    (
        "structure",
        "duplicate_paragraph",
        "struct",
        "dup_para",
        "the same paragraph (over 60 chars) twice in one article",
    ),
    (
        "structure",
        "skeleton",
        "struct",
        "skeleton",
        "more headings than paragraphs: the content under them went",
    ),
    (
        "structure",
        "list_intro_without_list",
        "struct",
        "list_intro",
        "a paragraph ending in a colon followed by a heading or the end: the list it introduced was dropped",
    ),
    (
        "structure",
        "facts_value_long",
        "struct",
        "fact_long",
        "an infobox value over 300 characters: a list or table flattened into one cell",
    ),
    (
        "structure",
        "facts_value_equals_name",
        "struct",
        "fact_same",
        "an infobox row whose value repeats its name: the value was an image or an identifier we lost",
    ),
    ("structure", "facts_many", "struct", "fact_many", "more than 60 infobox rows"),
    # --- headings
    (
        "headings",
        "junk_heading",
        "head",
        re.compile(
            r"^(?:References|External links|See also|Notes|Bibliography|Further reading|Sources|Citations|"
            r"Gallery|Footnotes|Notes and references|Works cited|Explanatory notes|Primary sources|"
            r"Secondary sources|Sources and further reading)$",
            re.I,
        ),
        "a section the converter should have dropped",
    ),
    (
        "headings",
        "heading_punct",
        "head",
        re.compile(r"[:.;,]$|^[^A-Za-z0-9\"'(]"),
        "a heading that ends in punctuation or starts with a symbol",
    ),
    (
        "headings",
        "heading_long",
        "head",
        re.compile(r"^(?:\S+\s+){14,}\S+$"),
        "a heading over 14 words",
    ),
    (
        "headings",
        "heading_caps",
        "head",
        re.compile(r"^[A-Z0-9 ,.'&-]{12,}$"),
        "a heading in capitals",
    ),
    (
        "headings",
        "heading_edit",
        "head",
        re.compile(r"\[edit\]|^edit$"),
        "the edit link survived",
    ),
    # --- words and spacing
    (
        "words",
        "long_word",
        "para",
        re.compile(r"(?<![\w-])[A-Za-z]{35,}(?![\w-])"),
        "35+ letters with no hyphen: glued words, or a real chemical name",
    ),
    (
        "words",
        "camel_glue",
        "para",
        re.compile(r"\b[a-z]{4,}[A-Z][a-z]{3,}\b"),
        "lowercase running into uppercase inside one word: two words glued",
    ),
    (
        "words",
        "sentence_glue",
        "para",
        re.compile(r"[a-z]{3,}[.!?][A-Z][a-z]{2,}"),
        "a period with no space after it: two sentences glued",
    ),
    (
        "words",
        "year_glue",
        "para",
        re.compile(r"\b(?:1[5-9]|20)\d\d[A-Z][a-z]+"),
        "a year running into a word",
    ),
    (
        "words",
        "comma_glue",
        "para",
        re.compile(r"\b(?!(?:alpha|beta|gamma|delta|epsilon|zeta|eta|theta|iota|kappa|lambda|omicron|rho|sigma|tau|upsilon|phi|chi|psi|omega|cis|trans|sec|tert|iso|neo|ortho|meta|para),)[a-z]{3,},[A-Za-z]{3,}"),
        "a comma with no space after it",
    ),
    ("words", "double_punct", "para", re.compile(r"[,;:]{2}|[,;] ?[,;]|, \.|; \.|\?\?|!!"), "doubled punctuation"),
    (
        "words",
        "space_before_punct",
        "para",
        re.compile(r"\w [,.;!?](?=\s|$)|[A-Za-z] :(?=\s[a-z]|$)"),
        "a space before a comma or period: something between them went (a colon after a digit is a ratio or a title)",
    ),
    (
        "words",
        "double_space",
        "para",
        re.compile(r"\S  +\S"),
        "two spaces: something between them went",
    ),
    (
        "words",
        "repeated_word",
        "para",
        re.compile(r"\b([A-Za-z]{2,}) \1\b"),
        "the the: a word doubled, often by a removal",
    ),
    (
        "words",
        "repeated_char",
        "para",
        re.compile(r"([^\s\d])\1{5,}"),
        "six of the same character in a row: dividers, dot leaders",
    ),
    ("words", "lowercase_sentence", "para",
     re.compile(r"(?<![A-Z])(?<!e\.g)(?<!i\.e)(?<!etc)(?<!\bvs)(?<!\bcf)(?<!\bca)(?<!\bal)(?<!\.\.)[.!?] [a-z]{3,}\b"),
     "a sentence starting lowercase"),
    (
        "words",
        "starts_with_punct",
        "para",
        re.compile(r"^[,;:.)\]]"),
        "a paragraph starting with punctuation",
    ),
    (
        "words",
        "ends_dangling",
        "para",
        re.compile(
            r"\b(?:and|or|the|of|a|an|in|to|by|with|from|for|as|at|that|which|is|are|was)\.?$"
        ),
        "a paragraph ending on a function word: its end was cut",
    ),
    (
        "words",
        "no_period_paragraph",
        "para",
        re.compile(r"^(?:[^.!?]){220,}$"),
        "over 220 characters with no sentence end: a glued list, a caption wall",
    ),
    ("words", "very_long_sentence", "struct", "long_sentence", "a sentence over 140 words"),
    ("words", "one_word_paragraph", "p", re.compile(r"^\S+$"), "a one-word paragraph: a caption, a label, a stray cell"),
    ("words", "caption_like", "p", re.compile(r"^(?:\S+ ){1,11}\S+$(?<![.!?\"')])"),
     "2 to 12 words with no sentence end: an image caption that outlived its image"),
    (
        "words",
        "numeric_paragraph",
        "para",
        re.compile(r"^[\d\s.,%:/()+-]{3,}$"),
        "a paragraph of digits only: a table cell",
    ),
    (
        "words",
        "all_caps_paragraph",
        "para",
        re.compile(r"^[A-Z0-9 ,.'&:;()-]{40,}$"),
        "a paragraph in capitals",
    ),
    # --- balance
    ("balance", "empty_parens", "para", re.compile(r"\(\s*[,;:\s]*\)"), "an empty parenthetical: its content was removed"),
    ("balance", "double_paren", "para", re.compile(r"\(\([^()]*\)\)"), "((x)) around one thing, or two closers with nothing opened between them"),
    (
        "balance",
        "punct_only_parens",
        "para",
        re.compile(r"\([^A-Za-z0-9()+\-\u2212\u00b1]{1,8}\)"),
        "a parenthetical of punctuation only",
    ),
    (
        "balance",
        "unbalanced_parens",
        "struct",
        "unbalanced_parens",
        "a paragraph with more ( than ) or the reverse",
    ),
    (
        "balance",
        "unbalanced_quotes",
        "struct",
        "unbalanced_quotes",
        "a paragraph with an odd number of double quotes",
    ),
    (
        "balance",
        "unbalanced_brackets",
        "struct",
        "unbalanced_brackets",
        "a paragraph with more [ than ] or the reverse",
    ),
    (
        "balance",
        "orphan_quote",
        "para",
        re.compile(r"(?<![\w.,!?\"'])\"\s*\"(?![\w\"'])|(?<!mark )(?<!marks )(?<!quote )(?<!quotes )(?<!character )\(\s*\"\s*\)|(?<![\w.,!?])\u201c\s*\u201d"),
        "an empty quotation",
    ),
    (
        "balance",
        "empty_label",
        "para",
        re.compile(r"\b(?:" + _LABELS + r")\s*:\s*[,;)]"),
        "a language label with nothing after it: the native word was removed, the label stayed",
    ),
    (
        "balance",
        "generic_empty_label",
        "para",
        re.compile(r"\b[A-Z][a-z]+(?: [A-Z][a-z]+)?:\s*[,;)]"),
        "any Capitalised label followed by nothing",
    ),
    # --- markup and source remnants
    (
        "remnants",
        "stray_markup",
        "text",
        re.compile(
            r"\{\{(?!\s*[A-Za-z0-9 ,]{1,12}\s*\})|\[\[(?!\d)|\]\]|<ref\b|&lt;|&gt;|&nbsp;|&amp;|&#\d+;|&[a-z]{2,8};"
        ),
        "wikitext or HTML that should not be in the text",
    ),
    (
        "remnants",
        "wikitext_line",
        "para",
        re.compile(r"^\s*[#:;]{1,3}\s|'''|^={2,}[^=\n]*={2,}\s*$|^={3,}\s|^\s*\|-\s*$|^\s*\|\}|^\s*\{\|"),
        "a wikitext list marker, bold marks or table syntax",
    ),
    (
        "remnants",
        "namespace_remnant",
        "text",
        re.compile(
            r"\bthumb\||\b\d{2,4}px\b|\b(?:File|Image|Category|Template|Wikipedia|Help|Special|Talk):(?=\S)"
        ),
        "file, image, category or template syntax",
    ),
    (
        "remnants",
        "citation_mark",
        "text",
        re.compile(
            r"\[\d{1,3}\]|\[[a-z]\]|\[note \d+\]|\[(?:citation|clarification|verification|page|full citation|"
            r"better source|not in citation|original research|dubious|failed verification|who|when|which|why|"
            r"where|how|according to whom|by whom|weasel words|vague|quantify|specify|year needed|non-primary "
            r"source needed|unreliable source\??|improper synthesis\??|neutrality is disputed|attribution "
            r"needed|discuss|update|contradictory|inconsistent|clarify|obsolete source|self-published source\??|"
            r"disputed|excessive citations|relevant\?|sic)[^\]]{0,20}\]",
            re.I,
        ),
        "a footnote mark or an inline maintenance tag",
    ),
    (
        "remnants",
        "hatnote",
        "para",
        re.compile(
            r"^(?:Main articles?:|See also:|Further information:|For other uses|For the [^.]{0,80}, see\b|Not to be confused|"
            r"This article is about|This page is about|\"[^\"]+\" redirects here|[A-Z][^.]{0,40} redirects here)"
        ),
        "a hatnote line",
    ),
    (
        "remnants",
        "maintenance_text",
        "text",
        re.compile(
            r"\bis a stub\b|needs additional citations|may require cleanup|neutrality of this|too long to read|"
            r"please help|Learn how and when to remove|may be too technical|does not cite any|"
            r"relies (?:too much|largely|excessively) on|may need to be rewritten|has multiple issues",
            re.I,
        ),
        "a maintenance banner's text",
    ),
    (
        "remnants",
        "navbox_remnant",
        "text",
        re.compile(r"\bv ?[\u00b7.] ?t ?[\u00b7.] ?e\b|This box:|\bview talk edit\b"),
        "a navbox",
    ),
    (
        "remnants",
        "reference_remnant",
        "text",
        re.compile(
            r"\bRetrieved \d|\bArchived from the original|\bdoi:|\bISBN\b|\bPMID\b|\bS2CID\b|\barchive\.org\b|\(help\)"
        ),
        "reference text in the body",
    ),
    ("remnants", "url_in_text", "text", re.compile(r"https?://|\bwww\.[a-z]"), "a URL"),
    (
        "remnants",
        "page_ref_glue",
        "para",
        re.compile(r"(?<=[a-z]{4}[.!?]):\s?(?:[ivxlc]{1,7}\.\s?)?\d{1,4}(?:[\u2013-]\d{1,4})?(?=[\s)]|$)"),
        "a page reference glued to the sentence's period: the rp template's output (\"Pop.: 249,626\" is a label)",
    ),
    (
        "remnants",
        "tex_remnant",
        "text",
        re.compile(
            r"\\displaystyle|\\frac|\\sqrt|\\mathbf|\\mathrm|\\left|\\right|(?<!\\)\{\\|\^\{|_\{|\\[a-zA-Z]{2,}\{|(?<![A-Za-z])\\[a-zA-Z]{2,}\b"
        ),
        "TeX in the text",
    ),
    (
        "remnants",
        "pipe_in_text",
        "para",
        re.compile(r"(?<!\|)\|(?!\|)"),
        "a pipe: a table or template cell divider",
    ),
    (
        "remnants",
        "bullet_in_paragraph",
        "para",
        re.compile(r"[\u2022\u25cf\u25cb\u25e6\u25aa\u25a0\u2023\u2043]"),
        "a bullet character inside a paragraph: a list flattened",
    ),
    (
        "remnants",
        "coordinates",
        "text",
        re.compile(r"\d+\u00b0\s?\d+\u2032|\d+\.\d+\u00b0[NSEW]|\d+\u00b0\s?[NSEW]\b \d+\u00b0"),
        "geographic coordinates in the body",
    ),
    # --- encoding
    (
        "encoding",
        "mojibake",
        "text",
        re.compile(r"\u00c3[\u0080-\u00bf]|\u00e2\u20ac|\u00c2[\u00a0 ]|\u00ef\u00bf\u00bd"),
        "UTF-8 read as Latin-1 somewhere upstream",
    ),
    (
        "encoding",
        "replacement_char",
        "text",
        re.compile(r"(?<!U\+FFFD )(?<!character )\ufffd(?! REPLACEMENT)"),
        "U+FFFD: a byte that was not text",
    ),
    (
        "encoding",
        "control_char",
        "text",
        re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]"),
        "a control character",
    ),
    (
        "encoding",
        "invisible_char",
        "text",
        re.compile(r"[\u00ad\u200b-\u200f\u2028\u2029\u202a-\u202e\u2060-\u2064\ufeff]"),
        "soft hyphen, zero-width space or joiner, bidi mark, word joiner, BOM: the font has glyph slots for these; what they draw as is a question for the panel",
    ),
    (
        "encoding",
        "odd_space",
        "text",
        re.compile(r"[\u2000-\u200a\u202f\u205f\u3000]"),
        "an en space, thin space, narrow no-break space: drawn at its own width",
    ),
    (
        "encoding",
        "private_use",
        "text",
        re.compile(r"[\ue000-\uf8ff]"),
        "private-use code points: nothing can draw these",
    ),
    # --- formulas and symbols
    (
        "formulas",
        "formula_hole",
        "para",
        re.compile(
            r"= =|\b(?:to|of|is|are|where|equals|let|denotes?|given by|defined as|such that|equal to) [.,;)]|"
            r"\bthe (?:of|and|is|are|to|in|for|with|by)\b|\b(?:is|are) (?:is|are)\b|\bof of\b|\band and\b|"
            r"\b(?:a|an|the) [.,;]|\bwhere (?:is|are|and|,)"
        ),
        "prose around a formula whose symbols went",
    ),
    (
        "formulas",
        "lone_letters_row",
        "para",
        re.compile(r"\b[b-hj-z] [b-hj-z] [b-hj-z]\b"),
        "three single letters in a row: a flattened formula",
    ),
    (
        "formulas",
        "bare_number_after_letter",
        "para",
        re.compile(r"\b(?:where|let|if|when) [b-hj-z] \d"),
        '"where n 2": an operator went',
    ),
    ("formulas", "superscript_lost", "para",
     re.compile(r"\b10 -?\d{1,2}\b(?![.,]\d)|\b(?:km|m|cm|mm|ft|mi) [23]\b(?![-.,]\d)"),
     "10 6 or km 2: an exponent became a separate number"),
    (
        "formulas",
        "caret_formula",
        "para",
        re.compile(r"\w\^\w|\w\^\(|\b\w+_\w+\b(?![\w/.-])"),
        "a caret or underscore formula written in ASCII",
    ),
    (
        "formulas",
        "padded_dash",
        "para",
        re.compile(r"\w [\u2013\u2014]\w|\w[\u2013\u2014] \w"),
        "a dash spaced on one side only",
    ),
    (
        "formulas",
        "hyphen_gap",
        "para",
        re.compile(r"\w -\w|\w- (?!and\b|or\b)\w"),
        "a hyphen spaced on one side",
    ),
    # --- tables
    (
        "tables",
        "omitted_table",
        "text",
        re.compile(r"\(a table was omitted\)"),
        "a table the converter could not lay out",
    ),
    (
        "tables",
        "table_cell_long",
        "xhtml",
        re.compile(r"<td>(?:[^<]|<(?!/td>)){400,}</td>"),
        "a table cell over 400 characters",
    ),
    (
        "tables",
        "table_empty_cell",
        "xhtml",
        re.compile(r"<td>\s*</td>|<th>\s*</th>"),
        "an empty table cell",
    ),
    # --- links
    (
        "links",
        "empty_link",
        "xhtml",
        re.compile(r"<a [^>]*>\s*</a>"),
        "a link with no text",
    ),
    (
        "links",
        "punct_link",
        "xhtml",
        re.compile(r"<a [^>]*>[^A-Za-z0-9<]{1,4}</a>"),
        "a link whose text is punctuation",
    ),
    (
        "links",
        "number_link",
        "xhtml",
        re.compile(r"<a [^>]*>\d{1,3}</a>"),
        "a link whose text is a small number: a footnote",
    ),
    (
        "links",
        "nested_inline",
        "xhtml",
        re.compile(r"<a [^>]*>[^<]*<a "),
        "a link inside a link",
    ),
]

NEAR_EMPTY = 300
ARTIFACT_DETECTORS = (
    "empty_parens", "double_paren", "orphan_quote", "double_space", "space_before_punct", "stray_markup",
    "tex_remnant", "invisible_char", "replacement_char", "empty_label", "unbalanced_parens",
    "unbalanced_brackets", "comma_glue", "wikitext_line", "navbox_remnant", "heading_edit",
)
_BLOCK = re.compile(r"</?(?:p|h[1-6]|li|ul|table|tr|td|th|body|html)\b[^>]*>")
_INLINE = re.compile(r"</?(?:a|b|i)\b[^>]*>")
_BLOCK_SPLIT = re.compile(r"<(h[1-6]|p|li|table)\b[^>]*>(.*?)</\1>", re.S)
_TAG = re.compile(r"<[^>]+>")
_ROW = re.compile(r"<tr><th>(.*?)</th><td>(.*?)</td></tr>", re.S)


def plain(xhtml):
    """The text as the reader lays it out: inline tags vanish (a link's
    closing tag is not a space before the period), block tags break lines,
    entities are characters."""
    t = xhtml.decode("utf-8", "replace")
    t = _INLINE.sub("", t)
    t = _BLOCK.sub("\n", t)
    t = re.sub(r"<[^>]+>", " ", t)
    t = html.unescape(t)
    t = re.sub(r"[ \t]+", " ", t)
    return re.sub(r"\n\s*\n+", "\n", t).strip()


def blocks(xhtml):
    """[(kind, text)] in document order; kind is h1..h6, p, li or table.
    A table's text is its rows as (name, value) pairs."""
    s = xhtml.decode("utf-8", "replace")
    out = []
    for m in _BLOCK_SPLIT.finditer(s):
        kind, inner = m.group(1), m.group(2)
        if kind == "table":
            rows = [
                (
                    html.unescape(_TAG.sub("", a)).strip(),
                    html.unescape(_TAG.sub("", b)).strip(),
                )
                for a, b in _ROW.findall(inner)
            ]
            out.append(("table", rows))
        else:
            out.append((kind, html.unescape(_TAG.sub("", inner)).strip()))
    return out


def _words(t):
    return len(t.split())


# a face stands after a space or an opening quote; ":(" glued to a word or a
# parenthesis ("(A+C):(B+D)", "states):(I)") is punctuation and counts
_FACE = re.compile(r"(?<![^\s\"\u201c\u2018])[:;]-?[()](?![A-Za-z0-9])")


def struct_hits(name, bl):
    """Structural detectors over the block list. Returns [context, ...]."""
    hits = []
    paras = [(k, t) for k, t in bl if k in ("p", "li")]
    heads = [(k, t) for k, t in bl if k[0] == "h" and k != "h1"]
    body_words = sum(_words(t) for k, t in paras) + sum(
        sum(_words(a) + _words(b) for a, b in t) for k, t in bl if k == "table"
    )
    if name == "no_lead":
        after = [k for k, t in bl if k != "h1"]
        if after and after[0][0] == "h":
            hits.append("first block is <%s>" % after[0])
    elif name == "short_lead":
        after = [(k, t) for k, t in bl if k != "h1"]
        if after and after[0][0] == "p" and _words(after[0][1]) < 25:
            hits.append(after[0][1][:160])
    elif name == "tiny":
        if body_words < 60:
            hits.append("%d words" % body_words)
    elif name == "huge":
        if body_words > 60000:
            hits.append("%d words" % body_words)
    elif name in ("empty_section", "only_table"):
        for i, (k, t) in enumerate(bl):
            if k[0] != "h" or k == "h1":
                continue
            level = int(k[1])
            content = []
            for k2, t2 in bl[i + 1 :]:
                if k2[0] == "h" and k2 != "h1" and int(k2[1]) <= level:
                    break
                content.append((k2, t2))
            if name == "empty_section" and not content:
                hits.append("<%s> %s" % (k, t))
            if (
                name == "only_table"
                and content
                and all(
                    k2 == "p" and t2 == "(a table was omitted)" for k2, t2 in content
                )
            ):
                hits.append("<%s> %s" % (k, t))
    elif name == "level_jump":
        prev = 1
        for k, t in bl:
            if k[0] == "h":
                level = int(k[1])
                if level > prev + 1:
                    hits.append("h%d to h%d at %s" % (prev, level, t))
                prev = level
    elif name == "dup_heading":
        seen = set()
        for k, t in heads:
            if t in seen:
                hits.append(t)
            seen.add(t)
    elif name == "dup_para":
        seen = set()
        for k, t in paras:
            if len(t) > 60:
                if t in seen:
                    hits.append(t[:120])
                seen.add(t)
    elif name == "skeleton":
        if len(heads) > 3 and len(heads) > len(paras):
            hits.append("%d headings, %d paragraphs" % (len(heads), len(paras)))
    elif name == "list_intro":
        for i, (k, t) in enumerate(bl):
            if k == "p" and t.endswith(":"):
                nxt = bl[i + 1][0] if i + 1 < len(bl) else "end"
                if nxt[0] == "h" or nxt == "end":
                    hits.append("%s ... <%s>" % (t[-100:], nxt))
    elif name in ("fact_long", "fact_same", "fact_many"):
        for k, rows in bl:
            if k != "table":
                continue
            if name == "fact_many" and len(rows) > 60:
                hits.append("%d rows" % len(rows))
            for a, b in rows:
                if name == "fact_long" and len(b) > 300:
                    hits.append("%s: %s" % (a, b[:120]))
                if name == "fact_same" and a and a == b:
                    hits.append("%s: %s" % (a, b))
    elif name == "long_sentence":
        for k, t in paras:
            for sent in re.split(r"(?<=[.!?])\s+(?=[A-Z\"'(])", t):
                if _words(sent) > 140:
                    hits.append(sent[:160])
    elif name == "unbalanced_parens":
        for k, t in paras:
            t = _FACE.sub("", t)  # ":)" is a face, not a parenthesis
            if t.count("(") != t.count(")"):
                hits.append(t[:160])
    elif name == "unbalanced_quotes":
        for k, t in paras:
            if t.count('"') % 2:
                hits.append(t[:160])
    elif name == "unbalanced_brackets":
        for k, t in paras:
            if t.count("[") != t.count("]"):
                hits.append(t[:160])
    return hits


class Percentiles:
    def __init__(self):
        self.values = []
        self.extremes = []  # (value, title)

    def add(self, v, title):
        self.values.append(v)
        self.extremes.append((v, title))

    def summary(self, low=8, high=8):
        vs = sorted(self.values)
        n = len(vs)
        if not n:
            return {}
        ex = sorted(self.extremes)
        return {
            "p1": vs[n // 100],
            "p10": vs[n // 10],
            "p50": vs[n // 2],
            "p90": vs[min(n - 1, n * 9 // 10)],
            "p99": vs[min(n - 1, n * 99 // 100)],
            "max": vs[-1],
            "lowest": [(v, t) for v, t in ex[:low]],
            "highest": [(v, t) for v, t in ex[-high:][::-1]],
        }


DIST_KEYS = ("words", "paragraphs", "headings", "list_items", "longest_paragraph_words", "longest_word", "facts")

_pack = None  # the worker's own reader


def _init_scan(pack_dir):
    global _pack
    _pack = pf.Pack(pack_dir)


def scan_article(a, examples_per, out):
    """Runs every detector over one article; adds to `out`, a dict of the
    per-chunk accumulators (counts, hit, examples, dist, sizes, near_empty)."""
    t = plain(a.xhtml)
    bl = blocks(a.xhtml)
    body = t[len(a.title) :].strip() if t.startswith(a.title) else t
    out["sizes"].append(len(body))
    if len(body) < NEAR_EMPTY:
        out["near_empty"].append((a.title, len(body)))
    paras = [tx for k, tx in bl if k in ("p", "li")]
    heads = [tx for k, tx in bl if k[0] == "h" and k != "h1"]
    dist = out["dist"]
    dist["words"].append((_words(body), a.title))
    dist["paragraphs"].append((sum(1 for k, tx in bl if k == "p"), a.title))
    dist["headings"].append((len(heads), a.title))
    dist["list_items"].append((sum(1 for k, tx in bl if k == "li"), a.title))
    dist["longest_paragraph_words"].append((max((_words(tx) for tx in paras), default=0), a.title))
    longest = max((len(w.strip(".,;:()\"'")) for w in body.split()), default=0)
    dist["longest_word"].append((longest, a.title))
    dist["facts"].append((sum(len(rows) for k, rows in bl if k == "table"), a.title))
    counts = out["counts"]
    examples = out["examples"]

    def note(name, ctx):
        counts[name] += 1
        if len(examples[name]) < examples_per:
            examples[name].append((a.title, ctx.replace("\n", " ")))

    hit_here = set()
    xh = a.xhtml.decode("utf-8", "replace")
    for cat, name, kind, pat, _ in DETECTORS:
        if kind == "text":
            for m in pat.finditer(t):
                note(name, t[max(0, m.start() - 60) : m.end() + 50])
                hit_here.add(name)
        elif kind == "xhtml":
            for m in pat.finditer(xh):
                note(name, xh[max(0, m.start() - 60) : m.end() + 50])
                hit_here.add(name)
        elif kind == "p":
            for tx in (tx for k, tx in bl if k == "p"):
                for m in pat.finditer(tx):
                    note(name, tx[max(0, m.start() - 60) : m.end() + 50])
                    hit_here.add(name)
        elif kind == "para":
            for tx in paras:
                for m in pat.finditer(tx):
                    note(name, tx[max(0, m.start() - 60) : m.end() + 50])
                    hit_here.add(name)
        elif kind == "head":
            for tx in heads:
                if pat.search(tx):
                    note(name, tx)
                    hit_here.add(name)
        elif kind == "struct":
            for ctx in struct_hits(pat, bl):
                note(name, ctx)
                hit_here.add(name)
    for name in hit_here:
        out["hit"][name] += 1


def _new_accumulators():
    return {
        "counts": collections.Counter(),
        "hit": collections.Counter(),
        "examples": collections.defaultdict(list),
        "dist": {k: [] for k in DIST_KEYS},
        "sizes": [],
        "near_empty": [],
    }


def _scan_chunk(args):
    chunk, examples_per = args
    out = _new_accumulators()
    for title, locator in chunk:
        scan_article(_pack.article(locator), examples_per, out)
    out["examples"] = dict(out["examples"])
    return out


def scan(pack_dir, sample_n=0, seed=20260911, examples_per=6, limit=0, workers=None, every=1):
    """Every article through every detector, on a pool of workers; each
    worker reads its own contiguous slice so the block cache stays warm."""
    p = pf.Pack(pack_dir)
    entries = [(e.title, e.locator) for e in p.iter_entries() if not e.redirect]
    p.close()
    if every > 1:
        entries = entries[::every]  # the full pack: every Nth article, still in title order
    if limit:
        entries = entries[:limit]
    n = len(entries)
    rng = random.Random(seed)
    sample_entries = rng.sample(entries, min(sample_n, n)) if sample_n else []
    workers = workers or max(1, (os.cpu_count() or 2) - 2)
    step = max(200, -(-n // (workers * 6)))
    chunks = [(entries[i : i + step], examples_per) for i in range(0, n, step)]
    counts = collections.Counter()
    articles_hit = collections.Counter()
    examples = collections.defaultdict(list)
    dist = {k: Percentiles() for k in DIST_KEYS}
    sizes = []
    near_empty = []
    done = 0
    ctx = mp.get_context("fork")
    with ctx.Pool(processes=workers, initializer=_init_scan, initargs=(pack_dir,)) as pool:
        for part in pool.imap_unordered(_scan_chunk, chunks):
            counts.update(part["counts"])
            articles_hit.update(part["hit"])
            for name, exs in part["examples"].items():
                room = examples_per - len(examples[name])
                if room > 0:
                    examples[name].extend(exs[:room])
            for k, vals in part["dist"].items():
                for v, title in vals:
                    dist[k].add(v, title)
            sizes.extend(part["sizes"])
            near_empty.extend(part["near_empty"])
            done += len(part["sizes"])
            print("%d of %d articles scanned" % (done, n), file=sys.stderr, flush=True)
    sample = []
    if sample_entries:
        p = pf.Pack(pack_dir)
        try:
            for title, locator in sample_entries:
                sample.append((title, plain(p.article(locator).xhtml)))
        finally:
            p.close()
    sizes.sort()
    report = {
        "articles": n,
        "body_chars": {
            "median": sizes[n // 2] if n else 0,
            "p10": sizes[n // 10] if n else 0,
            "p1": sizes[n // 100] if n else 0,
        },
        "near_empty": near_empty[:20],
        "near_empty_count": len(near_empty),
        "distributions": {k: v.summary() for k, v in dist.items()},
        "signatures": {
            name: {
                "category": cat,
                "note": note_,
                "hits": counts[name],
                "articles": articles_hit[name],
                "per_1000_articles": round(1000.0 * articles_hit[name] / n, 2)
                if n
                else 0,
                "examples": examples[name],
            }
            for cat, name, kind, pat, note_ in DETECTORS
        },
    }
    return report, sample


def write_report(report, path):
    lines = []
    w = lines.append
    w("# Pack quality report\n")
    w(
        "%d articles. Body chars median %d, p10 %d, p1 %d. Near-empty (under %d chars): %d.\n"
        % (
            report["articles"],
            report["body_chars"]["median"],
            report["body_chars"]["p10"],
            report["body_chars"]["p1"],
            NEAR_EMPTY,
            report["near_empty_count"],
        )
    )
    w("\n## Shapes\n")
    w("| measure | p1 | p10 | p50 | p90 | p99 | max | lowest | highest |")
    w("|---|---:|---:|---:|---:|---:|---:|---|---|")
    for k, d in report["distributions"].items():
        if not d:
            continue
        lo = ", ".join("%s (%d)" % (t.replace("|", "/"), v) for v, t in d["lowest"][:5])
        hi = ", ".join(
            "%s (%d)" % (t.replace("|", "/"), v) for v, t in d["highest"][:5]
        )
        w(
            "| %s | %d | %d | %d | %d | %d | %d | %s | %s |"
            % (k, d["p1"], d["p10"], d["p50"], d["p90"], d["p99"], d["max"], lo, hi)
        )
    w("\n## Detectors\n")
    w(
        "Sorted by articles affected within each category. A detector firing is a thing to look at, not a verdict.\n"
    )
    cats = []
    for name, r in report["signatures"].items():
        if r["category"] not in cats:
            cats.append(r["category"])
    for cat in cats:
        w("\n### %s\n" % cat)
        w("| detector | hits | articles | per 1000 | what it looks for |")
        w("|---|---:|---:|---:|---|")
        rows = [
            (name, r)
            for name, r in report["signatures"].items()
            if r["category"] == cat
        ]
        rows.sort(key=lambda kv: -kv[1]["articles"])
        for name, r in rows:
            w(
                "| %s | %d | %d | %.1f | %s |"
                % (name, r["hits"], r["articles"], r["per_1000_articles"], r["note"])
            )
        for name, r in rows:
            if not r["examples"]:
                continue
            w("\n**%s**\n" % name)
            for title, ctx in r["examples"]:
                w("- %s: \u201c%s\u201d" % (title, ctx.replace("|", "/")))
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
# The census, judged. A removed character is acceptable only when the panel
# truly cannot hold its script and the text keeps a romanisation beside it;
# everything else (Greek, accented Latin, maths, arrows, numbers, punctuation)
# carries meaning in English prose and must be drawn or spelled, never
# dropped. A build whose census has any of the latter fails this gate.
ACCEPTED_BLOCKS = (
    ("Han", 0x4E00, 0x9FFF), ("Han ext A", 0x3400, 0x4DBF), ("Han ext B+", 0x20000, 0x2FA1F),
    ("CJK symbols", 0x3000, 0x303F), ("Hiragana", 0x3040, 0x309F), ("Katakana", 0x30A0, 0x30FF),
    ("Hangul", 0xAC00, 0xD7AF), ("Hangul jamo", 0x1100, 0x11FF), ("Bopomofo", 0x3100, 0x312F),
    ("Arabic", 0x0600, 0x06FF), ("Arabic supplement", 0x0750, 0x077F), ("Arabic forms", 0xFB50, 0xFDFF),
    ("Arabic forms B", 0xFE70, 0xFEFF), ("Hebrew", 0x0590, 0x05FF), ("Syriac", 0x0700, 0x074F),
    ("Thaana", 0x0780, 0x07BF), ("Devanagari", 0x0900, 0x097F), ("Bengali", 0x0980, 0x09FF),
    ("Gurmukhi", 0x0A00, 0x0A7F), ("Gujarati", 0x0A80, 0x0AFF), ("Oriya", 0x0B00, 0x0B7F),
    ("Tamil", 0x0B80, 0x0BFF), ("Telugu", 0x0C00, 0x0C7F), ("Kannada", 0x0C80, 0x0CFF),
    ("Malayalam", 0x0D00, 0x0D7F), ("Sinhala", 0x0D80, 0x0DFF), ("Thai", 0x0E00, 0x0E7F),
    ("Lao", 0x0E80, 0x0EFF), ("Tibetan", 0x0F00, 0x0FFF), ("Myanmar", 0x1000, 0x109F),
    ("Georgian", 0x10A0, 0x10FF), ("Ethiopic", 0x1200, 0x137F), ("Khmer", 0x1780, 0x17FF),
    ("Mongolian", 0x1800, 0x18AF), ("Armenian", 0x0530, 0x058F), ("IPA", 0x0250, 0x02AF),
    ("Spacing modifiers", 0x02B0, 0x02FF), ("Combining marks", 0x0300, 0x036F),
    ("Yi", 0xA000, 0xA4CF), ("Cherokee", 0x13A0, 0x13FF), ("Canadian syllabics", 0x1400, 0x167F),
    ("Emoji and pictographs", 0x1F300, 0x1FAFF), ("Misc symbols", 0x2600, 0x26FF), ("Dingbats", 0x2700, 0x27BF),
    ("Private use", 0xE000, 0xF8FF), ("Specials", 0xFFF0, 0xFFFF), ("Variation selectors", 0xFE00, 0xFE0F),
    ("Enclosed alphanumerics", 0x2460, 0x24FF), ("Box drawing", 0x2500, 0x257F), ("Geometric shapes", 0x25A0, 0x25FF),
    ("Block elements", 0x2580, 0x259F), ("Coptic", 0x2C80, 0x2CFF), ("Glagolitic", 0x2C00, 0x2C5F),
    ("Cuneiform", 0x12000, 0x1254F), ("Egyptian hieroglyphs", 0x13000, 0x1342F), ("Old Turkic", 0x10C00, 0x10C4F),
    ("Linear B", 0x10000, 0x100FF), ("Old Italic", 0x10300, 0x1032F), ("Gothic", 0x10330, 0x1034F),
    ("Phoenician", 0x10900, 0x1091F), ("Old Persian", 0x103A0, 0x103DF), ("Ugaritic", 0x10380, 0x1039F),
    ("Runic", 0x16A0, 0x16FF), ("Ogham", 0x1680, 0x169F), ("Tifinagh", 0x2D30, 0x2D7F),
    ("Javanese", 0x0A980, 0x0A9DF), ("Sundanese", 0x1B80, 0x1BBF), ("Balinese", 0x1B00, 0x1B7F),
    ("Tagalog", 0x1700, 0x171F), ("Buginese", 0x1A00, 0x1A1F), ("Lepcha", 0x1C00, 0x1C4F),
    ("Vai", 0xA500, 0xA63F), ("Bamum", 0xA6A0, 0xA6FF), ("Syloti Nagri", 0xA800, 0xA82F),
    ("Musical symbols", 0x1D100, 0x1D1FF), ("Mahjong and cards", 0x1F000, 0x1F0FF), ("Mathematical alphanumerics", 0x1D400, 0x1D7FF),
    ("Phonetic extensions", 0x1D00, 0x1DBF), ("Combining supplement", 0x1DC0, 0x1DFF), ("Combining half marks", 0xFE20, 0xFE2F),
    ("Halfwidth and fullwidth", 0xFF00, 0xFFEF), ("CJK compatibility", 0x3300, 0x33FF), ("Enclosed CJK", 0x3200, 0x32FF),
    ("Kanbun", 0x3190, 0x319F), ("CJK radicals", 0x2E80, 0x2FDF), ("Hangul compatibility jamo", 0x3130, 0x318F),
    ("Hangul jamo extended", 0xA960, 0xA97F), ("Tai Le", 0x1950, 0x197F), ("New Tai Lue", 0x1980, 0x19DF),
    ("Tai Tham", 0x1A20, 0x1AAF), ("Cham", 0xAA00, 0xAA5F), ("Ol Chiki", 0x1C50, 0x1C7F),
    ("Saurashtra", 0xA880, 0xA8DF), ("Kayah Li", 0xA900, 0xA92F), ("Rejang", 0xA930, 0xA95F),
    ("Meetei Mayek", 0xABC0, 0xABFF), ("Lisu", 0xA4D0, 0xA4FF), ("Osmanya", 0x10480, 0x104AF),
    ("Shavian", 0x10450, 0x1047F), ("Deseret", 0x10400, 0x1044F), ("Brahmi", 0x11000, 0x1107F),
    ("Kharoshthi", 0x10A00, 0x10A5F), ("Avestan", 0x10B00, 0x10B3F), ("Inscriptional", 0x10B40, 0x10B7F),
    ("Imperial Aramaic", 0x10840, 0x1085F), ("Samaritan", 0x0800, 0x083F), ("Mandaic", 0x0840, 0x085F),
    ("Arabic extended", 0x08A0, 0x08FF), ("Ethiopic extended", 0x2D80, 0x2DDF), ("Ethiopic supplement", 0x1380, 0x139F),
    ("Cyrillic extended", 0x2DE0, 0x2DFF), ("Cyrillic extended B", 0xA640, 0xA69F), ("Anatolian", 0x14400, 0x1467F),
    ("Supplemental punctuation", 0x2E00, 0x2E7F), ("Ideographic description", 0x2FF0, 0x2FFF), ("Vertical forms", 0xFE10, 0xFE1F),
    ("CJK compatibility forms", 0xFE30, 0xFE4F), ("Small form variants", 0xFE50, 0xFE6F), ("Tags and variation", 0xE0000, 0xE01EF),
    ("Symbols for legacy computing", 0x1FB00, 0x1FBFF), ("Chess and games", 0x1FA00, 0x1FA6F), ("Ornamental dingbats", 0x1F650, 0x1F67F),
    ("Alchemical", 0x1F700, 0x1F77F), ("Geometric extended", 0x1F780, 0x1F7FF), ("Supplemental arrows C", 0x1F800, 0x1F8FF),
)
# Greek and Cyrillic are removed only where a romanisation stands beside
# them (elsewhere they are romanised in place), so their removal is accepted.
ACCEPTED_BLOCKS = ACCEPTED_BLOCKS + (
    ("Greek", 0x0370, 0x03FF), ("Greek extended", 0x1F00, 0x1FFF), ("Cyrillic", 0x0400, 0x04FF),
    ("Cyrillic supplement", 0x0500, 0x052F),
)
WATCH_BLOCKS = (
    ("Latin extended additional", 0x1E00, 0x1EFF),
    ("Latin extended B", 0x0180, 0x024F), ("Latin extended C/D", 0x2C60, 0x2C7F),
    ("General punctuation", 0x2000, 0x206F),
    ("Super and subscripts", 0x2070, 0x209F), ("Currency", 0x20A0, 0x20CF), ("Letterlike", 0x2100, 0x214F),
    ("Number forms", 0x2150, 0x218F), ("Arrows", 0x2190, 0x21FF), ("Mathematical operators", 0x2200, 0x22FF),
    ("Misc technical", 0x2300, 0x23FF), ("Supplemental math", 0x2A00, 0x2AFF), ("Misc math A/B", 0x27C0, 0x27EF),
    ("Latin-1", 0x0080, 0x00FF), ("Latin extended A", 0x0100, 0x017F), ("ASCII", 0x0000, 0x007F),
    ("Latin extended D", 0xA720, 0xA7FF), ("Latin extended E", 0xAB30, 0xAB6F), ("Misc symbols and arrows", 0x2B00, 0x2BFF),
    ("Supplemental arrows A/B", 0x27F0, 0x297F), ("Misc technical", 0x2300, 0x23FF),
)


def block_of(ch):
    cp = ord(ch)
    for name, lo, hi in ACCEPTED_BLOCKS:
        if lo <= cp <= hi:
            return name, True
    for name, lo, hi in WATCH_BLOCKS:
        if lo <= cp <= hi:
            return name, False
    return "other U+%04X" % cp, False


def judge_census(census):
    """census: [[char, count], ...]. Returns (accepted, refused) as
    {block: (count, [chars])}."""
    accepted, refused = {}, {}
    for ch, c in census:
        name, ok = block_of(ch)
        bucket = accepted if ok else refused
        cnt, chars = bucket.get(name, (0, []))
        if len(chars) < 12:
            chars.append(ch)
        bucket[name] = (cnt + c, chars)
    return accepted, refused


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("pack")
    ap.add_argument("--sample", type=int, default=0, help="write N random articles as plain text")
    ap.add_argument("--plain", help="where the sample goes (markdown)")
    ap.add_argument("--chars", type=int, default=1500, help="characters of each sampled article")
    ap.add_argument("--json", help="write the report here too")
    ap.add_argument("--seed", type=int, default=20260911)
    ap.add_argument("--summary", help="a build's summary.json: judge its census of removed characters")
    ap.add_argument("--report", help="write the full detector report (markdown) here")
    ap.add_argument("--limit", type=int, default=0, help="scan only the first N articles")
    ap.add_argument("--workers", type=int, default=0, help="worker processes (default: cores minus two)")
    ap.add_argument("--every", type=int, default=1, help="scan every Nth article (the full pack)")
    args = ap.parse_args(argv)
    gate_failed = []
    if args.summary:
        with open(args.summary, encoding="utf-8") as f:
            summary = json.load(f)
        accepted, refused = judge_census(summary.get("removed_chars", []))
        print("removed characters, by block:")
        for name, (cnt, chars) in sorted(accepted.items(), key=lambda kv: -kv[1][0]):
            print(f"  ok       {name:28s} {cnt:10,}  {' '.join(chars)}")
        for name, (cnt, chars) in sorted(refused.items(), key=lambda kv: -kv[1][0]):
            print(f"  REFUSED  {name:28s} {cnt:10,}  {' '.join(chars)}   (must be drawn or spelled, not dropped)")
        if refused:
            gate_failed.append("characters that carry meaning were dropped")
        print(f"symbols spelled: {summary.get('symbols_translated', 0):,}; diacritics reduced to base letters: {summary.get('diacritics_dropped', 0):,}")
    report, sample = scan(args.pack, args.sample, args.seed, limit=args.limit, workers=args.workers or None, every=args.every)
    # Mario, 2026-09-11: at the end nothing that reads as an artifact may
    # remain, whoever left it. These classes must be empty for the gate.
    artifacts = {name: report["signatures"][name]["articles"] for name in ARTIFACT_DETECTORS if report["signatures"][name]["hits"]}
    if artifacts:
        print("ARTIFACTS STILL PRESENT: " + ", ".join("%s in %d articles" % kv for kv in sorted(artifacts.items(), key=lambda kv: -kv[1])))
        gate_failed.append("artifacts remain in %d classes" % len(artifacts))
    print(f"{report['articles']:,} articles; body chars median {report['body_chars']['median']:,}, "
          f"p10 {report['body_chars']['p10']:,}, p1 {report['body_chars']['p1']:,}; "
          f"near-empty (<{NEAR_EMPTY}): {report['near_empty_count']}")
    for name, r in sorted(report["signatures"].items(), key=lambda kv: -kv[1]["articles"]):
        if not r["hits"]:
            continue
        print(f"  {name:28s} {r['hits']:8,} hits in {r['articles']:7,} articles ({r['per_1000_articles']:6.1f} per 1000)")
        for title, ctx in r["examples"][:2]:
            print(f"      {title}: ...{ctx.replace(chr(10), ' ')[:150]}...")
    if args.report:
        write_report(report, args.report)
        print(f"report -> {args.report}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report, f, ensure_ascii=False, indent=1)
    if args.plain and sample:
        with open(args.plain, "w", encoding="utf-8") as f:
            f.write("# %d random articles, as the reader shows them (first %d characters each)\n\n" % (len(sample), args.chars))
            for title, t in sample:
                f.write("## " + title + "\n\n" + t[:args.chars] + ("\n\n[...]\n\n" if len(t) > args.chars else "\n\n"))
        print(f"sample of {len(sample)} articles -> {args.plain}")
    if gate_failed:
        print("QUALITY GATE: FAILED, " + " and ".join(gate_failed))
        return 1
    print("QUALITY GATE: passed (every removed character is in an accepted script)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
