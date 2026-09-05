#!/usr/bin/env python3
"""Pin the cloze rules the converter has to match Anki on.

Anki's {{cloze:}} is three rules and every one of them is a way to leak the
answer if it is got wrong:

  1. On the question face THIS card's holes become [...] -- or the hint, when
     the note wrote one -- and every OTHER ordinal is shown filled in, because
     those are context this card is not testing.
  2. On the answer face every hole is filled and this card's is marked.
  3. One card per ordinal PRESENT in the text. A card whose ordinal is gone
     is what Anki calls an empty card, and showing it means showing a question
     with no hole in it, which is a card that answers itself.

Standard library only, and no Anki: the rules under test are ours, and a test
that needs a venv is a test that gets skipped. Run directly:

    python3 tools_local/study/test_cloze.py
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

print(f"{'PASS' if failures == 0 else 'FAIL'} {checks} checks, {failures} failed")
sys.exit(1 if failures else 0)
