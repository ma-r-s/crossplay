# Murdle: a murder-mystery deduction game

The design, settled before any code. Read [building-apps.md](../building-apps.md)
for the method and [design-language.md](../design-language.md) for the look.

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

Everything is called `murdle`: the worktree, the branch `app/murdle`, the code,
the files and the namespaces. The tree was `casefile` for its first day, from
before the game had a name, and that is now gone.

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
   solution and whose mask shape the tier allows. A few hundred clues. One
   refusal here: nothing may narrow who was standing at the crime scene to fewer
   than two, in either direction, because that is a guess at the murderer rather
   than a clue about them (section 8, round six).
3. Shuffle, move a few dossier clues to the front (section 8, round five), then
   add clues greedily until the survivor count reaches 1. If the dossier bias
   cannot close the case, build it again without the bias rather than losing
   the attempt.
4. Prune in a second random order: drop any clue the puzzle still solves without.
   The result is minimal, so no clue in the list is redundant.
5. **Reject unless it can be solved without guessing.** See below.
6. Retry with a derived seed, bounded at 64 attempts. No seed in the sweep needs
   more than a handful, and the bound exists so that a future tier which is
   accidentally impossible fails in a reportable way rather than hanging the
   device. It is not a safety net for a preference: anything that biases clue
   selection has to be able to give up on its own, because the cast is drawn
   from the seed and is therefore the same on all 64 attempts.

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

A tap writes the cell it landed on and no other. The crossing-out is derived on
the way to the screen, so it costs no writes; the ticks it derives from are the
player's and are never taken away by anything but their own tap. A tap that
could only be honoured by removing one does nothing and names the ticks in the
way instead.

---

## 4. Two pages and a door

Chosen by rendering all three arrangements side by side against the same case.
Tabs and a clue rail both spent vertical space on the toggle, and at four
categories that space is exactly what the grid's legend needs -- so the
arrangement with no toggle chrome at all is the only one where the whole key
fits under the grid and a letter can be looked up without leaving it.

The cost of no chrome is that nothing says the second page exists. So the door
is a labelled control, not a bare tap zone: **the header's right side carries
the name of the other page with a chevron** (`GRID >` from the case file,
`< CLUES` from the grid). It reads as a page turn rather than a mode switch,
it says where it goes rather than where you are, and it sits in the header
because the header is already there and already black -- so the door costs no
vertical space, which was the entire reason for choosing this arrangement.

The body cannot be the toggle, because the body has two jobs of its own: cells
to mark on the grid, and clues to tick off on the case file.

### The front door

A miniature of your own grid, marks and all, with two lines of facts under it:
clues used, squares settled. Chosen by building four and rendering them against
the same case.

It replaces a 4x4 block of your last sixteen cases, which was copied from
Connections without checking whether it meant anything here. In Connections it
is a calendar -- one puzzle a day, so sixteen days is a shape you recognise and
a streak you can lose. Murdle has no cadence at all: cases are endless and on
demand, so sixteen boxes in arbitrary order say nothing, and on a fresh device
they are sixteen _empty_ boxes taking half the screen. Decoration has to carry
the player's own data and that data has to mean something. The pips passed the
first test and failed the second.

The board passes both, and it is not really decoration -- it is the thing
itself. The three that lost: the case's cast listed (the case-file page again),
a bordered card of facts (calm, and the only one whose empty state looked
deliberate -- its numbers survive under the board), and one enormous case
number (fast, and nothing to look at on a panel that holds its image for hours).

### Marking

Tapping a square cycles unknown, ruled out, locked in. Locking one in rules out
the rest of its row and column **within that block**, which is the bookkeeping
a pencil does: one suspect cannot hold two weapons. Reaching across blocks is
the deduction, and doing that for the player would be playing the game for them.

Locking one in is **refused** while another lock stands in that row or column,
and the refusal is three words: `ALREADY TICKED ELSEWHERE`. Changing your mind
costs two more taps than it did -- clear the old lock, then rule the new square
out and lock it in -- and every one of them is the player's own.

**The refusal has a band of its own, reserved whether it says anything or not,
and it sits between the grid and the key.** It used to appear above the grid
only when there was something to say, which pushed the board down the moment a
tap was refused and pulled it back up on the next tap. On a panel that takes a
second to answer, that is the one surface being read by position moving under
the reader's finger. Reserving the band stopped the movement; putting it
between the grid and the key rather than above the grid is where Mario wanted
it (2026-09-05) -- a refusal sends the player to the key to check, so the
message reads next to the thing it points at. The band is reserved off the
**bottom** of the grid's room, so the grid draws at the top of the face and the
reserved height falls in the gap above the key; the grid is top-anchored and
width-bound at every tier, so its origin and every cell stay put whether or not
a notice shows, and the key keeps the top and height it always had. The band is
**one line** of the tile cut, measured against the real face in
`host-tests/murdle`; it pays for itself out of the slack the key was spreading
into and loses no cell size.

One line is what makes reserving it on every frame the right trade, and getting
there is why the wording is three words. The refusal used to name both blocking
pairs -- `ALREADY TICKED: SPADE/FARM AND PAN/GARDEN. CLEAR THEM TO TICK HERE.`
-- which at real fixture names wraps to two lines, so the board paid a paragraph
of its room on every frame to say something on a handful of them. The squares it
named are on screen already, with the player's own ticks in them; `ELSEWHERE` is
the only part of that sentence a player cannot see, because the square they
tapped is not itself ticked. `blockedLine` cannot see a puzzle any more, so no
new fixture can widen the band.

It used to overrule instead, and that is the worst bug this game has had. From a
finished, correct board a cold tester tapped one false square and watched three
squares they had never touched change: the two locks that had crossed that
square out became crosses themselves, and a third square went blank because
those two locks were the only thing crossing it. Nothing was flagged, and
cycling the tapped square back did not bring any of them back. The overrule was
not a stray write -- it was the documented behaviour, and it looked reasonable
right up until a board was full enough for one tap to cost two answers.

The crosses tell you whose they are. **The player's own is a heavy bar; one the
grid worked out from a lock is a light one** -- same shape, two weights, the way
a pen sits next to a pencil. They were pixel-identical before, which is why the
same tester could not see that the squares the tap consumed were the ones
holding their answers up. HOW TO SOLVE names all four states.

### The key under the grid

One row a category, four aligned columns, the rows spread through whatever
height is left. Two things were wrong with the first version and only one was
obvious: it packed four entries into a run-on line, _and_ it set the four lines
two pixels apart while leaving a hand's width of empty screen underneath. The
space was already there; the layout was not asking for it.

The fix is spacing, not size. **The grid keeps every pixel it had** -- it is the
thing being read -- and the key spends the remainder. An intermediate version
reserved the key's height before sizing the grid, which cost three pixels a cell
and was the wrong trade.

### Ticking clues off

Every clue carries a numbered box. Outlined while the clue is still in play,
filled once you have used it; tap anywhere on the clue to flip it. The pager
line counts them: `2 / 3      5 OF 12 DONE`.

Ticked, never hidden. A clue you have finished with is still one you may want
to re-read, and removing it would make the numbers in the list stop matching
the numbers in your head. The first version struck the text through instead,
which is a mess across three ragged wrapped lines; a box is one element doing
both jobs and it survives any amount of wrapping.

Portrait, not landscape. The grid is square and 480 is the binding dimension
either way, and the clue view is a column of sentences.

## 5. The cast, and the two rules that shape it

No lore. Every field exists because a clue reads it, and a field no clue can
reference does not get written. But two rules about _names_ turned out to
matter more than anything in the table:

**Every name is one word, no longer than seven characters, and a word you
already know.** ANNA, HUGO, HAMMER, KITCHEN, GREED. The first table reached for
EPEE, FLAIL, GARROTTE, HAYLOFT and URQUHART because they carried the letters it
needed -- and a fixture whose name you have to decode is a clue you cannot read.
Suspects are bare first names for the same reason: ROOKWOOD and URQUHART are one
more thing to parse, ANNA is not.

Seven characters is a layout constraint rather than taste. The grid's key lays
entries in four columns of about a hundred pixels, and `J=JEALOUSY` was eleven
glyphs, which ran into the entry beside it. The key now measures its own column
pitch against the real face at draw time, and the cap is a design rule on top of
that rather than the thing holding it up.

**Inside one case, no two items anywhere share an initial.** Not within a
category and not across them. If a suspect is ABARA then nothing else in that
case starts with A: not a weapon, not a place, not a motive. That is what makes
a single letter a usable label -- A means one thing on the whole grid, whichever
axis it is on, and you never have to work out which axis you are reading before
you can read it.

`drawCast()` enforces it. It takes the categories scarcest-first (motives,
places, weapons, suspects) and refuses any letter already spoken for, and the
tables are sized so it cannot fail: every category has at least four free
letters left however the earlier ones went. A hundred thousand draws assert it.

**The axes are icons, not letters**, and that is part of the same rule. They
were S, W, P and M until it became clear that S then meant both "the suspects
axis" and STABLE -- exactly the ambiguity the single letters exist to remove.
A person, a hammer, a map pin and a heart collide with nothing and give all
twenty-six letters back to the items. The same four marks label the legend
rows, so the mapping is learned once.

The rest of the table is what the clue generator reads:

- **Suspect**: name and four attributes (handedness, eye colour, hair colour,
  height). Attributes are the only reason an indirect clue can exist, and their
  distribution within a drawn cast is a generator input: an attribute held by
  exactly one drawn suspect yields a positive clue, one held by two yields an
  elimination.
- **Weapon** and **place**: name, phrase, and one **trait** -- a two or three
  word noun phrase, unique across its table. The place trait is what the murder
  clue points at, which is the only reason places have one.
- **Motive**: name only.

Sentences come from templates in `MurdleText`, one per clue shape. The test
that matters is that every template over every fixture produces a grammatical
sentence, and it is asserted rather than eyeballed.

## 6. What is built

1. **`MurdleCore`** -- predicate, enumerating solver, generator, fairness gate.
   1,600 cases across the four tiers assert five properties each. Done.
2. **`MurdleCast` and `MurdleText`** -- the tables and the sentences, with the
   grammar swept over every clue every tier can produce. Done.
3. **`MurdleScreens` and `MurdleActivity`** -- six views, the save file, the
   shelf row. Playable end to end. Done.
4. **The three variants, rendered and decided.** Two pages with a header door;
   the other two are deleted.
5. **Played through in the simulator**, screen by screen: the menu, every tier,
   both faces, marking, ticking clues off, paging, the difficulty list, the
   how-to, the "drop this case?" sheet, the accusation, a wrong verdict, and a
   full round trip out of the app and back with the case and its marks intact.

## 7. What play-testing found

Four critics, one case each across the four tiers, no access to the code or the
answers, told to solve it and then be harsh. **All four solved their case
correctly**, which is the useful confirmation: the engine is sound and the cases
are fair. Everything below is about quality, and not one of these findings is
visible to any assertion in `host-tests/murdle/`.

Ranked by how many of them raised it independently.

**1. The suspect attributes are a false affordance (all four).** Every case
prints handedness, eye colour, hair colour and height for four suspects. On
Elementary they are referenced by **zero of 1,610 clues** -- the tier forbids
attribute clues by construction. Nosy uses them in 26% of clues, Hard Boiled
11%, Impossible 6%. A player builds an attribute table, guards it, and finds out
it was set dressing. One critic added that two suspects in their case shared
both printed attributes, so the dossier could not have separated them anyway.

**2. The traits are advertised and never fire (all four).** Eight parenthesised
details a case; one or two get used, always by the crime-scene clue. The player
is told these are how clues refer to things indirectly, and then they are not.

**3. The murderer is known too early (three).** Measured: revealed in the first
half of the solve in 50-62% of cases, at round 1.4 of 2.9 on Elementary and 2.5
of 4.6 on Hard Boiled. "The case peaks at roughly 40% and coasts." Everything
after the crime scene resolves is filling in a form whose answer you have.

**4. One logical shape, three registers (three).** "The one in the tower..." and
"Whoever was in the tower..." alternate with no semantic difference, sometimes
two lines apart. It reads as templates firing at random rather than as variety.

**5. Clues that are minimal but feel duplicated (two).** "GRETA did not carry
the vase" / "ROSA did not carry the vase". Two either/or clues sharing the same
motive pair. Minimality proves no clue is _removable_; it does not stop two
clues being the same sentence twice.

**6. Backwards phrasing when the target is a suspect (one).** "Whoever carried
the pan was not FELIX" should be "FELIX did not carry the pan."

**7. The witness statements are all first-person (one, and it is the deepest).**
Every statement is a suspect reporting their own weapon or place. Nobody
testifies about anybody else, so the murderer's lie only ever denies one fact
about themselves. A statement that cross-linked -- "VERA says: OSCAR was in the
tower" -- would let the lie poison someone else's row, which is the whole point
of the mechanic. As built, the Impossible tier degrades to "one of these four
facts is wrong, find out which".

**8. Nosy never places anything positively (one).** The tier bans bare
positives, so every suspect-to-place link routes through an attribute. And an
attribute that splits 3-1 is just a name in a costume: "the inn person was
right-handed", when one suspect is right-handed, is "ROSA was at the inn".

**9. "The body was found next to flour on the floor" (one).** "Next to" works
for a broken step and breaks for a property of the floor.

## 8. Six rounds of play-testing, and what changed

`run.sh --play <tier> <seed>` prints a case with no answer in it. Twenty
critics across six rounds solved one each and were told to be harsh. **All
twenty solved correctly**, every round: the engine has never produced an
unsolvable or ambiguous case. Everything they found was quality, and none of it
was reachable by any assertion in the suite.

What the rounds fixed, in the order the critics forced it:

1. **Witnesses talk about each other.** Every statement used to be a suspect
   reporting on themselves, so the murderer's lie could only deny one fact about
   the liar. And the murderer now _always_ lies about somebody else -- a lie
   about your own whereabouts is inert, because the crime-scene clue already
   fixes where the murderer was. Two critics solved cases in seconds with
   "the murderer is the one whose alibi is only their own mouth".
2. **The crime scene can be named by the murder weapon, not just the room.**
   This was the deepest finding. Naming a place means the scene is known the
   instant that clue is read, whatever order it is printed in -- so moving it to
   the end changed nothing, and two critics said so in almost the same words.
   "The body was found beside the weapon with grease on it" cannot be cashed
   until the weapon column is solved. The murderer now resolves in the first
   half of the solve in 16% of Hard Boiled cases, down from 50%.
3. **The accusation is gated on resolving last.** Cases where the murderer's row
   closes early are regenerated, with the best near-miss kept so a tier can
   never fail to produce anything.
4. **No two witnesses assert the same proposition**, and none places the
   murderer at the scene. Both let a case fall in two lines.
5. **Attributes fire on every tier** (Elementary was 0 of 1,610 clues), the cast
   is redrawn until its dossier can actually carry a clue, and an attribute held
   by exactly one drawn suspect is banned where bare positives are -- otherwise
   "the one with the oar had green eyes" is a free identification in a costume.
6. **Every case contains a clue joining two non-suspect categories.** One critic
   got a case whose weapon grid and place grid never touched and solved them
   separately, correctly calling it two puzzles in one grid.
7. **No clue says the same thing twice**, no shape appears more than three
   times, no attribute more than twice, and near-synonym motives (GREED beside
   MONEY) never share a case.
8. **The prose**: one register per case; a clue about a suspect starts with the
   suspect; "the suspect in the study" rather than the conditional "whoever was
   in the study"; "IRON (with a burn mark)" rather than an apposition that made
   the iron _be_ the mark; a cane instead of a carried chair.

### Round five: the dossier was decorative for a reason

Critics said it three rounds running, and the audit agreed once it was asked the
right question. Attribute clues were **12% / 15% / 5% / 2%** of the clues in a
finished case, by tier. Four dossier axes get printed, the player builds a table
off them, and the two hardest tiers referenced that table about once a case.

Two causes, both structural rather than a matter of taste.

**Height could not appear at all above Elementary.** The only masks height
produced were "the tallest" and "the shortest", and those are masks with _one
bit set_ -- which `buildPool` refuses on every tier that bans bare positives,
because an attribute held by one drawn suspect names that suspect. So the one
axis the dossier prints as a number was unusable by construction on three tiers
out of four. It now also produces **comparisons against a named suspect**:
"was taller than ANNA" is the set of suspects above ANNA's height, which is a
proper subset of any size and reads off the same printed inches. This is the
Murdle clue template that most obviously should have been borrowed and was not.

**Attributes lost the shuffle.** They are generated for one target category
while plain negatives are generated for every ordered pair, so at four
categories they were a small slice of the pool and the greedy loop rarely
reached them. A few now start at the head of the shuffled pool, at most one per
distinct attribute. It stays a bias and not a quota: the greedy loop still only
takes a clue if the case is not yet unique, and the prune pass still drops any
the rest of the case turned out to imply.

Result: **38% / 28% / 18% / 11%**, with clue counts, round counts and the
reveal-timing distribution all unmoved.

The bias also has to be able to give up, and finding that out cost the round its
only real bug. A cast whose masks are all weak spends its promoted slots on
near-nothing and the case then hits `kMaxClues` before it is unique -- one
Impossible seed produced _no case at all_, which is far worse than a case with
no attribute clue in it. Backing off by attempt number does not fix it: `attrs`
is a function of the seed, not of the attempt, so a bad cast is bad on all 64.
The fallback lives inside the attempt now, next to the thing it falls back from.

### Round six: the critics overturned round five's other half

Four more play-testers, four fresh cases, one per tier and two on Hard Boiled.
**All four solved correctly**, which makes twenty out of twenty across five
rounds of this. Two of the three findings below were unanimous.

**Naming a thing by its detail buys nothing, so only the body clue does it now.**
Round five had extended that device from weapons to places, on the theory that
an extra lookup is most of what separates an easy tier from a hard one. Four
testers demolished it independently and in almost the same words -- "a seven-word
detour to avoid saying 'the wire'", "a pointer dereference", "clue 6 is just
cosplaying clue 13". The reason is structural. The case file has to print every
weapon and place with its detail attached, because printing only the crime
scene's would give the body clue away, so the mapping from detail to name is
already on the page. An indirection whose key is printed beside it is a synonym,
and a longer one. It also does harm: one case said "the garden" in one clue and
"the place with trampled flowers" two clues later, and a reader who misses that
those are one place is stuck on a puzzle that is not actually hard. So the
device is now reserved for the body clue, where it is the whole conceit, and the
weapon variant that predated round five is gone too.

**No clue may narrow who was standing at the crime scene to fewer than two.**
"Either ANNA or OSCAR was on the dock", printed beside "the body was found in
the place with wet footprints", is a coin flip on the only answer the case
exists to protect, available on line two before any deduction at all. Two
testers reported exactly that and both called it their worst finding. The
refusal is symmetric, which the first version of it was not: "QUINN was in the
garden" points from the suspect at the scene rather than from the scene at the
suspects, and names the murderer just as plainly. A negative always survives the
rule, because removing one candidate is what a logic grid is made of.

Worth recording honestly: this barely moves the aggregate. Early reveal on Hard
Boiled went 16% to 14%, and on Elementary it did not improve at all. At three
suspects and three places there is no room to narrow gently -- two eliminations
settle the scene in one round -- so an early reveal on Elementary is what a 3x3
grid is, not a defect to tune out. The rule is kept because it makes a named,
testable clue pair impossible to generate, not because a number moved.

**Prose.** "was taller than ANNA" rather than "stood taller than", since height
is a standing fact and not something the suspects did; and the how-to page no
longer says "Nobody shares", which is a transitive verb with nothing to be
transitive on.

### What the critics found that is still open

- **Traits collide thematically across categories.** One case drew `SPADE (with
wet mud on it)` alongside `FARM (with muddy boots)`; another drew `DOCK (with
wet footprints)` beside `CAVE (with cold water)`. No clue licenses the
  inference, but the draw manufactures the false lead for free, and a tester
  pointed out the real cost: a tired player misreads the body clue as the other
  water-themed place and confidently accuses the wrong suspect. That is a wrong
  answer produced by flavour text rather than by bad reasoning. `drawCast`
  should refuse it.
- **The reveal gate is a rubber stamp.** A case is meant to be regenerated
  unless the murderer's row closes in the final round, with the best near-miss
  kept so a tier can never fail to produce anything. The audit says the murderer
  is known at round 1.6 of 3.1, 3.1 of 4.6, 4.3 of 6.0 -- if the gate were
  passing those pairs would be equal. The fallback is not the exception, it is
  the normal path, and the scene rule above only treats a symptom.
- **The dossier is still around half decoration in any one case.** Round five
  fixed how often it fires overall, but four printed axes against two or three
  attribute clues cannot all be used. The sharp version of the complaint is that
  printing four columns where one is live teaches a player to skip the dossier,
  which will then burn them on a case that needs it.

## 9. What has not been checked

Two things, and one of them needs a person.

**Nobody has solved a case.** Every screen has been driven, but a scripted tap
sequence cannot deduce, so the correct-accusation path -- CASE CLOSED, the
solved counter, the record pips on the menu -- has only ever been reached in a
unit test, never on screen. Playing one case to the end covers all of it.

**Generation has never been timed on hardware.** Only in a simulator that runs
on a laptop, which says nothing about a 160MHz RISC-V core. It runs off the
render path behind a frame that says so, and the arithmetic is bounded (about
400 passes over 55,296 candidates, all table lookups), but the number is
unknown and nobody should call it fast until somebody measures it. If a new case
feels slow on the device, that is where to look first.
