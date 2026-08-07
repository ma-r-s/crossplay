# The dungeon bank

Where the puzzles in D&Diagrams came from, and why they can be trusted.

The short version: 65 puzzles, transcribed by other people, cross-checked
against a second independent transcription, and every one solved exhaustively to
prove it has exactly one answer.

## What the game is

A nonogram whose clues are a dungeon. Each row and column carries the number of
walls in it, and five rules decide the rest:

1. The number beside a row or column is how many walls it holds.
2. Every dead end holds a monster, and every monster is in a dead end.
3. Every chest sits in a 3x3 room of floor with exactly one way in.
4. Corridors are one cell wide: no 2x2 of floor outside a treasure room.
5. All floor is connected.

It is one of the games inside Zachtronics' _Last Call BBS_ (2022). The rules are
a puzzle genre; what is reproduced here is the set of 64 hand-made dungeons plus
the tutorial, which is content rather than mechanism. Treat it the way the fork
treats the Connections puzzles: fine for a device in Mario's pocket, not
something to publish as a product.

## Provenance

The layouts are not in any machine-readable form the publisher ships, so they
come from players who transcribed them.

| Source                                                                                          | What it holds                                             |
| ----------------------------------------------------------------------------------------------- | --------------------------------------------------------- |
| [VoidedHeadPort/dungeons-and-diagrams](https://github.com/VoidedHeadPort/dungeons-and-diagrams) | All 64 campaign dungeons + the tutorial, as Prolog facts  |
| [MischaU8/dungeons_diagrams](https://github.com/MischaU8/dungeons_diagrams)                     | 21 of the campaign dungeons, plus a separate 9-puzzle set |

The first is the bank. The second was used to check it.

### The cross-check, and the false alarm

21 dungeons appear in both sets. All 21 are **byte-identical** -- same clue
vectors, same monster and chest positions.

A third transcription
([Z903/Dungeon-Solve](https://github.com/Z903/Dungeon-Solve)) appears at first to
contradict this: it lists a "Tenaxxus's Gullet" with completely different clues,
and that puzzle is also perfectly valid and uniquely solvable. That looked like
evidence the game generates a different dungeon per save, which would have meant
porting a generator instead of a bank.

It is not. There are **two** sets of dungeons using the same names: the 64 in the
game, and a separate nine that shipped outside it. MischaU8's repository holds
both, under `NN_` and `pNN_` prefixes, and its `p01_tenaxxus_gullet` matches the
Z903 set exactly while its `62_tenaxxuss_gullet` matches ours. Same name,
different puzzle, no generator. Only the campaign set is shipped here.

## Verification

Two implementations of the rules, in different languages, agreeing on all 65
puzzles.

**In Python, at generation time.** `tools_local/gen_dungeons.py` reads
`assets_local/dungeons/dungeons.txt`, solves every puzzle by exhaustive search,
and refuses to write the header unless each has **exactly one** solution. The
answer in `DungeonPuzzles.h` is therefore derived, never transcribed, so the
clues and the answer cannot disagree.

That matters more than it sounds. A puzzle with two solutions is unplayable in a
way no test of the app could catch: the player finds a legal arrangement of
walls, the game says no, and nothing anywhere is wrong except the data.

```bash
python3 tools_local/gen_dungeons.py     # about a minute; prints one line per dungeon
```

**In C++, against the header that ships.** `host-tests/dungeon/` carries a second,
longhand implementation of the five rules and runs it over all 65 stored
solutions. It is this app's equivalent of perft: an exhaustive property that a
whole class of data bugs cannot survive. A hand-edit to the generated file, or a
bad merge, fails there rather than on the device.

```bash
./host-tests/dungeon/run.sh             # 8909 checks
```

The suite has been mutation-checked: flipping one bit of one stored solution
turns it red in seven places.

## Why the device does not know the rules

`DungeonCore` has no rule checker. A finished board is compared against the
stored solution -- one 64-bit comparison -- which is exactly equivalent because
the solution is unique, and that uniqueness is what the pipeline above proves.

Implementing the rules a second time on the device would mean two
implementations that have to agree forever, in exchange for nothing the player
can see.

## The names

Three names in the source transcription are misspelled ("corination"). The
generator's source file corrects only that one, because it is unambiguous;
"dais" versus "dias" and the various minotaur mazes are left as transcribed. The
longest name is 33 characters, which is why the board draws it in a strip of its
own rather than in the header band -- see the note in `DungeonScreens.cpp`.
