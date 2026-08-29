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

The rotation is worth following once, because it is the one fact in this app
the simulator cannot check. `docs/buttons.md`: GPIO0 is the key on the physical
LEFT in portrait and GPIO7 the one on the physical RIGHT.
`rotateCoordinates()` in `GfxRenderer.cpp` maps portrait logical _y_ onto panel
_x_, which is a quarter turn anticlockwise -- so the portrait right edge becomes
the landscape **top** and the portrait left edge becomes the landscape
**bottom**.

**If that is backwards on a real unit, the fix is two lines**: `kGotItKey` and
`kPassKey` at the top of `ForeheadActivity.cpp`. It is deliberately not
configurable. A settings row for it would be a permanent apology for never
having checked, and the screen labels its own edges, so a wrong mapping is
obvious inside the first three seconds of the first round rather than subtle.

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

| Screen                          | Orientation |
| ------------------------------- | ----------- |
| front door, picker, how to play | portrait    |
| ready card, round, results      | landscape   |

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

---

## 5. The words

**2722 entries across 17 categories**, in flash as a `constexpr` table.

Written for this fork rather than lifted: entry lists are generated from
`tools_local/forehead/words/*.txt` by `gen_forehead_words.py`, in the same shape
as Insider's. Edit a `.txt`, run the script, commit both -- the generated header
is committed because a checkout has to build without Python.

The generator refuses three things, all of which fail silently at 220ppi:

- **Non-ASCII.** Toybox's face is subset to ASCII and a glyph the font does not
  have draws as **nothing** -- no box, no fallback, no log line. A curly
  apostrophe pasted from a web page produces a card with a hole in it. This one
  is not hypothetical: it caught `SÉANCE` on the first run.
- **Anything over 22 characters**, which is what the card ladder is tuned
  against.
- **A duplicate inside one list**, which defeats the no-repeat deck silently:
  the mask marks one index and the other is still in the bag.

A word appearing in **two** categories is fine and often right -- CLAPPING is
both an action and a sound -- so those are reported and allowed. Only one
category is ever in play.

### Every list is bigger than a round can hold

`kMaxCards` is 128 and every category has more entries than that, so a round
can never lap its own deck and show you the same word twice. This is asserted
exhaustively in `host-tests/forehead/`, and it is how STORYBOOK (123) and MYTHS
AND MONSTERS (126) were found: both were under the cap and both grew. **If you
add a category, it needs more than 128 entries.**

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

407k checks. Three kinds, and the third is the one worth copying:

1. Hand-built states for each rule.
2. Deck soaks. The interesting deck bug is the **last card of a lap** --
   rejection sampling on positions would expect ~189 draws to find the one card
   left, and a spot check reaches that case roughly never. There is a test that
   sets up exactly it, for 200 seeds.
3. An exhaustive pass over all 2722 entries and all 17 slices: ASCII, length,
   case, no doubled spaces, slices tile the array with no gap or overlap, no
   duplicate within a list, every list bigger than `kMaxCards`. The generator
   already refuses bad content, but **the generator is not what ships** -- the
   committed header is, and a hand-edit would sail past a script nobody re-ran.

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
