# Minesweeper

Eight by ten with ten mines. The eleventh game in this fork, and the second run
of the build cycle -- chosen because it is the least like the game that cycle was
written from, which is the only way to find out whether a method generalises or
just describes one case.

## The rules, and the two that are built in

Tap a cell to dig it. A dug cell shows how many mines touch it, or floods its
neighbours if that number is zero. Hold a cell to plant a flag. Dig every safe
cell to win; dig a mine to lose.

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
