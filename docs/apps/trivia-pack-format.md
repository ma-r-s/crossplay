# The trivia pack

What `tools_local/trivia/build_pack.py` writes to `/trivia` on the card, and
what the app reads back. The curation behind it is in
[../trivia-curation.md](../trivia-curation.md); this is the format.

Shape follows [study-deck-format.md](study-deck-format.md): immutable content
with an offset index and a trailing sentinel, mutable state in a separate
fixed-width file. `tools_local/trivia/pack_format.py` is both the writer and a
reference reader, so the format is executable rather than only described.

## Why there is a pack at all

50,000 questions is 5.37 MB. The app partition is 7.94 MB total and the image
already uses most of it, so the questions cannot ride in flash. They live on the
card, like study decks and the xkcd archive.

## Two files

```
/trivia/pack.dat     immutable questions, written by the converter
/trivia/pack.state   one byte per question, written by the device
```

### pack.dat

    magic    "XTRIVIA\0"                8 bytes
    version  uint16                     format version, currently 1
    flags    uint8                      reserved, 0
    resv     uint8                      reserved, 0
    count    uint32                     number of questions
    index    uint32 * (count + 1)       byte offset of each record from `base`,
                                        plus a sentinel so a record's length is
                                        index[i+1] - index[i]
    blob     count records

`base` is `16 + 4 * (count + 1)`.

Each record:

    difficulty  uint8       1-5, derived (see below)
    year        uint16      air year, for a recency filter
    alt_count   uint8       number of alternate accepted answers
    wrong_count uint8       number of stored distractors (0 = quizmaster only)
    fields      uint16 length + UTF-8 bytes, repeated
                (2 + alt_count + wrong_count) times: question, answer, each
                alternate, then each distractor

**The index is never resident.** 195 KB of offsets for a 50k pack is RAM the
device does not have. Entry `i` and its successor are one 8-byte read at
`16 + 4*i`, then the record itself is a second seek. Two seeks, two reads, and
the only RAM used is the record: **186 bytes worst case**, 107 bytes on average.

### pack.state

`count` bytes, one per question, at the question's own index. Bit 0 `SEEN`,
bit 1 `FLAGGED`. Marking a question is one seek and one byte, never a rewrite,
so a power loss mid-write can lose at most one question's state and can never
touch the question text — the same durability rule study follows.

49 KB for a 50,000-question pack.

`FLAGGED` is the in-play "this question is bad" button. Those indices are read
back off the card and merged into `tools_local/trivia/verdicts.tsv`, which the
next build applies. That is the loop that improves the pack through play rather
than through a curation project.

## Difficulty is derived, not stored upstream

Jeopardy doubled its clue values on 2001-11-26, so raw `clue_value` is not
comparable across the 42 seasons. The builder doubles pre-2001 values, halves
Double Jeopardy to the round-one scale, and calls Final Jeopardy tier 5. The
result is an even spread; raw values would not be.

## The clue cap is measured in pixels, not characters

A clue is rejected at build time if its **wrapped height** exceeds the clue box
(448 x 583 px at `notosans_18_regular`), so the device is never handed text it
cannot draw. This matters because the panel truncates silently: an overflowing
line ends in U+2026, Jersey has no glyph for it, and the sentence just stops --
photographing as a perfectly reasonable clue that happens to end early.

**A character cap cannot express this, and gets it wrong in both directions.**
Measured on this corpus: 8 clues under 140 characters overflow the box, and 36
clues over 140 characters fit comfortably (the longest is 275 characters and
uses 451 px of the 583 available, because it is song lyrics with short words).
Two 24-character strings can differ by 69 pixels in the same face.

`tools_local/trivia/textfit.py` reads advances out of the generated font header,
where they are 12.4 fixed point -- sixteenths of a pixel, which is the detail
that makes a hand-rolled measurement come out 16x too wide. `test_pack.py`
re-checks the same bound, so a font change or a new season cannot quietly
reintroduce an overflow.

## Building it

```bash
python3 tools_local/trivia/build_pack.py \
    --src <combined_season1-42.tsv> \
    --out pack.jsonl --dat /trivia/pack.dat \
    --limit 50000 --verdicts tools_local/trivia/verdicts.tsv
python3 tools_local/trivia/test_pack.py pack.jsonl
```

## Where the pack comes from

Nobody copies files to a card. Following xkcd: a rolling `trivia-pack` GitHub
**prerelease**, so the OTA's `releases/latest` can never see it, and the app's
first run offers to fetch it. Files land as `.part` and are renamed only when
complete, so a torn download leaves the card exactly as it was.


## Solo multiple choice

15,932 of the 50,000 carry precomputed distractors. The rest are quizmaster-only,
which is why both modes exist rather than one.

**The type comes from the clue itself.** A Jeopardy clue names what its answer
is -- "this **country**", "this **writer**", "this **metal**" -- so distractors
are drawn from questions of the same type. 104 types have a pool of 25 or more,
which is what makes an option plausible instead of absurd.

**Distractors are length-matched on purpose.** The longest option being the
correct one is the tell every published corpus leaks: measured at 31.7% for The
Trivia API, 35.0% for OpenTriviaQA and 38.0% for OpenTDB, against a 25% baseline.
Matching lengths puts this pack at **6.1%** -- below chance, so option length
carries no information at all. `test_pack.py` fails the build above 15%.

Six distractors are stored and the device picks three, so replaying a question
does not give the same four options. The device shuffles; nothing about stored
order may be relied on.
