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
| 2   | PASS LEFT       | No names, no seats, no turn order to remember.                 |
| 3   | PICK ONE        | Two spectra, chosen before the target is drawn.                |
| 4   | YOUR TARGET     | Hold to reveal. The band shows only while a thumb is down.     |
| 5   | SAY IT OUT LOUD | Both ends large, target confirmed hidden.                      |
| 6   | DIAL            | Flat on the table. Tap a slot to move. One press locks.        |
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
the next PASS LEFT and on the end screen, because the board is public in this
game and so is walking away from a target you did not like.

## Whose game is on the card

A round in progress is written to `/.crosspoint/wavelength.sav` on every screen
change and every move of the marker, not on the way out. Home destroys the
activity and deep sleep is a chip reset, so anything only written by `onExit()`
is a round the table loses.

That save had **no notion of going stale**, and this is a party game, so a
different group is the normal case rather than the edge one. Days later a new
table opened the app and was dropped into the middle of the previous group's
round 2: a clue they never heard, a score they did not earn, and nothing on the
panel saying so. **The session goes stale, not just the round in flight.** A
save sitting on the front door with round 7 and 23 points behind it has no round
in flight at all, and the menu's own button offers to play its round 7 into its
score -- the same bug with one tap in front of it.

**The axis is the boot, because this device cannot measure elapsed time it can
rely on.** Wake is a chip reset, so `millis()` restarts. The wall clock is a
fitted part on some boards and absent on others (plain X4 has no RTC), it is
only ever set by an NTP sync over Wi-Fi, and a flat coin cell returns it to
1970. A rule resting on it would behave differently on two devices sitting on
the same table. So the save records which run of the chip wrote it -- a
zero-initialised static, seeded once from `esp_random()`, which is exactly "this
boot" because `.bss` is cleared on every reset. Within one boot the device has
not been away: Home and back, or the shelf and back, resumes silently and costs
nothing. Across a reset the answer is genuinely unknown.

**So a stale session is neither discarded nor resumed. It is offered.** Guessing
wrong one way drops a group into a stranger's game; guessing wrong the other
destroys a round somebody meant to come back to. Asking is wrong in neither, and
`IS THIS THE SAME GROUP?` costs one screen that only appears when there is
something to ask about. `CARRY ON ROUND N` occupies **exactly the front door's
`PLAY ROUND N` rect**, so a returning table's blind tap is the safe answer, and
`START A NEW GAME` sits below every control the menu has, so no remembered tap
can reach the one control that throws an evening away. Back decides neither and
leaves the card untouched, so the question comes back.

**The clock informs the question and never decides it.** When the device can
say, the screen adds `LEFT 6 DAYS AGO`, which answers "is this ours?" outright
where a round number does not. When it cannot -- no stamp, no clock, or a clock
that moved backwards -- the row is simply absent and everything else is
unchanged.

Save version 3. A v2 card (any build up to this one) carries no boot id, which
reads as "cannot know", so an upgrade mid-session asks once. A v1 card carries
the record and no evening, and asks nothing. **Every version's length is named**
rather than just the current one: the check deciding whether a session block is
present used to compare against the whole file, and appending to the tail would
have made every v2 card look truncated and silently dropped the session it was
carrying.

**Where the boot axis is inert, and it is a setting.** `Sleep Timeout` has a
`Never` position, and Developer Mode inhibits sleep for as long as it is on. On
a device left in either, the chip never resets on its own, so the boot never
changes and a new group is dropped into the previous group's round exactly as
before -- bounded by battery life rather than by days. That is not a regression,
but it is the one configuration in which this fix does nothing.

**And a chip reset is commoner than "the device slept".** Hacker News and Study
call `silentRestart()` on touch boards, so WAVELENGTH, another app, WAVELENGTH
is a new boot thirty seconds later and the same table is asked. So is a panic
reboot. Every one of those errs on the safe side -- the question, never a silent
adoption -- but the friction is real and is the half that surprises.

**A firmware downgrade drops the record.** A build older than this one refuses a
v3 card outright and starts from nothing, and its next write replaces the file.
Older builds already did this to each other's formats; the bump makes it
reachable again.

## Moving the marker

**A tap places the marker on the slot tapped**, the keys nudge it by one, and
`LOCK IT IN` -- or `ENTER`, where the board has that key -- locks. Nothing else
moves it: hold-to-sweep was deleted rather than
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
  target has been seen the footer dims to LOOK FIRST, using the same
  `disabledStepperStyles()` the front door uses for END SESSION, and a bare tap
  on the peek pad relabels it PRESS AND HOLD IT. Drawn solid black and silent,
  it read as a dead device: a cold player tapped both controls twice each and
  stopped playing. A difference of KIND, not degree -- a subtler cue gets
  rationalised away inside twenty minutes. The LOCK carried the same nudge until
  it stopped being a hold; an ordinary button does not need one.
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

245 pairs, the retail CMYK deck (designers Alex Hague, Justin Vickers, Wolfgang
Warsch), generated into `WavelengthPairs.h` by
`tools_local/wavelength/gen_pairs.py` from `pairs_en.txt`.

**SEVEN CARDS ARE DELIBERATELY REMOVED**, on Mario's instruction after cold
players stalled on them, in three kinds:

- **Synonyms**, where there is no thing that is maximally one and minimally the
  other: `STRANGE/WEIRD`, `MANNERS/ETIQUETTE`, `PROHIBITED/ILLEGAL`.
- **Same direction**, where both ends mean the same thing and one is just more
  so, leaving the strip no opposition to run between: `TINY/SMALL`,
  `IDENTICAL/SIMILAR`.
- **Not a continuum**, where nothing sits between the ends:
  `SLYTHERIN/GRYFFINDOR` (which also silently excludes two of the four houses)
  and `TOCK/TICK` (no shared intuition to rate anything against).

`HOMOGENOUS` was also corrected to `HOMOGENEOUS`: the card paired the biology
spelling with the ordinary `HETEROGENEOUS`.

So the count differs from retail BY DESIGN. If a future spot-check against the
physical box finds 252, that is expected and not a transcription fault.

> **PERMISSION, on Mario's testimony.** On 2026-08-31 Mario stated that CMYK
> granted permission to use these pairs, given in a Reddit exchange. The
> exchange is not archived in this repository and no link is cited, because
> inventing a citation would be worse than recording the provenance honestly. If
> the thread is still reachable, put the link here and this paragraph can become
> a reference instead of a testimony.
>
> **The transcription is a different claim and is still unchecked.** Permission
> to use CMYK's cards is not evidence that these lines ARE CMYK's cards.
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
with the clue-giver still holding the device, and skipped PASS LEFT entirely,
which is the only screen telling the table to change hands -- so the same person
gave two clues running with the score still counting. Taps within `kSettleMs`
(500ms, about one refresh) of a view change are swallowed. People double-tap
BECAUSE the panel is slow, so the panel's own latency is the right thing to
spend.

**Abandoning announces itself.** Back from a committed screen passes left, but
silently it looked identical to a normal pass, so a clue-giver who disliked
their target could back out and redraw with nobody at the table any the wiser.
The next PASS LEFT says LAST ROUND WAS ABANDONED.

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

**`WavelengthSave.cpp` was compiled by nothing until 2026-09-03**, though its
own header said the tests existed. The layer carrying a game in progress across
a chip reset had no test of any kind, which is how it came to ship with no
notion of a saved session going stale. It is in the run script now, and
`pack`/`unpack` round-trip, the v1 and v2 fixtures, and every branch of
`resumeFor` are covered.

**The activity's wiring around that decision is not.** No activity in this fork
is host-testable, so which screen a load lands on, `saveState()` refusing to
write while the question is up, and Back deciding neither answer were verified
by driving the simulator, not by a test. The simulator does not compile
`lib/hal`, so that is evidence about the app's logic and not about storage.

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
`endWord()` walks the cut ladder down because 54 of the deck's pairs do not
fit at the display cut.

**The LOCK is an ordinary button and the guard is GEOMETRY.** It was
`HOLD TO LOCK`, a 600ms hold on a bar spanning x=80..399 -- and the strip the
table has been tapping all round answers out to x=226, so the commit control sat
directly under it. Two faults followed. Nothing on the panel stated the
duration, which makes a hold a guessing game rather than a safeguard; and firing
mid-contact drew the reveal under a finger that was still down, so the lift-off
pressed whatever the new screen put there and four cold testers advanced past
their own score.

The bar is now `LOCK IT IN`, carries `ActionLock`, and is routed on the touch
RELEASE by the frame like every other control. The stray tap is stopped by where
it sits instead: `lockBarRect()` gives it only the NUMBER COLUMN's third of the
footer. Measured off the rendered screens rather than derived by hand, every
bound inclusive:

| what | x | y |
| --- | --- | --- |
| strip, where `dialSlotAt` answers | 40..226 | 70..663 |
| `LOCK IT IN`, where `ActionLock` routes | 240..399 | 722..783 |
| the reveal's `NEXT ROUND` | 16..463 | 640..701 |

So the two columns are disjoint, there are 59px of dead paper below the strip's
lowest live row, and the bottom-left corner clearance goes from 64px to 224px
while the right stays at 64. Nothing below the strip is live at all -- not a
smaller target, no target.

**Separate the coordinates; do not rely on the action being harmless.** The
touch table goes live before the panel has painted the new screen, so a rect
whose MEANING changes across a transition is the fault, not the action behind
it. No pixel answers both tables above, and that is checked by ROUTING taps
through them rather than by reading the source.

The one place two meanings still share pixels is inbound: `SAY IT OUT LOUD`'s
forward button is `footer(62, kMargin)` = x=16..463 y=722..783, which CONTAINS
the lock's rect. `kSettleMs` covers exactly the window in which the change is
invisible, because the panel has not painted yet; after it the button is visible
and labelled. The lock is routed BELOW that window deliberately. Separating it
would mean either reflowing the number column -- which moves the game's main
action out of the footer every other screen trains the table to look at -- or
shortening a label the wording pass settled, at the cost of a silent truncation.

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

**Whether the boot is the right axis in a real evening.** It is right in the
cases that were reasoned about, but the friction it buys -- a table that leaves
the device idle long enough to sleep on the front door is asked once on waking
-- has only been seen in a simulator, where the sleep timeout and a real party's
rhythm are both absent.
