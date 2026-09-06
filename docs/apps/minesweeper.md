# Minesweeper

Eight by ten with ten mines. The eleventh game in this fork, and the second run
of the build cycle -- chosen because it is the least like the game that cycle was
written from, which is the only way to find out whether a method generalises or
just describes one case.

## The rules, and the two that are built in

Tap a cell to dig it. A dug cell shows how many mines touch it, or floods its
neighbours if that number is zero. Hold a cell to plant a flag. Tap a number
that already carries all its flags to open the rest of its neighbours at once.
Dig every safe cell to win; dig a mine to lose.

Two properties are **constructed rather than sampled for**, which is this
project's rule about promised properties:

**The first dig is always safe, and always floods.** Mines do not exist until
you tap; they are laid afterwards, excluding that cell *and its eight
neighbours*. Excluding only the cell would leave the opening a lone number and a
guess, which is the failure classic Minesweeper is famous for. Re-rolling until
the first tap missed would be the same result bolted on, and would loop on a
dense board.

**A flag stops the flood and cannot be torn off by it.** Losing a flag you
placed deliberately to a cascade you did not aim is the most annoying thing this
game can do.

## Chording: a shortcut, never a second kind of move

Tap a revealed number whose flag count already equals it and every remaining
neighbour opens at once. It is the standard Minesweeper move, and the card that
asked for it (#249) called its absence "the difference between playing and
clicking": without it the same three-cell shape is opened by hand over and over
on every board.

**The gesture is a plain tap**, on a cell the player has learnt is spent. No new
gesture was added, and that is deliberate: `wasTouchTap` has no duration gate on
this platform, so a hold-to-chord would have had to fight the flag hold that
already owns a long press. A press held past the 400ms flag threshold is
swallowed and does not chord -- see the section below for why that is load
bearing rather than an oversight.

**A chord that finds a mine detonates.** The move trusts the player's flags, so
a number whose count is right and whose placement is wrong opens a mine and ends
the game. That is the real game, and the gentle alternative -- refusing the
chord unless the flags are correct -- cannot be built without the game telling
the player something it should not know. Mario's call, recorded on the card.

**A count that does not match does nothing, silently.** No partial opening of
"the ones that must be safe": that is the game doing the player's reasoning for
them. `chord()` returns false and touches nothing, and the activity repaints
only on a change, which is how this game already says no.

The correctness is structural rather than promised: `chord()` calls `reveal()`
on each neighbour in turn, so it reaches **byte-for-byte the state that tapping
those cells one at a time would**. The flood, the loss, the win check and the
saved board therefore need no knowledge of chording at all, and there is no
second implementation that has to agree with the first. `reveal()` refuses
everything once it has set `Lost`, so a detonating chord stops exactly where the
taps would have. The host suite asserts that equivalence directly, on hand-built
boards and on 585 chords found across 400 dealt ones.

The routing -- what a tap on a cell *means* -- lives in `dig()` in the core,
next to the rules, not in the activity: it is a rule, and the core is the only
layer the host suite can prove.

The simulator is not nothing here, and it is worth being exact about what it is.
It substitutes its own `HalGPIO` rather than compiling `lib/hal`, and that stub
does model suppression and touch-down routing -- a scripted `TAP:x,y,hold-ms`
really does exercise the hold, the swallow and the lift, which is how the
screenshots below were taken. What it cannot model is the device's **two** slop
thresholds against its one, and that gap is precisely where the bug in the
section below lives. Real, but not covering the case that mattered.

The how-to gained a fifth page for it. Every other rule in this game can be
found by tapping the board; this one cannot, because the cell it wants is one
the player has been taught is finished.

## Tap to dig, hold to flag

Two verbs on one cell, and no right click. The first version made this a mode --
a DIG/FLAG capsule at the bottom -- on the belief that a tap is the only gesture
the device gives games. **That belief was false.**
`MappedInputManager::isScreenTouchHeld` and `swallowCurrentTouch` are public,
`KeyboardEntryActivity` already long-presses, `sim-shot.sh` has always taken
`TAP:x,y,hold-ms`, and the SDK's `InputLongPress` is defined, routed and
host-tested. `swallowCurrentTouch` exists precisely so a hold can fire while the
finger is still down without the lift also arriving as a tap.

Removing the mode removed the largest solid black on the screen -- inverting on
every toggle, against the ink-budget rule -- a convention that was backwards
(a filled capsule means "tap me" everywhere else here, and the filled half was
the one where tapping did nothing), and an indicator six hundred pixels from
where the tap lands in a game where one wrong tap ends the run.

Sliding to another cell restarts the hold, so dragging never plants a flag
nobody meant, and the cell under a finger draws a heavy border while the hold
builds: on this panel an unmarked hold is indistinguishable from a tap that
missed.

`swallowCurrentTouch` fires **unconditionally**, on every hold that reaches
400ms, including holds on revealed cells where `toggleFlag` does nothing. That
looks like a wart -- why eat a contact the hold ignored? -- and making it
conditional is a way to dig a cell the finger never rested on:

`isScreenTouchHeld` reports `touchUpPoint`, the **latest** contact sample, with
no slop gate at all. `wasScreenTapped` reports `touchDownPoint`, the **first**
sample, and stays valid until motion passes the 59px release slop
(`TOUCH_TAP_RELEASE_SLOP_PX`, against a 28px stationary slop and a 60px cell). A
finger that lands on covered cell A, rolls onto revealed cell B and rests there
re-arms the hold at B; the hold fires at B, `toggleFlag` refuses, and the lift
then taps **A**. The window is about one cell wide, and the cell it digs can be
a mine.

Suppressing the contact is the only thing making those two positions unable to
disagree. The price is that a press held past 400ms on a number does not chord.
Chording is a quick tap, which is what it is everywhere else, and that is the
cheap half of the trade. Neither the host suite nor the simulator can catch the
other half: the suite never touches the activity at all, and the simulator --
which does model the hold, the swallow and the lift -- has one slop threshold
where the device has two, so the 29-59px window does not exist there to be hit.

## The grid is hit-tested, not registered

`toybox::kMaxInteractions` is 24. A minefield has 80 cells. Registering a button
per cell silently dropped fifty-six of them.

That is not a limit to raise: it is sized for screens made of discrete controls,
and a regular grid is not one. `cellRect` and `cellAt` are exact inverses tested
against each other from all four corners of every cell, which is this fork's
existing rule -- hit-testing derived from the pixels, never computed a second
time -- arriving from a direction nobody had hit before.

## One state machine, not two

The cycle asks for a shell machine and a game machine. This game has only the
shell. Its game machine already exists as `Status` in the rules, where it
belongs; restating it in the flow would be two facts that must agree. A game
with turns needs both because "whose turn" is not a rules concept the core can
own alone. A solitaire does not.

## What the critics found

Two cold reviewers, after the game was "finished" and a 3.2M-assertion suite was
green.

**The flood was silently truncated in 41.8% of games.** Cells were deduplicated
at push but marked revealed at pop, so a cell touched by several zeroes enqueued
several times, overflowed the eighty-slot queue, and the excess was dropped. On
an empty board one tap opened thirty of eighty. The symptom is a blank revealed
cell beside a covered one, which cannot happen in this game.

**The suite missed it because its bounds were weak.** It asserted "more than one
cell opened" and "more than ten cells opened" against a true answer near forty.
Both passed on a badly broken flood. That is the most useful lesson here: a weak
bound is how a test agrees with a bug.

Four more mutants survived and are now killed: two that left whole rows
permanently mine-free while keeping the count at ten, one where `start()` did
not clear a finished board (exactly what PLAY AGAIN does), and one where a
flagged safe cell counted as cleared.

The design critic measured the board at half the fork's ink density, found the
only screen that never called `insetContent` (its control bled into three
bezels), found the board had no frame so it dissolved into the page late in a
game, and found the flag glyph could not work **at any size** because every
hand-drawn version put the pennant and the pole at the same height. It is
Lucide's now, which is what the design language says to do.
