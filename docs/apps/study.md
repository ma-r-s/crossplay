# Study: your Anki decks on the reader

Study is a flashcard app that behaves like a real Anki client. It reads a deck
converted from your own Anki collection, schedules reviews with the same FSRS
algorithm and the same parameters Anki uses, and syncs every review back so
Anki (and AnkiWeb) treat them exactly as if you had done them on your phone.

No editing, no audio. Everything else -- adding cards, retraining FSRS,
browsing -- happens in Anki, where those things are good. The card can hold
several decks; the deck screen switches between them.

## The no-terminal way

The site has an installer page: **[crossplay.ma-r-s.com/study](https://crossplay.ma-r-s.com/study/)**.
Export your deck from Anki (`File > Export`, .apkg, scheduling included),
drop it on the page, see your own cards running on the emulated device, and
write the deck to the SD card -- then, after studying, the same page replays
your reviews back into your Anki collection. Everything happens in the tab;
nothing is uploaded anywhere. The page runs the exact tools documented
below, so the two routes cannot disagree; the command line remains the power
tool, and the only route that pushes to AnkiWeb itself.

## What you need (the command-line way)

- **The tools.** They live in this repository, not in the firmware: clone it
  (or download it as a zip from the same place you got the firmware) and run
  everything below from the repository root. Nothing needs to be installed
  from it -- the scripts run in place.
- Anki installed on this computer, with the deck you want to study. A deck
  downloaded from AnkiWeb's shared decks counts: import it into Anki first,
  and it converts like any other.
- The reader's SD card, mounted (or any directory, for a look around).
- Python 3. Everything else installs itself into a private environment
  (`.venv-study/`) the first time it is needed; nothing touches your system
  Python.

## First time: put a deck on the card

```bash
./tools_local/study/study.py setup
```

That is the whole command. It finds your Anki collection, lists your decks,
finds the SD card, and asks one question for each. Then it converts the deck,
builds fonts if the deck needs them, and packs any sentence images. Answers are
remembered in `~/.config/crosspoint-study.json`, so the next run asks nothing.

Put the card in the reader: **Apps > STUDY**.

Run setup again to put another deck on the card -- adding alongside is the
default, replacing is offered. The browser installer at /study/ does the same:
it writes `/study/<slug>/` per deck and only replaces a deck of the same name.
On the reader, a third door on the deck screen, `CHANGE DECK`, cycles between
them and shows the position (`2 OF 3`); the reader remembers which one you had
open.
Each deck keeps its own scheduling state, review log, stats and fonts.

Every prompt has a flag (`--collection`, `--deck`, `--to`, `--name`, `--add`,
`--replace`), so the command is scriptable once you know your answers.

### Which decks convert

Any deck whose note type the converter understands, which is nearly all of
them:

- **Stock "Basic"** (and "Basic (and reversed card)", where each direction
  becomes its own card): first field is the word, second is the meaning.
- **Any homemade note type**: same rule, first field and second field. The
  converter prints the mapping it chose; if it guessed wrong, name the fields
  yourself:

  ```bash
  ./tools_local/study/study.py setup --map headword=Word --map reading=Pronunciation
  ```

  Slots: `headword`, `reading`, `meaning`, `partOfSpeech`, `sentence`,
  `sentenceReading`, `sentenceMeaning`.

- **HSK / HSK+ / Basic+** have full built-in profiles (all seven slots).
- **Cloze notes** convert, one card per hole, exactly as Anki generates them.
  The question face is the note's text with this card's hole shown as `[...]`
  -- or as the hint, when the note wrote one -- and every *other* hole filled
  in, because those are context this card is not testing. The answer face
  fills the hole and underlines it. Back Extra, if the note type has one,
  goes under a rule beneath the answer.

  Two holes sharing an ordinal (`{{c1::Berlin}} is the capital of
  {{c1::Germany}}`) are one card with two holes, again as in Anki. A card
  whose hole is no longer in the text is dropped and counted -- that is what
  Anki calls an *empty card*, and deletes under **Tools > Empty Cards**.

  Cloze cards are drawn in the sentence face rather than the big headword
  face: a hole belongs in the sentence it was cut from, and a paragraph at
  headword size fits about four words on the screen.

### Which scripts work

**English, Chinese, Japanese and Korean.**

- **Chinese** needs the CJK faces from your Anki media folder (below), or any
  TTF via `--font`.
- **Japanese** works the same way, with one addition: **furigana is drawn as
  ruby**, the reading set above the word in a smaller cut of the same face.
  Anki's ` 漢字[かんじ]` syntax is understood wherever it appears, and the
  Japanese Support add-on's Expression / Reading / Meaning note type is
  handled as Anki's own template handles it -- the furigana lives in the
  Reading field, and that is the form the headword is drawn from. Slots drawn
  in the built-in serif get the readings alone or the kanji alone, exactly as
  `{{kana:}}` and `{{kanji:}}` would.
- **Korean** needs a Korean TTF via `--font`. (Hangul used to be treated as
  Latin, which sent it to the built-in face -- 1070 glyphs and no Hangul -- so
  a Korean deck converted with no error and no readable card. Fixed.)

Arabic and Hebrew are out of scope for a reason worth stating: they need
bidirectional layout and contextual shaping, and the renderer has neither. A
right-to-left script drawn left to right is not a degraded card, it is a wrong
one. Cyrillic, Greek, Devanagari and Thai are simply not done. Setup names any
script it cannot draw rather than letting it arrive as a screen of nothing.

Line breaking follows the script: between characters for Chinese and
Japanese, with the leading half of kinsoku (a line never opens with 。 or a
closing bracket), and at spaces for Korean and English.

Scheduling state comes along: a card due in 21 days in Anki is due in 21 days
on the reader, with the same stability and difficulty. New decks start fresh,
exactly as they would in Anki.

### Fonts

- **A Chinese or Japanese deck** needs real CJK faces. The converter builds
  them from the TTFs in your Anki media folder -- `_simsun.ttf`, `_simhei.ttf`,
  `_msyahei.ttf`, `_kaiti.ttf`, `_fangsong.ttf` -- and the reader randomises
  the typeface per card, which stops you learning the shape of one font instead
  of the character. Ship whichever of the five you have; the reader uses what
  it finds.
- **Any other deck** needs nothing: the built-in serif draws it. Setup offers
  to build a large headword face from a font your system already has (Georgia
  on a Mac); accept and the type looks right with no further thought. To pick
  your own face, hand it any TTF -- and `--no-font` skips the question:

  ```bash
  ./tools_local/study/study.py setup --font ~/Fonts/Georgia.ttf
  ```

  The size is fitted to the deck: the face is built as large as the longest
  word allows, so an English deck's `incontrovertible` fits where a Chinese
  deck's four characters would.

- **A deck with furigana** gets a third, smaller cut of the same face, built
  from the readings alone -- about a hundred kana, not the deck's whole
  character set at a third size. Only a deck that has furigana in it pays for
  this. A card whose fonts predate the ruby cut draws the base text and loses
  the reading, rather than losing the sentence.

- A card whose text the installed fonts cannot draw falls back to the built-in
  face on its own, per card. A wrong font install can look plain; it cannot
  look blank.

## After every session: sync

```bash
./tools_local/study/study.py sync
```

Quit Anki first (two writers is how a collection gets corrupted; the tool
checks and refuses). Every review you did on the reader -- across every deck on
the card -- is replayed into your collection: same grades, same timestamps,
same revlog entries Anki itself would have written. A backup of the collection
is made first. Reviews you undid on the reader are skipped -- as far as Anki
learns, they never happened. With `--ankiweb` the push happens once, after all
decks have replayed.

Add flags for the two common extras:

```bash
./tools_local/study/study.py sync --ankiweb --reconvert
```

- `--ankiweb` pushes the updated collection to AnkiWeb afterwards, using
  Anki's own sync client. Credentials come from `$ANKI_USERNAME` /
  `$ANKI_PASSWORD` or a prompt; they are used for the login call and never
  stored or logged. The sync library installs itself into the tooling venv on
  first use.
- `--reconvert` refreshes every deck on the card afterwards, so new cards you
  added in Anki, edits, and re-optimised FSRS parameters all reach the reader.
  Fonts that no longer cover a grown deck are rebuilt on the spot.
- `--dry-run` reports what would change and writes nothing.

`./tools_local/study/study.py status` shows what is configured and how many
reviews are waiting to sync.

## The loop, in full

1. Study on the reader. Reviews are written to the card after every answer, so
   a dead battery loses nothing.
2. `study.py sync` when you are back at the computer.
3. In Anki, study other decks, add cards, or run
   **Deck options > Optimize FSRS parameters** whenever Anki suggests it -- your
   reader reviews count toward the optimisation like any others.
4. `study.py sync --reconvert` (or plain `setup` again) carries new cards and
   new parameters back to the card.

There is no wall: the reader is a full citizen of your Anki ecosystem, not an
export.

## On the reader

- Tap anywhere to reveal the answer; tap AGAIN / HARD / GOOD / EASY to grade.
  The times on the buttons are computed the same way Anki computes them.
- **UNDO** (footer, question side) takes back the last answer -- one level,
  during the session.
- **PHOTO** (header, answer side) shows the card's picture full screen, when
  it has one -- the first `<img>` on the note, in whichever field it sits.
  Tap to come back.
- The deck screen shows the last two weeks, today's due count, retention, and
  the streak.

## Moving a card from another build

Card text is rebuilt from Anki on every conversion. **Your practice is not in
the card text.** It is in two files, and both are keyed by Anki card id rather
than by position or by content:

| | |
| --- | --- |
| `deck.dat`, `meta.dat`, `glyphs-*.txt`, `fonts/` | Card text, parameters, faces. Rewritten every conversion. |
| `cards.dat` | Scheduling state -- each record carries the Anki card id it belongs to. |
| `revlog.dat` | Every review, keyed by card id and the millisecond it was answered, which is Anki's own revlog primary key. |

`deck_to_anki.py` reads only the last two, and reads them by card id: it never
opens `deck.dat` at all. So days of practice done on a card written by a
different build -- another branch, an older release, someone else's fork --
carry over without that build's field layout, note order, fonts or rendering
mattering in the slightest. The cards may look different afterwards. The
schedule will not be.

Check the card first, because the next step writes into your collection:

```bash
./tools_local/study/inspect_deck.py /Volumes/SDCARD/study/<deck> \
    --collection ~/…/collection.anki2
```

It reports what wrote the card and how much practice is on it, and validates
every record before it says anything is safe. The failure it exists for is a
build whose records are a different size: the byte count still divides evenly,
every field is then read from the wrong offset, and the replay cannot tell
that from a strange review. It exits non-zero and says so rather than letting
that reach Anki.

Then, with Anki closed:

```bash
./tools_local/study/deck_to_anki.py /Volumes/SDCARD/study/<deck> ~/…/collection.anki2
./tools_local/study/study.py setup --replace
```

The first replays the practice into Anki; the second rebuilds the deck with
this build's converter, and the scheduling state comes back out of Anki with
your days of work in it. The replay is idempotent, so running it twice applies
nothing the second time.

## When something looks wrong

| Symptom                                  | Cause and fix                                                                                                                                   |
| ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| "No deck on the card yet" on the reader  | The card has no converted deck. Run `study.py setup`.                                                                                           |
| A headword draws in the small plain face | The installed fonts cannot draw that card, so it fell back. Re-run `setup` (it rebuilds fonts that no longer cover the deck), or pass `--font`. |
| Japanese readings appear beside the word, not above it | The deck has no ruby cut. Re-run `setup`, or `make_fonts.py`, after a conversion that reported a `ruby` glyph count. |
| Korean draws as blank | An older card, converted before Hangul was classified correctly. Re-run `setup`; the fonts are rebuilt with the `hangul` interval. |
| `no convertible cards` during setup      | A note type whose first field is empty, or a deck whose cloze cards are all empty ones. Use `--map` to name the right fields.                   |
| Sync says Anki is running                | Quit Anki and re-run. `--force` exists but means two writers.                                                                                   |
| Word and meaning came out swapped        | The converter guessed fields by order. Re-run setup with `--map headword=... --map meaning=...`.                                                |
| Reviews look doubled in Anki             | They cannot: replay is keyed by review timestamp, and rows that already exist are skipped. Run `sync` as often as you like.                     |
| A card from another build shows no deck   | Its `deck.dat` is a format this firmware does not read, which is harmless: run `inspect_deck.py`, replay the progress, and re-convert. See "Moving a card from another build".               |

## What is deliberately not here

- **Editing, adding, custom study, browsing** -- Anki does these better on a
  screen with a keyboard.
- **Audio** -- the device has no speaker.
- **Typing the answer** (`{{type:Field}}`) -- there is no keyboard, and a
  soft one on an 800x480 panel that takes a second to redraw is not one
  either. The card shows as an ordinary question and answer.
- **A second level of undo** -- the state before the previous review is not
  kept.

## Under the hood

What of Anki converts, what converts in a reduced form, and what is
deliberately left behind -- with the reason in each case -- is
[study-anki-compatibility.md](study-anki-compatibility.md).

The on-card format, the FSRS implementation and its Anki-agreement tests, and
the sync mechanics are documented in
[study-deck-format.md](study-deck-format.md). The one-page printable version of
this guide is [study-quick-reference.pdf](study-quick-reference.pdf).
