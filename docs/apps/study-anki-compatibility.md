# Study and Anki: what converts, what is reduced, what stays behind

The reader is meant to be a real Anki client rather than an export, so the
useful question is not "does it work" but **"where does it differ, and did
someone decide that on purpose"**. This file is that list. Everything in it
was checked against the code at the commit that added it, not remembered.

The device it is deciding for: an **800x480 one-bit e-ink panel** that takes
about a second to redraw, **two physical buttons and touch**, **no keyboard,
no speaker, no colour**, an SD card, and no clock until a sync sets one. Most
of what is left behind is left behind by one of those, and the entry says
which.

## Package formats

| Format | Status |
| --- | --- |
| `.apkg`, package version 3 (Anki 2.1.50+, today's default) | **Yes.** `collection.anki21b` unzstd'd; the decoy `collection.anki2` is never opened. |
| `.apkg`, package version 1 / 2 (schema 11) | **Yes.** Every AnkiWeb shared deck arrives this way. `apkg.py` materialises the note types, fields, templates, decks and presets that schema keeps as JSON in the `col` row, so the converter runs unchanged. Such a package predates FSRS and carries no weights. |
| `.colpkg` (whole-collection export) | **Yes** — the same zip, and `--deck` picks the deck out of it. |
| A live `collection.anki2` | **Yes**, and it is the only route that has the deck's FSRS parameters, because those exist only in the live collection. |

## Note types and card generation

| Anki | Status |
| --- | --- |
| Basic | **Yes.** First field is the question, second the answer. |
| Basic (and reversed card) | **Yes.** Each direction is its own card, and the second swaps the two fields. |
| Basic (type in the answer) | **Converts as an ordinary card.** There is no keyboard, and a soft one on a panel that takes a second to redraw is not one either, so `{{type:Field}}` is not offered — the answer is simply revealed. |
| Cloze, and cloze-shaped custom types | **Yes.** One card per hole, hints honoured, other ordinals shown filled in — Anki's `{{cloze:}}` exactly. Two holes sharing an ordinal are one card with two holes. See [study.md](study.md). |
| An empty cloze card (its hole edited out of the text) | **Dropped and counted.** Anki calls these empty cards and deletes them under **Tools > Empty Cards**; showing one means showing a question with no hole in it. |
| Image Occlusion | **No.** The masks are SVG shapes drawn over a picture and the card is those shapes toggling. That is a second renderer, and the image pipeline here dithers to one bit at conversion time and cannot composite on the device. |
| Any homemade note type | **Yes**, by field name first and position second. `--map` overrides. |
| More than two card templates on a type | **Yes** — every card is converted; the field mapping is per note type, so the third template asks the same question as the first. |
| Conditional template sections (`{{#Field}}`) | **Not evaluated.** The card exists if Anki generated it; the fields shown are the mapped ones. |
| Card templates themselves (`qfmt`/`afmt`, `{{FrontSide}}`, filters like `{{hint:}}`, `{{furigana:}}`) | **Not rendered.** The device draws seven named slots into a fixed layout designed for this panel rather than laying out arbitrary HTML/CSS. A template engine is the single biggest thing that is *not* here, and it is deliberate: the layouts that read well at 800x480 in one bit are a small set, and picking from that set beats reproducing a stylesheet badly. |

## Field content

| Anki | Status |
| --- | --- |
| Line breaks, paragraphs, `<div>` per line | **Kept**, as hard breaks the wrap honours. |
| Lists (`<ul>`, `<ol>`) | **Kept** as one bulleted line each. |
| Tables | **Reduced**: cells become spaces, rows become lines. There is no column layout. |
| HTML entities, named and numeric | **Decoded**, all of them. |
| Bold, italic, colour, font size | **Dropped.** One bit, one weight per face; a synthesised bold on this panel is an antialiasing flood in another costume. The one exception is the emphasis span (below). |
| The example sentence's `<b>` target, and a cloze card's revealed hole | **Kept as an underline.** Recorded as a codepoint span rather than as markup, and drawn by the same wrap that lays the line out. |
| Images (`<img>`) | **Yes**, through `make_images.py`: scaled and dithered to one bit at conversion time and shown full screen by **PHOTO** on the answer face. |
| Audio and video (`[sound:...]`) | **Dropped.** No speaker. |
| Text-to-speech (`[anki:tts]`) | **Tag dropped, words kept.** The text inside is usually the answer. |
| LaTeX and MathJax | **Delimiters dropped, source kept.** Anki renders these through a TeX install; there is none here. A formula whose source you can read beats a card that shows `[latex]` and beats one that shows nothing. |
| Furigana ruby syntax (`漢字[かんじ]`) | **Drawn as ruby**, above the text it reads. See the Scripts section. |
| A field longer than 1024 bytes of record | **Trimmed** by the converter, longest fields together, marked with an ellipsis. The device refuses an oversized record, which is a card that silently is not there. |

## Scripts

Which script a character belongs to decides which glyph file it lands in,
which decides which face gets built for it. `tools_local/study/scripts.py` is
the one place that answers it, and `study::isWideScript` in `StudyText.h`
mirrors the half of it the wrap needs.

| | Status |
| --- | --- |
| English / Latin | **Yes**, built-in serif, or a large headword face built from any TTF. |
| Chinese, simplified or traditional | **Yes.** Five CJK faces built from the TTFs in your Anki media folder, randomised per card, or any TTF via `--font`. |
| Japanese | **Yes**, kanji and both kana, and **furigana is drawn as ruby** — see below. |
| Korean | **Yes**, with a Korean TTF via `--font`. Hangul used to be classified as Latin, which routed it to the built-in serif — 1070 glyphs, none of them Hangul — so a Korean deck converted with no error and no readable card. It now goes into the deck's own face and asks the font pipeline for the `hangul` interval. |
| Arabic, Hebrew | **No.** Both need bidirectional layout and contextual shaping, and the renderer has neither: it walks a string placing one glyph after another left to right. This is a real piece of work, not a missing font. |
| Cyrillic, Greek, Devanagari, Thai and the rest | **Not supported, not refused.** Nothing stops the converter; the fonts and, for the Indic and Thai scripts, the shaping are not there. The converter names any such script in its output rather than letting it arrive as a screen of nothing. |

### Line breaking

| | Status |
| --- | --- |
| Chinese and Japanese | **Break between characters**, which is how those scripts break, plus **kinsoku shori, both halves**. A full stop, a closing bracket, a long-vowel mark or a repeat mark is never left to open a line -- it stays on the line that ends, one character of overhang, which is what a typesetter does. An opening bracket or quote is never left to end one -- the wrap takes it back off and places it at the head of the next line, which is the only way a greedy wrap can act on a decision it has already made. |
| Korean | **Breaks at spaces**, which is correct: Korean is written with spaces between words, and breaking between syllables would split words the space rule keeps whole. |
| Latin | **Breaks at spaces.** A word longer than the line overhangs rather than being split, and `fitsAsDrawn` refuses a face that would let that happen; a run longer than the whole line buffer is broken by codepoint, because the alternative is a blank card. |

### Furigana

**Yes, drawn as ruby** — the reading above the text it reads, in a smaller cut
of the deck's own face.

| Anki | Status |
| --- | --- |
| ` 漢字[かんじ]` in a field | **Yes.** Parsed into base and reading, and carried to the card as a segment rather than as brackets. |
| `{{furigana:Field}}` | **This is what the card does.** |
| `{{kanji:Field}}` | **Applied** to every slot drawn in the built-in serif, which has no CJK: a reading there would be boxes above boxes. |
| `{{kana:Field}}` | **Applied** to the reading slots, so a reading line says `わたしはがくせいです` rather than repeating the source. |
| The Japanese Support add-on's Expression / Reading / Meaning | **Yes**, and this is the case that needed care: the add-on writes Expression as plain kanji and puts the furigana in Reading. Anki's own template draws the second one, so the converter promotes it — otherwise the most common Japanese note type in existence converts with no furigana anywhere. |
| A hint, footnote or cloze hole that merely looks like `x[y]` | **Not read as furigana.** A reading has to contain kana. That one rule separates a real reading from `see also[1]`, from `[hint]`, and — the collision that matters — from a cloze card's `[...]`, where reading a hole as a reading would print the answer directly above the gap hiding it. |
| Ruby on a deck whose fonts predate the ruby cut | **Degrades to the base text.** The reading is lost, the sentence is not. |

## Scheduling

| Anki | Status |
| --- | --- |
| FSRS-5, the deck preset's own weights | **Yes**, read out of the live collection into `meta.dat`. `host-tests/study/test_fsrs.cpp` pins agreement with Anki's own numbers at 287/301 real cards. |
| Desired retention, maximum interval | **Yes**, per deck. |
| Learning and relearning steps | **Yes**, per deck, up to six of each. |
| New cards/day, reviews/day | **Yes**, and the deck screen shows the *capped* counts, as Anki's deck list does. |
| Card state, due date, reps, lapses, memory state | **Yes.** A card due in 21 days in Anki is due in 21 days here, with the same stability and difficulty. Where current Anki no longer stores memory state, it is recomputed by replaying that card's own review log. |
| Suspended and buried cards | **Carried and never shown.** Kept in the deck rather than dropped so note indices stay stable across reconversions. |
| Filtered decks ("cram") | **Their cards convert**, under the deck they came from. Their reviews are excluded from memory state, which is Anki's own rule. The filtered deck itself does not exist on the reader. |
| Subdecks | **Yes** — converting `A` takes `A` and everything under it. |
| Deck hierarchy as a hierarchy | **No.** The reader holds a flat list of decks and **CHANGE DECK** cycles them. |
| **SM-2 (FSRS turned off)** | **The one real divergence.** The device has one scheduler and it is FSRS. An SM-2 collection converts and its history comes with it, but every interval the reader computes differs from the one Anki would compute. The converter now says so, loudly, before it writes anything, and points at Deck Options > FSRS. |
| Interval fuzz | **Not applied.** Anki spreads due dates by a few percent so a day's reviews do not clump; the reader gives the unfuzzed interval and Anki keeps its own fuzz on cards it schedules. The effect is a due date up to a few percent early. |
| Leeches (tag and suspend at N lapses) | **Not applied on the device.** A card that lapses repeatedly keeps coming back until the reviews reach Anki, which then applies the rule. |
| Burying siblings | **Not applied.** Two cards of the same note can both come up in one session. |
| New-card gather and sort order, review sort order, new/review mix | **Not applied.** Reviews come before new — Anki's order, and the one that matters, since new cards first means the backlog never shrinks — and within each, deck order. |
| Easy Days, custom study, "Set due date" | **No.** All three are decisions better made in Anki, and two of them need a keyboard. |

## The review screen

| Anki | Status |
| --- | --- |
| Again / Hard / Good / Easy, with the intervals on the buttons | **Yes**, computed the way Anki computes them. |
| Undo | **One level**, during the session. The state before the previous review is not kept. |
| Flags, marking, suspend/bury from the reviewer | **No.** The revlog is the only thing that travels back, and it carries reviews rather than card edits; adding a second channel means a second thing that can disagree with Anki. |
| Editing a card mid-review | **No.** Anki does this better on a screen with a keyboard. |
| Card info / review history | **Partly.** The answer face carries the card's own record — seen, lapsed, current interval — which is what you actually want when choosing between Hard and Good. |
| Deck statistics | **Partly.** The deck screen shows two weeks back and two weeks forward, today's due count, retention and the streak. |

## Sync

| Anki | Status |
| --- | --- |
| Reviews back into the collection | **Yes**, keyed by the millisecond answered, which is Anki's own revlog primary key — so re-running applies nothing twice. |
| Pushing to AnkiWeb | **Yes**, from the computer, using Anki's own sync client rather than a hand-written one. |
| Syncing from the device itself | **Deliberately not.** AnkiWeb's protocol is undocumented, and a client that gets it subtly wrong corrupts the collection on the server — which for most people is the only copy — with no way to show the user what went wrong. |
| Adding, editing, browsing, retraining FSRS | **In Anki**, where those things are good. Reader reviews count toward **Optimize FSRS parameters** like any others, and re-converting carries the new weights back. |

## Cards written by another build

A card may have been written by a different build of these tools -- another
branch, an older release, someone else's fork. What carries over is decided by
one thing: **the two files holding your practice are keyed by Anki card id,
not by position or by content.**

| File | Rewritten every conversion? | Carries progress? |
| --- | --- | --- |
| `deck.dat` | Yes | No |
| `meta.dat` | Yes | No |
| `glyphs-*.txt`, `fonts/` | Yes | No |
| `cards.dat` | No | **Yes** — each record holds its Anki card id |
| `revlog.dat` | Never (append-only) | **Yes** — keyed by card id and the millisecond answered |

`deck_to_anki.py` never opens `deck.dat`: `read_cards` builds a dict keyed on
the id inside each record, and `read_reviews` does the same. So a build that
changed the field layout, the note order, the fonts or the whole renderer
changed nothing that the replay depends on. Practice done on such a card
carries over; the cards look different afterwards and schedule identically.

What could genuinely break it is the 32-byte record layout of those two files.
`inspect_deck.py` checks that from the files themselves rather than trusting
either build, and refuses when it does not hold — the case worth guarding is a
build using a longer record, where the byte count still divides evenly, every
field is then read from the wrong offset, and nothing downstream can tell that
from a strange review. It runs immediately before the step that writes into
what is usually the only copy of years of study.

## If you want one of these

The ones with a real case, in the order they would be worth doing:

1. **Leeches and sibling burying.** Both are per-preset numbers already sitting in the deck config protobuf, and both are small state machines on top of the queue that is already there.
2. **Interval fuzz.** Twenty lines, and it removes a small standing divergence.
3. **Image Occlusion.** The card kind with the largest audience of those refused. It needs shape data in the deck file and a compositing step on the device, so it is a real piece of work rather than a rule.
4. **A template renderer.** The biggest, and the one to be most careful about: the value is not in reproducing a stylesheet but in letting a deck say *which of a few layouts* it wants.

Arabic and Hebrew are not on that list, and should not be until someone wants
them enough to write a bidi pass. A right-to-left script drawn left to right
is not a degraded card, it is a wrong one.

SM-2 is deliberately not on that list. A second scheduler is not the fix for a
collection that has FSRS off; turning FSRS on is, and Anki agrees.
