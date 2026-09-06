# The picross bank

Where the puzzles in Picross came from, why they can be trusted, and the one
rule decision that shapes every screen.

The short version: 321 nonograms in two tiers (137 at 10x10, 184 at 15x15),
every one proved to have exactly one solution and to be reachable by single-line
reasoning, by a generator that refuses to emit any that are not.

**Every one of them is third-party work.** They were designed by six named
people, published on janko.at, and are used here BY PERMISSION -- a permission
granted to this project, not a licence, and one a fork does not inherit.
[`assets_local/picross/PROVENANCE.md`](../../assets_local/picross/PROVENANCE.md)
is the record; read it before copying puzzles out of this repository. The fork's
own 68 hand-drawn pictures are still in `assets_local/picross/pictures.txt` and
are deliberately NOT in the bank -- Mario's call, recorded there.

## What the game is

A nonogram. The numbers beside a row or column are the lengths of the runs of
filled cells in it, in order; the player deduces which cells are filled and a
picture appears. Filling a cell asserts it is solid; a wrong assertion is a
mistake (see below).

## The mistake rule, and the switch that changes it

The shipped rule is the classic Nintendo Picross one, and it was Mario's call:

- **FILL** a cell that really is solid and it fills. FILL a cell that is empty
  and it is a **mistake**: it locks in permanently, shows as a struck-out cell,
  and bumps a visible mistake count.
- **MARK** is a free annotation ("I think this is empty"). It is never a
  mistake, never counted, and always reversible.

Because a wrong fill becomes a locked mistake rather than a filled cell, a
_filled_ cell is always a correct one. Two things depend on that invariant:

1. The **satisfied-clue cross-out** (a clue is crossed out once its line is done)
   is honest -- a line's fill count can equal its clue total only when the line is
   actually solved, never by a lucky miscount.
2. **Win** is "every solid cell filled", one comparison, with no separate rule
   checker.

**The alternative Mario can choose is free-erase**, and it is `Rules::FreeErase`
in [`PicrossCore.h`](../../src/apps_local/picross/PicrossCore.h) -- one branch in
`Board::fill`. Under it a wrong fill becomes a plain, reversible cross instead of
a locked mistake: no penalty, no count. That keeps a filled cell correct, so the
dimming and the win check stay honest with no other change. (If a future variant
instead wants wrong fills to _stay filled_ until erased, the dimming must move
off the count-based check -- the dependency is written at `Board::rowSatisfied`.)

## Provenance and licence

The bank is built from **one file per origin**, and the generator reads them in
order (`gen_picross.SOURCES`), and **a file is shipped only by being in that
list**:

| file | puzzles | origin | rights | in the bank |
|---|---|---|---|---|
| `assets_local/picross/janko.txt` | 321 | janko.at, six named designers | used by permission | yes |
| `assets_local/picross/pictures.txt` | 68 | drawn for this fork | CC0 1.0 | **no** |

`pictures.txt` is not in `SOURCES`. Its 68 pictures are valid, pass the same
gate and are CC0, and Mario's call is that the hand-drawn artwork "is not and
won't be close to good enough" beside puzzles somebody designed; he also dropped
5x5 as a tier, and every 5x5 in it is hand-drawn. The file stays as the worked
example of the format -- adding it back to `SOURCES` is one line -- and
`PROVENANCE.md` records the decision so it does not get helpfully re-added. A
file named in `SOURCES` that is missing is a hard error, not a shorter bank.

`parse()` starts each file with fresh file-level defaults, so if the bank ever
mixes origins again one file's `@@license` cannot leak onto the other's puzzles.

To author more of our own: write candidates into `pictures.txt` and triage them
with `gen_picross.py --curate` (PASS/FAIL per picture, no emit).

For the shipped puzzles: the permission was obtained by the project owner from
the designers Yilmaz Ekici and Danilo Kusmin, and separately from Otto Janko for
the collection, on 2026-09-05. It is **not a public licence**, it does not
extend to forks, and it did not come with the data -- the grids travelled
janko.at -> `puzzlekit` -> `puzzlekit-dataset`, and that last carries no LICENSE
and no provenance statement at all. `PROVENANCE.md` says all of this at length,
and the importer will not write the file unless it does.

An arbitrary two-tone image converted to clues is usually _not_ a valid nonogram
(it has several solutions), and any collection taken from elsewhere needs its
source and licence honoured -- the Wavelength retail-deck problem. The generator
answers the first; the permission record and the per-puzzle provenance field
answer the second.

### Provenance is a field, not a paragraph

Every puzzle carries an index into a `kProvenances[]` table of
`{author, license, source}` triples, emitted beside the bitmaps by the
generator. One row per distinct origin, so this fork's own bank costs a single
row -- and a bank that ever mixes origins can still state, per picture, who drew
it and under what terms.

It is a field rather than a note in this file because a note describes a bank
that no longer exists the moment anything is added to it. `host-tests/picross`
asserts that every puzzle names a row that exists and that no row leaves its
author or licence blank: an empty licence and "all rights reserved" are the same
fact, so the empty string must never stand in for one.

### Importing a corpus, and what stops one shipping

`tools_local/picross/import_picross.py` converts a third-party corpus into
`pictures.txt` format, running each candidate through the SAME `evaluate()` the
hand-drawn pictures face (it imports it from `gen_picross`, rather than keeping
a second copy to drift). It exists because the pictures people enjoy solving are
_designed_, and a corpus somebody drew and somebody else played is the only
place to find a lot of them at once.

The script **refuses to write inside this repository** unless the licence it was
given is one of a short redistributable list, OR `--permission` cites a record
inside the tree that states who granted it, that it is not a public licence,
that it does not extend to forks, and when. A puzzle whose licence is unstated
is all rights reserved; a file in `assets_local/` is in every clone and every
release. The refusal is a mechanism rather than a line in a checklist, and the
thing that opens it is a written record rather than a flag, because a flag
records nothing for the next reader.

The `pictures.txt` format carries the provenance itself: `@@author` /
`@@license` / `@@source` set a file-level default from that point down, and a
single-`@` line above a name overrides it for that one picture.

### The gate cannot see the picture, so the selection is a judgement

Unique, line-solvable and fills-its-grid are all properties of **the clues**. A
puzzle can satisfy every one of them and still solve into a scatter of blobs
nobody can name, and no filter anywhere can tell the difference -- the finished
picture is simply not in the data the gate looks at.

So the import is **curated**, not bulk. `--ids` takes a file of corpus ids and
imports only those; [`assets_local/picross/janko-selection.json`](../../assets_local/picross/janko-selection.json)
is the list, with the method that produced it and the counts it rests on.

**All 531 gate-passing candidates were judged**, one question each: could the
subject be named without the caption telling you? 321 were kept -- **137 of 263
at 10x10 (52%) and 184 of 268 at 15x15 (69%)**. Both the keeps and the drops are
recorded, so a second opinion can disagree with a specific puzzle rather than
with a rate.

Two things make the judgement worth trusting. It is made **at the size the
picture is actually seen**: contact sheets of 48, rendered at 9px cells for a
10x10 and 6px for a 15x15, which is the ~90px picture the picker draws a solved
tile at. A picture that only reads when blown up is a false keep. And the
question stays the same one -- a puzzle that solves into an interesting-looking
pattern nobody can name is a drop, however much structure it has.

`assets_local/picross/janko-authors.json` records the author of all 531
candidates, read from each puzzle's own page, so "every puzzle has a named
author" can be checked rather than believed.

**The 10x10 tier is a real on-ramp, not a token one**: 137 puzzles against the
15x15's 184. The corpus is more legible at 15x15 (69% against 52%) because a
bigger grid draws a better picture, but 10x10 passes enough of them that the
bank does not have to open on its hardest size.

### Reproducing the imported half

    python3 tools_local/picross/import_picross.py \
        --corpus <path>/Nonogram_dataset.json --format janko-json \
        --sizes 10,15 \
        --ids assets_local/picross/janko-selection.json \
        --author-map assets_local/picross/janko-authors.json \
        --license 'all rights reserved, used by permission' \
        --source-template 'https://www.janko.at/Raetsel/Nonogramme/{id04}.a.htm' \
        --name-prefix JANKO \
        --permission assets_local/picross/PROVENANCE.md \
        --out assets_local/picross/janko.txt
    python3 tools_local/picross/gen_picross.py

`{id04}` rather than `{id}`: janko serves `0001.a.htm` and 404s on `1.a.htm`,
and redirects the unpadded three-digit form. A provenance URL that 404s is worse
than no URL at all, because it looks like the origin was recorded and checked.

**Only 10x10 and 15x15 are imported, and that is a layout decision.** The corpus
also holds 393 at 20x20, 372 at 25x25 and 435 at 30x30, all excluded: at the
densest 20x20 the row-clue gutter takes 217px of the 480px panel against a 240px
grid, cells fall to 12px, and the satisfied-clue strikethroughs read as vertical
smears through the row-clue digits. 15x15 at its worst is clean (19px cells, a
177px gutter). Making the larger sizes playable is a clue-gutter redesign, not
an import setting.

## Verification

Two implementations of "unique" and "line-solvable", in different languages,
agreeing on all 321 puzzles. This is the app's equivalent of the dungeon bank's
cross-check.

**In Python, at generation time.**
[`tools_local/picross/gen_picross.py`](../../tools_local/picross/gen_picross.py)
derives each puzzle's clues from its picture, then:

- runs a **line-solver** from a blank grid using only single-line deductions. If
  it determines every cell and lands back on the source picture, the puzzle is
  line-solvable AND unique -- a line-solver that fixes every cell has proved
  every cell was forced, which is exactly what one solution means;
- runs an **independent exhaustive count** (a different algorithm: row-by-row
  DFS with column pruning) and requires the count to be exactly 1;
- requires the picture to **fill the grid it claims**: no empty first or last row,
  no empty first or last column. An uncropped drawing makes a puzzle smaller than
  its label, and the SIZE LABEL is what tells the player the difficulty tier -- a
  15x15 whose ink only spans eight rows is a 15x8 lying about its tier. Interior
  empty lines stay legal (a picture may genuinely have a gap), which is the only
  way a "0" clue should ever appear.

It refuses to write the header unless every puzzle passes both. The clues are
never stored, only the picture, so the clues cannot disagree with it.

```bash
python3 tools_local/picross/gen_picross.py   # a second or two; one line per puzzle
```

Expect designs to be rejected, on any of the three grounds. SAILBOAT and an early
KEY admitted two pictures each; about one authored picture in five is not
line-solvable; and a key and a crescent moon at 15x15 could not be drawn to touch
all four edges at all, so they became ENVELOPE and STAR instead. That is the tool
working -- a rejected drawing is cheaper than an unfair puzzle or a lying label.

**In C++, against the header that ships.**
[`host-tests/picross/`](../../host-tests/picross/) carries a second, brute-force
implementation of both properties (a line-solver and a solution counter over
`2^n` patterns, obviously correct for `n <= 15`) and runs them over every stored
picture, plus the mistake/win/clue/restore rules of `PicrossCore`.

```bash
./host-tests/picross/run.sh        # ~28k checks over 321 puzzles
```

A hand-edit to the generated file, or a bad merge, fails here rather than on the
device.

## Why the device does not know the rules

`PicrossCore` has no nonogram solver. Clues are derived from the stored bitmap
(`lineRuns`), and the board is finished when every solid cell is filled -- which
is exactly equivalent to "correct" because the solution is unique, and that
uniqueness is what the pipeline above proves. Implementing a solver on the device
would mean two implementations that must agree forever, for nothing the player
can see.

## The satisfied clue is crossed out, diagonally

A clue whose line is finished sits on a light dotted "done" chip and is crossed
out. The cross is **diagonal, at one consistent angle, on every struck clue**,
and that is not a style choice: a "1" is a vertical stem, so any HORIZONTAL rule
through its centre makes a dagger or a plus no matter how far it overshoots or
what sits behind it. Geometry beats every cosmetic defence -- a centred,
overshooting horizontal rule still read as a "t" at native size. A diagonal still
passes through the centre of the number and still reads as "crossed out", but
cannot be mistaken for a plus. The cross stays a pixel inside the chip on both
axes (it used to poke past, which read as unfinished), and a **"0" clue is never
crossed out**: an empty line has nothing to satisfy, and a struck zero reads as a
slashed zero. Prove it on a render at NATIVE resolution every time -- a struck
lone "1" is the specific test, and a 4x zoom will flatter it.

## The picker does not spoil

A solved puzzle's tile shows its finished picture; an unsolved one shows only its
number and size. The reveal is the whole reward, so the name and the picture stay
hidden until the puzzle is done -- the name especially, because the name _is_ the
answer, which is also why the board never shows it while you play.

**The name has exactly one site on screen**: the win screen (`buildWin`), under
the revealed picture and above the grade. (`buildWin` takes its bands with
`takeBottom`, which stacks UPWARD, so the source order actions / grade / credit
/ name reads bottom-to-top; on the panel it is picture, name, designer credit,
grade, buttons.) Nothing else in the app draws it --
not the picker, not the board, not the status strip. That is worth knowing
before importing a corpus with no titles: a made-up or catalogue name cannot
spoil anything, because it is never shown to anybody who has not already seen
the picture. It only has to be a decent punchline once.

## The picker: size tabs, one selection language

321 puzzles is far too many for one grid, so the picker is **size-tabbed**: a row of
`10x10 / 15x15` tabs across the top, each carrying its own solved count, over
a 4-column paged grid of that size (page dots below). It opens on the tab and page
that contain the puzzle RESUME/PLAY would open. Chosen from three rendered variants
(a solid grid, a list, and this tabbed grid) and a cold review of them; the tabs
are the only layout that gives direct access to a size instead of blind paging.

The selected / in-progress tile is **fully inverted** (solid black, white content)
-- the fill-is-selected language the mode capsule and the shelf rows already speak,
and the least ambiguous mark 1-bit e-ink has. The earlier corner brackets were
dropped (they clashed with the rounded tiles) and so was a gutter underline (it
read as belonging to the tile below).

## Sizes and storage

Both tiers fit the 480px-wide portrait panel: a 15x15 lands on ~19px cells after
its clue gutters, a 10x10 on ~37px ones. (The code still handles 5x5, and
`pictures.txt` has 22 of them; the tier is simply not shipped.) The bank is
`uint16_t rows[kMaxSize]` plus a name, a size and a provenance index per puzzle.
At 321 puzzles that is about **51KB** of flash: 13KB of `Puzzle` table, 4KB of
`Provenance` table and ~34KB of strings, most of the last being the 321 janko
source URLs -- a URL costs more than the picture it points at. It stays
flash-resident like the dungeon's, with no SD pack. That is the number to weigh
a further import against, and the URL share is where a bigger one would be
trimmed first (a per-collection prefix plus an id would halve it).
`kMaxSize` is computed by the generator from the widest picture (15 today); the
row type is `uint16_t`, so **a picture wider than 16 needs the row type widened**
and is why 20x20 is not shipped.

The bank is emitted **size-sorted**, and that is load-bearing rather than tidy.
The picker recovers its size tabs by run-scanning the bank for changes of size
into `kSizeGroupCount` slots (itself derived by the generator), so each size has
to be one contiguous run. An unsorted bank -- which is what appending an import
produces -- makes every alternation a new group, fills the slots, and leaves
every puzzle after that point unreachable from the tabs, with nothing drawn
wrong and nothing logged. `gen_picross.sort_by_size` constructs the order and
`host-tests/picross` re-proves it over the shipped header.

The slot count used to be a literal `4`, and that was not a comfortable margin:
a local evaluation bank spanning the sizes the source corpus actually offers
(10, 15, 20, 25, 30) produced **`kSizeGroupCount` = 5**. Under the old array the
fifth run had nowhere to go, the `break` under it would have fired, and every
30x30 puzzle in the bank would have been unreachable from the picker -- drawn
nowhere, reported nowhere, simply absent. Deriving the count from the bank is
what makes that impossible rather than unlikely. It is the fork's
`loop-bounds-not-derived-from-the-array` pattern with a measured number against
it instead of a prediction.

Solved-progress is a bitset sized from the bank (`kProgressWords` 32-bit words in
`Progress`), not the single word the original 17 used, and the on-SD save
(`SaveState`, **version 4**) carries the same array. Every reader and writer walks
the words; a host test marks bits either side of each 32-bit boundary so the
widening stays honest.

**Growing the bank changes that save even when no field is touched**, so
`kSaveVersion` is bumped every time it grows. `kProgressWords` is derived from
`kPuzzleCount`, so the struct changes size; and the bank is emitted size-sorted,
so adding 10x10 puzzles renumbers every 15x15 and a solved bit that survived
would name a different puzzle. Old progress cannot be migrated, only discarded --
v3 was the `uint8_t` index, v4 the janko import -- and the version bump is what
discards it deliberately, with a log line, rather than by a short read that says
nothing.
