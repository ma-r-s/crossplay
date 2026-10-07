# Wappo

A chase puzzle on a six by six board: walk Wappo to the gap in the edge before
the monsters catch him. It follows Wappo, a 2003 game for Siemens phones, in
its rules only. The code, the art and all 125 levels are this fork's.

## The rules

Every step Wappo takes, each monster takes two. A monster always tries to close
the column gap first, sideways towards Wappo; only when a wall blocks that does
it move up or down towards him, and when that is blocked too it stays put.
Walls stop everyone. Wappo cannot pass.

A monster that steps into a pit is stuck there for three turns. Two monsters
that land on the same square merge into one big monster that takes three steps
a turn and walks over pits. Stepping into a pit, or onto a monster, loses; so
does a monster reaching Wappo. Reaching the exit wins, but only after the
monsters have had their turn: standing on the exit with a monster one step away
is still a loss.

These are the original's rules exactly, and that is checked rather than
claimed. A model of the original's turn loop was written down from its
behaviour and run against `WappoCore` over 157k random moves; the two agree on
every position while the game is on. They differ only at the moment it ends:
walking into a monster that sits in a pit is "caught" in the original and "fell
in a pit" here, and on a catch the other monster may stop a step short.

## Seeing the monsters' move

The original animated the monsters stepping after each move. E-ink cannot, so
the turn is split in two: after Wappo moves, the board shows where each monster
is about to go, as an arrow along its path, and a tap or any side key plays it.
A turn where no monster can move skips the pause.

The preview and the real move cannot disagree because they are the same code:
`chaseStep()` decides one monster step and both the planner and
`advanceMonsters()` call it. An earlier version had two copies, and the planner
gave up on both monsters whenever one was trapped -- the free one then moved
with no arrow and no pause. `host-tests/wappo` keeps that case.

A trapped monster carries a T3..T0 badge: turns left in the pit. T0 means it
climbs out on the next move.

## Undo costs a move

The move counter against par counts what the level cost, not where Wappo
stands. UNDO puts the board back one move and adds one to the counter, so
stepping and taking it back costs two. Only RESTART sets the counter to zero.
The game keeps two numbers for this: `turn` is board time, which undo rewinds
and the pit traps count down on, and `moves` is the cost, which only goes up.

## The levels are generated

`tools_local/wappo/gen_levels.py` writes `WappoLevels.h`. It does not design
levels; it searches for them. Each level starts as a random board -- walls,
pits, monsters, a start and an exit -- and is mutated one change at a time while
the optimal solution gets closer to that level's target par. A board is kept
only when:

- it is solvable and its optimum is **exactly** the target par;
- the monsters matter: the par is at least eight moves longer than the plain
  walk to the exit;
- with `--avoid FILE`, its walls overlap no level in that file by more than a
  third, under any of the four mirror images.

The targets are the original's difficulty curve as numbers: par 20 rising to the
low 30s, with a long level every so often (36, 42, 49, 52, 58, 63). In the set
that ships, the walk to the exit is five to nine moves, so the length is all in
the monsters; the optimal solution found for most levels uses a pit trap, and
for a handful a merge.

**Par is a proof, not an estimate.** The par stored for a level is the length of
its shortest solution, found breadth-first over the full game state (Wappo, both
monsters, their trap counters, merged or not). So par can always be reached and
never beaten. `gen_levels.py --check` re-solves the committed file and
`host-tests/wappo` runs it.

The generator is deterministic. Reproducing the committed set needs the same
seed and the same `--avoid` file; without it the script still makes a valid set,
just a different one.

## Progress and the ending

`/.crosspoint/wappo.sav` is one line:

```
<current> <maxUnlocked> <best>
```

`<best>` is two hex digits per level: `00` not cleared, `ff` cleared before
bests were recorded, anything else the fewest moves that cleared it. An older
two-number save still loads; every level below the unlocked one becomes `ff`,
ticked but scoring nothing until it is played again.

Points are the original's: par × 100 ÷ moves per level, summed, out of 12,500.
The level grid ticks cleared levels and puts levels cleared in par moves in a
filled badge. Clearing level 125 turns NEXT into FINISH, which opens the ending
screen with the points; after that the menu says ALL LEVELS CLEARED and PLAY
starts again from level 1.

## Drawing on a dithered floor

The board alternates plain and dithered squares, and a one-bit sprite drawn on
the dither lets the dots show through every gap meant to be white -- faces,
eyes, bodies. Each piece is therefore drawn twice: a white mask first, then its
ink. The mask is the sprite's filled shape -- its ink plus every transparent
pixel the outside cannot reach -- grown by `kHaloPx` (two) all round, so the
piece also gets a white outline against the floor. `haloOf()` computes it at
compile time from the sprite itself, so the two cannot drift.

## Verification

- `host-tests/wappo/run.sh`: the rules on hand-built boards (chasing, blocked
  monsters, merging, the exit, a trapped monster beside a moving one), every
  level loading as defined, the save format and its migration, and the
  optimal-par check over all 125 levels.
- Optimal solutions for every level win in exactly par moves when replayed
  through `WappoCore`.
