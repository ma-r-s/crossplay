# WAVELENGTH

The spectrum party game, one device passed round a bar table. A strip runs
between two opposing words, slot 1 at the bottom and slot 20 at the top. One
player sees a hidden target and says a single clue out loud that, to them, sits
exactly there. Everyone else argues, moves the marker, locks it in, and calls
which side of their guess the target was really on.

Three layers, as the shelf expects: `WavelengthCore` is the rules and nothing
else (freestanding, host-tested), `WavelengthScreens` draws them (freestanding
builders over plain models), `WavelengthActivity` is the only part that knows
about hardware.

## The rule everything else defers to

**The board is public and only the target is hidden.**

Two cold critics, briefed as players rather than reviewers, independently found
the same fatal flaw in the first design: a six-inch greyscale panel held in one
person's hands makes the board, the marker and the ends as private as the
secret, and that person then holds four powers at once, being the only one who
can see it, move it, lock it and referee it. In the physical game the dial sits
in the middle of the table and you can put your hand on it.

So from the clue onward the device **lies flat and anyone may touch it**. The
holder is not a role. Any change that quietly takes the board back into one pair
of hands is a regression, and the first revision made exactly that mistake by
passing the device back to the clue-giver for the reveal.

## The round

| #   | Screen          | What it is for                                                 |
| --- | --------------- | -------------------------------------------------------------- |
| 1   | MENU            | Front door. Headline is the hit target. Record and ornament.   |
| 2   | PASS THE DEVICE | No names, no seats, no turn order to remember.                 |
| 3   | PICK ONE        | Two spectra, chosen before the target is drawn.                |
| 4   | YOUR TARGET     | Hold to reveal. The band shows only while a thumb is down.     |
| 5   | SAY IT OUT LOUD | Both ends large, target confirmed hidden.                      |
| 6   | DIAL            | Flat on the table. Tap or hold to move. Hold the bar to lock.  |
| 7   | WHICH WAY       | The end-call, worth one point.                                 |
| 8   | REVEAL          | Full refresh. Band, guess, points, verdict.                    |
| 9   | SESSION         | Totals against a reference, and the ornament again.            |

Back unwinds to the menu, except inside a round, where it opens the **pause**,
and on the reveal, where it returns to the menu.
Back must never be a synonym for the forward button beside it: on the reveal it
once dealt the next round, which reads as working right up until somebody wanted
to look at the last screen again. That is deliberate: if backing out re-dealt for the
same person, a clue-giver could quietly hunt for an easy axis, and the deck's
strangest cards would never be played.

## The pause, and why abandoning is counted

Back used to abandon the round silently, which was three faults wearing one
gesture. There was no on-screen way out of a round at all; the scoring table
lived only on the practice reveal, so "how many points is one off again?" cost
the round to answer; and **the clue-giver could re-deal until they liked their
target**. That last one is worse than a cheat: the screen before it has just
told everyone else to look away, so the game itself clears the room, and a
re-deal that preserves the round number and the score is indistinguishable from
playing properly.

So Back opens a pause carrying the scoring table, `RESUME THE ROUND`, and an
explicit `ABANDON THIS ROUND` that says what it costs. Back out of the pause
resumes: the safe direction is the default. **Abandons are counted and shown** on
the next pass screen and on the end screen, because the board is public in this
game and so is walking away from a target you did not like.

## Moving the marker

**A tap places the marker on the slot tapped**, the keys nudge it by one, and
`ENTER` locks. Nothing else moves it: hold-to-sweep was deleted rather than
repaired, because dragging 17 to 3 landed on 12 and dragging back landed on 10
(the position is sampled on touch-down), and tap-to-place already saves the
nineteen taps it was added for. A broken gesture earning nothing is a deletion.

**The hit test stops where the strip's column stops**, and a tap one slot past
either end clamps to that end. Full-width zones meant that pointing at the
screen mid-argument changed the answer, and a tap just under slot 1 that did
nothing read as a dead device.

**Bounding one of two input paths fixes nothing.** The screen also registered
two full-width step regions from the original design, so after the hit test was
bounded the mark still moved. Two paths to one control, one fixed, looks exactly
like a fix that worked. They are gone; so is `dialDirectionAt`, which the sweep
had left used by nothing but its own test.

 A cold player tapped near the
top of the strip expecting to jump there, moved one slot, and faced ten refreshes
to cross the board. A held finger sweeps the marker along under it and stops
where the finger stops. It is a sweep, not a runaway repeat,
which matters on a panel that repaints between steps.

`dialSlotAt()` and `dialDirectionAt()` each re-derive `layout()`'s arithmetic,
and two copies of one layout is how a tap zone drifts away from the marker it
sits under. Neither copy looks wrong alone, so `host-tests/ui` asserts them
against **each other** over every point on the panel, plus that every slot is
reachable by a tap: a rounding error at either end silently makes slot 1 or 20
untappable, and those are the two the deck's clearest clues point at.

The marker resets to the middle of the strip each round. Carrying the last
round's slot over reads as a suggestion, and the device is the one participant
that cannot have an opinion about the answer.

## What stops the clue-giver

Three things, because the clue-giver is the only player who can break the game
by accident:

- **The peek cannot be skipped, and the guard is a DURATION.** A tap is a
  touch-down before it is a release, so a guard asking whether the pad was
  touched is satisfied by the very tap that reveals nothing. It means the band
  was on the panel for 400ms.
- **A control that cannot act must not look like one that can.** Until the
  target has been seen the footer dims to SEE THE NUMBER FIRST, using the same
  `disabledStepperStyles()` the front door uses for the end-of-session button,
  and a bare tap on either hold control relabels it HOLD IT DOWN TO SEE (or TO
  LOCK). Drawn solid black and
  silent, it read as a dead device: a cold player tapped both controls twice
  each and stopped playing. A difference of KIND, not degree -- a subtler cue
  gets rationalised away inside twenty minutes.
- **The peek is one-way.** No route back to the target once the clue is passed.
- **The clue screen says `YOU DO NOT TOUCH IT AGAIN.`** Nothing else on the
  device stops the one person who knows the answer from dialling it in, and the
  device is about to hand them the strip and the scoring end-call.

## Scoring

Exact 5, off by one 3, off by two 1, nothing beyond. A correct end-call adds 1.

**At slot 1 and slot 20 there is only one end-call.** Nothing sits above 20, so
calling toward the top there cannot be right and cannot be argued for: it is a
strictly dominated option, which is a trap rather than a decision. Those two
screens offer the answer that exists and say why.

Round one is a practice round and does not score, because every table's first
round is a zero and that is the worst place to land a discouraging number.

Two properties are asserted exhaustively in `host-tests/wavelength/`, because
both encode decisions that would be easy to reverse by accident:

- **No slot is dominated.** An earlier draft kept the target away from the ends
  so the five-wide band always fitted, which made slots 1, 2, 19 and 20 strictly
  worse guesses than 3 and 18. Targets are unrestricted.
- **A perfect round is unbeatable.** The end-call is made before the reveal, so a
  table that locked exactly cannot have called either way correctly; it counts
  as correct anyway. Without that, an off-by-one with a good call outscores a
  perfect round and nobody can explain why.

## The deck

252 pairs, the retail CMYK deck (designers Alex Hague, Justin Vickers, Wolfgang
Warsch), generated into `WavelengthPairs.h` by
`tools_local/wavelength/gen_pairs.py` from `pairs_en.txt`.

> **PERMISSION, on Mario's testimony.** On 2026-08-31 Mario stated that CMYK
> granted permission to use these pairs, given in a Reddit exchange. The
> exchange is not archived in this repository and no link is cited, because
> inventing a citation would be worse than recording the provenance honestly. If
> the thread is still reachable, put the link here and this paragraph can become
> a reference instead of a testimony.
>
> **The transcription is a different claim and is still unchecked.** Permission
> to use CMYK's cards is not evidence that these 252 lines ARE CMYK's cards.
> Twenty pairs spot-checked against the physical box would settle it; nobody has
> done that yet.

The transcription came via `github.com/heybenchen/wavelength`, which carries no
licence file and no provenance note, so treat `pairs_en.txt` as a working
transcription to be spot-checked against the physical cards rather than as an
authoritative copy.

**The generator's job is refusal, not formatting.** It measures every end word
against the real font tables and rejects anything that cannot fit, so an
overlong word cannot reach the device and be truncated into a word that simply
stops. `advanceX` in the generated cuts is 12.4 fixed point, sixteenths of a
pixel; reading it as pixels makes everything sixteen times too wide.

A character count is not a width. `LIVED IN` and `GROWN-UP` are both eight
characters and differ by 70px at the display cut.

## Two things the panel's own slowness costs

**A double tap crosses screens.** Consecutive footers land 6-7px apart, so the
second tap of a pair hits the next screen's button: it skipped SAY IT OUT LOUD
with the clue-giver still holding the device, and skipped the pass screen entirely,
which is the only screen telling the table to change hands -- so the same person
gave two clues running with the score still counting. Taps within `kSettleMs`
(500ms, about one refresh) of a view change are swallowed. People double-tap
BECAUSE the panel is slow, so the panel's own latency is the right thing to
spend.

**Abandoning announces itself.** Back from a committed screen passes left, but
silently it looked identical to a normal pass, so a clue-giver who disliked
their target could back out and redraw with nobody at the table any the wiser.
The next pass screen says `1 ROUND ABANDONED SO FAR`, in the same count shape
the pause screen uses, so the second abandon does not read as a different event
from the first.

## The words, fixed 2026-09-01

The app had **four names for two roles**, **four for one scoring event** and
**three for the movable marker**, which is how a table ends up arguing about
what the screen means rather than about the clue. One name each, everywhere:

| Thing                          | The name   | What it replaced                                       |
| ------------------------------ | ---------- | ------------------------------------------------------ |
| The player who sees the number | CLUE-GIVER | THE GIVER, ONE OF YOU, THE NEXT PLAYER                 |
| Everyone else                  | GUESSERS   | EVERYONE ELSE (as a role), YOU                         |
| The end-of-round bet           | SIDE CALL  | RIGHT SIDE, CALL RIGHT, RIGHT ANSWER, CALLS WHICH SIDE |
| The bar the table moves        | THE GUESS  | THE MARK, YOUR GUESS                                   |
| The hidden number              | THE NUMBER | THE TARGET (outside the side-call screen)              |

Two consequences worth keeping:

- **The verdict words are the scoring table's own** -- EXACT, ONE OFF, TWO OFF,
  MISS. The old set (TELEPATHIC / CLOSE / WARM / MISS) was a second vocabulary
  that contradicted the first: WARM sat below CLOSE with no COLD anywhere, so
  reading the two together says two off beats one off.
- **The front door and the end screen count the same way.** `ROUND 7, 8 POINTS`
  against `8 POINTS IN 5 ROUNDS` one tap away described one evening with two
  numbers, because the first counted the round about to start. The round about
  to start is on the BUTTON now (`PLAY ROUND 7`), where it is an instruction.

**The side-call screen was deliberately left alone** (`IS THE TARGET NEARER`,
`NOTHING IS ABOVE 20.`, `THE ONLY WAY LEFT.`, `GUESSERS DECIDE. NOT THE GIVER.`,
`RIGHT ANSWER IS WORTH ONE POINT.`). Those are the worst strings in the app AND
the screen leaves co-op if the shelve work in `wavelength-teams.md` lands, so
fixing them may be work thrown away. THE GIVER and RIGHT ANSWER therefore still
exist, on that screen only.

## What the tests can and cannot prove

`host-tests/wavelength/` covers the rules exhaustively and now also gates every
drawable string's WIDTH, via `tools_local/wavelength/check_widths.py`, against
the real glyph tables.

**That gate cannot live in `host-tests/ui`.** `FakeTarget::measureText` returns
ten pixels a character there, so an overflow assertion in that suite is a
character count wearing a width's clothes, and a character count is not a width:
`LIVED IN` and `GROWN-UP` are both eight characters and differ by 70px. The ui
suite gets the geometry instead.

**The width gate reads the faces from the call site.** The three font slots
resolve to whatever `Faces` the ACTIVITY binds, and this app binds a literal
inline in `render()` rather than calling a helper. An audit that assumed the
slot names measured the title slot as `toybox_44` when it is `toybox_64`, 45%
narrow, and reported clean.

## Traps, all of them paid for

**Every string on a button is a width, not a label.** The end words on the
end-call were plain button labels, set at one fixed size, and the deck's four
longest (UNDERRATED LETTER OF THE ALPHABET at 444px against about 440px of
button interior) would have been ellipsised into a different word. `endButton()`
steps the cut ladder the way `endWord()` does. The same measurement caught
`PRACTICE. NOT SCORED.` at 570px of 448 within a minute of it being promoted to
a larger slot for emphasis.

**A slot is not a cut, and this app hit it three times.** The font slot decides
how big the ink is; `CutMetrics` only decides where it is centred. Passing a
mismatched pair is silent. It produced "NOT SCORED" rendered at 82px, a headline
truncated to "WAVELE", and a line drawn straight through the one under it.
`caps()` now derives the metrics from the slot so the two cannot disagree, and
`endWord()` walks the cut ladder down because 54 of the deck's 252 pairs do not
fit at the display cut.

**Do not pass a sentinel slot to mean "no marker".** It drew a phantom marker at
the foot of the strip. `drawScale` and `drawMarker` are separate for that
reason; a screen with no guess yet simply does not ask for one.

**The hold-to-reveal check must not return early.** A tap is a touch-down before
it is a release, so an early return ate every tap on the peek screen and the
game could be entered and never left. It surfaced only because six consecutive
screenshots came back byte-identical.

**`render()` must end with `displayBuffer()`.** Without it the activity enters,
draws into the buffer, and never publishes; the previous screen stays up and it
looks exactly as though the app never started. The tell is the absent
`[GFX] Time = N ms` log line.

**Auto-sleep is suppressed for the whole round**, released only at the menu.
There is a long stretch where the table argues and nobody touches the glass.

**The icon was spliced, not regenerated.** `tools_local/toybox/icons.txt` warns
that a straight regeneration drops `icon_yahtzee_32` and `icon_connectfour_32`,
whose SVG sources were never committed. Verified true before splicing: the
scratch generation contains neither. Icon count went 84 to 86.

## Not verified

**Ghosting on the peek, which is the one thing the simulator cannot answer.**
The target band is high-contrast, held, then hidden on a full refresh. If a
residue survives that refresh, the game leaks its only secret. Nothing in the
simulator models ghosting. This needs two minutes on a real X4 Pro: peek,
release, look at the strip.

**Whether the deck is fun**, which no test answers and which needs a table.
