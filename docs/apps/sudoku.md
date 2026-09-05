# Sudoku

The sixteenth game. Classic 9x9, generated on the device, graded by the hardest
technique it actually requires. Solitaire: no link layer, and that is a decision
rather than an omission.

Four layers, as usual: `SudokuCore` (the rules, freestanding), `SudokuGame`
(what the player touches, freestanding, and also the save), `SudokuScreens`
(freestanding builders), `SudokuActivity` (the only part that knows about
hardware).

## The promise the whole thing exists to serve

**A puzzle is exactly as hard as its label says.** Not estimated from the clue
count, which is the usual dodge and is close to meaningless: the generated
puzzles average 27.5 clues at every level, EASY and EXPERT alike.

Difficulty is the hardest technique a puzzle _requires_, and both halves of that
are established rather than hoped for:

- **The ceiling is constructed.** The dig is driven by a bounded solver, so a
  puzzle can never need a technique above its band. If the solver cannot finish
  the grid without the clue that was just removed, the clue goes back.
- **The floor is proved.** `carve()` grades the finished puzzle with the same
  solver and reports what it made. `generate()` discards anything that does not
  grade as asked. Nothing unverified reaches a player, and nothing is ever
  relabelled.

Uniqueness needs no separate check at all, which is the neatest part: a puzzle a
technique solver drove to completion was _forced_ at every step, so it has
exactly one solution by definition. `host-tests/sudoku` still verifies it with a
brute-force solution counter that shares no code with the solver making the
claim, because a property checked only by the thing that promises it is not
checked.

## The ladder

Eleven techniques, ordered by how hard a human finds them. That order **is** the
difficulty scale: `solve()` always applies the cheapest technique that fires and
restarts, so the hardest one it ever needed is a measurement.

| Band   | Techniques                                              |
| ------ | ------------------------------------------------------- |
| EASY   | naked single, hidden single                             |
| MEDIUM | locked candidates (pointing and claiming)               |
| HARD   | naked pair, hidden pair                                 |
| EXPERT | naked triple, hidden triple, X-wing, XY-wing, swordfish |

**Expert is deliberately the widest band, and the width is a measurement.** Each
of triples, X-wing, XY-wing and swordfish is individually uncommon. With Expert
holding only naked triple and X-wing, the generator carved one puzzle in forty
attempts at 939ms each. Widening the band took that to 12.8ms. The band got
wider; what EXPERT promises did not get weaker.

## Two things that were tried and are wrong

**A two-pass floor construction does not work.** The first design dug to
maximality at the band _below_ the target, then continued with the target's own
ceiling, on the argument that any clue removed in the second pass provably takes
the puzzle above the lower band. The argument is sound and the algorithm is
useless: the first pass digs to maximality and the second pass finds nothing.
Measured over 200 grids per level, clue counts went 27.8 in and 27.8 out, and
only 3% of attempts removed anything at all. One maximal dig at the full ceiling
plus a grade is simpler and three times cheaper.

**The greedy dig really is maximal.** Re-sweeping the same clue list in another
order finds nothing, because removing clues only ever makes a puzzle harder, so
a pair rejected once stays rejected. That is worth knowing before anyone tries
to squeeze the clue count down; getting below about 27 symmetric needs search,
not another sweep.

## The measured distribution

A symmetric maximal dig at the full ceiling, over 1500 grids:

| hardest technique required | share |
| -------------------------- | ----- |
| singles                    | 83%   |
| locked candidates          | 9.6%  |
| pairs                      | 3.0%  |
| triples, fish and XY-wing  | 4.4%  |

So EASY costs about one attempt and HARD about thirty, at well under a
millisecond each. `host-tests/sudoku` prints the per-level cost on every run, so
a change that wrecks it is visible rather than silent. On the host: 0.8ms EASY,
8.6ms MEDIUM, 15.6ms HARD, 12.8ms EXPERT.

Generation runs one loop pass _after_ the frame that says `MAKING ONE`, with a
budget of 24 attempts per pass and the activity looping across passes, so the
main loop is never pinned for the whole carve.

## How you play it

There is **one** function that changes a cell, `tapCell`, and every case does
something. A dead tap on a panel with no press feedback is indistinguishable
from a tap that missed, so there are none:

- a **clue** -> picks that digit up; the pad arms it and every copy of it on the
  board is bracketed
- your own digit, **same as armed** -> clears it
- your own digit, **different** -> overwritten
- **empty** -> takes the armed digit

**Hold to pencil.** Same tap-and-hold split Minesweeper uses for dig and flag,
deliberately: it is the one two-gesture idiom this device already has, so it is
the one a player has already met. There is no NOTES mode, no erase key and no
mode indicator, because there is no mode.

**A note is never edited by anything but the player.** Placing a 5 does not
strike 5 from its peers' notes. `visibleNotes` hides digits a cell can no longer
take, computed at draw time, so the stored marks stay exactly as pencilled, undo
stays one cell wide, and un-doing a placement brings the marks back with no
bookkeeping.

**Mistake feedback is clashes only.** A digit that collides with one it can see
inverts its cell. That is derivable from the board alone, it is what a person
would notice on paper, and it never reveals the answer -- a wrong digit that
happens to clash with nothing yet sits there looking fine, which is correct.

**HINT names a cell and the rule, never the digit.** It checks first for an
entry that disagrees with the answer, because any deduction made past a wrong
digit is a lie. Otherwise it pushes eliminations forward silently and reports
the first _placement_, labelled with the hardest rule it needed to get there:
"you can rule 4 out of this row" is true and useless to someone stuck.

**No clock is drawn while you solve.** A running timer on a panel that takes
300ms to repaint would spend the battery telling you what time it is. Elapsed
time is recorded and shown once, at the end. A solve leaning on hints does not
set a best time.

**The ending is the board.** A finished grid does not navigate anywhere: it
stays, wearing SOLVED as its capsule, and that capsule is the door to the stats.
Minesweeper learned this the expensive way.

## The screen

Everything is derived from four numbers, and they line the page up on the
board's own edges:

```
header 76 + rule + 16      -> the grid starts at 92
50px cells, 9 of them      -> 450, plus a 9px board frame -> 468 wide
468 wide on a 480 panel    -> a 6px margin, and the grid ends at 560
a 207px pad on the margin  -> 577 to 784, so the gap under the grid is 17
```

The digit pad is three by three **because a Sudoku box is three by three**. It
is the one control on the screen made of the game's own material, so it needs no
label, and the digits sit where a phone keypad puts them.

Neither the grid nor the pad is in the interaction buffer: ninety regions
against twenty-four slots. Both are hit-tested arithmetically from the same
functions that drew them, which leaves the board screen spending three
interactions in total. The pad's keys **abut** rather than sitting on a gap, so
`padKeyRect` and `padKeyAt` are an exact inverse; a real gap would be pixels
belonging to no key, and the hit test would have to either reject them (a dead
strip in the middle of the pad) or claim them (a hit test that no longer matches
the pixels). `host-tests/ui` asserts the inverse over every pixel of both.

There was a progress bar under each pad digit. It read as an underline on every
key -- nine cells that looked like fill-in-the-blanks rather than buttons -- and
it is gone. How many of a digit are down is already on the board, where arming
it brackets every copy.

### What the render caught that the code read did not

Every screen was built in three complete arrangements behind a `SUDOKU_VARIANT`
macro, rendered through the device path against a seeded save, and composed side
by side. Mario picked the shaded-clue board, a front door where the grid **is**
the page, and a how-to that shows each gesture as a **before and after** pair
with an arrow between. The switch and the losers went in the same commits.

Four of the five lessons describe a gesture, and a gesture is a change: one
still frame cannot show it and two can. That is the whole argument for the
before/after pair over a single diagram, and it is why the pictures teach rather
than decorate.

The renders caught what reading the code did not, and the list is worth keeping
because none of it was visible in the source:

- The front door's state and record lines **collided**: a 28px band is shorter
  than a 20px cut's line box.
- The digits **crossed the cell border**, which turned out to be a fork-wide
  trap rather than a Sudoku one. See [design-language.md](../design-language.md).
- The how-to's body text **ran over the board**, because `maxLines = 3` in an
  84px band and the line height is 42. A band must be at least
  `maxLines * lineHeight`; the component wraps and centres but never clips.
- A lesson line **truncated invisibly**, the same ellipsis-with-no-glyph failure
  as the status capsule.
- The teaching board was blown up to fill the page and the digits looked lost in
  it: the largest cut puts 38px of ink in a cell, so a 145px teaching cell is 26%
  full where the real board's 50px cell is 76%. A picture gets big by holding
  MORE cells, not bigger ones.
- The front door's ornament sat **32px short on one side**, and one arrangement's
  stat row ended 90px short of the right margin while everything else ran the
  full width.
- Two diagrams **contradicted the rule they illustrated**: their "before" boards
  already held two fives in one box, which the game would draw as a clash, and
  one of them left a third five grey while marking two black. The faces now use
  clues 1, 3 and 9 only, so nothing clashes until the lesson says it does.

## The save

One line of small integers, then the clues, then the player's digits, then their
marks as three hex digits each. About 450 bytes.

**The answer is not saved, it is re-derived.** The puzzle has exactly one
solution, so the clues determine it; a save that cannot be solved is a corrupt
save rather than a puzzle with a surprising answer. That is also what makes the
file independent of the generator: a later generator changes nothing about how
an old grid reads. The level and the hardest technique are re-graded on load for
the same reason -- there is nothing in the file that can disagree with the
puzzle.

Except one field, which is why the load ends by overruling it. `menuLevel` is
what the NEXT puzzle will be, and it is stored rather than derived, so a card
written while the player was browsing the difficulty row holds a level the saved
grid does not have. The load snaps it back to `puzzle.level` whenever there is
an unsolved game, so the headline, the caption and the door all describe the
same puzzle. A level picked and never played is not worth carrying across a
restart; a half-solved puzzle is.

## RESUME or NEW PUZZLE is derived, never latched

The front door is one button whose label is a fact about the save:
`sudoku::canResume()` asks whether there is an unsolved game whose
`puzzle.level` is the level the menu is showing. `SudokuActivity` and
`buildMenu` call the same predicate, so the label a player reads and the door
they get cannot come apart.

It used to be a flag set on every DIFFICULTY tap and cleared only by entering
the app or starting a game. The row steps `(level + 1) % 4`, so **four taps
returned the menu to the puzzle's own level with the flag still set**: the grid
was still drawn, cell for cell, above a button that now overwrote it, with no
confirmation. A latch cannot represent coming back to where you were, which is
the whole of the bug. `host-tests/sudoku` walks a full lap of the row from every
starting level.

## Why there is no PLAY NEARBY

Asked and answered rather than skipped. Sudoku is a solitaire, and the only
shape a second device could take is a race, which is _continuously_
simultaneous where `linkplay` is strictly turn-based. Battleship's trick of
splitting a simultaneous phase into two moves works because placing a fleet
ends; racing does not. Forcing it through would be new work in the link layer
rather than in the game.

## Reproducing the screenshots

`tools_local/sudoku/sudoku_mkstate.cpp` writes a save from a fixed seed, so the
same argument gives the same grid every time:

```bash
c++ -std=c++17 -O2 -Isrc/apps_local/sudoku \
  src/apps_local/sudoku/SudokuCore.cpp tools_local/sudoku/sudoku_mkstate.cpp -o /tmp/sudoku_mkstate
/tmp/sudoku_mkstate midway > fs_agent/.crosspoint/sudoku.sav
rm -f fs_agent/.crosspoint/shelf.cfg
```

That second line is not optional and it cost a render to learn: **the shelf
remembers which folder page and row you were on, and it survives across runs**,
so a tap script that pages is only reproducible from a cleared card. Without it
the second run opened Jaipur and the screenshot looked perfectly plausible.

## Tests

- `host-tests/sudoku/run.sh` -- the rules and the game. Uniqueness by an
  independent brute-force count, the ladder property at every level (solvable at
  its own ceiling, **not** solvable one band below), symmetry, hints played out
  until the grid fills, no dead taps on any cell in any state, undo restoring
  every digit and every mark exactly.
- `host-tests/ui/run.sh` -- the screens. Both hit tests as exact inverses over
  every pixel, the board spending three interactions, the status capsule inert
  until the grid is finished, and the ornament changing when the game does.

Mutation-checked in both suites: ten planted defects, ten caught, including a
mislabelled grade, a dig that ignores uniqueness, an XY-wing eliminating the
wrong digit, a capsule that answers taps mid-game and an ornament that ignores
the player's own digits.
