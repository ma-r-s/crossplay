# Murdle: a murder-mystery deduction game

The design, settled before any code. Read [building-apps.md](building-apps.md)
for the method and [design-language.md](design-language.md) for the look.

The mechanic is **Murdle**'s, by G.T. Karber, and the name is kept because it is
the name of the thing. Everything else is ours: the cast, the fixtures and the
sentences are generated here and share nothing with his. The logic-grid itself
is not anyone's property and is a hundred years old. What follows is what the
mechanic actually is, taken from the book's own tutorial rather than from a
summary of it.

**There is no lore.** No detective, no running story, no character sketches. A
case is a random draw of names and fixtures, and the only requirement on the
writing is that the sentences come out correct English. Section 5 is sized to
that and not one field larger.

The worktree and branch are called `casefile`, which is what they were named
before the game had a name. Code, files and namespaces are all `murdle`.

---

## 1. What the game is

Every suspect has a weapon and a location (and at the top tiers a motive). Those
assignments are a bijection: one suspect per location, one weapon per suspect.
Exactly one of those suspects is the murderer.

So the game is two things stacked, and this is the part a generic logic-grid
implementation gets wrong:

1. **Solve the whole assignment** by deduction, from a list of clues.
2. **One further clue names the crime scene**, usually by referring to a detail
   in a location's description ("the body was found next to peeling paint", and
   only one location's description mentions peeling paint). That pins the
   murderer, since by then you know who was where.

Then you accuse: one suspect, one weapon, one location, one motive.

### The grid is a staircase, not a square

With three categories the sub-grids are laid out like this, which is what the
book draws and what every logic-grid puzzle has drawn since the 1960s:

```
                SUSPECTS      LOCATIONS
              +---------+   +---------+
    WEAPONS   | Wea x Su|   | Wea x Lo|
              +---------+   +---------+
  LOCATIONS   | Loc x Su|
              +---------+
```

Locations appear twice, once across the top and once down the side, so every
pair of categories meets in exactly one block. With `n` categories there are
`n*(n-1)/2` blocks: 3 blocks at three categories, 6 at four. At four categories
of four items the whole thing is 12x12 cells, of which 96 are live.

Every cell is one of three marks: unknown, no, yes.

### The clues, and the one thing they all are

The book's tutorial uses five clues and they look like five different kinds of
sentence:

- "Whoever was in the stadium was right-handed." (only one suspect is)
- "The suspect with the sharp pencil resented the person at Old Main."
- "The suspect with a graduation cord had beautiful hazel eyes."
- "Dean Glaucous seemed to carry a lot of logic textbooks around." (only the
  backpack's description mentions logic textbooks)
- "The body was found next to peeling paint."

Logically they are all **one predicate**:

> the row containing item `x` of category `A` has, in category `B`, a value
> drawn from set `S`.

A direct positive is `S` with one bit set. A direct negative is `S` with one bit
clear. An attribute clue is `S` = the set of suspects with that attribute. An
either/or is `S` with two bits. "Resented the person at" is a negative wearing a
grudge. The murder clue is the same predicate applied to the murderer.

That collapse is the whole engineering story. One predicate means one solver,
one verifier, one serialisation, and all the variety lives in the prose. It is
also why the difficulty knob can be honest: harder tiers simply forbid the
one-bit positives and force the indirect forms.

### Difficulty, the two axes you named

You had it right, and it matches the book's own four tiers.

| Tier            | Categories | Items | Clue palette                          | Clues |
| --------------- | ---------- | ----- | ------------------------------------- | ----- |
| **ELEMENTARY**  | 3          | 3     | direct positives and negatives        | 4-7   |
| **NOSY**        | 3          | 4     | + attribute clues, no bare positives  | 7-13  |
| **HARD BOILED** | 4          | 4     | + either/or, motives in play          | 10-16 |
| **IMPOSSIBLE**  | 4          | 4     | + statements, where the murderer lies | 9-15  |

The clue counts are measured over 400 generated cases per tier, not chosen.
They come out that way because every case is pruned to a minimal set.
Impossible carries fewer lines than Hard Boiled because four of its lines are
statements, and a statement is information too.

The last one is a genuine jump and is still pure logic: each suspect makes a
claim, every claim is true except the murderer's, so the statements and the
murder identity have to be solved together. No ciphers, no anagrams. They are a
decoding chore before the deduction rather than part of it, and on a screen with
no scratch paper they would be worse than they are on paper.

---

## 2. The generator

Freestanding, deterministic from a seed, on-device. No pack, no network, no SD
dependency, infinite cases. `MurdleCore` in the same shape as `ChessCore` and
`InsiderCore`, so `host-tests/murdle/` can generate thousands of them on a
laptop and assert on every one.

### The candidate space is small enough to enumerate

A solution is `n-1` permutations of `k` items relative to the suspects, times
`k` choices of murderer. So the space is `(k!)^(n-1) * k`:

| Shape | Candidates |
| ----- | ---------- |
| 3x3   | 108        |
| 3x4   | 2,304      |
| 4x4   | 55,296     |
| 4x5   | 8,640,000  |

Everything the tiers need is under 56k, so the solver is exhaustive enumeration
with an early exit on a candidate's first failing clue and a stop at two
solutions. No SAT solver, no backtracking search, no heap, and no survivor bit
set either -- that was the first design and re-walking the space turned out to
cost less than the 6.9KB of DRAM it would have needed. **Five items is refused**,
which is why the tiers stop where they do; three or four of each is what the
game asks for anyway.

Enumeration walks nested loops over permutation indices rather than decoding an
index, so there is not a single division in the hot path.

### Generating one case

1. Seed an xorshift32 (the same one Insider uses), draw a random solution and a
   murderer.
2. Build the pool: every instance of the predicate that is **true** under that
   solution and whose mask shape the tier allows. A few hundred clues.
3. Shuffle, then add clues greedily until the survivor count reaches 1.
4. Prune in a second random order: drop any clue the puzzle still solves without.
   The result is minimal, so no clue in the list is redundant.
5. **Reject unless it can be solved without guessing.** See below.
6. Retry with a derived seed, bounded at 64 attempts. The sweep has never seen
   the budget run out on any tier, and the bound exists so that a future tier
   which is accidentally impossible fails in a reportable way rather than
   hanging the device.

### Step 5 is the one that matters

A puzzle with a unique solution can still require trial and error, and that
feels like the game cheating. So a second solver runs the deductions a person
would: apply each clue as an elimination, promote a row or column with one
survivor to yes, cross the rest of that row and column, and propagate
transitively across blocks (if A is with B and B is not with C, then A is not
with C). Iterate to a fixpoint.

If that reaches the full solution, the puzzle is solvable by pure deduction and
the number of rounds it took is a **measured** difficulty rather than a guessed
one. If it does not, the puzzle is thrown away. This is the difference between
a generator that produces puzzles and one that produces good ones.

### How it gets verified

The exhaustive property, in the spirit of perft: for every tier, 2,000
sequential seeds, and for each one assert that there is exactly one solution,
that every clue is true under it, that no clue can be removed, that propagation
alone solves it, and that the clue count is in band. Then mutate the uniqueness
check and confirm the suite goes red, because a green suite that cannot go red
is worse than no suite.

Generation cost has not been measured on hardware, only in a simulator that
runs on a laptop, which says nothing about a 160MHz RISC-V core. It runs off the
render path either way (below), behind a frame that says so. The number is
unknown and nobody should call it fast until somebody times it.

---

## 3. The state machine

This is the part you asked to settle first, so it is settled here in full.

```
                         shelf::leave()
                               ^
                               | Back
                            +--+---+
              +------------>| MENU |<-------------+
              |             +--+---+              |
         Back |          /     |     \            | DONE
              |  DIFFICULTY  play   HOW TO        |
              |        |       |        |         |
          +---+----+   |       |    +---+-----+   |
          |SETTINGS|<--+       |    | HOW TO  |   |
          +--------+           |    +---------+   |
                               v                  |
                          +---------+             |
              +---------->|  CASE   |             |
              |           |  clues  |             |
       KEEP   |           |  grid   |             |
       LOOKING|           +----+----+             |
              |                | ACCUSE           |
              |           +----+-----+            |
              |           | ACCUSE   |            |
              |           +----+-----+            |
              |                | CONFIRM          |
              |           +----+-----+            |
              +-----------+ VERDICT  +------------+
                          +----------+
                               | NEW CASE
                               v
                            (generate)
```

Six views. Back is uniform and has no exceptions: from `MENU` it calls
`shelf::leave()`, from anywhere else it goes to its parent.

### Clues and grid are not two views

They are two faces of `CASE`, and the toggle is not navigation. Back from either
face leaves the case. If the toggle were a state transition then Back would
sometimes mean "other face" and sometimes mean "leave", which is the kind of
thing that is invisible until somebody loses their marks. One state, one field,
`face: Clues | Grid`, persisted so you come back to the one you left on.

### Every new case goes through one funnel

`requestNewCase()` is the only thing that generates. `PLAY` on a fresh menu
calls it, `NEW CASE` calls it, `PLAY AGAIN` on the verdict calls it. It asks for
confirmation when an unsolved case is open and generates otherwise. A new door
added later cannot get this wrong, because there is no choice left at the call
site. This is the chess lesson: an intent that means two things belongs in one
funnel, not in a guard at each caller.

### Generation is deferred one loop pass

Set a flag, paint the "A NEW CASE" frame, generate on the next pass. The frame
lands before the work starts, and the case arrives on a full refresh, because a
new case is a page turn and the flash is the punctuation for it.

### Difficulty never touches an open case

The setting is read exactly once, at generation. A case in progress carries its
own shape, so changing the tier mid-case is harmless and the restored case comes
back at the size it was made at. Guard on the condition, not on a proxy for it.

### What is saved, and when

One file. Difficulty outlives the case, so it sits at the head and survives the
case being discarded.

```
u8   version
u8   tier                  the setting, not the open case
u8   caseTier              the open case's own shape
u32  seed
u32  fingerprint           hash of the generated puzzle
u8   marks[36]             two bits per cell, 12x12 worst case
u32  cluesStruck           reading aid, one bit per clue
u8   wrongAccusations
u8   face
u8   solved
```

The case is not stored, it is regenerated from the seed on load and checked
against the fingerprint. If the generator ever changes, the fingerprint stops
matching and the save is dropped rather than restored wrong. Same discipline as
the cache format version and the `GameId` bump.

Written on `onExit()` and on Back, never on a tap.

### Marking

Tapping a cell cycles unknown, no, yes. Marking a **yes** crosses out the rest of
that row and column **within the same block only**, because that is the
bookkeeping the pencil does and not the deduction. Cross-block propagation is
the game, and doing it for you would be playing it for you.

---

## 4. The two views, and the one thing left for you to choose

All three are built and rendered. `qa-artifacts/variants-grid.png` and
`variants-clues.png` put them side by side on the same case, which is the only
way this gets decided: the winner usually wins on one specific element and that
is invisible in any single screenshot.

**A. Two tabs** (`MURDLE_VIEW_VARIANT=1`, the current default). A CLUES / GRID
bar under the header. The clearest affordance of the three and the only one
where the toggle names itself. It costs 54px of grid, which at four categories
is the difference between the legend fitting under the grid and not fitting.

**B. Grid with a clue rail.** The grid keeps the screen and one clue sits under
it with a stepper, so marking while reading is one gesture instead of three.
Biggest grid, but the rail's clue box is cramped: a two-line clue nearly fills
it, and the ALL CLUES pill is small. Its first render had the rail's door out
and nothing facing back, so the clue list was a room with no exit -- which is
the sort of thing three layouts side by side make obvious in a second.

**C. Swipe between two full pages.** No toggle chrome at all, so it gets the
biggest grid *and* the full legend under it at four categories, which neither
of the others manages. The risk is exactly what the still image shows: nothing
on screen says the body is tappable.

The real trade-off the render exposed, which none of the prose above predicted:
**at four categories the legend only fits when the toggle costs nothing.** So
the choice is not really tabs-versus-swipe, it is whether the key lives under
the grid or one page away in the case file. Both work; they are different games
to hold.

Whichever you pick, the other two get deleted with the `MURDLE_VIEW_VARIANT`
switch in the same commit. A variant macro that survives is a second codepath
nobody renders.

Portrait, not landscape. The grid is square and 480 is the binding dimension
either way, and the clue view is a column of sentences.

**Axes carry letters, not icons.** A 31px column head cannot hold a word and
this renderer does not rotate type, so each item gets one letter, distinct
within the case and mnemonic where it can be. Icons were the plan and are still
possible, but they would need artwork per fixture rather than per app: sixteen
weapons and sixteen places is not something Lucide has silhouettes for.

## 5. The cast, which is a table and not a story

No lore. Every field below exists because the clue generator reads it, and a
field no clue can reference does not get written.

- **Suspect**: name, icon, and four attributes (handedness, eye colour, hair
  colour, height band). The attributes are the entire reason an indirect clue
  can exist, and their distribution within a drawn cast is a generator input: an
  attribute held by exactly one drawn suspect yields a positive clue, one held
  by two yields an elimination. Nothing else about a suspect is modelled.
- **Weapon**: name, icon, weight class, and one **trait**, a two or three word
  noun phrase ("a scratched handle").
- **Location**: name, icon, indoor or outdoor, and one trait. The location trait
  is what the murder clue points at, which is the only reason locations have one.
- **Motive**: name only.

A trait is one short phrase, not a paragraph. It is what makes the final clue
indirect ("the body was found next to peeling paint") without anybody having to
write prose, and it costs four words per fixture.

Roughly 24 suspects, 16 weapons, 16 locations, 12 motives, which is a few KB of
flash and an afternoon of filling in a table. Each case draws its cast at
random, so a shape never reads the same twice.

Sentences come from templates in `MurdleText`, one per clue shape, filled from
those fields. The test that matters is that every template, over every fixture
in the tables, produces a grammatical sentence: articles agree, plurals agree,
and nothing reads as a mad lib. That is assertable and it is asserted.

---

## 6. What is built

1. **`MurdleCore`** -- predicate, enumerating solver, generator, fairness gate.
   1,600 cases across the four tiers assert five properties each. Done.
2. **`MurdleCast` and `MurdleText`** -- the tables and the sentences, with the
   grammar swept over every clue every tier can produce. Done.
3. **`MurdleScreens` and `MurdleActivity`** -- six views, the save file, the
   shelf row. Playable end to end. Done.
4. **The three variants, rendered.** Waiting on you.
5. Left undone on purpose: the how-to deck is four plain pages and has not been
   rendered against a real case; the verdict screen has been built but not
   played into; and generation has never been timed on real hardware, only in
   the simulator, which runs on a laptop and tells you nothing about the
   ESP32-C3. The deferred "A NEW CASE" frame covers it either way, but the
   number is unknown and should be measured before anyone calls it fast.
