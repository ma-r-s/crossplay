#!/usr/bin/env python3
"""Build .apkg fixtures with Anki's own exporter, so they are real.

A hand-built zip would test our guess about the format; an export made by the
anki library tests the format itself, and breaks the moment Anki changes it,
which is exactly when we want to hear about it.

    .venv-study/bin/python tools_local/study/make_fixture_apkg.py --out DIR

writes two packages into DIR:

  sat-vocabulary.apkg   modern format (version 3): an English Basic deck with
                        some cards carrying review state, one media image, and
                        one cloze note (which the converter must skip)
  legacy.apkg           the "support older Anki versions" format the pipeline
                        refuses, for testing that the refusal is polite

The modern one doubles as the site's sample deck: the installer page offers
it to anyone who wants to see the flow before exporting their own.
"""

import argparse
import pathlib
import sys
import tempfile
import time

WORDS = [
    ("abate", "to lessen in intensity or degree"),
    ("aberrant", "deviating from the norm"),
    ("alacrity", "eager and enthusiastic willingness"),
    ("anomaly", "deviation from what is standard or expected"),
    ("approbation", "praise or approval"),
    ("arduous", "requiring great effort; difficult"),
    ("assuage", "to make something unpleasant less severe"),
    ("audacious", "recklessly bold or daring"),
    ("avarice", "extreme greed for wealth"),
    ("banal", "so lacking in originality as to be obvious"),
    ("candor", "the quality of being open and honest"),
    ("capricious", "given to sudden changes of mood or behavior"),
    ("cogent", "clear, logical, and convincing"),
    ("copious", "abundant in supply or quantity"),
    ("corroborate", "to confirm or give support to"),
    ("deleterious", "causing harm or damage"),
    ("ephemeral", "lasting for a very short time"),
    ("equivocate", "to use ambiguous language to conceal the truth"),
    ("garrulous", "excessively talkative"),
    ("gregarious", "fond of company; sociable"),
    ("iconoclast", "a person who attacks cherished beliefs"),
    ("incontrovertible", "not able to be denied or disputed"),
    ("laconic", "using very few words"),
    ("mercurial", "subject to sudden changes of mood"),
    ("obdurate", "stubbornly refusing to change one's opinion"),
    ("perfidious", "deceitful and untrustworthy"),
    ("quotidian", "occurring every day; ordinary"),
    ("recalcitrant", "obstinately uncooperative"),
    ("sycophant", "a person who flatters someone important"),
    ("ubiquitous", "present or found everywhere"),
]


def tiny_png(path):
    """A small grey-gradient PNG, made with Pillow so it is a real image."""
    from PIL import Image

    image = Image.new("L", (64, 48))
    image.putdata([(x * 4 + y * 3) % 256 for y in range(48) for x in range(64)])
    image.save(path)


def build_collection(base_dir):
    from anki.collection import Collection

    col = Collection(str(base_dir / "collection.anki2"))
    deck_id = col.decks.id("SAT Vocabulary")

    basic = col.models.by_name("Basic")
    image_path = base_dir / "sample.png"
    tiny_png(image_path)
    media_name = col.media.add_file(str(image_path))

    for index, (word, meaning) in enumerate(WORDS):
        note = col.new_note(basic)
        note.fields[0] = word
        # One note carries an image, so media extraction has something to find.
        extra = f' <img src="{media_name}">' if index == 0 else ""
        note.fields[1] = meaning + extra
        col.add_note(note, deck_id)

    cloze = col.models.by_name("Cloze")
    note = col.new_note(cloze)
    note.fields[0] = "A word that means everywhere at once: {{c1::ubiquitous}}"
    col.add_note(note, deck_id)

    # A note type shaped like Barron's SAT list: the second field is the part
    # of speech, not the meaning, and the field names say so. The positional
    # guess once put "V." on the answer face of every such card; the fixture
    # keeps that bug dead.
    vocab = col.models.new("Vocabulary")
    for field_name in ("Word", "Part of Speech", "Definition", "Sentence"):
        col.models.add_field(vocab, col.models.new_field(field_name))
    template = col.models.new_template("Card 1")
    template["qfmt"] = "{{Word}}"
    template["afmt"] = "{{FrontSide}}<hr>{{Definition}}"
    col.models.add_template(vocab, template)
    col.models.add(vocab)
    vocab = col.models.by_name("Vocabulary")
    for word, pos, definition, sentence in (
        ("abase", "V.", "lower; humiliate", "He refused to abase himself."),
        ("lucid", "ADJ.", "clear and easy to understand", "A lucid answer."),
        ("zeal", "N.", "eager enthusiasm", "Her zeal was obvious."),
    ):
        note = col.new_note(vocab)
        note.fields[0] = word
        note.fields[1] = pos
        note.fields[2] = definition
        note.fields[3] = sentence
        col.add_note(note, deck_id)

    # Give the first ten cards real review state, written the way Anki stores
    # it: type/queue 2, a due day, an interval, and FSRS memory in `data`.
    # Ten revlog rows go with them so scheduling_summary sees reviews.
    today = int((time.time() - col.crt) // 86400)
    card_ids = col.db.list("select id from cards order by id limit 10")
    now_ms = int(time.time() * 1000)
    for i, card_id in enumerate(card_ids):
        interval = 3 + i * 4
        col.db.execute(
            "update cards set type=2, queue=2, due=?, ivl=?, factor=2500,"
            " reps=?, lapses=0, data=? where id=?",
            today + (i % 5) + 1,
            interval,
            2 + i % 3,
            '{"s":%.2f,"d":%.2f,"dr":0.9}' % (4.0 + i * 2.5, 5.5 - i * 0.3),
            card_id,
        )
        col.db.execute(
            "insert into revlog (id, cid, usn, ease, ivl, lastIvl, factor,"
            " time, type) values (?,?,?,?,?,?,?,?,?)",
            now_ms - (10 - i) * 86400000,
            card_id,
            -1,
            3,
            interval,
            max(1, interval - 4),
            2500,
            4500 + i * 300,
            1,
        )
    col.save()
    return col, deck_id


def export(col, deck_id, out_path, legacy):
    from anki.collection import DeckIdLimit, ExportAnkiPackageOptions

    return col.export_anki_package(
        out_path=str(out_path),
        options=ExportAnkiPackageOptions(
            with_scheduling=True,
            with_deck_configs=True,
            with_media=True,
            legacy=legacy,
        ),
        limit=DeckIdLimit(deck_id),
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        col, deck_id = build_collection(pathlib.Path(tmp))
        modern = args.out / "sat-vocabulary.apkg"
        legacy = args.out / "legacy.apkg"
        exported = export(col, deck_id, modern, legacy=False)
        export(col, deck_id, legacy, legacy=True)
        col.close()

    print(f"wrote {modern} ({exported} cards) and {legacy.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
