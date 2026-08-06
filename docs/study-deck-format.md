# The study deck format

What `tools_local/study/anki_to_deck.py` writes to the SD card, and why it is
shaped this way. The reader is `StudyDeck` in `src/apps_local/study/`.

Decks live on the **SD card**, never in flash. Mario's Mandarin deck is 5001
notes and about 900KB of text before fonts; the app partition has 758KB free in
total. This is not a close call.

    /study/
      mandarin/
        deck.dat        immutable card content, written by the converter
        cards.dat       scheduling state, rewritten by the device
        revlog.dat      append-only review history, read back by the sync script
        meta.dat        FSRS parameters, learning steps, limits, identity
        glyphs-*.txt    the codepoints the deck uses, split by the size they
                        are rendered at, for the font pipeline

## The one design rule

**Content is read-only and mutable state is fixed-width.** `deck.dat` is
written once and never touched by the device, so a half-finished write can
never corrupt card text. Everything the device changes lives in `cards.dat`,
where every record is exactly `kCardRecordSize` bytes at a computed offset --
so recording a review is one seek and one 32-byte write, not a rewrite of a
900KB file. On a device that can lose power mid-review, that difference is the
whole durability story.

## deck.dat

    magic    "XSTUDYD\0"                     8 bytes
    version  uint16                          format version, currently 2
    fields   uint8                           fields per note (7)
    flags    uint8                           reserved, 0
    count    uint32                          number of notes
    index    uint32 * (count + 1)            byte offset of each note's record,
                                             plus a sentinel so a record's
                                             length is index[i+1] - index[i]
    blob     count records

Each record is `fields` little-endian `uint16` lengths followed by that many
bytes of UTF-8, in field order. Empty fields are length 0, never omitted, so
the reader indexes rather than scans.

The trailing sentinel in the index is the entire reason a note can be read with
two seeks and no scanning, and costs four bytes.

### Field order

    0  headword          the big character(s) -- Anki's "Simplified"
    1  reading           pinyin with tone marks -- "Pinyin.1"
    2  meaning           the English gloss
    3  partOfSpeech      shown small under the meaning
    4  sentence          example sentence, HTML stripped
    5  sentenceReading   its pinyin
    6  sentenceMeaning   its translation

### The emphasis span

Anki wraps the target word in the example sentence in `<b>`. There is no bold
CJK face on this device -- a synthesised one is the antialiasing flood in
another costume -- so the converter strips the markup and records where it was:
the last two bytes of the `sentence` field are **not** text but a
`uint8` codepoint offset and a `uint8` codepoint length, appended after the
UTF-8. The renderer can underline that span; a reader that ignores the two
bytes still gets valid text, because the length prefix covers them.

Offset and length are in **codepoints, not bytes**, because that is what the
renderer counts when it walks a string to place glyphs.

## cards.dat

One 32-byte record per note, in the same order as `deck.dat`, so note _i_ and
card _i_ are the same card and no id lookup is needed on the review path.

    ankiCardId    int64     the id in Anki's collection -- the sync key
    stability     float32   FSRS memory state
    difficulty    float32
    dueDay        int32     day number, in the deck's own day numbering
    lastReviewDay int32     -1 if never reviewed
    reps          uint16
    lapses        uint16
    state         uint8     0 new, 1 learning, 2 review, 3 relearning
    stepIndex     uint8     position in the learning / relearning step list
    dueMinute     uint16    minutes since dueDay began

`lastReviewDay` is load-bearing and was once left unset. FSRS needs the gap
since the previous review; without it every card's first review on the device
looks like a *same-day* review and takes the short-term stability path, which
inflates the interval badly. Anki does not store that day on the card, so the
converter derives it: for a review card it is exactly `due - ivl`.

`dueMinute` exists because "come back in ten minutes" cannot be said in day
numbering, and a learning step is the one thing that needs finer resolution
than a day.

Day numbers are days since the collection's creation, counted from the deck's
rollover hour, which is exactly Anki's own numbering. Keeping that means a due
date computed here and a due date computed on the phone agree without any
timezone conversation.

## revlog.dat

Append-only, 32 bytes per review, never shortened. This is the file that makes
the device a real Anki client rather than a toy: `deck_to_anki.py` replays it
back into the collection, and it is also the input FSRS optimisation would need
to retrain the parameters.

    ankiCardId    int64
    reviewedAtMs  int64     wall-clock ms, so Anki can key the revlog row
    rating        uint8     1..4
    state         uint8     the state the card was in *before* the review
    elapsedDays   int16
    intervalDays  int32     days ahead, or negative seconds for a step
    tookMs        uint32    zero: nothing here times the user
    flags         uint8     bit 0: the user undid this review
    reserved      3 bytes

`intervalDays` follows Anki's own convention of storing sub-day intervals as
negative seconds, so a ten-minute relearning step is -600 and needs no
translation at import.

`tookMs` is deliberately zero rather than plausible. Inventing an answer time
would poison the only data FSRS optimisation has to retrain from.

`flags` is how undo works, and it is the one thing here written in place rather
than appended. When the user takes a review back, the device restores the card
record and sets bit 0 on the review it just wrote. The record stays: the file is
append-only by design, and shortening it would mean reaching past `HalFile` into
SdFat, which the HAL rule forbids for good reason. Every reader skips a voided
record instead -- `StudyStats` for the deck screen's figures, `deck_to_anki.py`
for the sync -- so an undone review reaches neither the stats nor Anki.

Byte 28 was reserved and written as zero, so a log recorded before undo existed
reads as "nothing voided" and needed no version bump.

Undo is one level deep and only while the card view is up. The state before the
previous review is not kept, and the deck screen has no control for it, so the
last review of a session cannot be taken back once "Done" is showing.

## meta.dat

    magic         "XSTUDYM\0"       8
    version       uint16
    reserved      uint16
    params        float32 * 19      the deck preset's FSRS-5 weights
    desiredRet    float32
    maxInterval   int32
    newPerDay     int32
    revPerDay     int32
    collectionCrt int64             epoch of day zero, from Anki's `col.crt`
    rolloverHour  uint8
    learnCount    uint8             how many learning steps follow
    relearnCount  uint8
    learnSteps    float32 * 6       minutes; Mario's deck is 1 then 10
    relearnSteps  float32 * 6       minutes; Mario's deck is 10
    nameLen       uint8
    name          nameLen bytes     display name, UTF-8

The learning steps travel with the deck for the same reason the weights do:
they are per-preset and Mario changes them. They are what FSRS does *not*
answer -- FSRS says how many days, and the steps say what happens in the next
ten minutes. See `StudyScheduler.h`.

A deck converted from the "Current" preset therefore schedules exactly as that
preset does, verified by reading the weights back out of `meta.dat`.

## What the converter deliberately drops

- **Audio.** Both `[sound:...]` fields. The X4 Pro has no speaker.
- **Images.** `SentenceImage` is populated on some notes; deferred, not
  refused. It would be the first thing to add if the sentences want it.
- **Traditional characters** and the two cloze fields. Mario studies
  simplified; carrying the rest would double the glyph set for nothing.
- **Filtered-deck review history.** See `StudyFsrs.h` -- Anki does not feed
  those into memory state, so neither do we.

## Getting a deck onto the card, and reviews back off it

    # build the deck and the five CJK faces
    tools_local/study/anki_to_deck.py <collection.anki2> \
        --deck 'Mandarin: Vocabulary' --out /Volumes/SDCARD/study/mandarin
    tools_local/study/make_fonts.py --media <collection.media> \
        --deck /Volumes/SDCARD/study/mandarin --out /Volumes/SDCARD/study/fonts

    # after studying, replay the reviews back into Anki (close Anki first)
    tools_local/study/deck_to_anki.py /Volumes/SDCARD/study/mandarin <collection.anki2>

The sync is idempotent: a device review is keyed by the millisecond it was
answered, which is also Anki's revlog primary key, so re-running it applies
nothing the second time.

### Syncing straight to AnkiWeb

`deck_to_anki.py --sync` applies the device's reviews and then pushes the
collection, using **Anki's own client** from the `anki` package rather than a
hand-written one. That is not laziness. AnkiWeb's sync is an undocumented
protobuf protocol with USN tracking, chunked transfer and a full-sync fallback,
and a client that gets it subtly wrong does not fail loudly -- it corrupts the
collection on the server, which for most people is the only copy.

    deck_to_anki.py /Volumes/SDCARD/study/mandarin ~/…/collection.anki2 --sync

Credentials come from `$ANKI_USERNAME` / `$ANKI_PASSWORD` or a prompt, and are
never stored or logged. Anki must not be running: it holds the collection open,
and two writers is how a collection gets corrupted.

If AnkiWeb answers "full sync required" the script stops and says so, because
resolving that means choosing whether this computer or the server wins -- a
decision for a human in front of Anki, not a script holding their only copy.
The reviews are already in the local collection either way.

Doing the sync here rather than on the device is deliberate. The ESP32 could in
principle speak the protocol; it should not, for the same reason as above,
multiplied by having no way to show the user what went wrong.

### Retraining the parameters

There is no optimiser here and there should not be one. Reviews reach the
collection, so **Anki's own "Optimize FSRS parameters"** is the optimiser --
it has more history than the device ever will, and it is the same code that
produced the weights the device is already running. Re-running
`anki_to_deck.py` afterwards carries the new weights back in `meta.dat`, and
the device schedules with them from the next card.

    deck_to_anki.py …                      # reviews land in the collection
    # Anki: Deck options > FSRS > Optimize
    anki_to_deck.py … --out …/mandarin     # new weights come back

That loop is why `revlog.dat` writes `tookMs` as zero rather than as something
plausible: answer time is one of the inputs an optimiser weighs, and inventing
it would quietly bias the parameters the device then runs. `revlog.dat` is never truncated by the sync -- it is
an append-only record rather than a queue, and it is the only copy of that
history.

The fonts live under `/study/fonts/` rather than `/.fonts/` deliberately. The
font registry scans `/.fonts` and `/fonts`, and a family installed there
appears in **Settings > Reader > Font Family** -- where these would be a trap,
because they are subset to this deck's 2769 glyphs and would show boxes for
every character a real Chinese book contains that the deck does not.
