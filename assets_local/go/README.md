# Go: the 3x3 playout pattern table

`PAT3_BITS`, 961 bytes, vendored from **ArduGO** by Jay Chan (MIT, see
`LICENSE-ardugo`). Generated into
[`src/apps_local/go/GoPatterns.h`](../../src/apps_local/go/GoPatterns.h) by
[`tools_local/go/gen_go_patterns.py`](../../tools_local/go/gen_go_patterns.py),
which is committed so a checkout builds without the upstream file.

## What it is

The MoGo 3x3 playout patterns (Gelly, Wang, Munos and Teytaud, 2006), as
transcribed in Petr Baudis's `michi.py` (MIT) and compiled offline into truth
tables. A bit per local configuration: set means "this shape is worth playing".
It carries every rotation, reflection and colour swap, so there is no symmetry
code to write and no chance of getting one of the eight wrong.

This is the single cheapest strength in the whole app. The measurement behind
that, from `docs/apps/go.md`: the playout policy is worth about **+512 Elo** on
its own, which is four ranks, and a policy-guided engine at 500 playouts beats a
knowledge-free one at 10,000. The alternative is not "slightly weaker", it is an
engine whose groups die in bulk without it noticing -- which on a panel somebody
is watching reads as a crash rather than as a loss.

## Why THIS table and not michi's own

Two encodings of the same patterns:

| | |
| --- | --- |
| michi / michi-c: two bits a neighbour (empty / mine / theirs / **off-board**), eight of them, a 16-bit index | **8,192 bytes** |
| ArduGO: off-board is a property of WHERE YOU ARE, not of the neighbour. Nine position classes, base-3 over the on-board neighbours only | **961 bytes** |

The naive encoding spends 8.5x more because most of its 65,536 configurations
are geometrically impossible. On a fork with about fifty kilobytes of flash to
spare that difference is the whole decision.

**The megabyte pattern files are a different thing and are not wanted.**
`patterns.prob` and `patterns.spat` are michi's harvested large-scale patterns,
they are a TREE PRIOR rather than a playout policy, they derive from a
commercial game database, and their download URL is dead with no archive. michi-c2
reaches GNU Go 3.8 parity without them.

## Regenerating

```bash
python3 tools_local/go/gen_go_patterns.py <path-to-ardu_go/pattern_table.h>
```

The generator asserts the offsets and the byte count rather than trusting the
input, because a table that is silently short would not fail to build: it would
make the opponent quietly worse in one corner of the board.
