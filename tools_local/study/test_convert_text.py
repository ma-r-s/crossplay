#!/usr/bin/env python3
"""Pin the text rules the converter has to match Anki on.

Two families, both about what reaches the card face.

Anki's {{cloze:}} is three rules and every one of them is a way to leak the
answer if it is got wrong:

  1. On the question face THIS card's holes become [...] -- or the hint, when
     the note wrote one -- and every OTHER ordinal is shown filled in, because
     those are context this card is not testing.
  2. On the answer face every hole is filled and this card's is marked.
  3. One card per ordinal PRESENT in the text. A card whose ordinal is gone
     is what Anki calls an empty card, and showing it means showing a question
     with no hole in it, which is a card that answers itself.

The second is what survives an Anki field's HTML: an Anki field is HTML, its
structure is usually the point, and flattening a four-item list to one grey
line is a card the user cannot read even though nothing failed.

Standard library only, and no Anki: the rules under test are ours, and a test
that needs a venv is a test that gets skipped. Run directly:

    python3 tools_local/study/test_convert_text.py
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import anki_to_deck as conv  # noqa: E402

failures = 0
checks = 0


def check(condition, label):
    global failures, checks
    checks += 1
    if not condition:
        failures += 1
        print(f"  FAIL: {label}")


def faces(text, ordinal):
    """(question, answer) for one card, or None when the card is empty."""
    rendered = conv.render_cloze(text, ordinal)
    if rendered is None:
        return None
    return rendered[0], rendered[1]


def span(text, ordinal):
    """The marked run of the answer, as the device would underline it."""
    rendered = conv.render_cloze(text, ordinal)
    if rendered is None:
        return None
    _question, answer, offset, length = rendered
    return answer[offset : offset + length]


# --- rule 1: the hole, and only this card's hole -----------------------------

q, a = faces("The capital of France is {{c1::Paris}}.", 1)
check(q == "The capital of France is [...].", "a plain hole becomes [...]")
check(a == "The capital of France is Paris.", "the answer fills the hole")
check(span("The capital of France is {{c1::Paris}}.", 1) == "Paris", "the answer marks the hole")

q, _ = faces("Mitochondria are the {{c1::powerhouse::organelle}} of the cell.", 1)
check(q == "Mitochondria are the [organelle] of the cell.", "a hint replaces the ellipsis")

text = "{{c1::Rome}} was founded in {{c2::753 BC}}."
q1, a1 = faces(text, 1)
q2, a2 = faces(text, 2)
check(q1 == "[...] was founded in 753 BC.", "card 1 hides only c1")
check(q2 == "Rome was founded in [...].", "card 2 hides only c2")
check(a1 == a2 == "Rome was founded in 753 BC.", "both cards answer with the whole text")
check(span(text, 2) == "753 BC", "the mark follows the card, not the text")

# Two holes sharing one ordinal are ONE card with two holes -- Anki's own
# behaviour, and the case a naive "first match wins" loop gets wrong by
# revealing the second.
q, a = faces("{{c1::Berlin}} is the capital of {{c1::Germany}}.", 1)
check(q == "[...] is the capital of [...].", "one ordinal, two holes, both hidden")
check(a == "Berlin is the capital of Germany.", "both are filled on the answer")
check(span("{{c1::Berlin}} is the capital of {{c1::Germany}}.", 1) == "Berlin",
      "the mark lands on the first of the shared holes")

# --- rule 3: empty cards -----------------------------------------------------

check(faces("Only {{c1::one}} hole remains here.", 2) is None, "an absent ordinal is an empty card")
check(faces("No cloze markup at all.", 1) is None, "text with no markup makes no card")
check(faces("{{c10::ten}} and {{c1::one}}", 10) is not None, "a two-digit ordinal is found")
check(faces("{{c10::ten}} and {{c1::one}}", 1)[0] == "ten and [...]", "c1 is not matched by c10")

# --- markup around the markup ------------------------------------------------

q, a = faces("A <b>bold</b> {{c1::answer}}&nbsp;here.", 1)
check(q == "A bold [...] here.", "HTML is stripped around the hole")
check(a == "A bold answer here.", "and around the filled hole")

q, _ = faces("Tagged {{c1::<i>italic</i>}} inside.", 1)
check(q == "Tagged [...] inside.", "HTML inside the hole does not break the hole")
check(span("Tagged {{c1::<i>italic</i>}} inside.", 1) == "italic", "the mark skips the stripped tags")

check(faces("Audio [sound:a.mp3] and {{c1::text}}.", 1)[0] == "Audio and [...].",
      "a sound tag is dropped, not read aloud as brackets")

# The offset is in CODEPOINTS, which is what the renderer counts. A byte
# offset would put the underline several characters early on any deck that is
# not pure ASCII -- which is most of the decks cloze is used for.
marked = span("中文句子：{{c1::北京}}。", 1)
check(marked == "北京", "the span is measured in codepoints, not bytes")

# --- the deck-level rules ----------------------------------------------------

check(conv.cloze_field_index(["plain", "has {{c1::markup}}"]) == 1, "the cloze field is the one with markup")
check(conv.cloze_field_index(["none", "here"]) == -1, "no markup, no cloze field")
check(conv.cloze_extra_index(["{{c1::a}}", "extra text"], 0, ["Text", "Back Extra"]) == 1,
      "Back Extra is claimed by name")
check(conv.cloze_extra_index(["{{c1::a}}", "", "tail"], 0, ["Text", "Back Extra", "Source"]) == 2,
      "an empty Back Extra does not win over a filled field")

# --- what clean() keeps and what it drops ------------------------------------
#
# An Anki field is HTML, and its structure is usually the point: a list of
# four things, a Back Extra of two paragraphs. Flattening all of it to spaces
# is what this did for its first year, and it is worst on exactly the note
# types cloze brings in.

check(conv.clean("<div>one</div><div>two</div>") == "one\ntwo", "a div is a line")
check(conv.clean("a<br>b") == "a\nb", "a br is a line")
check(conv.clean("<p>a</p><p>b</p>") == "a\nb", "so is a paragraph")
check(conv.clean("<ul><li>x</li><li>y</li></ul>") == "\u2022 x\n\u2022 y",
      "a list item is a line and a bullet")
check(conv.clean("<table><tr><td>a</td><td>b</td></tr></table>") == "a b",
      "table cells do not run together into one word")

# Entities. A deck written in the Anki editor is full of these, and every one
# that survived reached the card as literal ampersand-r-s-q-u-o.
check(conv.clean("It&rsquo;s &#39;fine&#39; &amp; good") == "It's 'fine' & good",
      "named and numeric entities are both decoded")
check(conv.clean("a&nbsp;b") == "a b", "a non-breaking space is a space")

# Things there is no renderer for. The delimiters go; the content stays,
# because a formula whose source you can read beats a card showing "[latex]"
# and beats a card showing nothing.
check(conv.clean("[latex]E=mc^2[/latex]") == "E=mc^2", "latex delimiters are dropped")
check(conv.clean("[$]x^2[/$]") == "x^2", "so are the short latex forms")
check(conv.clean("x \\(a+b\\) y") == "x a+b y", "and MathJax's")
check(conv.clean("[anki:tts lang=en_US]spoken[/anki:tts]") == "spoken",
      "a tts tag goes and the words it would have spoken stay")
check(conv.clean("word [sound:a.mp3]") == "word", "a sound tag goes entirely: there is no speaker")

# Blank lines. The editor emits <div><br></div> for an empty line and a field
# can carry a dozen; on a 480px panel that is the whole card.
check(conv.clean("<div><br></div><div><br></div>real") == "real", "runs of blank lines collapse")
check(conv.clean("  spaced   out  ") == "spaced out", "horizontal space still collapses")
check(conv.clean("a\n\n\nb") == "a\nb", "and so do bare blank lines")

# The emphasis span is located in the CLEANED text, so cleaning that moved
# characters around would move the underline.
_full, off, length = conv.clean_sentence("The <b>quick</b> fox&nbsp;ran.")
check(_full == "The quick fox ran.", "a sentence cleans as one line")
check(_full[off : off + length] == "quick", "and its emphasis still points at the right word")

# --- the record budget -------------------------------------------------------
#
# A cloze card stores its text twice, so it is the note kind that reaches
# study::kMaxNoteBytes first. Over the limit the device refuses the record and
# the card silently is not there; the converter trims instead.
long_text = "word " * 400
note = conv.make_note(1, "Cloze", ["", "", "", "", long_text, "", "", long_text],
                      (0, 0), "", 0, 0, 0, 0, 0, 0, 0, -1)
trimmed = conv.enforce_note_budget([note])
check(trimmed > 0, "an oversized note is trimmed")
size = sum(len(f.encode("utf-8")) for f in note["fields"]) + 3 * len(conv.DEVICE_FIELDS) + 3
check(size <= conv.MAX_NOTE_BYTES, "and ends up inside the device's budget")
check(note["fields"][4].endswith("…"), "with a marker saying it was cut")
check(conv.trim_to_bytes("中文字", 5) == "…" or
      conv.trim_to_bytes("中文字", 6).endswith("…"),
      "a multi-byte string is cut on a codepoint boundary")

# --- scripts -----------------------------------------------------------------
#
# The classification decides which glyph file a character lands in, which
# decides which face gets built for it. Hangul is the reason this is tested:
# it is in no CJK range, so the old predicate called it Latin and routed every
# Korean deck to the built-in serif -- 1070 glyphs, no Hangul, no error and no
# readable card.

import scripts  # noqa: E402

check(scripts.script_of("a") == "latin", "ASCII is latin")
check(scripts.script_of("漢") == "han", "a hanzi is han")
check(scripts.script_of("々") == "han", "and so is the repeat mark")
check(scripts.script_of("か") == "kana", "hiragana is kana")
check(scripts.script_of("カ") == "kana", "and so is katakana")
check(scripts.script_of("한") == "hangul", "a Hangul syllable is hangul, not latin")
check(scripts.script_of("ㄱ") == "hangul", "and so is a compatibility jamo")
check(scripts.script_of("в") == "other", "Cyrillic is neither, and is not called latin")
check(scripts.script_of("\u4e00") == "han" and scripts.script_of("\u3400") == "han",
      "CJK Extension A is han too")

# Wide means full width AND written without spaces, which together is what
# lets the wrap break beside a character. Korean has spaces, so it is not
# wide: breaking between syllables would split words the space rule keeps.
check(scripts.is_wide("漢") and scripts.is_wide("か"), "han and kana are wide")
check(not scripts.is_wide("한"), "hangul is not, because Korean is written with spaces")
check(not scripts.is_wide("a"), "and neither is Latin")

check(scripts.scripts_in(["학생", "abc"]) == {"hangul", "latin"}, "a deck's scripts are collected")
check("hangul" in scripts.intervals_for({"hangul"}), "a Korean deck asks for the hangul interval")
check("hangul" not in scripts.intervals_for({"han"}), "and a Chinese deck does not pay for it")
check(scripts.intervals_for({"latin"}).startswith("latin-ext"),
      "latin-ext is always in: the deck's own face draws the headword whatever script it is")

# --- furigana ----------------------------------------------------------------
#
# Anki writes a reading as " 漢字[かんじ]" and reads it three ways:
# {{furigana:}} draws it as ruby, {{kanji:}} shows the base, {{kana:}} the
# reading. All three have to agree with Anki, because the deck was authored
# against Anki's answer.

JP = "私[わたし]は 学生[がくせい]です"
check(scripts.ruby_base(JP) == "私は学生です", "kanji: drops the readings AND the syntax space")
check(scripts.ruby_reading(JP) == "わたしはがくせいです", "kana: keeps the particles between readings")
check(scripts.has_ruby(JP), "and the field is recognised as carrying furigana")
check(scripts.ruby_segments(JP) == [("私", "わたし"), ("は", ""), ("学生", "がくせい"), ("です", "")],
      "the segmentation is base/reading pairs with the plain runs between")

# The encoding round-trips, and what the device counts codepoints over is the
# base text -- an emphasis span counted over the encoded form would underline
# however many kana too far along.
encoded = scripts.ruby_encode(JP)
check(scripts.visible_text(encoded) == scripts.ruby_base(JP), "encode then decode gives the base back")
check(scripts.reading_text(encoded) == "わたしがくせい", "and the readings, for the ruby-size face")
check(scripts.decode_ruby(encoded) == scripts.ruby_segments(JP), "the decoder mirrors the segmenter")

# What must NOT be read as furigana. The reading has to contain kana, and
# that one rule settles all three of these.
for not_ruby in (
    "see also[1]",  # a footnote in an English deck
    "The capital is [...].",  # A CLOZE HOLE. Reading this as a reading would
    "Mitochondria are the [organelle] of it.",  # print the answer above the gap.
    "plain text",
):
    check(not scripts.has_ruby(not_ruby), f"not furigana: {not_ruby!r}")
    check(scripts.ruby_encode(not_ruby) == not_ruby, f"and left alone: {not_ruby!r}")

check(scripts.decode_ruby("a\x1eb") == [("a\x1eb", "")],
      "a stray marker is text, not an error: a card should lose a reading, never a sentence")

# --- where a picture lives ---------------------------------------------------
#
# This looked in field 18 and nowhere else, because that is where the HSK note
# type keeps SentenceImage -- so every other deck converted with no pictures
# at all and nothing said why.

import make_images  # noqa: E402

check(make_images.first_image(["a", '<img src="x.png">', "b"]) == "x.png",
      "a picture is found in whichever field holds it")
check(make_images.first_image(["<img class=q src='y.jpg' alt=1>"]) == "y.jpg",
      "single quotes are quotes too")
check(make_images.first_image(["<img src=w.gif>"]) == "w.gif", "and so is no quote at all")
check(make_images.first_image(['<img src="one.png"> <img src="two.png">']) == "one.png",
      "the first picture in field order wins")
check(make_images.first_image(["no image here", ""]) is None, "a note with no picture has none")
check(make_images.first_image([]) is None, "and neither does a note with no fields")

print(f"{'PASS' if failures == 0 else 'FAIL'} {checks} checks, {failures} failed")
sys.exit(1 if failures else 0)
