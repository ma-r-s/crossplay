# Train Tracks

**Status: in playtesting.** The whole loop is built and host-tested: rules,
solver and generator, the play screen, the front door, the new-puzzle screen,
CHECK, HINT, how to play, saving and wake resume. It is being played on a BOOX
Picco, and what that turns up is recorded under "On-device findings" and in
the sections it changed.

This document is the game's specification: its rules, mechanics, screens and
the reasons behind them. Change it together with the code.

Lay one unbroken track between the two stations. The number beside
each row and above each column says how many squares of that line hold track.
Train Tracks is the logic puzzle known as Tracks in Simon Tatham's Portable
Puzzle Collection. This is a new implementation of that puzzle type for an e-ink
panel.

## The rules

- The grid is N x N. Two stations sit outside two edge squares, and the track
  enters and leaves through them. The player sees **stations**, drawn as
  subway-map dots; the code, and the rest of this document, calls them
  **gates**. The line can be laid from either end, so the two look the same,
  though the code still has a start and an end.
- Every row and column clue is the exact count of track squares in that line.
- The track is one simple path: it never branches, crosses or loops, and it
  passes through each square at most once.
- **Given squares are locked track, and their shape is shown.** A given
  straight or curve fixes that square's two connections, which is also how the
  solver reads it. This was decided on 2026-11-19, over
  a plain framed block: it makes Easy read like real Train Tracks, and it
  gives the generator a strong clue to spend.

**What wins.** No square of the route left showing a question mark (an
undecided piece, below), and either of two things (`isSolved`):

- the marks alone satisfy the rules (`isWin`): every clue met, and some single
  path from gate to gate runs through exactly the marked squares while keeping
  every given's shape. Any valid solution wins this way; or
- the line drawn from the start gate, followed piece by piece, leaves through
  the end gate having run through every route square (`lineSolves`). Track
  laid elsewhere does not stop it counting: on a hard puzzle the player may
  have working track out on the board and not know the line is done. The route
  is unique, so that line is the answer.

Crosses are notes only: they never count and are never required. On a solve
the stray track is taken away (`tidySolved`) so the solved board shows the
route alone. (The first version had only the rules win, and on the Picco a
correct line with unconnected track elsewhere did not finish the puzzle.)

The question mark holds the win back because finishing is laying the route,
pieces and all. The first version let it count like any track, and on the
Picco the last square laid solved the puzzle and turned every question mark
into its piece: efficient, and it took the finish away from the player, who
had not completed the puzzle yet. Any piece on those squares will do, as with
every other square; only the question mark is held back. One off the route is
stray track and holds nothing back.

## Squares, and the pieces on them

The rules are about squares: which hold track. Winning is that, so a piece
drawn in the wrong shape never blocks a solve; only a piece left undecided on
the route does. But a track square also
shows a piece, a straight or a curve, and the player can choose it.

- **A tap** lays track with an automatic piece, which follows its neighbours
  until touched again. The next tap leaves the piece undecided, drawn as a
  question mark; each tap after that gives the next of the six pieces in a
  fixed order (across, up and down, then the corners clockwise from
  north-east), the same on every square whatever is around it; after the
  last, it is a question mark again and the round starts over. A tap never clears a square. A tap on a cross goes
  straight to the question mark, and then all six pieces follow.
- **A hold** takes a square back a step: track or a cross to empty, and an
  empty square to a cross.
- **Every track square draws a whole piece.** Automatic pieces
  (`inferShapes`) never close a loop and never run off the board except
  through a gate, unless the square is boxed in and no piece could avoid it.

The full rules for drawing and input are under "Screens, input and refresh".
This section once described a first version in which a tap cycled empty,
track and cross and the player never chose a shape; playing it on the Picco
is what replaced it.

## The solver, and what its tiers mean

`TrainTracksSolver.h`. The unknowns are every square (track or not) and every
edge between two adjacent squares (connected or not). Edges on the boundary are
fixed: connected at a gate and nowhere else. Every assignment goes on a trail,
so backtracking unwinds instead of copying state, and the search keeps its own
decision stack instead of recursing. The whole solver is about 8 KB and is
held by its owner, never put on the stack.

| Tier | Rules | Difficulty |
| --- | --- | --- |
| 1 | Line counts (a full line closes; a line with exactly enough room fills); a non-track square has no connections; a connected edge makes both its squares track; a track square takes exactly two connections | Easy |
| 2 | Dead ends (a square with fewer than two possible connections is not track); chain ends (an edge that would close a loop, or join start to end while track is still missing, is cut) | Medium |
| 3 | One-step lookahead: assume a value, run tiers 1 and 2, and keep the opposite value on contradiction | Hard |

`deduce(maxTier)` always uses the lowest tier that makes progress, and records
the highest tier it needed. That number is the puzzle's rating.
`countSolutions(limit)` counts distinct **sets of track squares**, because that
is what the player marks. Two paths over the same squares count once.
`countRoutes(limit)` counts the paths themselves, with every square fixed, and
the generator requires exactly one (below).

**The rating is part of what a seed means.** The generator will keep or reject
a candidate by its rating, so changing a rule changes which puzzle a saved seed
regenerates into. `testRatingIsPinned` pins the tier distribution over a fixed
sample, and it will fail on any change to rule strength. When that change is
intended, update the pinned numbers **and** bump the save format version.

### Tier 2 is rarely the hardest rule, and that is fine

On random paths with sparse givens, tier 2 is seldom the hardest rule needed
(pinned sample: tier 1 226, tier 2 14, tier 3 120, unsolved 40). That looked
like a risk for Medium, but the generator does not sample: it keeps trying
until a puzzle rates exactly tier 2. That takes about 4.4 attempts and 1 ms on
a Mac. Stronger tier-2 rules (cut parity, or Tatham's loose-end rule) stay in
reserve in case playtests find Medium too close to Easy.

**Lookahead is incremental where it matters.** Tier 1 runs off a worklist: an
assignment queues only the squares and lines it can affect. Tier-3 probing
resumes where the last deduction was found instead of starting over. Together
they made Hard generation five times faster, and the pinned rating did not
move, which shows neither change altered what the rules conclude.

## How it is tested

`host-tests/traintracks/run.sh`, freestanding, about 25 seconds in all.
`test_traintracks` covers the rules and the solver (about 2 s),
`test_generator` covers generation, and `test_game` covers CHECK, HINT, the
navigation between screens and the save file (about 2 s).

- **An independent brute force.** On 4x4 and 5x5 grids every simple path
  between the gates is enumerated directly. `countSolutions` must match the
  number of distinct track sets on 400 puzzles, with and without givens.
- **Every subset.** On 4x4 grids, every one of the 65,536 sets of marked
  squares that meets the clues is run through `isWin`, which must accept
  exactly the sets the brute force found. This includes ambiguous puzzles, so
  winning with an answer other than the generated one is covered.
- **Soundness.** On 600 puzzles from 4x4 to 12x12, everything any tier deduces
  must agree with the generated solution, and tiers must be monotonic (solved
  at tier k means solved at tier k+1).
- Undo and redo across the 64-move ring, tap cycling, locked givens, clue
  states, shape inference, and a pinned RNG value.
- **The generator:** 1,000 puzzles per level. Each must be a single path
  between the gates, have clues that match, have givens that are solution
  squares with their real shape and within the level's limits, rate exactly
  at the level's tier and not below it, have one track set, and win from its
  own solution. Each must also rebuild byte for byte from `(seed, attempt)`
  alone. A full replay is deterministic, and every fallback entry
  regenerates.

**Mutation-checked** with `scripts_local/mutate.sh`. Every rule in both files
was broken once. Each mutant was caught except three, and those three are
equivalent: the final `completePath` check at a search leaf, the `allCluesMet`
early-out in `isWin`, and `countSolutions(1)` versus `(2)` once every square is
fixed. In each case, other rules already guarantee the same thing. The
generator's mutants (exact tier, given cap, per-attempt RNG stream, path
length, strip keep and gate distance) are all caught. Run generator mutants
with `TT_QUICK=1`: it stops at the first failing test and skips the
1,000-puzzle sweep, which a broken generator can make very slow.

## Prior art: Tatham's Tracks, evaluated and not adopted

Evaluated on 2026-11-19 from a GitHub mirror of the collection
(`ghewgill/puzzles`, `tracks.c` and `LICENCE`). The canonical
git.tartarus.org links returned 403. The licence is MIT: (c) 2004-2014 Simon
Tatham, with eleven named contributors.

What it does, briefly:

- **Generation:** a random self-avoiding walk from the left edge to the bottom
  edge, restarting on a dead end with no cap. Clue squares are added until the
  puzzle solves at the requested level and not one level below, then stripped
  back. There is no backtracking uniqueness check: a full deduction solve is
  taken as proof of a unique solution.
- **Solver, Easy:** local square rules, single-line counts, and a union-find
  loop check that also refuses to join the entrance to the exit while track is
  missing. That is close to tiers 1 and 2 here.
- **Solver, Tricky:** two per-line rules, "single" (one more square needed with
  one free end) and "loose ends" (counting the ends that run along a line).
- It allocates a fresh union-find on every solver pass, and has no recursion.
  The mirror copy has a precedence slip in `solve_check_single_sub`
  (`!sflags[i] & S_TRACK`) that weakens the rule without making it unsound.

**Decision: no code and no rule adopted.** The solver here was already written
against this game's own model, and the generator reaches every level's target
without Tatham's Tricky rules (see the solver section above), so there is
nothing to port. The one idea in common is generic: build givens up from
nothing for Hard, as his `add_clues` builds clues up. Because no code or
substantial logic is adapted, `THIRD-PARTY-NOTICES.md` is unchanged. If the
loose-end rule is ever added to tier 2, that adaptation brings the notice with
it, in that file and in the solver's header.

## Generation, difficulty and timing

`TrainTracksGenerator.h`. A seed and a level fully define a puzzle.
Generation runs in **attempts**, and each attempt draws from its own RNG
stream, `attemptSeed(seed, attempt)`. That lets the attempt that succeeded be
rebuilt directly, so a save stores `(seed, attempt)` and resumes in one
attempt's time.

One attempt:

1. Two gates on edge squares, at least `size` apart by Manhattan distance.
2. A random path between them, **grown** rather than searched (see "Path
   shape" below), to a length inside the level's range.
3. Clues from the path.
4. Givens. **Easy and Medium** start fully given and strip in random order,
   keeping each removal only if the puzzle still solves within the level's
   tier. They stop once the given count is at the target and the rating is
   exact. **Hard** builds up from none instead: deduce, and if that is not
   enough, give one track square the solver could not decide, at most twice.
   Then it strips whatever it can.
5. Rate with all tiers: the rating must equal the level's tier exactly.
6. **One route, not only one set of squares.** With every square fixed to the
   answer, `countRoutes(2)` must find one path. CHECK and HINT judge each
   square's piece against the answer's, which is only fair if no other route
   runs through the same squares. A cold rules critic found that one set of
   squares often carried two: 9 of 1,000 Easy puzzles, 71 Medium and 175 Hard.
   On those, a player who laid the other route had correct pieces flagged by
   CHECK and "fixed" by HINT. `test_generator` checks every puzzle it makes
   with a plain depth-first route count that shares nothing with the solver.

Attempts are capped by **count**, never by time, so a seed means the same
puzzle on every device. The activity adds its own wall-clock budget. If that
runs out, it falls back to `TrainTracksFallback.h`: six `(seed, attempt)`
pairs per level that are known to succeed, so a fallback costs one attempt. A
host test regenerates every entry. A deal belongs to the new-puzzle screen:
it pauses while how to play is open over it, its budget restarting on the way
back, and any other screen drops it. (It used to carry on, unstepped, after
Back left the screen mid-deal, and the flag that keeps the loop from idling
during a deal kept the CPU at full speed for the rest of the session; a code
review found it.)

All generator state other than the output lives in `GenWork` (about 9 KB:
the solver plus the path search's arrays), which the activity allocates once.
Generation puts almost nothing on the stack.

| Level | Grid | Tier | Path length | Givens | Attempts (avg / max) | Host time per puzzle |
| --- | --- | --- | --- | --- | --- | --- |
| Easy | 6x6 | 1 | 40-60% of squares | at most 25% of track, cap 9 (avg 4.3) | 1.0 / 3 | 0.1 ms |
| Medium | 8x8 | 2 | 40-60% | at most 10% of track, cap 5 (avg 2.6) | 2.2 / 11 | 0.7 ms |
| Hard | 10x10 | 3 | 40-60% | 0-2 (avg 0.9) | 2.0 / 9 | 17 ms |

Measured on an Apple Silicon Mac over 1,000 seeds per level, including the
test's own checks. **Host timings say nothing about the device.** The ESP32-S3
is very roughly 15-30 times slower, which would put Hard at a few hundred
milliseconds on average and around a second at worst. The target is a deal
inside 6 s; the Picco measurements are under "On-device findings".

### Path shape

The first builder was a depth-first search with random neighbour order. It
produced valid puzzles, but they looked alike when rendered: long sweeps, one
side of the board left blank, and easy footholds from lines with a clue of 0
or 1. The builder now **grows** a path instead:

1. Start from a random shortest path between the gates.
2. Repeatedly push a segment sideways: a step a->b becomes a->c->d->b through
   the two free squares beside it. Each bump adds two squares, so the length
   keeps the shortest path's parity.
3. Each bump is the best of 40 random candidates. A candidate scores the
   percentage of free squares in the 5x5 window around each new square (a
   percentage, so the border is neither favoured nor shunned), plus a bonus
   for entering a row or column that has no track yet.
4. Reject a path that leaves more than one line empty.

Measured over 300 generated puzzles per level, before and after (Hard shown;
Easy and Medium moved the same way):

| Hard 10x10 | Depth-first | Grown |
| --- | --- | --- |
| Lines with no track, per puzzle | 0.8 | 0.1 |
| Clues of 0 or 1, per puzzle | 2.0 | 1.1 |
| Largest empty square, avg side | 4.0 | 3.0 |
| Share of track on the outer ring (ring is 36% of the grid) | 34% | 25% |
| Host time per puzzle | 29 ms avg, 211 ms worst | 9 ms avg, 32 ms worst |

Three things that were tried and dropped, so they are not tried again:

- Scoring free neighbours within one square: a border square has fewer
  neighbours, so growth avoided the edge and left outer lines empty.
- Counting off-grid as free in a 5x5 window: this overcorrected, and paths ran
  as parallel hairpins along the edges around an empty middle.
- Six candidates per bump: too few to find the segment facing the big gap, so
  the largest empty square did not move.

`test_generator` guards the result: no puzzle may have more than one empty
line, and per level the average largest empty square and the average number of
clues of 0 or 1 must stay near these figures.

Still to judge in playtesting: some paths keep a blank corner or a pair of
parallel hairpins, and track is somewhat under-represented on the outer ring.
The given targets and path ranges are first guesses, to be tuned from play.

**Pinned.** `testPinnedPuzzles` pins one puzzle per level by hash, and
`testRatingIsPinned` pins the rater. Changing either changes which puzzle an
old save rebuilds, so update the pins **and** bump the save format version
together.

## Screens, input and refresh

`TrainTracksScreens.cpp` builds the screens from plain models, and
`host-tests/ui` builds them against a fake draw target. Navigation is
`TrainTracksFlow.h`; the game in progress, CHECK and HINT are
`TrainTracksGame.h`.

### The front door

There is one saved game at a time, and the front door depends on it:

- **A game in progress:** the game itself, bracketed, with "MEDIUM IN
  PROGRESS, 4:12" above it, then CONTINUE (tall, where the
  thumb rests) and NEW PUZZLE. Tapping the picture also continues.
- **Nothing in progress** (first visit, or the last game was solved): the
  new-puzzle screen is the front door.

Back from the front door leaves the app; Back from anywhere else walks toward
it (`tt::back`, `tt::leavesApp`).

### The new-puzzle screen

The puzzle it will deal fills the page as a preview, showing its givens and
gates and unbracketed (it is the puzzle, not an ornament standing for it), with EASY / MEDIUM / HARD, a tall PLAY PUZZLE and HOW TO PLAY on the
bottom margin. The preview is generated while the screen is up, a few attempts
per loop pass; until it lands the big button reads LAYING TRACK, dimmed.
Tapping any difficulty deals again, the one already chosen included, which is
how to ask for a different puzzle at the same level. The screen opens on the
difficulty used last, which the save remembers.

PLAY PUZZLE is the only thing that replaces the saved game, so backing out of
this screen never loses one. It says nothing about the game it replaces: the
front door offered CONTINUE first, and one screen for both cases is simpler
than a warning.

Three layouts were rendered for this screen and for the front door. The
original front door carried the difficulty row itself, which read as the
status of the game in progress; moving the choice to its own screen, next to
the puzzle it deals, is what made it read as a choice.

### The play screen

Portrait, top to bottom: the header band (the name alone: "TRAIN TRACKS"
beside a level does not fit the display cut, and stepping it down made this
header unlike every other screen's, so it follows Minesweeper and
Knucklebones; SOLVED is short and carries the level),
the column clues, the grid with the row clues on its left, a status line, and
a thumb rail: CHECK, HINT and MENU at pill height, then UNDO and REDO 72px tall
on the bottom margin. The thumb rail won from three rendered layouts because it
matches where the other games keep their controls and puts the two most-used
ones nearest the thumb.

The board runs to about 6px from the panel's edges, because every pixel of it
is grid; the buttons under it, on the play and the solved screen, keep the
16px page margin the front door's do. Squeezing the buttons out to the board's
edges was the first version, and on the Picco they looked crowded against the
bezel next to the front door's.

The status line is blank while solving: the clues already say how each line
stands, and a running count of laid track only repeated the clue total. It
speaks only for CHECK and HINT.

**Sizes.** Each level is two squares bigger than the last. The panel's width
sets the cell, capped at 64px so a small grid still reads as a board:

| Level  | Grid  | Cell  | On the Picco (~220 ppi) |
| ------ | ----- | ----- | ----------------------- |
| Easy   | 6x6   | 64 px | about 7.4 mm            |
| Medium | 8x8   | 50 px | about 5.7 mm            |
| Hard   | 10x10 | 40 px | about 4.6 mm            |

9x9 for Hard (44px) was rendered too. 10x10 kept the two-square step and the
room for long routes, and Picross already ships a 10x10 at about 37px.

**Drawing a square.** Every track square draws one whole piece, a straight
or a quarter-arc curve, each with two sleepers. A given's piece is its own,
and so is one the player chose, whatever it points at. The rest are automatic
(`inferShapes`), and follow this rule book. Each rule only acts on the joins
the rules above it left open, and none ever closes a loop:

1. **The gate.** A gate square connects through its gate.
2. **What is drawn stays drawn.** A join on screen (`Board::shape`, recorded
   by `settle` after every move and kept in the save) stays, between two
   squares that are still automatic track, or between an automatic square and
   a fixed piece that still points back at it.
3. **New track extends a line.** An end on screen pointing at a square that
   has since become track joins it.
4. **Fixed pieces.** A given or chosen piece pointing at an automatic square
   that still has room pulls it in.
5. **Forced, then most constrained.** Neighbours join where a square has no
   other way to reach two connections, then by the most constrained square
   first.
6. **Open ends.** A square still short of two points its open ends at empty
   squares (or its gate): first into a row and column that both still have
   room for track, then preferring to run straight on. A square boxed in by
   crosses, the edge or track that does not join it still draws a whole piece,
   the one that leaves the board least; CHECK says whether it is right.

What is drawn outranks a chosen piece's pull. With the pull first, a piece
being turned beside a line could drag a line square toward it, the board
remembered the new join, and the line stayed broken after the piece went back
to empty; a cold rules critic found 75 lines left that way in 2,857 probes. A
chosen piece pointing into a full line now just points there, and CHECK
judges it. The room preference in rule 6 came from the play critic: a line
laid up a column ran its open end straight on into a row whose clue was
already met.

Rules 2 and 3 came from the Picco. Without them inference was stateless, so
track laid beside a line from the start gate could reroute the line through
the new square, and a line the player had built would break somewhere else.
The host test that lays half a route and then probes every square beside it
found 2,675 joins broken that way across 120 puzzles. An earlier version also
drew a boxed-in square as a stub or a small black block. The black block was
one more symbol for the player to learn, and it said the same thing CHECK says.

The first version joined every pair of neighbouring track squares and pruned
afterwards. On the Picco that drew curves turning back on themselves in any
2x2 of track, which is never legal, and open blocks where a square had three
track neighbours. Marks say which squares are track, not how they join, so the
fix is both a better guess and a way to override it. Crosses are an
X. Givens sit on a dithered grey ground and show their real shape. Each
station breaks the frame and is drawn as a ringed dot on the opening, as a
subway map marks a stop; it sits over the end of the rail, so the line arrives
at it. Both look the same. The first version drew arrows, in at the start and
out at the end, but the start does not matter to the player, and the arrows
read as notches in the frame rather than as places. A platform slab and a
buffer stop were rendered too; both came out as a small T, which at this size
reads as one more piece of track. A clue is boxed when its line is met and inverted when it is over.

Curves carry sleepers too, along the radius a third and two thirds of the way
round. A first version left them bare, because a lone bar across a diagonal
looked like the cross mark; on a real winding board the bare curves were what
looked wrong, since a route full of bends stopped reading as track. None, one
and two were rendered side by side; two was chosen, with its busyness at
Hard's 40px cell to be judged on the panel.

A solved board draws every square from the answer's own shapes instead. Marks
say which squares are track, not how they join, so a tightly wound route left
to inference shows open blocks even when it is complete and correct.

**Input.** A tap lays track: an empty square becomes track with an automatic
piece, which keeps following its neighbours until touched again. The next tap
leaves the piece undecided, and further taps step it through all six pieces
in `kPieces` order: across, up and down, then the corners clockwise from
north-east. After the last the square is the question mark again, and the
round starts over. The next piece depends only on the one on show, so undo,
a hint or a reload never lose a place in the cycle, and nothing about it is
saved. The automatic piece is not skipped, so it can come round a second
time: that costs a tap, and keeps every piece the same number of taps from
the question mark on every square. Clearing is the hold's alone: on the
Picco, a cycle that ended on empty threw away track the player had meant
only to turn. A tap on a cross gives an undecided piece straight away.

The cycle used to offer pieces most likely first, ranked by how many ends
met track that would join them. The ranking changed with the neighbours, and
the neighbours re-fit around each piece chosen, so the order differed from
square to square and from one tap to the next. On the Picco that read as
hammering at a square that behaved differently every time, and keeping it
from repeating or skipping a piece took a set of shown pieces per square
that two critic rounds each found broken. Each
chosen piece is pinned (`Board::pin`) and the neighbours fit around it.

An undecided piece (`kPinUnknown`) is for a square the player believes is
track without yet knowing which way it turns. It is track to everything that
judges the board: the clues count it and CHECK judges it, though while one
stands on the route the puzzle is not yet solved (see "What wins"). Inference treats it as an
automatic piece and works out a shape for it, which is simply not drawn, so
neighbours join it exactly as they would join automatic track; only the
drawing shows a question mark, at the clue size on every board. The display
size on Easy's and the how-to's 64px squares was tried, to keep the mark in
proportion to the square, and on the Picco it outweighed the cross beside it. It sits right after the automatic piece, and
a tap on a cross reaches it in one, since a square once ruled out and now
believed to be track is when the player most often knows the square but not
the piece. A hold
of 400ms, as Sudoku's pencil and Minesweeper's flag, takes a square back a
step: track or a cross to empty, an empty square to a cross. A cross therefore
only goes on an empty square, and clearing track is one hold. (The first
version had a hold cross out track; on the Picco the player found themselves
crossing squares only to clear them.) From 250ms the held square's own grid
lines are drawn heavy all the way round, as HINT's brackets are at the
corners, and a second ring appears just outside the board's frame, since the
square itself is under the thumb. Both show the hold is registering (taps took 140 to 250ms on the Picco, so a
sooner frame would repaint for most taps). Winning reads only which squares are
track, so a chosen piece never blocks a solve (an undecided one on the route
does); CHECK and HINT also judge
shapes (below). The grid is
hit-tested from the same `Geometry` that draws it (`cellRect` and `cellAt`,
tested as exact inverses at every level's size), so its 144 squares spend
none of the interaction table's slots. The side keys are undo and redo on the
board and turn pages in how to play.

### CHECK and HINT

Both mark the square, never the piece (`docs/design-language.md`). HINT's
brackets clear on the next move. CHECK's notches stay through every move,
undo included, until the next CHECK redraws them or the puzzle is solved
(or a HINT puts that square right, which clears its notch and no other), so
one CHECK serves for as many moves as it takes to put its flags right; the
player finds out a flag is fixed by checking again. They are the game's own
(`Game::marked`) and saved with it, so leaving the board and coming back, or
reopening the app, keeps them; they used to live in the activity and were
lost on the way out, which a play critic found.

- **CHECK** puts a solid notch, a quarter of the square on a side, in the
  top-right corner of every square that
  contradicts the answer, and says "2 WRONG" or "NOTHING WRONG SO FAR". That
  is a marked square in the wrong state (track where there is none, or a cross
  on track), and, since the route is unique and so is each of its squares'
  piece, a route square drawn in a piece that is not the answer's. CHECK
  judges what is on the screen: a piece the game guessed is judged exactly as
  one the player chose, so a lone square drawn straight where the answer bends
  is wrong. An undecided piece on the route is wrong too: the answer has a
  piece there and the player has not set it. An empty square is never wrong. (The first version judged squares
  only, and on the Picco a wrongly turned piece passed CHECK. The notch was a
  third of the square at first; on a curve turning through that corner it
  covered the sleepers, which is marking the piece rather than the square.)
- **HINT** places one correct square, as an ordinary move that undo takes
  back, brackets it (four corner brackets drawn on the square's own grid
  lines, a fifth of the square to an arm: track only meets those lines at the
  middle of a side, and a curve's sleepers come no nearer a corner than about
  a quarter of the square, so the brackets never touch the piece. They were
  drawn just inside the square at first, and a curve ran through one), and says how many hints so far ("1ST HINT", "2ND HINT").
  It only ever lays track or turns track already laid to the answer's piece:
  revealing track is what the player presses HINT for. It never lays a cross
  and never takes track away; track laid off the route is left for the player,
  and CHECK, to find. Track it lays is set to the answer's piece, so a
  bracketed square never shows a guess the answer does not have. A cross on
  the route comes first, becoming track; then a route square drawn in the
  wrong piece or left undecided. Otherwise the solver reasons forward from the player's marks
  that are right (`Solver::applyRightMarks`), so nothing is deduced from a
  wrong one, and stops at the lowest tier that proves an empty square is
  track. (The first version offered crosses, which tell the player little,
  and then cleared wrong track, which left a bracket around an empty square.) A hint that would finish the puzzle is
  refused ("LAST MOVE IS YOURS") and not counted, and CHECK's marks stay: the
  last move is the player's, and nothing on the board changed. It is tried on a copy of the board the activity
  keeps (`hintSolves`), so the board on screen, which the render task may be
  drawing, never shows a hint that is then taken back.

The game counts both. The solved board shows the time and the tally:
"CLEAN WIN" (no hints and no checks), "1 HINT", "3 CHECKS" or "2 HINTS, 1 CHECK", stepping down to the
small cut rather than cutting a word if the tally is ever too long.
`Game::assisted()` is either count being non-zero, for a future record.

### Solved

Solving changes the header to SOLVED and the rail to one tall NEW PUZZLE, in
the same footprint so the finished board does not move. The result fills the
band where CHECK, HINT and MENU were: the time in the display cut, the tally
("CLEAN WIN", "2 HINTS, 1 CHECK") under it, plain ink on white. Three were
rendered: that, a solid capsule in the rail's top row (Sudoku's move), and a
solid block filling the band. Both solid ones read as a second button above
NEW PUZZLE, so the result is ink and NEW PUZZLE stays the only solid shape.
The first version put the time and tally on one small line and left the band
empty, on the screen most likely to sit on a desk for hours. It is a full refresh,
as is a new puzzle arriving; every other change is a fast partial refresh.
NEW PUZZLE and Back both go to the new-puzzle screen, starting on the same
difficulty.

### How to play

Four pages, each a sentence in the UI cut over a 4x4 board drawn by the play
screen's own code and a second sentence under it, at the same size. Every
board has the same given, so the shaded square is there from the first page
rather than appearing partway through:

1. "CONNECT THE STATIONS WITH A CONTINUOUS SET OF TRAIN TRACKS", over a solved
   example, and "NUMBERS INDICATE TRACKS PER COLUMN OR ROW" under it. That
   second line was once a smaller line on the bottom margin, the only small
   type left in the how-to once page 2's moved up, which the look critic had
   flagged; it fits at full size.
2. "TAP TO LAY TRACK, AGAIN TO CHANGE IT", over track and a given, and "HOLD
   TO CLEAR" under it.
3. "HOLD EMPTY BOX TO CROSS OUT" over crosses and two question marks, and "TAP
   AGAIN TO MARK ? AS POSSIBLE TRACK" under them: the two marks that help work
   a puzzle out share a page, and the second sentence follows the first, since
   a tap on a cross is the quick way to a question mark. The player's own
   wording. The question marks sit on the route, so the clues beside them read
   met, which shows they count as track without saying so. The crosses are the
   ones the numbers give: the first column's 1 is the station square, and the
   bottom row's 0 has no track at all. Crosses picked off the answer read as
   arbitrary, and one of the first set sat on the route.
4. "OUTLINED NUMBER MEANS MET" above met, under and over clues, and "FILLED
   NUMBER MEANS TOO MANY" under them: one sentence joining the two read as
   wordy.

Sentences are broken by hand where they read best; if a line ever outgrew the
panel, every sentence would step down to the small cut together. Every page
reserves the same room above and below the board, sized for the fullest page,
so the board stands still while the pages turn and only its marks change. The pages once carried titles (THE RULE, MARKING, CLUES), which the
header's HOW TO PLAY already said. NEXT, GOT IT and the side keys page through
them.

### Saving and resume

`/.crosspoint/traintracks.sav`, written on leaving, on sleep, when a puzzle is
dealt and when one is solved. During play it is also written once play pauses:
4 s after the last move or CHECK, or 30 s after the first unsaved one if play
never pauses, and never while a finger is holding a square. A run of taps is one
SD write, and a dead battery or a crash costs seconds of play.
`TrainTracksSave.h` is freestanding and
host-tested: one line of small integers (format version, remembered
difficulty, whether a game is saved and whether the board was on screen,
level, seed, attempt, puzzle hash, solved, hints, checks, elapsed time and the
history ring's cursors), then the squares (state, chosen piece and the shape
last drawn, so a reload draws every line exactly as it was), then the 64 undo
moves, then CHECK's flags, four squares to a hex digit. Every header number
is read as 64-bit and range-checked for the field it is read into (the
device's `strtoul` is 32-bit and capped an overflow instead of reporting it),
and a game marked solved has to be solved. A version 5 save, which is the
same without the flags, still opens its game.

The puzzle is not stored. It is regenerated from `(level, seed, attempt)` and
must hash to the stored value, so a save from a generator that has since
changed is discarded rather than laid over a different puzzle. Anything that
does not hold together opens the front door without a word, keeping the
remembered difficulty if it can. A given is track whatever the file says, and
a history that names a given is dropped rather than allowed to unlock it, as
is one that does not replay against the saved squares (each applied move,
undone newest first, has to find its square as it left it), since undo would
otherwise write a move's old state over whatever is there. A shape that is
not one of the six pieces, and a flag on a given or past the last square,
reject the game.

Wake from deep sleep is a chip reset. The shelf reopens the app
(`shelf::resumeFromWake`), and if the board was on screen when the device
slept, or when the player went Home, it opens straight onto the board.

### Refresh

A move on the board rebuilds the frame and shows it with one fast partial
refresh of the whole panel, as Sudoku does. Everything that replaces the
panel's content wholesale gets a full refresh instead: every screen change,
every how-to page, every new preview on the new-puzzle screen, a new puzzle
and a solve. On the Picco a fast refresh over a page turn left the previous
page's track as a faint ghost in the next page's grid, which on a puzzle reads
as track that is not there.

For moves: no game here refreshes a single square's region. Picross also forces a full refresh after a run of board changes to
clear ghosting; whether Train Tracks needs the same is one of the on-device
checks below.

## On-device findings

First run on a BOOX Picco, 2026-11-21, from the debug `picco` build.

**Fine as built:**

- **Touch:** Hard's 40px squares are comfortable to hit, on what is a small
  panel physically.
- **Curves:** two sleepers per curve read crisply; not too busy at 40px.
- **Memory:** about 83KB of internal heap free and flat across several games
  (low-water 80.8KB); PSRAM barely touched. The app's task stack keeps 7.8KB
  of its 16KB free. (The simulator's "2048 bytes free" was the simulator.)
- **Refresh:** a move is one fast refresh of about 148ms, of which drawing is
  about 15ms; a new puzzle or a solve is a full refresh of 350 to 430ms.
- **Easy and Medium deals:** about 50ms and 100 to 126ms, most of the Easy
  figure being the frame drawn before generation starts.

**Fixed after it: Hard froze the loop.** Hard deals took 446, 1,809 and
4,962ms, at roughly 1.2s per failed attempt. Three attempts ran per loop pass,
so one pass held the loop for 4.1s, deaf to taps and close to the 5s
watchdog. Two causes and three fixes:

1. The loop's idle timer dropped the CPU to its low-power clock mid-deal, as
   nothing told it generation was work. `skipLoopDelay()` now returns true
   while a deal is being laid.
2. One attempt per loop pass instead of three, so the longest the loop can be
   held is one attempt.
3. A Hard attempt was made cheaper without changing any puzzle (the pinned
   hashes and rating distribution are unchanged, so every seed deals exactly
   what it did). Sampling put about 85% of the time in edge lookups, most of
   them inside the tier-2 scan that every lookahead probe repeats. Now:
   - the solver tabulates where each square's edges live when it loads, with
     two constant slots for off-board edges, so a lookup is one array read;
   - the tier-2 scan counts each square's edges once per pass rather than at
     every use (exact: the pass only ever sets things to "no", so the counts
     it reads cannot change within it);
   - the generator no longer re-solves a Hard puzzle to rate it, or runs the
     exhaustive search after a complete deduction (implied by soundness;
     `test_generator` still runs it on every puzzle it makes).

| Hard, host (600 deals) | Before | After |
| ---------------------- | ------ | ----- |
| Median deal            | 8.5ms  | 3.2ms |
| Slowest deal           | 90ms   | 25ms  |
| Slowest single attempt | 35ms   | 7.9ms |

The device runs this roughly a hundred times slower than the host, so the
slowest attempt should now hold the loop well under a second.
On the Picco after the fix: Hard deals took 594 and 719ms, the longest loop
pass was 700ms (one attempt), Medium 53 to 128ms and Easy 30 to 34ms.

A race showed up in the same log: the full refresh asked for when a preview
landed was spent on the LAYING TRACK frame still being drawn, so the preview
itself arrived on a fast refresh. The loop and the render task share no lock
by default; the preview is now published under the render lock, and a frame
reads the full-refresh flag before it draws rather than after.

The first version of that fix BLOCKED on the lock, and the render task holds
it through the panel refresh, 730ms for a full one. A deal landing mid-refresh
held the loop that long, and taps made during it were never read: HOW TO PLAY
seemed dead until tapped again. The publish now only tries the lock and, if a
frame is drawing, keeps the finished deal for a later pass. The simulator never
showed it, because its panel is instant and the lock is never held for long.

**The bottom row needed several tries.** Taps aimed at NEW PUZZLE on the
front door and HOW TO PLAY on the new-puzzle screen (drawn at y 732 to 784)
were reported at y 781 to 794: most fell below the button, into the margin.
Mid-panel touch is accurate. Every control on the bottom margin now extends
its tap area down to the panel's edge (`placeButton` in the screens; tested
by `theBottomRowReachesTheEdge`), which is right for an edge control on any
panel. The Picco's bottom inset is recorded as unmeasured in docs/picco.md.

**Still to check:** ghosting over a long game, and whether a periodic full
refresh is needed, as Picross has.
