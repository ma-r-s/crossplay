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
        meta.dat        FSRS parameters, limits, identity
        glyphs.txt      every codepoint the deck uses, for the font pipeline

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
    version  uint16                          format version, currently 1
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
    flags         uint8
    reserved      uint16

Day numbers are days since the collection's creation, counted from the deck's
rollover hour, which is exactly Anki's own numbering. Keeping that means a due
date computed here and a due date computed on the phone agree without any
timezone conversation.

## revlog.dat

Append-only, 32 bytes per review, never rewritten. This is the file that makes
the device a real Anki client rather than a toy: `deck_to_anki.py` replays it
back into the collection, and it is also the input FSRS optimisation would need
to retrain the parameters.

    ankiCardId    int64
    reviewedAtMs  int64     wall-clock ms, so Anki can key the revlog row
    rating        uint8     1..4
    state         uint8     the state the card was in *before* the review
    elapsedDays   int16
    intervalDays  int32     what the review scheduled next
    tookMs        uint32
    reserved      uint16

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
    nameLen       uint8
    name          nameLen bytes     display name, UTF-8

The parameters travel **with the deck** rather than being compiled in, because
they are per-preset and Mario re-optimises them. A deck converted from the
"Current" preset schedules exactly as that preset does.

## What the converter deliberately drops

- **Audio.** Both `[sound:...]` fields. The X4 Pro has no speaker.
- **Images.** `SentenceImage` is populated on some notes; deferred, not
  refused. It would be the first thing to add if the sentences want it.
- **Traditional characters** and the two cloze fields. Mario studies
  simplified; carrying the rest would double the glyph set for nothing.
- **Filtered-deck review history.** See `StudyFsrs.h` -- Anki does not feed
  those into memory state, so neither do we.
