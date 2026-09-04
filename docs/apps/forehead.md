# FOREHEAD

The party game where the person holding the device is the only one who cannot
see it. One player holds it against their forehead with the screen facing out,
everybody else shouts clues, and the guesser presses one key for GOT IT and the
other to give up. Sixty seconds. The score is how many they got.

Everyone knows this game from a phone. What this device does differently is the
only part of the design worth arguing about, and all of it comes out of two
facts about the hardware.

---

## 1. There is no accelerometer, and that is a feature

The phone version is played by tilting: down is correct, up is pass. The X4
Pro's board profile ends `RtcType::Pcf8563, ImuType::None` -- there is no IMU on
this device and there is nothing to tilt.

So the two side keys are the answer, and they turn out to be better than the
tilt they replace. A tilt has to be big enough to detect and small enough not to
lose your grip, it fires on a stumble, and it means the phone leaves your
forehead on every single card. A key does not move the device at all.

**The mapping is the tilt's, in the only vocabulary this hardware has:**

| Key                    | In landscape | Does   | Phone equivalent |
| ---------------------- | ------------ | ------ | ---------------- |
| `Button::Up` (GPIO0)   | bottom edge  | GOT IT | tilt down        |
| `Button::Down` (GPIO7) | top edge     | PASS   | tilt up          |

**Confirmed on hardware on 2026-08-30**, by playing it. It was derived rather
than measured for two days, and the derivation held -- but follow the rotation
once anyway, because the next app to use these keys in landscape needs the
reasoning and not the answer. `docs/buttons.md`: GPIO0 is the key on the
physical LEFT in portrait and GPIO7 the one on the physical RIGHT.
`rotateCoordinates()` in `GfxRenderer.cpp` maps portrait logical _y_ onto panel
_x_, which is a quarter turn anticlockwise -- so the portrait right edge becomes
the landscape **top** and the portrait left edge becomes the landscape
**bottom**.

It is deliberately not configurable, and now that a person has played it that
stays true: a settings row for this would be a permanent apology for never
having checked. The screen labels its own edges, so a wrong mapping would have
been obvious inside the first three seconds of the first round -- which is
exactly how it got confirmed.

### Touch is there, but not as two halves

Every button action in this fork stays reachable by touch, and this one does
too -- but the guesser's fingers curl over the long edges to reach the keys,
which is exactly where a half-screen target would be. The tappable regions are
bands across the **middle** of each half, inset 140px from the sides and 90px
from the top and bottom. A player who wants to play with a thumb can; a player
holding it normally cannot answer their own card by accident.

### `ScreenUp` / `ScreenDown` are not used, and must not be

They look like exactly the right API and they are a trap here. In
`LandscapeCounterClockwise`, `mapScreenDirection()` resolves them to
`Button::Left` and `Button::Right`, which are `PIN_UNASSIGNED` on this board and
can never fire -- and the whole mapping is gated on
`SETTINGS.frontButtonFollowOrientation`, a reader preference most players have
never opened.

---

## 2. The clock is a repaint schedule, not a time

This is the constraint that shaped the round screen, and it is the one that
would sink a naive port.

A partial refresh is ~0.3s. A per-second countdown across a sixty-second round
is **sixty** of them: eighteen seconds of the minute spent mid-update, on the
one screen in the entire fork that somebody across the room is trying to read.
The card flips already cost a refresh each, and those are unavoidable and
earned. Everything else is a budget.

So the clock is a **bar**, `barSegments()` is the schedule, and the activity
repaints on nothing but "a segment moved":

| What               | Repaints in a 60s round    |
| ------------------ | -------------------------- |
| card flips         | however many words you get |
| a per-second clock | 60 -- rejected             |
| seconds in figures | 11 -- rejected             |
| `barSegments()`    | 5                          |

Three complete round screens were built and rendered before this was decided:
seconds in figures, the bar, and no clock at all until a last-ten-seconds
inversion. The side-by-side settled it on something neither prose nor a single
screenshot showed: **the word is the same size in all three**, because 64px is
the largest cut we have, so "give the word more room" bought white space and
nothing else. That left the clock as the only real axis, and the bar wins it
twice. It costs five forced repaints where figures cost eleven, on the one
screen in this fork somebody is reading from a sofa while it updates. And it is
legible from that sofa, which a 30px numeral is not: the guesser cannot see any
of this, so every element on the round screen is for the room.

`host-tests/forehead/` pins both the ceiling and the floor on that count,
because a schedule that never moves is a clock that lies.

**The buzzer is the full-refresh blink.** The board profile says `NO_AUDIO`, so
there is no beep to be had -- and the blink is better anyway, because everybody
in the room is already looking at the panel at exactly that instant. It is the
"refresh flash as punctuation" rule in `docs/design-language.md` spent on the
one moment in the game that has earned it.

---

## 3. Two orientations, and the turn teaches itself

| Screen                                    | Orientation |
| ----------------------------------------- | ----------- |
| front door, picker, settings, how to play | portrait    |
| ready card, round, results                | landscape   |

Portrait for what you hold in your hand, landscape for what you hold against
your head -- and landscape because the word gets 800px instead of 480.

There is no rotate icon and no "please turn the device" screen. The ready card
is drawn in landscape while the device is still being held in portrait, so it
arrives sideways, and a sideways screenful of type is the only rotation
instruction anybody has ever needed.

`setOrientation` is global, so it is set on every view change and put back in
`onExit()`. Leaving it turned rotates whatever activity comes next.

---

## 4. The type had to grow

The 30px display cut puts a 38px capital on the panel: 4.4mm at 220ppi, which is
right for a header held at reading distance and useless at three metres.

Two new cuts, `toybox_44` and `toybox_64`, give 57px and 82px of capital -- 6.6mm
and 9.5mm. The larger one is bigger than the word on a phone playing the same
game, which is the benchmark that settled the size.

Both, not one, because the ladder needs a middle rung: the longest single word
in the lists is fifteen characters, which does not fit the panel at the huge cut
at any line count and does fit at the large one.

`layOutCard()` picks the largest cut the entry fits in, at most three lines,
breaking on spaces only and never inside a word. It can measure all three
because the round screen binds all three at once (`toybox::cardFaces()`):
a freestanding screen builder has no way to ask for a slot rebind mid-layout, so
whatever it can measure is whatever is bound. The 30px cut doubles as the
screen's chrome face, which is why there is no UI cut on the round screen and no
need for one -- the only other text is two edge labels and a score.

**The word is fitted to one rect and drawn in another**, and the difference is
worth knowing before anybody "simplifies" it. It is CENTRED in the whole white
area between the two key bands, because that is the frame a reader sees. It is
FITTED to that area minus the chrome height at BOTH ends, so the tallest layout
the ladder can choose still cannot reach the timer bar. Centring it in the
leftover space under the chrome instead is what had a four-letter word hanging
four millimetres low against its own frame, which measures as obviously as it
sounds and is invisible while you are reading the code that causes it.

**The word is also the one place this design knowingly leaves something on the
table.** `toybox_64` is sized so the worst case fits one line -- 22 characters
is the full panel width -- so every ordinary four-to-nine letter word, which is
almost all of them, is drawn at the size the rarest word needs. A 96px cut, or
pixel-doubling the 64px cut for the word slot alone (which costs no flash at
all, this being a deliberate-pixel face), would make the common case half again
as large. Deliberately not done: see the note at the end of this file.

---

## 5. The words

**2474 entries across 17 categories**, in flash as a `constexpr` table.

Generated from `tools_local/forehead/words/*.txt` by `gen_forehead_words.py`, in
the same shape as Insider's. Edit a `.txt`, run the script, commit both -- the
generated header is committed because a checkout has to build without Python.

### The lists are read, not remembered

The first version of these lists was written from memory, and it had a
structural fault that no amount of reviewing it against itself would have found:
**there was no easy tier at all.** Measured against published, play-tested
charades and party-word lists, ACT IT OUT was missing all fifteen of the easy
words; FOOD was missing BREAD, APPLE, CHEESE, EGG, RICE and MILK; ANIMALS was
missing DOG, COW, PIG, BIRD, FISH and BEAR. Every list started at the difficulty
a published list reaches halfway down. A game whose easiest word is PORCUPINE is
not a hard game, it is a game nobody finishes a round of.

`tools_local/forehead/sources/` holds what was read and what was done with it:
`published.py` is the transcribed source material, `curate.py` the fixes, cuts
and additions applied on top, and `README.md` names the sources.

Recorded there too: **frequency filtering was tried and rejected.** Scoring
entries by corpus frequency to find the too-obscure ones flagged ALLIGATOR,
PENGUIN, BROOM, CRAYON and TOOTHBRUSH -- five words every six-year-old mimes on
demand. Word frequency measures how often a word is written down, and this game
is about what is easy to act out. Nobody should spend an evening rediscovering
that.

### What the generator refuses

Four things, all of which fail silently at 220ppi:

- **Non-ASCII.** Toybox's face is subset to ASCII and a glyph the font does not
  have draws as **nothing** -- no box, no fallback, no log line. A curly
  apostrophe pasted from a web page produces a card with a hole in it. This one
  is not hypothetical: it caught `SÉANCE` on the first run.
- **Anything too wide in PIXELS**, measured against the real font tables rather
  than counted in characters. Entries are checked against the 276px results
  column, titles against the 280px the picker row leaves beside its value, hints
  against the 760px of the landscape ready card. A character cap is what this
  used to be, and it cannot work: "HOLD IT ON YOUR FOREHEAD" and "EASY ONES FOR
  SMALL ONES" are both 24 characters and differ by 69 pixels. `measure.py` will
  tell you what any string costs at any cut.
- **A duplicate inside one list**, which defeats the no-repeat deck silently:
  the mask marks one index and the other is still in the bag.
- **A NEAR duplicate**: one answer written two ways. A plural beside its
  singular (GRAPE/GRAPES), a word order beside its reverse (DOG BARKING/BARKING
  DOG), a title with and without its article (LION KING/THE LION KING), two
  spellings of one word (DONUT/DOUGHNUT). The deck deals each pair as two cards
  and the room has one answer for both, so the holder who says the other one is
  marked wrong.

  Every pair of this shape that shipped was **manufactured by the curation
  step**, whose "is it already there" test was an exact string match: it added
  the variant of a word that was already present, and the exact-duplicate check
  above waved it through. Subsets are deliberately NOT refused -- TABLE TENNIS
  and TENNIS are two sports -- and `dupes.py` reports those for a human instead.
- **A category with fewer than `kMaxCards` entries**, covered below.

### Two traps in the curation script

`curate.py`'s tables are written as `"""WORD WORD WORD""".split()`, which is readable and
correct for single words and **shreds anything with a space in it**.
`"SITTING DOWN STANDING UP"` became four entries, and `act.txt` shipped
containing the cards `DOWN` and `UP`. The same line hit `food` (`ICE CREAM` ->
`ICE`, `CREAM`) and `house` (`LIGHT SWITCH` -> `LIGHT`, `SWITCH`): six junk
cards from one habit. Multi-word entries go in an explicit list, appended with
`+[...]`, where a space cannot be mistaken for a separator.

The other one: `ADD` runs **after** `FIX`, so an added entry is exempt from
every correction in the file -- and since `ADD` re-adds the uncorrected spelling
on every run, the list ends up holding both forms. That is how `SCOOBY DOO` and
`SCOOBY-DOO` both shipped, in two categories, out of the very table meant to
remove duplicates. Additions go through `FIX` now, and running `curate.py`
twice is a no-op, which is the property that proves it.

### A name that does not fit is dropped, not shortened

The pixel cap is mirrored into `curate.py`, so an entry too wide for the
results column fails where it is **written** rather than one step later at
generation. That matters because of what happens at the later point: faced with
`CHRISTOPHER COLUMBUS` at 280px against a 276px column, the fix applied once in
this repo was to shorten it to `COLUMBUS`, which trades an overflow for a city
in Ohio. `PIRATES OF CARIBBEAN` and `JACK AND BEANSTALK` are the same mistake
already made. If the real name does not fit, the entry goes.

A word appearing in **two** categories is fine and often right -- CLAPPING is
both an action and a sound -- so those are reported and allowed. Only one
category is ever in play.

### Every list is bigger than a round can hold

`kMaxCards` is 128 and every category has more entries than that. The generator
enforces it, reading the number out of `ForeheadCore.h` rather than keeping its
own copy, and it is how STORYBOOK (123) and MYTHS (126) were caught: both were
under the cap and both grew past 180. **A new category needs more than 128
entries.**

That is necessary and it is not sufficient, which is the subtler half. The deck
is **persistent** -- it lives in the save file -- so after a few evenings a
category holds fewer unseen cards than a round will answer, and the round
crosses a lap. A lap that cleared the whole slice would hand back words already
on that round's own results screen: measured at 63% of evenings on a busy
category, sometimes twice in a row. So `Deck::draw` does **not** lap. It returns
-1 when the category is spent and `Round::dealNext` laps with `lapExcept()`,
passing the cards this round has already dealt so they stay marked. The deck
cannot own that decision because it cannot know what is on the screen.

### The deck

One bit per entry across the whole table, and a category is a contiguous slice
of it, so "unseen in this category" is a scan of a slice and nothing needs a
per-category structure. It survives sleep in `/.crosspoint/forehead.sav`, so a
list deals all 189 animals before it repeats one, and then laps silently rather
than asking anybody to press a reset button.

---

## 6. The shape of the code

| Layer         | File                      | Knows about                           |
| ------------- | ------------------------- | ------------------------------------- |
| Rules / state | `ForeheadCore.h/.cpp`     | nothing -- freestanding C++17         |
| Words         | `ForeheadWords.h`         | nothing -- generated, plain data      |
| Icons         | `ForeheadCategoryIcons.h` | the SDK's `Icon`, generated           |
| Screens       | `ForeheadScreens.h/.cpp`  | FreeInkUI and Toybox tokens           |
| Activity      | `ForeheadActivity.cpp`    | the clock, orientation, storage, keys |

The icon table is a **second** generated file from the same source, and the
split is load-bearing: `ForeheadWords.h` is included by the rules layer, and a
`freeink::Icon*` in it would drag the SDK into a header that has to compile with
nothing on the include path. Deriving the icon symbol from the category slug
(`icon_cat_<slug>_32`) means one token spells both, so there is no hand-kept
parallel column to desynchronise -- the failure mode that survived a mutation
test in this fork once already.

**There is no clock in `ForeheadCore`.** Time is the activity's business; the
rules layer is told only that a round ended. That is what lets the tests play
hundreds of thousands of cards in a second, and it is why every function in the
core is about cards rather than seconds.

---

## 7. Testing

```bash
./host-tests/forehead/run.sh
```

355k checks. Three kinds, and the third is the one worth copying:

1. Hand-built states for each rule.
2. Deck soaks. The interesting deck bug is the **last card of a lap** --
   rejection sampling on positions would expect ~189 draws to find the one card
   left, and a spot check reaches that case roughly never. There is a test that
   sets up exactly it, for 200 seeds.
3. An exhaustive pass over all 2460 entries and all 17 slices: ASCII, length,
   case, no doubled spaces, slices tile the array with no gap or overlap, no
   duplicate within a list, every list bigger than `kMaxCards`. The generator
   already refuses bad content, but **the generator is not what ships** -- the
   committed header is, and a hand-edit would sail past a script nobody re-ran.

### The overflow gate is the simulator, not a test

Every text overflow this app has had was invisible in a screenshot. The SDK
truncates with U+2026, and **the Toybox cuts do not all carry it**: `toybox_10`
has the ellipsis, and 14, 20, 30, 44 and 64 do not. A glyph the face lacks draws
as nothing, so at every cut this app uses, an overflowing line does not clip --
it **stops**, at a plausible-looking place. Two shipped that way: a settings row
that read `RE` and a first run whose only sentence ran off the side.

That asymmetry is also the gate's coverage. At the 10px cut an overflow ends in
a visible `...` and the renderer logs nothing, so the gate is silent there -- and
does not need to be loud, because a human can see it. At 14 and above the
failure is invisible to a human and the gate is the only thing that sees it.
FOREHEAD draws at 14 and above throughout; `kTileCut` (10) is used by SEA SALT,
SUDOKU and TOY BATTLE.

The renderer logs `No glyph for codepoint 8230` every time, on every screen, for
computed boxes as well as fixed ones. `sim-shot.sh` now **fails** on that line
rather than printing it into the trace and exiting 0. That is a better gate than
any table of strings could be, because it needs no table: it sees whatever the
app actually drew.

**What it cannot see, which matters as much as what it can.** The gate fires
only for text the renderer ACTUALLY DREW and truncated. An overflow that depends
on data the app has not got yet -- a pack not downloaded, an empty state nobody
reached, a name nobody has typed that long -- draws nothing, truncates nothing,
and passes. The shelf-wide sweep that found the XKCD bug visited each app's
OPENING screen; "every app is clean" means every app's first screen is clean and
no more than that.

A worked example, found by a peer within an hour of the gate landing: GET BOOKS
logs `No glyph for codepoint 10` -- a raw newline out of an OPDS feed reaching
`drawText` while it renders book titles. Two things follow. It is NOT the
truncation case, so the width tooling explains nothing about it (the gate says
so itself now, and splits its advice by codepoint). And the shelf sweep could
never have found it: Get Books needs Wi-Fi and a seeded `opds.json` before it
reaches that screen at all. Every network-fed app has that shape.

So the gate is a backstop, not a proof. Where a length is known before the
device sees it -- a generated word list, a downloaded content pack -- cap it at
BUILD time against the measured cut, so the device is never handed a string that
cannot fit and the gate has nothing left to find. That is what
`gen_forehead_words.py` does with `MAX_ENTRY_PX`, and it is the reason the
generator refuses rather than the panel revealing.

When it fires, measure the string:

```bash
tools_local/forehead/measure.py 20 'RESET EVERYTHING'   # 309px
```

### The simulator takes taps in PORTRAIT coordinates, always

A trap that cost a tester most of a session: `sim-shot.sh` SCALES taps from the
portrait 480x800 frame onto whatever is on screen -- it does not rotate them. So
on the landscape round and results screens,

    portrait_x = landscape_x * 0.6
    portrait_y = landscape_y * 1.667

PLAY AGAIN is `TAP:123,727` and DONE is `TAP:357,727`, not the coordinates you
read off the landscape screenshot. Tapped in landscape coordinates both look
DEAD, which is exactly what got reported before the scaling was worked out.

### Rendering it

```bash
python3 tools_local/forehead/seed_save.py         # fill the ornament first
CROSSPLAY_AUTOSTART=FOREHEAD ./scripts_local/sim-shot.sh '<input>' '<shots>'
```

Seed the card before judging any layout: the front door's ornament is sixteen
rounds of your record, and on a fresh card that panel is a rectangle with one
line of apology in it. `seed_save.py` writes the real save format, so it also
exercises the loader.

Landscape taps need converting, because `sim-shot.sh` normalises its input once
at startup against the portrait logical size:

```bash
land() { python3 -c "print(f'{round($1*479/800)},{round($2*799/480)}')"; }
```

Button presses (`UP`, `DOWN`) need no conversion and drive the whole round.

---

## 8. What is deliberately not here

- **No multiplayer.** This game is one device passed around a room, which is
  what `link/` exists to avoid needing. Two devices would be two rooms.
- **No per-player scores.** There is one device and any number of people
  holding it, so a per-player record would be a fiction. Everything the record
  keeps is "this device, lately".
- **No SD word packs.** 2722 entries is an evening several hundred times over,
  and a pack format is a parser, a doc, an installer and a failure mode when the
  card is missing. If it ever earns its place it goes the way the Study decks
  did, not before.
- **No tilt, no shake, no timer sound.** There is no IMU and no speaker.
- **No cut above `toybox_64`.** A cold look review measured the word at 9.4mm
  of capital in 768px of usable width and called it the biggest thing left on
  the table, correctly: two thirds of the panel's width is unused for a typical
  word. Mario's call, made against the flash cost (~100KB for a 96px cut) and
  the fact that 9.5mm already beats a phone playing the same game by nearly two
  to one and reads to about 2.4m. If a big or loud room ever makes it worth
  revisiting, the cheap route is the one the review pointed at and I had not
  costed: **pixel-double `toybox_64` for the word slot only**, which costs no
  flash and is exactly what a deliberate-pixel face wants. It needs a scaling
  glyph blitter, since `drawText` has no scale and doubling through the SDK text
  path would give up `measureText` and alignment with it.
