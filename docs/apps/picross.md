# The picross bank

Where the puzzles in Picross came from, why they can be trusted, and the one
rule decision that shapes every screen.

The short version: 137 nonograms, all of them 10x10, every one proved to have
exactly one solution and to be reachable by single-line reasoning, by a
generator that refuses to emit any that are not.

**The game is 10x10 and nothing else.** It shipped with a 15x15 tier too; Mario
played one on the panel and the call is his: *"it's just not gonna work"*. A
15x15 lands on 19px cells with a 177px row-clue gutter, and on glass that is a
grid you can read but not reliably tap. The 184 puzzles at that size are still
in `janko.txt` -- a generator's input is reproducible work, and re-deriving them
later must not mean re-crawling janko.at -- but `gen_picross.SHIPPED_SIZES` is
`(10,)` and only what is in it reaches the device.

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
  and it is a **mistake**: it locks in permanently, shows as an **asterisk**,
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
| `assets_local/picross/janko.txt` | 321 read, 137 shipped | janko.at, six named designers | used by permission | yes, the 10x10s |
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

### The attribution is not in the firmware, and a generated table is why that is safe

It used to be a field: every puzzle carried an index into a `kProvenances[]`
table of `{author, license, source}` triples, emitted beside the bitmaps. The
source URLs alone were ~34KB of an ~51KB bank -- a URL costing more flash than
the puzzle it pointed at, on a device where flash is the scarce thing and where
no player has ever read one. Mario's call was to take it out of the image
entirely (*"as long as it doesn't reach firmware anywhere and uses space there
I'm good"*), and the one string a puzzle carries now is its **name**.

**The credit obligation did not change**, and `PROVENANCE.md` is now the only
place it is met. So it carries the whole per-puzzle mapping -- puzzle, designer,
licence, source URL -- rather than a summary naming the six, and that table is
**generated by `gen_picross.py` in the same pass that writes the bank**. A
document is the wrong place for an answer maintained by hand; it is a fine place
for one that is generated and then checked.

`host-tests/picrossprov` is the check. It re-derives the mapping independently,
from `janko.txt` and from the **bitmaps actually emitted into the header**, and
fails if the table is not exactly that. Matching is by bitmap rather than by
name, which is the point of the design: the string a puzzle carries is Mario's
name for the picture and says nothing about where it came from. The bitmap is
the puzzle. The same suite greps the header for every designer's name and for
`janko.at`, so a later session helpfully re-adding the attribution to flash
fails a test rather than shipping.

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
at 10x10 (52%) and 184 of 268 at 15x15 (69%)**; the 10x10 half is what ships.
Both the keeps and the drops are
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

**137 puzzles at 10x10 is the whole game now.** The corpus is more legible at
15x15 (69% against 52%) because a bigger grid draws a better picture -- that
tension is real and it is why the tier existed -- but a grid you cannot reliably
tap is not a game, and 137 is a bank nobody will exhaust soon.

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

**Only 10x10 and 15x15 are imported (and only the 10x10s ship), and that is a
layout decision.** The corpus
also holds 393 at 20x20, 372 at 25x25 and 435 at 30x30, all excluded: at the
densest 20x20 the row-clue gutter takes 217px of the 480px panel against a 240px
grid, cells fall to 12px, and the satisfied-clue strikethroughs read as vertical
smears through the row-clue digits. 15x15 at its worst is clean (19px cells, a
177px gutter). Making the larger sizes playable is a clue-gutter redesign, not
an import setting.

## Verification

Two implementations of "unique" and "line-solvable", in different languages,
agreeing on all 137 puzzles. This is the app's equivalent of the dungeon bank's
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
./host-tests/picross/run.sh        # ~10k checks over 137 puzzles
./host-tests/picrossprov/run.sh    # the credit table against the shipped bank
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

## The two marks on a cell, and why they are different glyphs

A cell can carry two marks, and until Mario played it on hardware they were told
apart by WEIGHT rather than by shape:

- **X** -- the PLAYER's own mark: "I have reasoned that this cell is empty".
  Free, reversible, asserts nothing, and unchanged by any of this.
- **asterisk** -- the GAME's record: "you filled here and you were wrong".
  Locked and counted.

The mistake used to be a white X knocked out of a **solid black cell**, chosen
so it could never be confused with the free note. It could not -- but a solid
black cell is what the PICTURE is made of, so every mistake added a black square
to the image the player is trying to read, and a board with a dozen of them
showed a picture that was not the puzzle's. Mario, having played it: *"The x is
not heavy. What's heavy is the x with black background when a mistake is made."*
And: *"how about an asterisk instead of a cross which means 'user filled here
but he was wrong'"*.

So the fill is gone and the mark is an asterisk on plain paper. It occupies
**exactly the X's box** -- same reach, same 2px weight -- so the two differ by
glyph and by nothing else. Six arms against four, and the vertical arm is the
tell: the X has none, so even when both blur into rosettes the asterisk is the
one standing upright.

The arm ANGLES had to be looked at rather than reasoned about. The first version
put the diagonals at 63 degrees from horizontal, which bunches all three strokes
near the vertical; rendered at a 37px cell it read as a dense double-dagger, not
as an asterisk. Six even arms means 60 degrees apart, so the diagonals sit at 30
degrees from horizontal (dx:dy of 7:4). A 1px weight was rendered too and
rejected -- e-ink swallows it.

`host-tests/ui` asserts both halves: no black fill under a mistake cell, and
three strokes with exactly one vertical against the X's two with none. It could
not assert either until `FakeTarget` started recording `line()`, which it used
to drop on the floor -- a mark made of lines was invisible to every test.

## Lines auto-mark themselves once they are satisfied

When a fill completes a line -- every solid cell in that row or column filled --
every remaining blank cell in it is crossed. Mario asked for it; it is
`Board::autoMark`.

It is safe against the invariant above because it only ever writes `Crossed`. A
mark asserts nothing, so auto-marking cannot manufacture a mistake, cannot move
the count, and cannot make `solved()` true.

**An auto-placed mark is an ordinary mark.** Same `Cell::Crossed`, no flag saying
who put it there, so the player rubs it out exactly like one of their own.
Telling them apart would need a fifth cell state, a save format that carries it,
and a rule for what happens when the line stops being satisfied -- for an
annotation that is never punished either way.

It sweeps EVERY satisfied line, not just the two through the filled cell. Those
two are the only ones a fill can change, but a line whose clue is "0" is
satisfied from the start and is never the line a fill lands in, so the local
version would leave exactly the emptiest rows uncrossed -- which reads as the
feature failing rather than as a rule. It runs only from a fill that landed,
never from `load()` or `restore()`: a board that arrived pre-crossed would report
`touched()`, and a fresh puzzle would offer to RESUME itself.

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
number. The reveal is the whole reward, so the name and the picture stay hidden
until the puzzle is done -- the name especially, because the name _is_ the
answer, which is also why the board never shows it while you play.

**The name has exactly one site on screen**: the win screen (`buildWin`), under
the revealed picture and above the grade. Nothing else in the app draws it --
not the picker, not the board, not the status strip.

**The names are Mario's, written by hand**, into
`assets_local/picross/janko-names.json`, keyed by the puzzle's name in
`janko.txt` (`"JANKO222"`) or by the bare janko id that name ends in (`"222"`).
Both forms are accepted; a **bank index is never a key**, because the bank is
emitted size-sorted and renumbers whenever it changes. `gen_picross.load_names`
refuses the file outright on a key that matches no puzzle, a character the cut
has no glyph for, or a name that does not render at full size -- every one a
hard error, because a half-applied annotation pass looks exactly like a full
one.

**A name is accepted exactly when it renders at full size, and that is
MEASURED.** `tools_local/picross/name_fit.py` is the one place that answers it:
it reads the real Jersey metrics out of `src/apps_local/ui/fonts/` and restates
`EpdFont::getTextBounds`, against a band width measured from the real screen
builder into `tools_local/picross/name_band.txt` (448px) rather than copied by
hand.

It is a restatement and not an approximation, and the difference points the
unsafe way. The device reports the width of the **ink box** (`maxX - minX` in
`getTextDimensions`), which is not the sum of the advances: each advance is
rounded to a whole pixel *as it is accumulated* (`fp4::toPixel`, which is
`(fp + 8) >> 4`), so the fractions never cancel, and the box ends at the last
glyph's **right edge** rather than after its advance. Summing float advances put
sixteen capital As at 488px against the device's 493 -- under-reporting, which
is exactly the direction that says "fits" for a name the panel sets a cut down.
The 493 was measured on hardware by the session that built the naming tool; the
algorithm here was then checked against `lib/EpdFont/EpdFont.cpp` directly.

**This replaced a nine-character cap**, and the cap was the wrong instrument in
both directions at once. `CHRISTMAS TREE` is fourteen characters and measures
410px, comfortably inside the band; ten capital Ws measure 437px and also fit,
while eleven (481px) do not. Any fixed count is either too tight for good names
or too loose for wide ones. Roughly: eleven to fourteen ordinary letters, ten of
the widest, thirty-two `I`s.

`toybox::fittedTitle` is why this matters at all. It does not clip an overlong
name, it **shrinks** it, walking down the font slots until one fits, and nothing
logs that. The failure being guarded against is not a broken screen; it is a
reveal set two-thirds size with nobody told.

**Two implementations, pinned rather than trusted.** The naming tool
(`site/picross-names/logic.js`) cannot import Python, so it restates the same
measurement in JavaScript for live feedback while Mario types. A second copy of
a rule that must agree to the pixel is the drift this fork keeps paying for, so
`tools_local/picross/name_fit_corpus.json` is the pin: `name_fit.py --corpus`
writes it, `host-tests/picrossprov` fails if those numbers are not what
`measure()` computes today, and `host-tests/picrossnames` drives the JavaScript
against the same file. The two were checked against each other over 84
measurements in three cuts and agreed on every one.

He writes the names in the tool at `site/picross-names/`, which emits that exact
file; [naming the picross puzzles](picross-names.md) is how it works and where
his answers live while he is part-way through.


**A puzzle with no name draws no name band at all**, and the picture takes the
space. Not an empty band: the names arrive by hand, so a part-named bank is the
normal state, and a blank 52px gap over the picture reads as a name that failed
to render.

## The picker: one flat run of pages

A 4-column paged grid of rounded tiles, page dots below, opening on the page
holding the puzzle RESUME/PLAY would start. Chosen from three rendered variants
(a solid grid, a list, and a size-tabbed grid) and a cold review of them.

**The size tabs are gone, and their absence is the design.** They answered
"puzzles across several sizes" with direct access instead of blind paging; with
one size they were a row of one tab -- a control with nothing to choose between,
which is not a control -- spending 60px of the grid's height and a hit rect to
say "10x10" a second time. Losing them let the grid grow from four rows to five,
so a page holds 20 tiles instead of 16 and the bank pages seven ways instead of
nine. There is a `static_assert` on `kSizeGroupCount == 1` in
`PicrossScreens.cpp`: bring a second size back and the build stops there rather
than shipping a picker that silently runs two tiers together.

**Tiles per page is derived, not written down.** `gridGeom()` computes the cell
from the body width and the rows from the body height, and `buildMenu` reports
what it drew in `PickerLayout::pageOnScreen`. The activity does not compute a
page at all: it sets `MenuModel::followSelection` and reads back what the picker
chose. It used to divide by a literal 16 -- the number that fitted under the tab
band -- and that literal would have been wrong the moment the band went, opening
the picker on the wrong page with nothing reporting it.

**Both side keys page it.** Physically they are the moulded page-turn keys, and
they are the only buttons the X4 Pro has; before this the dots were the only way
through 137 puzzles, which on a page-turn key reads as broken. `stepPage()` is a
free function for one reason: the simulator never runs `InputManager`, so
nothing about the press is provable off-device, but the decision it feeds is.
It clamps rather than wraps -- the reader these keys were made for stops at the
ends of a book. On the board the same two keys still select FILL and MARK; a key
is read against the view on screen, so neither can starve the other.

The selected / in-progress tile is **fully inverted** (solid black, white content)
-- the fill-is-selected language the mode capsule and the shelf rows already speak,
and the least ambiguous mark 1-bit e-ink has. The earlier corner brackets were
dropped (they clashed with the rounded tiles) and so was a gutter underline (it
read as belonging to the tile below).

## Sizes and storage

A 10x10 lands on ~37px cells after its clue gutters on the 480px-wide portrait
panel. (The code still handles other sizes -- nothing is hardcoded to ten -- but
`SHIPPED_SIZES` is `(10,)`.) The bank is `uint16_t rows[kMaxSize]` plus a name
pointer and a size per puzzle: 28 bytes a row at `kMaxSize` 10, so **about 3.8KB
of `Puzzle` table** plus the name strings, which are one shared empty literal
until Mario has named something.

It was ~51KB. Two changes took it there and the second is the big one: dropping
the 15x15s halved the count and cut `kMaxSize` from 15 to 10, and moving the
attribution out of the firmware dropped a 4KB `Provenance` table and ~34KB of
strings, most of that the 321 source URLs. It stays flash-resident like the
dungeon's, with no SD pack.

`kMaxSize` is computed by the generator from the widest picture SHIPPED (10
today, not 15 -- it follows the filter); the row type is `uint16_t`, so **a
picture wider than 16 needs the row type widened**, which is one of the reasons
20x20 is not shipped.

The bank is emitted **size-sorted**, and that stays load-bearing even with one
size: `kSizeGroupCount` is derived by run-scanning the bank for changes of size,
and it is what the picker's `static_assert` reads to decide that no size tabs are
needed. An unsorted bank -- which is what appending an import
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
(`SaveState`, **version 5**) carries the same array. Every reader and writer walks
the words; a host test marks bits either side of each 32-bit boundary so the
widening stays honest.

**Growing the bank changes that save even when no field is touched**, so
`kSaveVersion` is bumped every time it grows. `kProgressWords` is derived from
`kPuzzleCount`, so the struct changes size; and the bank is emitted size-sorted,
so adding 10x10 puzzles renumbers every 15x15 and a solved bit that survived
would name a different puzzle. Old progress cannot be migrated, only discarded --
v3 was the `uint8_t` index, v4 the janko import, **v5 dropping the 15x15s** --
and the version bump is what discards it deliberately, with a log line, rather
than by a short read that says nothing. v5 is the clearest case of the trap: not
one field was edited, and yet `cells` lost 125 bytes (`kMaxSize` 15 to 10),
`solved` lost six words (321 puzzles to 137), and every stored index named a
puzzle chosen from a different, longer list. A v4 save read as a v5 is garbage
that parses.
