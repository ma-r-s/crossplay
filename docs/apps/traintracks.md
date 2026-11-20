# Train Tracks

**Status: in progress.** The rules, solver and generator are built and
host-tested (Milestones 1 and 2), and the play screen is on the shelf
(Milestone 3). It opens straight onto a Medium puzzle: the front door,
difficulty picker, CHECK, HINT, saving and wake resume come in Milestone 4.
Sections marked _(to come)_ are placeholders for that work.

Lay one unbroken track from the start gate to the end gate. The number beside
each row and above each column says how many squares of that line hold track.
Train Tracks is the logic puzzle known as Tracks in Simon Tatham's Portable
Puzzle Collection. This is a new implementation of that puzzle type for an e-ink
panel.

## The rules

- The grid is N x N. The start and end gates sit outside two edge squares, and
  the track enters and leaves through them.
- Every row and column clue is the exact count of track squares in that line.
- The track is one simple path: it never branches, crosses or loops, and it
  passes through each square at most once.
- **Given squares are locked track, and their shape is shown.** A given
  straight or curve fixes that square's two connections, which is also how the
  solver reads it. This was decided with the product owner on 2026-11-19
  instead of the PRD's plain framed block: it makes Easy read like real Train
  Tracks, and it gives the generator a strong clue to spend.

**What wins.** The win is checked against the rules, not compared with a
stored answer. Every clue must be met, and some single path from gate to gate
must run through exactly the marked squares while keeping every given's shape.
Crosses are notes only: they never count and are never required. Any valid
solution wins, so a puzzle that turned out to be ambiguous would still be
winnable from either answer. The generator will reject ambiguous puzzles anyway.

## Marking squares, not pieces

A tap cycles a square through empty, track and crossed. The player never
chooses a shape: `inferShapes` works out what to draw from the marks.

- Each track square starts with a connection toward every neighbouring track
  square, plus its gate if it has one. Givens keep their own shape.
- **Symmetry:** a square drops any connection its neighbour does not return.
- **Saturation:** a connection is forced from a neighbour that is a given, or
  that has two or fewer connections, and a gate is always forced. A square
  with exactly two forced connections drops the rest.
- Repeat until nothing changes. A square still left with three or four
  connections is ambiguous and is drawn as a plain block until more marks
  resolve it.

So a U through a 2x2 block resolves with no help: each top square has its gate
plus the settled square below it. A plus or a T junction stays a block.

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
`test_traintracks` covers the rules and the solver (about 2 s), and
`test_generator` covers generation.

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
   Confirm uniqueness with the exhaustive search.

Attempts are capped by **count**, never by time, so a seed means the same
puzzle on every device. The activity adds its own wall-clock budget. If that
runs out, it falls back to `TrainTracksFallback.h`: six `(seed, attempt)`
pairs per level that are known to succeed, so a fallback costs one attempt. A
host test regenerates every entry.

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
milliseconds on average and around a second at worst. That is inside the PRD's
6 s target, but it is a guess until it is measured on the Picco.

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
The given targets and path ranges are still the PRD's first guesses.

**Pinned.** `testPinnedPuzzles` pins one puzzle per level by hash, and
`testRatingIsPinned` pins the rater. Changing either changes which puzzle an
old save rebuilds, so update the pins **and** bump the save format version
together.

## Screens, input and refresh

`TrainTracksScreens.cpp` builds the screens from plain models, and
`host-tests/ui` builds them against a fake draw target.

**The play screen**, portrait, top to bottom: the header band (title, and the
level on the right), the column clues, the grid with the row clues on its
left, a status line ("12 OF 31 LAID", "LAYING TRACK", "SOLVED"), and a thumb
rail: CHECK, HINT and MENU at pill height, then UNDO and REDO 72px tall on the
bottom margin. Three layouts were rendered side by side; the thumb rail won
because it matches where the other games keep their controls and puts the two
most-used ones nearest the thumb.

**Sizes.** Each level is two squares bigger than the last. The panel's width
sets the cell, capped at 64px so a small grid still reads as a board:

| Level  | Grid  | Cell  | On the Picco (~220 ppi) |
| ------ | ----- | ----- | ----------------------- |
| Easy   | 6x6   | 64 px | about 7.4 mm            |
| Medium | 8x8   | 50 px | about 5.7 mm            |
| Hard   | 10x10 | 40 px | about 4.6 mm            |

9x9 for Hard (44px) was rendered too. 10x10 kept the two-square step and the
room for long routes, and Picross already ships a 10x10 at about 37px.

**Drawing a square.** Track draws its shape from `inferShapes`: two
connections are a straight with two sleepers or a quarter-arc curve, one is a
stub ending in a dot, none is a small solid block, and three or more (a
junction the marks do not settle yet) is a larger open block. Crosses are an
X. Givens sit on a dithered grey ground and show their real shape. Gates
break the frame, with an arrow in the band outside it: in at the start, out at
the end. A clue is boxed when its line is met and inverted when it is over.

**Input.** A tap on a square cycles empty, track, cross. The grid is
hit-tested from the same `Geometry` that draws it (`cellRect` and `cellAt`,
tested as exact inverses at every level's size), so its 144 squares spend
none of the interaction table's slots. The side keys are undo and redo.

**Refresh.** Every move rebuilds the frame and shows it with one fast partial
refresh of the whole panel, as Sudoku does. No game here refreshes a single
square's region. Picross also forces a full refresh after a run of board
changes to clear ghosting; whether Train Tracks needs the same is one of the
on-device checks below.

## On-device findings _(to come)_

The only device on hand is a BOOX Picco, so generation time, free heap,
ghosting and touch feel will be measured there, not on the X4 Pro.
