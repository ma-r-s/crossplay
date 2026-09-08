#!/usr/bin/env python3
"""Which writing system a character belongs to, and what that costs to draw.

One copy, imported by anki_to_deck.py and check_deck.py, because the two used
to carry the same `is_cjk` and the same hole in it.

The question this answers is not linguistic. It is: **does this character
need a face the deck has to ship, and is it drawn at full width?** Those two
together decide which glyph file it lands in, which font intervals get built,
and whether the wrap may break beside it.

The hole it exists to close: `is_cjk` tested 0x2E80-0x9FFF, 0xF900-0xFAFF and
0xFF00-0xFFEF. Hangul syllables live at 0xAC00-0xD7A3 and are in none of
them, so every Korean character was classified as Latin -- which routed it to
the built-in serif, which has 1070 glyphs and no Hangul. A Korean deck
converted with no error, no warning and no readable card. The same range
missed CJK Extension A (0x3400-0x4DBF), where a few thousand rare hanzi and
kanji live.
"""

# (first, last, script). Ordered so the common case is found first: a
# Japanese sentence is mostly kana and CJK Unified.
_RANGES = (
    (0x3040, 0x309F, "kana"),  # hiragana
    (0x30A0, 0x30FF, "kana"),  # katakana
    (0x31F0, 0x31FF, "kana"),  # katakana phonetic extensions
    (0x4E00, 0x9FFF, "han"),  # CJK Unified Ideographs
    (0x3400, 0x4DBF, "han"),  # Extension A: rare, and real
    (0xF900, 0xFAFF, "han"),  # compatibility ideographs
    (0x2E80, 0x2EFF, "han"),  # radicals supplement
    (0x2F00, 0x2FDF, "han"),  # Kangxi radicals
    (0x20000, 0x2FA1F, "han"),  # Extensions B..F and the compat supplement
    (0xAC00, 0xD7A3, "hangul"),  # syllables: what Korean is actually written in
    (0x1100, 0x11FF, "hangul"),  # jamo
    (0x3130, 0x318F, "hangul"),  # compatibility jamo
    (0xA960, 0xA97F, "hangul"),  # jamo extended-A
    (0xD7B0, 0xD7FF, "hangul"),  # jamo extended-B
    # Shared CJK punctuation and the fullwidth forms. Called "han" because
    # what matters here is which face draws them, and the answer is the same
    # face that draws the characters around them -- the built-in serif has no
    # fullwidth comma.
    (0x3000, 0x303F, "han"),
    (0xFF00, 0xFFEF, "han"),
)

# The scripts this app can actually render, given a deck that ships a face.
# Everything else converts and then draws as nothing, so setup warns rather
# than pretending.
SUPPORTED = frozenset({"latin", "han", "kana", "hangul"})

# Scripts drawn at full width, one em per character, written without spaces.
# This is the set the line wrap may break beside -- Hangul is deliberately
# NOT in it: Korean is written with spaces between words, and breaking
# between syllables would split words that a space rule keeps whole.
WIDE = frozenset({"han", "kana"})


# The extra ranges the stock converter's "latin-ext" font interval builds a
# glyph for, beyond plain ASCII/Latin-1: Latin Extended-A/B, spacing
# modifiers, Latin Extended Additional, General Punctuation, and the Latin
# ligatures. Mirrored from INTERVAL_PRESETS["latin-ext"] in
# lib/EpdFont/scripts/fontconvert_sdcard.py rather than imported from it,
# because scripts.py ships standalone into the browser installer's tools.zip
# and that file does not.
#
# General Punctuation (0x2000-0x206F) is the one that matters most: it is
# curly quotes, en/em dashes, the ellipsis -- ordinary English typography,
# not a foreign script. Before this existed, a curly quote in an otherwise
# plain English sentence was classified the same as Greek or Cyrillic and
# reported as "a script the reader has no face for", when the built-in serif
# draws it without trouble.
_LATIN_EXT_RANGES = (
    (0x0100, 0x024F),
    (0x02B0, 0x02FF),
    (0x1E00, 0x1EFF),
    (0x2000, 0x206F),
    (0xFB00, 0xFB06),
)


def script_of(ch):
    """The script of one character: latin, han, kana, hangul, or other."""
    o = ord(ch)
    if o < 0x0370:
        return "latin"
    for first, last, name in _RANGES:
        if first <= o <= last:
            return name
    for first, last in _LATIN_EXT_RANGES:
        if first <= o <= last:
            return "latin"
    # Greek, Cyrillic, Arabic, Hebrew, Devanagari and the rest. Not "latin",
    # because the built-in serif cannot draw them either and calling them
    # Latin is how they used to reach it silently.
    if 0x0370 <= o <= 0x2DFF or 0xA000 <= o <= 0xABFF:
        return "other"
    return "latin"


def is_wide(ch):
    """Is this character drawn full width, and breakable on either side?"""
    return script_of(ch) in WIDE


def scripts_in(texts):
    """The set of scripts appearing across an iterable of strings."""
    found = set()
    for text in texts:
        for ch in text:
            if ch >= " ":
                found.add(script_of(ch))
    return found


# Font intervals, in the stock converter's own preset names, for each script.
# Requested per deck rather than always: `hangul` alone is 11184 codepoints,
# and a codepoint in range but absent from the subset still costs an empty
# 16-byte record on the card. A Chinese deck should not pay for Korean.
INTERVALS_FOR = {
    "latin": ("latin-ext", "punctuation"),
    "other": ("latin-ext", "punctuation"),
    "han": ("cjk", "(0x2E80-0x2FDF)", "(0x3400-0x4DBF)"),
    "kana": ("cjk",),
    "hangul": ("hangul",),
}


def intervals_for(scripts):
    """The --intervals string a deck using these scripts needs.

    Latin is always in, because the device draws a headword in the deck's own
    face whatever script it is: a face with no ASCII draws nothing at all for
    "T恤" or for a deck of English words, silently, since a missing glyph is
    simply not painted.
    """
    wanted = ["latin-ext", "punctuation"]
    for script in sorted(scripts):
        for name in INTERVALS_FOR.get(script, ()):
            if name not in wanted:
                wanted.append(name)
    return ",".join(wanted)


# --- Japanese ruby ----------------------------------------------------------
#
# Anki writes furigana as a space, the base text, then the reading in square
# brackets: " 漢字[かんじ]". The Japanese Support add-on generates whole
# fields in that form ("私[わたし]は 学生[がくせい]です"), and three template
# filters read them: {{furigana:}} draws the ruby, {{kanji:}} shows the base
# alone, {{kana:}} shows the readings alone.
#
# The leading space is part of the syntax and not part of the text: it is how
# Anki knows where the base begins, because a base is not delimited any other
# way. It is eaten, except where it separates two words that had a space
# between them anyway.

import re as _re

RUBY_RE = _re.compile(r" ?([^ \[\]]+?)\[([^\]]+?)\]")

# How a ruby segment is carried from the converter to the device:
#
#     RUBY_START  base  RUBY_SEP  reading  RUBY_END
#
# Three markers rather than two, so a left-to-right parser knows a segment is
# coming BEFORE it reads the base. With only a separator and a terminator the
# device would have to scan ahead at every character to find out whether the
# run it is in is a base or ordinary text, which is exactly the work the wrap
# is trying not to do per glyph.
#
# Control characters, not brackets, and that is load-bearing. The obvious
# encoding is to leave Anki's own "漢字[かんじ]" in the field and parse it on
# the device -- and it collides head-on with cloze, whose question face is
# text with "[...]" in it: a hole would be read as a reading and the answer
# printed above the gap hiding it. None of these three can occur in a cleaned
# field, because clean() drops every character below space except the newline.
RUBY_START = "\x1e"
RUBY_SEP = "\x1f"
RUBY_END = "\x1d"


def _is_reading(text):
    """Does this bracketed run look like a furigana reading?

    It must contain kana. Anki's own regex does not ask, and cannot afford
    to -- it runs only where a template said {{furigana:}}, so the field is
    known to be Japanese. Here there is no template, so the question has to be
    answered from the text, and this is the answer that separates the three
    things that all look like "something[something]":

      -- 漢字[かんじ]   a reading, and kana is what makes it one
      -- see also[1]    a footnote in an English deck
      -- The capital is [...]   a CLOZE HOLE, which is the collision that
         matters most: reading a hole as a reading would print the answer
         above the gap it is hiding.
    """
    return any(script_of(ch) == "kana" for ch in text)


def ruby_segments(text):
    """Split a field into (base, reading) pairs; reading is "" for plain text.

    Consecutive plain text is returned as one segment, so the caller can treat
    the result as a layout without re-joining anything. The leading space is
    consumed with the match, as Anki consumes it: it is how Anki knows where
    the base begins, not a space in the sentence.
    """
    out = []
    last = 0
    for match in RUBY_RE.finditer(text):
        if not _is_reading(match.group(2)):
            continue
        plain = text[last : match.start()]
        if plain:
            out.append((plain, ""))
        out.append((match.group(1), match.group(2)))
        last = match.end()
    if text[last:]:
        out.append((text[last:], ""))
    return out


def has_ruby(text):
    return any(reading for _base, reading in ruby_segments(text))


def ruby_base(text):
    """Anki's {{kanji:}}: the text with the readings removed."""
    return "".join(base for base, _reading in ruby_segments(text))


def ruby_reading(text):
    """Anki's {{kana:}}: the readings, with the bases removed.

    Plain runs stay -- they are the kana and particles that were never
    bracketed, and dropping them would turn "私[わたし]は" into "わたし"
    rather than "わたしは".
    """
    return "".join(reading or base for base, reading in ruby_segments(text))


def ruby_encode(text):
    """The on-card form: base, separator, reading, terminator, per segment."""
    out = []
    for base, reading in ruby_segments(text):
        if reading:
            out.append(RUBY_START + base + RUBY_SEP + reading + RUBY_END)
        else:
            out.append(base)
    return "".join(out)


def decode_ruby(text):
    """Undo ruby_encode: (base, reading) pairs, reading "" for plain runs.

    The device's forEachRubySegment in StudyText.h, in Python. Anything
    malformed is plain text rather than an error, exactly as there: this is
    drawn on a card, and a stray control byte should cost a reading, not a
    sentence.
    """
    out = []
    i = 0
    plain = []
    while i < len(text):
        if text[i] != RUBY_START:
            plain.append(text[i])
            i += 1
            continue
        sep = text.find(RUBY_SEP, i + 1)
        end = text.find(RUBY_END, sep + 1) if sep >= 0 else -1
        nxt = text.find(RUBY_START, i + 1)
        if sep < 0 or end < 0 or (nxt >= 0 and nxt < end):
            plain.append(text[i])
            i += 1
            continue
        if plain:
            out.append(("".join(plain), ""))
            plain = []
        out.append((text[i + 1 : sep], text[sep + 1 : end]))
        i = end + 1
    if plain:
        out.append(("".join(plain), ""))
    return out


def visible_text(encoded):
    """What a reader sees, and what the device counts codepoints over: the
    bases, without the readings or the markers."""
    return "".join(base for base, _reading in decode_ruby(encoded))


def reading_text(encoded):
    """Just the readings, for the glyph set the ruby-size face is built from."""
    return "".join(reading for _base, reading in decode_ruby(encoded))
