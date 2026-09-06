# The picross bank

Where the puzzles in Picross came from, why they can be trusted, and the one
rule decision that shapes every screen.

The short version: 199 nonograms across four sizes -- 33 at 5x5, 28 at 8x8, 29
at 9x9 and 109 at 10x10 -- every one proved to have exactly one solution and to
be reachable by single-line reasoning, by a generator that refuses to emit any
that are not. Every one arrives with a title, and that title is the name the win
screen reveals.

**15x15 is the size that is not here.** Mario played one on the panel and the
call is his: *"it's just not gonna work"*. A 15x15 lands on 19px cells with a
177px row-clue gutter, and on glass that is a grid you can read but not reliably
tap. `gen_picross.SIZES` still understands it -- the gate can prove one -- but
`SHIPPED_SIZES` is `(5, 8, 9, 10)` and only what is in that tuple reaches the
device.

**The puzzles are not public domain and they are not this fork's own work.**
CrossPlay's code is MIT and a fork inherits that; these pictures came to the
project privately and are not ours to relicense, republish, or offer as a
downloadable pack. That is the whole of what is known and the whole of what is
claimed: there is nobody to credit and no licence to honour, so no author, no
licence and no source URL appears in the bank, in the firmware or in this
repository. **Do not add one.** The fork's own 68 hand-drawn pictures are still
in `assets_local/picross/pictures.txt` and are deliberately not in the bank --
Mario's call, that the hand-drawn artwork "is not and won't be close to good
enough" beside designed puzzles. Nothing loads that file; it is kept as the
worked example of the source format, and adding it to `gen_picross.SOURCES` is
one line.

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

## Where the bank comes from

One file, `assets_local/picross/nonograms.txt`, written by
`tools_local/picross/import_picross.py` from the pack Mario was given and
committed as the generator's input. A generator's input is reproducible work, so
it lives in the tree rather than in whichever directory the importing session
happened to read.

    python3 tools_local/picross/import_picross.py \
        --corpus <pack>/nonograms_all.json --format nonogram-pack \
        --sizes 5,8,9,10 \
        --license unknown \
        --rename assets_local/picross/title-overrides.json \
        --unattributed '<why there is nobody to credit>' \
        --out assets_local/picross/nonograms.txt

**The pack is read for its SOLUTIONS ONLY.** It also carries `row_clues`,
`col_clues` and a `line_solvable` flag, and not one of the three is read. The
clues are derived from the picture by the generator, so they cannot disagree
with it; line-solvability is proved by our own gate rather than believed from a
field. A flag saying a puzzle is fine is not the same fact as this repository
having checked, and telling those two apart is the entire point of the gate. All
199 pass it, at every size, in both implementations.

### The importer will not write an unprovenanced corpus into this repository

`import_picross.py` refuses to write inside the tree unless the licence it was
given is one of a short redistributable list, OR `--permission` cites a record
inside the tree stating who granted it, that it is not a public licence, that it
does not extend to forks, and when. An imported puzzle whose licence is unstated
is all rights reserved, and a file in `assets_local/` is in every clone and every
release. The refusal is a mechanism rather than a line in a checklist.

`--unattributed` is the third state and it is the one this bank uses. It is for
a corpus with genuinely nobody to credit and no licence to honour, and it
**claims nothing**: it requires `--license unknown`, refuses `--permission`,
`--author-map` and `--source-template` outright as contradictions, writes every
author and licence as `unknown`, writes no source URL, and stamps a banner
carrying the reason it was used. It is not a way past `--permission` for a
corpus that has an author somebody could have asked.

**There is no `PROVENANCE.md` and there is no `host-tests/picrossprov`, and
their absence is deliberate.** Both existed for the previous bank, which was six
named designers' copyrighted work used by permission: the credit had to be
visible and had to be kept mechanically honest against the shipped bitmaps. Those
puzzles are gone. This bank carries no attribution obligation at all -- nobody to
credit, no licence to honour, no mapping that can drift -- and a suite guarding a
promise nobody is making is dead weight. An *emptied* provenance file would have
been worse than none, because the next reader takes it for an oversight and
tries to fill it in.

### Titles, and the one Mario changed

Every puzzle arrives titled and the title is its identity: it keys the override
files, and it is the string the win screen reveals. They are unique across the
bank.

A title that does not FIT the panel is renamed in
`assets_local/picross/title-overrides.json`, which the **importer** applies, so a
re-import reproduces the same bank instead of quietly restoring the original. It
holds exactly one entry. `Widescreen Monitor` measures 473px against the win
screen's 448px band, and `toybox::fittedTitle` would have set it a rung smaller
with nothing reporting that it had. Mario's call, verbatim: **`Monitor`**. A key
that matches no puzzle is a hard error -- a rename that matches nothing is a
rename that silently did not happen.

`assets_local/picross/name-overrides.json` is the other layer and it is empty. It
replaces a title with a different word for the *reveal*, and the **generator**
applies it. Two files rather than one because they fix different problems at
different moments, and a width fix applied after the import would be undone by
the next one.

### Importing a corpus

`tools_local/picross/import_picross.py` converts a third-party corpus into the
`pictures.txt` format, running each candidate through the SAME `evaluate()` the
hand-drawn pictures face (it imports it from `gen_picross`, rather than keeping a
second copy to drift). It exists because the pictures people enjoy solving are
_designed_, and a corpus somebody drew and somebody else played is the only place
to find a lot of them at once.

A corpus that TITLES its puzzles is titled verbatim, case and all. One that does
not gets `<prefix><id>`, upper-cased -- a catalogue id has no case to preserve.
The two are separate paths rather than one path with an `.upper()` on the end,
because `.upper()` over a real title destroys a decision somebody made. It used
to sit on that line, and it would have shipped 199 shouted titles.

The `pictures.txt` format carries provenance itself: `@@author` / `@@license` /
`@@source` set a file-level default from that point down, and a single-`@` line
above a name overrides it for that one picture. `parse()` starts each file with
fresh file-level defaults, so if the bank ever mixes origins again one file's
declaration cannot leak onto the other's puzzles.

### The gate cannot see the picture

Unique, line-solvable and fills-its-grid are all properties of **the clues**. A
puzzle can satisfy every one of them and still solve into a scatter of blobs
nobody can name, and no filter anywhere can tell the difference -- the finished
picture is simply not in the data the gate looks at. This bank arrives already
curated and already titled, which is the same judgement made by somebody else
before it got here; a future bulk import still needs a human to look.

An arbitrary two-tone image converted to clues is usually _not_ a valid nonogram
(it has several solutions). The generator answers that; nothing answers "is this
a good picture" except somebody looking at it.

To author more of our own: write candidates into `pictures.txt` and triage them
with `gen_picross.py --curate` (PASS/FAIL per picture, no emit).

## Verification

Two implementations of "unique" and "line-solvable", in different languages,
agreeing on all 199 puzzles. This is the app's equivalent of the dungeon bank's
cross-check, and it is the reason the pack's own `line_solvable` flag is ignored.

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
  10x10 whose ink only spans six rows is a 10x6 lying about its tier. Interior
  empty lines stay legal (a picture may genuinely have a gap), which is the only
  way a "0" clue should ever appear;
- **measures every shipped name** against the win screen's band. Not just an
  overridden one: the titles arrive with the corpus, so the widest one arrives
  with it too, and this pack carried exactly one that was too wide. See below.

It refuses to write the header unless every puzzle passes. The clues are never
stored, only the picture, so the clues cannot disagree with it.

```bash
python3 tools_local/picross/gen_picross.py   # a second or two; one line per puzzle
```

Expect designs to be rejected, on any of the grounds above. About one authored
picture in five is not line-solvable, and a key and a crescent moon could not be
drawn to touch all four edges at all. That is the tool working -- a rejected
drawing is cheaper than an unfair puzzle or a lying label.

**In C++, against the header that ships.**
[`host-tests/picross/`](../../host-tests/picross/) carries a second, brute-force
implementation of both properties (a line-solver and a solution counter over
`2^n` patterns, obviously correct for `n <= 15`) and runs them over every stored
picture, plus the mistake/win/clue/restore rules of `PicrossCore`. It also proves
the bank is size-sorted and that the recovered size runs **account for every
puzzle in the bank** -- see the picker, below, for why that subtraction matters.

```bash
./host-tests/picross/run.sh        # ~14.6k checks over 199 puzzles
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

**The name is the puzzle's TITLE**, carried straight out of
`assets_local/picross/nonograms.txt` into the bank. The previous bank had no
titles at all, so the names were typed by hand into a file keyed by catalogue id;
this one arrives named and there is nothing to annotate.
`assets_local/picross/name-overrides.json` replaces a title for the reveal and is
empty. A key that matches no puzzle is a hard error, because a half-applied
annotation pass looks exactly like a full one, and a bank index is never a key --
the bank is emitted size-sorted and renumbers whenever it changes.

**The titles are Title Case** (`Bowling Ball`), where the previous bank's names
were uppercase. Rendered and looked at rather than argued about: the display cut
draws the lowercase cleanly at full size, descenders included, and the mixed case
gives the screen a hierarchy it did not have -- the name is the reveal and the
grade under it (`PERFECT -- NO MISTAKES`) is the footnote. When both were
uppercase they competed. The name is the only mixed-case string in the app, and
that is right: it is the only piece of authored content on the screen.

**A name is accepted exactly when it renders at full size, and that is
MEASURED.** `tools_local/picross/name_fit.py` is the one place that answers it:
it reads the real Jersey metrics out of `src/apps_local/ui/fonts/` and restates
`EpdFont::getTextBounds`, against a band width measured from the real screen
builder into `tools_local/picross/name_band.txt` (448px) rather than copied by
hand.

**EVERY SHIPPED NAME IS MEASURED, not just an overridden one**, and that changed
with this bank. While the names were typed by hand, checking the file Mario wrote
checked everything that could be wrong. Now the titles arrive with the corpus,
which means the widest one arrives with it too: this pack carried exactly one
name over the band, `Widescreen Monitor` at 473px, and nothing about it looked
wrong anywhere. `gen_picross.emit` measures all 199 on every regenerate and fails
by name, so the next corpus cannot introduce one silently.

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
measurement in JavaScript. A second copy of a rule that must agree to the pixel
is the drift this fork keeps paying for, so
`tools_local/picross/name_fit_corpus.json` is the pin: `name_fit.py --corpus`
writes it, `host-tests/picrossnames` checks those numbers are still what
`measure()` computes today and drives the JavaScript against the same file. The
two were checked against each other over 84 measurements in three cuts and agreed
on every one.

**The naming tool at `site/picross-names/` has nothing left to do**, and it is
still here. It existed because the previous bank was unnamed; this one is not.
`gen_name_tool.py` detects that state from the bank rather than being told --
`--check` passes and reports "superseded" while every shipped puzzle carries a
name, and the staleness check it used to run comes back the moment a bank ships
an unnamed one. `data.js` is deliberately NOT regenerated: it still describes the
previous bank, and it is left untouched so anything Mario typed into the page
still loads. **The page, `gen_name_tool.py` and `docs/apps/picross-names.md` are
proposed for removal together** -- his call.

**A puzzle with no name draws no name band at all**, and the picture takes the
space. Every puzzle in this bank has one, so the case does not arise today; it
stays because an empty 52px gap over the picture reads as a name that failed to
render, and that is the wrong thing to draw for a picture that simply has no
title.

## The picker: four size tabs over a paged grid

A row of size tabs above a 4-column paged grid of rounded tiles, page dots below,
and a PLAY/RESUME button at the foot. Chosen from three rendered variants (a
solid grid, a list, and this tabbed grid) and a cold review of them.

**The tabs answer "puzzles across four sizes" with direct access instead of blind
paging**, and each carries its own solved count, so the row doubles as "which
tier still has puzzles left". They were removed while the game was 10x10-only,
where they were a row of one tab -- a control with nothing to choose between,
which is not a control. Four is a control.

**The label is `10x10` and not `10 x 10`, and that is a measurement.** At four
tabs the band gives each pill 106px, and `10 x 10` sets 103px of it in the body
cut: one and a half pixels of air inside a 20px corner radius. It was the spaced
form while there were three tabs and it does not survive a fourth. Closing the
spaces takes it to 83px. Measured with `name_fit.py` against the real cut, not
judged from a render, and `host-tests/ui` fails if the spaced form comes back.

**The size appears on the tabs and nowhere else.** Not on every tile, not in the
board's status strip: you chose the tier on the way in, and repeating it over the
puzzle is noise about a fact you cannot change from there. Mario's call, and it
survives the tabs coming back.

**Tiles per page is derived, not written down.** `gridGeom()` computes the cell
from the body width and the rows from the body height, and `buildMenu` reports
what it drew in `PickerLayout::pageOnScreen` and `tabOnScreen`. The activity
computes neither: it sets `MenuModel::followSelection` and reads back what the
picker chose. It used to divide by a literal 16 -- the number that fitted under
the tab band -- and that literal went silently wrong the moment the band moved,
opening the picker on the wrong page with nothing reporting it. With tabs back,
`followSelection` resolves the **tab as well as the page**, so the activity does
not need to know where one size run ends either.

**Both side keys page it.** Physically they are the moulded page-turn keys and
the only buttons the X4 Pro has. `stepPage()` is a free function for one reason:
the simulator never runs `InputManager`, so nothing about the press is provable
off-device, but the decision it feeds is. It clamps rather than wraps -- the
reader these keys were made for stops at the ends of a book. On the board the
same two keys select FILL and MARK; a key is read against the view on screen.

The selected / in-progress tile is **fully inverted** (solid black, white
content) -- the fill-is-selected language the mode capsule and the shelf rows
already speak, and the least ambiguous mark 1-bit e-ink has.

### Four is exactly the boundary the old bug sat on

`kSizeGroupCount` is **derived by the generator** by run-scanning the bank it
just wrote, and the picker's group array is sized from it. That is not tidiness.
The slots were once a literal `4` with a `break` underneath, and that pairing is
a silent data-loss bug rather than a bound: a bank producing more runs than slots
hits the break, and every puzzle after it is unreachable from the tabs -- drawn
nowhere, logged nowhere, with nothing on the screen looking wrong. A local
evaluation bank spanning 10, 15, 20, 25 and 30 produced **five** groups and would
have lost every 30x30. **This bank has four.**

Three mechanisms, because the failure is invisible:

- **A compile-time count.** `countSizeRuns()` in `PicrossScreens.cpp` walks the
  `constexpr` bank and a `static_assert` requires the number of RUNS to equal the
  number of distinct SIZES. Those are equal if and only if the bank is
  size-sorted, so an appended import -- which is exactly what produces an
  unsorted bank -- stops the build instead of losing puzzles.
- **`host-tests/picross`** re-derives the runs and requires them to **account for
  every puzzle in the bank**. That subtraction is the only thing that catches a
  break that fired: with a three-slot array it reports "the tabs reach 62 of 199
  puzzles -- 137 are unreachable".
- **`host-tests/ui`** sweeps the rendered frame for a live `ActionTab` rect per
  group, opens each tab in turn and adds up the tiles its pages actually lay out,
  and requires the total to be the whole bank. A tab that draws but answers
  nothing fails the first half; a tier the pages cannot reach fails the second.
  It also checks the width each tab was given against `kTabMinWidth`, which is a
  runtime number and therefore cannot be a `static_assert`.

## Sizes and storage

A 10x10 lands on ~37px cells after its clue gutters on the 480px-wide portrait
panel; a 5x5 is capped so it stays a board rather than five enormous squares.
Nothing is hardcoded to a size: the grid, the clue gutters and the tile
thumbnails are all computed from `Puzzle::size`, which is why adding 8 and 9 was
a data change and not a layout change.

The bank is `uint16_t rows[kMaxSize]` plus a name pointer and a size per puzzle,
so **about 5.6KB of `Puzzle` table** at 199 puzzles plus the name strings. It
stays flash-resident like the dungeon's, with no SD pack.

`kMaxSize` is computed by the generator from the widest picture SHIPPED (10). The
row type is `uint16_t`, so **a picture wider than 16 needs the row type
widened**.

The bank is emitted **size-sorted**, and that is load-bearing: `kSizeGroupCount`
is derived by run-scanning for changes of size, and an unsorted bank makes every
alternation a new run. `gen_picross.sort_by_size` constructs the order and three
separate mechanisms re-prove it (above).

Solved-progress is a bitset sized from the bank (`kProgressWords` 32-bit words in
`Progress`), and the on-SD save (`SaveState`, **version 6**) carries the same
array. Every reader and writer walks the words; a host test marks bits either
side of each 32-bit boundary so the widening stays honest.

**Changing the bank changes that save even when no field is touched**, so
`kSaveVersion` is bumped every time it does. `kProgressWords` is derived from
`kPuzzleCount`, so the struct changes size; and the bank is emitted size-sorted,
so every stored index names a puzzle chosen from a different list. Old progress
cannot be migrated, only discarded -- v3 was the `uint8_t` index, v4 the janko
import, v5 dropping the 15x15s, **v6 replacing the bank wholesale**. v6 is the
same trap as v5: not one field was edited, and yet `solved` grew from five words
to seven (137 puzzles to 199) and every stored index names a picture from an
entirely different set, so a v5 save read as a v6 would restore one player's
marks onto a picture they have never seen. The version bump is what discards it
deliberately, with a log line, rather than by a short read that says nothing.
