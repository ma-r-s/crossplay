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
| 9   | SESSION         | The score so far, and the only way to end the session.         |

Back unwinds to the menu, except inside a round, where it opens the **pause**,
and on the reveal, where it returns to the menu.
Back must never be a synonym for the forward button beside it: on the reveal it
once dealt the next round, which reads as working right up until somebody wanted
to look at the last screen again. That is deliberate: if backing out re-dealt for the
same person, a clue-giver could quietly hunt for an easy axis, and the deck's
strangest cards would never be played.

## Leaving does not end the game

**A game in progress survives leaving the app and survives a power cycle.**
Home, the shelf, the idle timeout, a held power button, a flat battery: the
round, the hidden number, the marker and the session score are all still there
when WAVELENGTH is opened again.

That is not how it started. A cold tester on the guess screen pressed **Home**,
which sits one key from Back, walked back in through Games, and got NO SESSION
RUNNING. TAP START. Back was carefully handled -- it opens the pause, and the
guess screen advertises BACK PAUSES -- and Home was mentioned nowhere and
protected by nothing. Sleep was the same fault by a different route: deep sleep
on this chip is a chip reset, so the wake that says `[SHELF] Wake: resuming
WAVELENGTH` was starting the app from nothing.

**Persisted, not guarded.** A confirmation on the way out would have covered the
Home key and nothing else: it cannot be shown to a device whose battery has
gone, and the round is just as lost. And a device passed round a table gets put
down mid-argument, which is the case the auto-sleep suppression already exists
for.

**`onExit()` was not the fault, and fixing it would not have helped.** It runs on
both routes -- the Home gesture and the sleep both go through
`replaceActivity()`, which runs the outgoing activity's `onExit()` before the
new one enters -- and WAVELENGTH already called it. What it wrote was the
all-time record and the seen deck, and nothing about the evening in progress.
The round was not lost on the way out; it was never written down.

**Written at every position anyway.** `go()` writes the card on every screen
change and `step()` writes it on every move of the marker: about 120 bytes
beside a panel repaint that costs a hundred times more, and it means a crash, a
watchdog reset or a flat battery costs one screen rather than an evening. Chess
reached the same place first and says so at the call site -- it saves on the
completed move rather than the completed game.

**Two games in this fork still write only at the door.** TOY BATTLE and JAIPUR
both save in `onExit()` and when you walk out to their own menu, and nowhere
else; Toy Battle's `saveGame()` comment says outright that "the one that matters
is `onExit()`". That is enough for Home and for sleep, and it is not enough for
a panic or a battery. If either gets revisited, the shape here is worth
copying: a freestanding pack/unpack module beside the rules, host-tested, and a
write at every state change rather than at the exit.

**What comes back is where you were**, except HOW TO PLAY and the score sheet,
which are each one tap from the front door and are not a position anybody is in
the middle of. A screen whose round is missing from the file comes back as the
front door with the session intact rather than as a half-drawn board.

### The hidden number is on the card, in the clear

It has to be. Re-drawing it on resume would break the clue already spoken out
loud, and would hand the clue-giver the re-deal the pause screen exists to
prevent.

What protects it is where it lives, and here is exactly how far that goes,
because a reassuring summary would be worse than none.

- **The card in a reader** reads it, obviously. So does anything else on the
  card, including the firmware.
- **WebDAV cannot.** `WebDAVHandler::isProtectedPath()` rejects a path with a
  dot in ANY segment, unconditionally.
- **File Transfer will not list it.** The browser hides dot entries unless the
  owner turns on Show Hidden Files in Settings.
- **File Transfer WILL serve it to a direct URL, and that is a real hole.**
  `handleDownload()` in `src/network/CrossPointWebServer.cpp` checks only the
  LAST path segment, so `?path=/.crosspoint/wavelength.sav` passes the dot test
  on `wavelength.sav` and streams the file. It is not this app's to fix -- it is
  upstream's shared web server and it exposes every app's save file the same way
  -- but it is the honest bound on the paragraph above.

What that buys an actual cheat is small: File Transfer is an activity, so
starting it takes the game off the panel in front of everybody, and the phone
has to be on the same network and know the path. Anyone willing to do that in
the middle of a round can look over the clue-giver's shoulder for less effort.

Obfuscating the byte would be theatre: the mask would sit in the same file and
this source is public.

**The route that did need engineering is the in-app one.** Resume never lands on
a screen that draws the number while the game says it is hidden. The peek is
the only screen that draws it, and the only saved screen that can BE the peek is
one where the clue has not been given yet, because the peek is one-way and
nothing later can go back to it. Resuming into the peek shows the strip with no
band; the number still costs a 400ms hold.

The one thing this does open, and it is small: a clue-giver who leaves at the
peek screen and hands the device on lets the next person hold the pad. At that
point in the round the device has not left the clue-giver yet by the game's own
model, and a guesser who looks has simply become the clue-giver and ruined their
own round.

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
| The hidden number              | THE NUMBER | THE TARGET                                             |

Two consequences worth keeping:

- **The verdict words are the scoring table's own** -- EXACT, ONE OFF, TWO OFF,
  and then the distance itself: THREE OFF, SEVEN OFF, NINETEEN OFF. The old set
  (TELEPATHIC / CLOSE / WARM / MISS) was a second vocabulary that contradicted
  the first: WARM sat below CLOSE with no COLD anywhere, so reading the two
  together says two off beats one off.

  **MISS is gone, and it was the last word doing that job.** The banner reports
  the GUESS'S distance and the figure beside it is the ROUND'S points, so a
  seven-off guess with a right side call printed MISS above +1 -- a verdict and
  an arithmetic that cannot both be true. There is no reading of those two that
  agrees, and a tester read it as a bug in the scoring. A distance is a fact
  rather than a judgement, so SEVEN OFF beside +1 says what happened.
- **The front door and the end screen count the same way.** `ROUND 7, 8 POINTS`
  against `8 POINTS IN 5 ROUNDS` one tap away described one evening with two
  numbers, because the first counted the round about to start. The round about
  to start is on the BUTTON now (`PLAY ROUND 7`), where it is an instruction.

**The side-call screen was the one exception, and it is fixed now.** It was left
alone on the argument that the screen leaves co-op if the shelved work in
`wavelength-teams.md` lands, so the words might be thrown away. What that
actually bought was one screen saying TARGET where every other screen says THE
NUMBER, and the only screen that ASKS for the side call never using the words
the rules and the scoring table both use. It reads:

| Was                               | Is                                 |
| --------------------------------- | ---------------------------------- |
| `IS THE TARGET NEARER`            | `SIDE CALL: IS THE NUMBER NEARER`  |
| `GUESSERS DECIDE. NOT THE GIVER.` | `THE CLUE-GIVER STAYS OUT OF THIS.`|
| `RIGHT ANSWER IS WORTH ONE POINT.`| `THE SIDE CALL IS WORTH ONE POINT.`|

The second one is the negative half of the old sentence rather than both halves,
because CLUE-GIVER measures 134px against GIVER's 64 and the whole sentence came
to 458px of 448. The negative half is the one that prevents the fault.

THE GIVER and RIGHT ANSWER no longer exist anywhere in the app.

## The two buttons that said the opposite of what they did

Both on the way to and from the score sheet, and between them they made the
score sheet unreadable as a screen: nothing on it was called what it was.

- **`FINISH AND SEE THE SCORE` did not finish anything.** It opens the score
  sheet, whose own first button walks straight back into the next round with the
  session intact. It is `SEE THE SCORE SO FAR` now.
- **`START OVER (CLEARS THIS SCORE)` started nothing.** It is the only control
  in the app that ends a session, and it returns to the front door. It is
  `END THE SESSION` now, it is drawn as an outline rather than as a third solid
  bar the thumb finds by reflex -- there is no red on a 1-bit panel, so
  destructive can only look like DIFFERENT -- and the two lines above it say
  what it costs and what it does not: `THIS CLEARS THE SCORE ABOVE.` /
  `THE ALL-TIME RECORD IS KEPT.` Same shape the pause screen uses for
  `ABANDON THIS ROUND`.
- **`BACK TO THE GAME` described a return**, and what it does is start the next
  round. It says `PLAY ROUND 3`, in the front door's own words, so the two
  screens that offer the same thing offer it in the same sentence.

## Three smaller things a cold table found

- **The all-time chart had two fills and no legend.** The 6+ row was dithered on
  the argument that it is the row you want to be short; what it produced was a
  reader guessing at the rule twice, wrong both times, with the faintest marks
  on the page on the row that says most about how the table is doing. One
  material now, and both columns are named: `HOW FAR OFF, ALL TIME` on the left
  and `ROUNDS` on the right, on the heading line the chart already had.
- **`IN 0 SCORED ROUNDS` still printed an average.** A table that had played
  only the practice round was scored 0.0 against a benchmark of 2.5 -- an
  average over no rounds, presented as a verdict on the one round the game
  itself refuses to count. It says so instead, and the front door's own
  all-time average prints `--` rather than `0.0` before anything has scored.
- **`A GOOD TABLE 2.5` had no unit and no explanation anywhere.** The pair is
  headed `POINTS PER ROUND` now, which is the same phrase the front door uses
  for the all-time figure.

## What the tests can and cannot prove

`host-tests/wavelength/` covers the rules exhaustively, gates every drawable
string's WIDTH via `tools_local/wavelength/check_widths.py` against the real
glyph tables, and round-trips the save file -- including a live round, a version
1 card, a truncated one, an impossible slot and a session that has been ended.
`WavelengthSave.cpp` is freestanding for exactly that reason: the round that
survives the Home key is provable on a laptop.

**UNDERSTOOD IS NOT MEASURED, and the width gate conflated the two.** It marked
a draw covered the moment it recognised the SHAPE `caps(rect, identifier,
slot)`. If that identifier had no `snprintf` behind it -- a `const char*` picked
out of a table of words -- nothing ever measured it, and it was missing from the
overflow column and from the unmeasured column at the same time. The reveal's
verdict, both scoring tables, the peek's advice column and all three slot
numerals were invisible that way: 69 strings were reported measured out of 220.
Sites are recorded now only when a width was actually checked, expressions are
expanded into every literal they can evaluate to, and the final sweep takes the
text argument as a whole expression rather than demanding a literal or a bare
name -- so a draw it cannot resolve is REPORTED rather than silently absent.

Two source changes came out of that. The peek's advice table was renamed
`kAdvice`, because HOW TO PLAY has a `kLines` of its own drawn into twice the
width and the gate resolves a table by name across the whole file: two tables
sharing one name measured the 224px column's strings against 448 and reported
clean. And a name declared twice with different words now maps to nothing, the
way a rect declared twice already did.

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
