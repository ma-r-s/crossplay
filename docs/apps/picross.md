# The picross bank

Where the puzzles in Picross came from, why they can be trusted, and the one
rule decision that shapes every screen.

The short version: 17 original nonograms (eight 5x5 warmups, nine 10x10), every
one proved to have exactly one solution and to be reachable by single-line
reasoning, by a generator that refuses to emit any that are not.

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

1. The **satisfied-clue dimming** (a clue number greys once its line is done) is
   honest -- a line's fill count can equal its clue total only when the line is
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

The pictures are **original artwork authored for this fork** and are placed in
the public domain by the author. They live as ASCII grids in
[`assets_local/picross/pictures.txt`](../../assets_local/picross/pictures.txt),
one `#`/`.` grid per name.

There is deliberately no third-party set. An arbitrary two-tone image converted
to clues is usually _not_ a valid nonogram (it has several solutions), and any
collection taken from elsewhere would need its source and licence recorded here
the way this paragraph would -- the Wavelength retail-deck problem. Drawing the
pictures ourselves sidesteps both: the licence is ours, and the generator throws
out any drawing that does not make a fair puzzle.

## Verification

Two implementations of "unique" and "line-solvable", in different languages,
agreeing on all 17 puzzles. This is the app's equivalent of the dungeon bank's
cross-check.

**In Python, at generation time.**
[`tools_local/picross/gen_picross.py`](../../tools_local/picross/gen_picross.py)
derives each puzzle's clues from its picture, then:

- runs a **line-solver** from a blank grid using only single-line deductions. If
  it determines every cell and lands back on the source picture, the puzzle is
  line-solvable AND unique -- a line-solver that fixes every cell has proved
  every cell was forced, which is exactly what one solution means;
- runs an **independent exhaustive count** (a different algorithm: row-by-row
  DFS with column pruning) and requires the count to be exactly 1.

It refuses to write the header unless every puzzle passes both. The clues are
never stored, only the picture, so the clues cannot disagree with it.

```bash
python3 tools_local/picross/gen_picross.py   # a second or two; one line per puzzle
```

Expect designs to be rejected -- SAILBOAT and KEY were, for admitting two
pictures each. That is the tool working.

**In C++, against the header that ships.**
[`host-tests/picross/`](../../host-tests/picross/) carries a second, brute-force
implementation of both properties (a line-solver and a solution counter over
`2^n` patterns, obviously correct for `n <= 10`) and runs them over all 17
stored pictures, plus the mistake/win/clue/restore rules of `PicrossCore`.

```bash
./host-tests/picross/run.sh        # 435 checks
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

## The picker does not spoil

A solved puzzle's tile shows its finished picture; an unsolved one shows only its
number and size. The reveal is the whole reward, so the name and the picture stay
hidden until the puzzle is done -- the name especially, because the name _is_ the
answer, which is also why the board never shows it while you play.

## Sizes and storage

Both sizes fit the 480px-wide portrait panel: a 10x10 lands on ~36px cells after
its four-deep clue gutters, a 5x5 on comfortably larger ones. The whole bank is
`uint16_t rows[10]` plus a name and a size per puzzle -- well under a kilobyte --
so it is flash-resident like the dungeon's, with no SD pack.
