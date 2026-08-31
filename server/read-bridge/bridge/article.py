"""Instapaper's text-view HTML -> the flat document the panel draws.

Freestanding on purpose: no FastAPI, no network, no store, so tests/
test_article.py drives every rule here without a server. This is the only
module in the service that touches user-controlled markup, and it is the one
whose output the device renders verbatim.

Three jobs, and the second and third are the ones that are easy to skip:

1. Flatten. Paragraphs separated by a blank line, list items prefixed "- ",
   <pre> keeping its own line breaks, headings on their own paragraph,
   images and figures dropped. The device pages one flat document; it has no
   notion of nesting and never will.

2. Fold typography to ASCII. A glyph outside the reading cut's subset draws
   as NOTHING AT ALL on this panel -- a Hacker News comment once read
   "(Ive turned off" because HN sent U+2019 -- and Instapaper's corpus is
   professional prose, which is to say it is full of curly quotes, en
   dashes and ellipses. The device folds too; doing it here as well means
   the bytes on the card are already safe and a future reader of that file
   is not depending on a second pass.

3. Judge whether the panel can show it at all. The fold cannot invent glyphs
   for a script the cut has no coverage of, so an article that is mostly
   outside Latin-1 is marked unrenderable and the queue says so on the row.
   This is the Hacker News readability gate's lesson rather than its rule:
   one verdict, delivered at the moment it is actually known, instead of a
   blank page that looks like a bug.
"""

import html
import re
import unicodedata
from html.parser import HTMLParser

# Everything inside these is thrown away, tag and content both. figure goes
# with them: its image cannot be drawn and a caption with no picture above it
# reads as a stray sentence.
SKIP_TAGS = {
    "script",
    "style",
    "noscript",
    "svg",
    "iframe",
    "video",
    "audio",
    "form",
    "figure",
    "aside",
    "nav",
    "head",
}

# Tags that end the current paragraph. Anything not listed is inline, which is
# the right default: Instapaper's text view is clean, and an unknown tag is far
# more likely to be a <span> than a block.
BLOCK_TAGS = {
    "p",
    "div",
    "h1",
    "h2",
    "h3",
    "h4",
    "h5",
    "h6",
    "li",
    "ul",
    "ol",
    "blockquote",
    "pre",
    "section",
    "article",
    "header",
    "footer",
    "table",
    "tr",
    "td",
    "th",
    "hr",
    "dl",
    "dt",
    "dd",
    "figcaption",
}

# Straight equivalents for the punctuation professional prose is written with.
# A table rather than a unicodedata call because these are opinions, not
# decompositions: an em dash becomes a double hyphen, which NFKD would never
# do. Unspaced, so "a --- b" cannot come out of "a -- b": flatten() has
# already squeezed the whitespace by the time this runs, and a fold that
# added its own spaces would double them.
FOLDS = {
    "‘": "'",
    "’": "'",
    "‚": "'",
    "‛": "'",
    "“": '"',
    "”": '"',
    "„": '"',
    "′": "'",
    "″": '"',
    "–": "-",
    "—": "--",
    "―": "--",
    "−": "-",
    "…": "...",
    " ": " ",
    " ": " ",
    " ": " ",
    " ": " ",
    " ": " ",
    " ": " ",
    "​": "",
    "‌": "",
    "‍": "",
    "﻿": "",
    "•": "-",
    "·": "-",
    "‐": "-",
    "‑": "-",
    "­": "",
    "⁄": "/",
    "«": '"',
    "»": '"',
    "‹": "'",
    "›": "'",
    "™": "(TM)",
    "®": "(R)",
    "←": "<-",
    "→": "->",
    "≤": "<=",
    "≥": ">=",
    "×": "x",
}

# Words a minute. Reading-time estimates are conventionally 200-250; the
# middle of that is close enough for a row that says "6 min" and precise
# enough that nobody has to be told what it assumes.
WORDS_PER_MINUTE = 220

# Below this, a "conversion" produced nothing worth sending. A real article
# that extracts to forty characters is a paywall, an error page, or a video
# post -- the same three failures the Hacker News gate catches, arriving here
# through a different door.
MIN_USEFUL_CHARS = 120

# Above this share of letters outside Latin-1, the reading cut has no glyphs
# and the page would draw blank. Deliberately generous: an English article
# quoting a few hanzi stays readable, and one written in Chinese does not.
MAX_EXOTIC_RATIO = 0.15


class _Flattener(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.blocks: list[str] = []
        self._buf: list[str] = []
        self._skip = 0
        self._pre = 0
        self._pending_prefix = ""

    # -- block bookkeeping
    def _flush(self):
        text = "".join(self._buf)
        self._buf = []
        if not self._pre:
            text = re.sub(r"[ \t\r\f\v]+", " ", text.replace("\n", " ")).strip()
        else:
            text = text.strip("\n")
        if text:
            self.blocks.append(text)

    def handle_starttag(self, tag, attrs):
        if tag in SKIP_TAGS:
            self._skip += 1
            return
        if self._skip:
            return
        if tag == "br":
            # A line break inside a paragraph, not a paragraph boundary. The
            # panel wraps, so a hard break only matters in <pre>.
            self._buf.append("\n" if self._pre else " ")
            return
        if tag in BLOCK_TAGS:
            self._flush()
            if tag == "pre":
                self._pre += 1
            if tag == "li":
                self._pending_prefix = "- "

    def handle_startendtag(self, tag, attrs):
        if tag == "br" and not self._skip:
            self._buf.append("\n" if self._pre else " ")

    def handle_endtag(self, tag):
        if tag in SKIP_TAGS:
            self._skip = max(0, self._skip - 1)
            return
        if self._skip:
            return
        if tag in BLOCK_TAGS:
            self._flush()
            if tag == "pre":
                self._pre = max(0, self._pre - 1)

    def handle_data(self, data):
        if self._skip:
            return
        if self._pending_prefix and data.strip():
            self._buf.append(self._pending_prefix)
            self._pending_prefix = ""
        self._buf.append(data)

    def close(self):
        super().close()
        self._flush()


def fold_typography(text: str) -> str:
    """Curly punctuation and exotic spaces -> what the reading cut can draw.

    Accented Latin letters are LEFT ALONE: the cut carries Latin-1, so
    "cafe" does not need to lose its accent, and stripping it would be a
    visible downgrade for every European name in the corpus."""
    out = []
    for ch in text:
        if ch in FOLDS:
            out.append(FOLDS[ch])
            continue
        if ch in "\n\t" or ch >= " ":
            out.append(ch)
        # Anything else is a control character; dropping it is the point.
    return "".join(out)


def flatten(markup: str) -> str:
    """Instapaper text-view HTML -> the flat document, folded and squeezed."""
    parser = _Flattener()
    try:
        parser.feed(markup)
        parser.close()
    except Exception:
        # html.parser is forgiving, but a truncated response can still raise.
        # Whatever it managed to collect before the fault is better than
        # nothing, and the caller's MIN_USEFUL_CHARS gate judges the result.
        pass
    text = "\n\n".join(parser.blocks)
    text = fold_typography(text)
    text = re.sub(r"[ \t]+\n", "\n", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


def exotic_ratio(text: str) -> float:
    """Share of the LETTERS that live outside Latin-1.

    Letters only: punctuation, digits and whitespace say nothing about which
    script this is, and counting them would let a long code block dilute a
    page of Japanese below the threshold."""
    letters = 0
    exotic = 0
    for ch in text:
        if not unicodedata.category(ch).startswith("L"):
            continue
        letters += 1
        if ord(ch) > 0xFF:
            exotic += 1
    if letters == 0:
        return 0.0
    return exotic / letters


def word_count(text: str) -> int:
    return len(text.split())


def reading_minutes(words: int) -> int:
    """At least one, so a two-paragraph link never claims to take no time."""
    return max(1, round(words / WORDS_PER_MINUTE))


def domain_of(url: str) -> str:
    """The host, without www. and without a scheme, for the queue's subtitle.

    Hand-rolled rather than urlparse because Instapaper hands back
    instapaper://private-content/... for email-in bookmarks, which urlparse
    parses into a netloc nobody wants to read."""
    if not url:
        return ""
    if url.startswith("instapaper://"):
        return "saved by email"
    text = re.sub(r"^[a-zA-Z][a-zA-Z0-9+.-]*://", "", url)
    host = text.split("/", 1)[0].split("?", 1)[0].split("@")[-1]
    host = host.split(":")[0]
    if host.startswith("www."):
        host = host[4:]
    return host.lower()


class Unconvertible(Exception):
    """The HTML produced nothing a person could read. Carries the sentence
    the device shows, because every refusal in this stack is a sentence."""


def convert(markup: str) -> dict:
    """-> {text, words, minutes, renderable}. Raises Unconvertible."""
    text = flatten(markup)
    if len(text) < MIN_USEFUL_CHARS:
        raise Unconvertible("Instapaper had no readable text for this one.")
    ratio = exotic_ratio(text)
    return {
        "text": text,
        "words": word_count(text),
        "minutes": reading_minutes(word_count(text)),
        "renderable": ratio <= MAX_EXOTIC_RATIO,
    }


def clean_title(title: str) -> str:
    """Titles arrive HTML-escaped often enough to matter, and they land in a
    tab-separated index on the card, so a stray tab is not cosmetic."""
    return fold_typography(html.unescape(title or "")).replace("\t", " ").strip()
