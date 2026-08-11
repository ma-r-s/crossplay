# Toy Battle

Paolo Mori & Alessandro Zucchini, Repos Production, 2025. Two players, 15
minutes. AS d'or 2026 nominee.

**The rules below come from the official English rulebook PDF and the official
English player aid PDF, both pulled from Repos' own CDN, not from summaries.**
That distinction cost a session on Jaipur, where three things every secondary
source said were wrong. Two things the secondary sources had wrong here too, and
both change the engine:

- **Hook cannot teleport onto the H.Q.** It ignores the connection rule for any
  _base_, but the H.Q. is not a base, and placing Hook there still needs a
  connection. A brain that misses this hallucinates a one-tile win from across
  the map.
- **Kwak is a joker, not strength 0.** It covers anything and anything covers
  it, which is not what "lowest strength" would produce: strength 0 could not
  cover another 0, and Kwak can.

## The rules

### Setup

1. Choose 1 of 8 terrains. The terrain fixes the medals objective and which
   colour each player takes.
2. A medal marker on every star space.
3. Each player shuffles their own 24 troops facedown as their **reserve**, then
   **removes 4 without looking**. Those 4 are out of the game and _nobody_ knows
   which. 20 remain.
4. First player racks 3 troops, second player racks 4.
5. An empty space beside the board is the **discard**, face up.

### A turn

Exactly one of:

- **Draw 2** from your reserve onto your rack. The rack holds **8 maximum**, so
  this is illegal at 8. With 1 troop left in reserve, or 1 free rack space, you
  may still take the action and draw only 1.
- **Place 1 troop** from your rack onto a slot, then apply its effect (optional,
  every effect is "you may"), then the base's effect if it is a special base.

### Placement

Two rules, and they are the whole game.

**Slot.** You may place on:

- an empty base,
- a base occupied by any of your own troops,
- a base occupied by an enemy troop of **strictly lower** strength,
- your opponent's H.Q. (never your own).

Placing on an occupied base **stacks on top**. Stacks are unlimited, and **only
the visible top tile occupies the base**. Either player may look through a stack
but never reorder it.

**Connection.** The slot must trace a continuous path from your H.Q. through
bases **you occupy**. Empty bases and enemy-held bases cut the line. The H.Q. is
not a base and is never a stepping stone.

### Regions and medals

A region is a closed zone fenced by paths and bases. The instant you occupy
**every base surrounding a region**, take all medals in it. You keep them for
good, even if you lose the region immediately after. A region already looted
gives nothing. Several regions can fall on one turn.

### The eight troops

Three copies of each, so 24 per player.

| Troop  | Str   | Effect                                                                    |
| ------ | ----- | ------------------------------------------------------------------------- |
| Kwak   | joker | May be placed on top of any enemy troop, and any enemy troop may cover it |
| Skully | 1     | Draw 2 from reserve (only 1 if your rack already holds 7)                 |
| Cap'n  | 2     | Place 1 extra troop and apply its effect                                  |
| Jumbo  | 3     | Discard 1 visible enemy troop adjacent to Jumbo, face up                  |
| Hook   | 4     | Ignore the connection rule, for any **base** (see the H.Q. caveat above)  |
| XB-42  | 5     | Take 1 troop at random from the enemy rack and discard it face up         |
| Star   | 6     | Draw 1 from reserve                                                       |
| Roxy   | 7     | None                                                                      |

Adjacency, for Jumbo and for the Volcanic Jungle base, is **one section of
path**: two slots joined by a single edge.

Cap'n's ordering is spelled out on the aid: **apply all the effects of the
troops you just placed first, then any special base effects.**

### Winning

Two immediate wins, checked the moment they happen:

- place a troop on an enemy H.Q. (any one of them, if the terrain has several);
- reach the terrain's medals objective.

Otherwise the game ends when a player can neither draw nor place. Most medals
wins, and **a tie goes to the player who did not end it**.

### Special bases

Officially optional: _"For your first games, we suggest playing without special
base effects. Treat all special bases like bases with no effect."_ That sanction
is why this is **a setting rather than a phase of the project**, and why "off"
is a real way to play rather than a crippled one.

**The setting lives in `Game`, not in app settings.** Two linked devices must
agree on it, the opponent has to see it to play correctly, and a brain that
cannot read it would misvalue every position on a board it thinks is inert. The
app setting chooses the default; `newGame` bakes it into the state and it never
changes mid-game.

Two global rules govern them when they land:

- You may only interact with **visible** troops. A special base effect never
  touches a covered one.
- The 8-troop rack cap blocks any special base effect that would exceed it.

| Terrain         | Special base                                                                                                                                                                                 |
| --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Castle Field    | Return 1 of your **other** troops, from anywhere on the terrain, to your rack                                                                                                                |
| City of Clouds  | Draw 1 from reserve to your rack                                                                                                                                                             |
| Volcanic Jungle | Move 1 enemy troop adjacent to this base to a base adjacent to its start, ignoring placement rules                                                                                           |
| Cursed Cemetery | Take 1 of your troops out of the discard onto your rack                                                                                                                                      |
| Caribbean Sea   | No special bases at all, but **asymmetric: 2 blue H.Q. against 1 red**                                                                                                                       |
| Battlefield     | Point at 1 troop on the enemy rack without looking. They lay it facedown and cannot place it on their turn; it returns to their rack at the end of it, and counts against their 8 throughout |
| Tropical Pool   | **Placement restriction**: only the printed values may be placed on these bases and on the H.Q.                                                                                              |
| Station Metal-X | **Placement restriction**: troop effects do not apply on these bases                                                                                                                         |

The last two are restrictions that apply _before_ placing, not effects that fire
after.

**One reading had to be chosen, and it is worth Mario's eye.** Station Metal-X
says troop effects are not applied on its bases. Hook's effect _is_ the
connection waiver, so the strict reading is that Hook cannot use it to land on a
Metal-X base, and a Cap'n placed on one grants no extra placement. The loose
reading would treat the waiver as spent before the base is reached. The engine
implements the strict one, because the alternative lets a troop use an effect on
a base defined as the place effects do not happen, and because a rule that
sometimes applies is worse than either rule. Both are defensible; if the printed
board contradicts this, it is a one-line change and the test that pins it is
`testGateAndNullify`.

The ordering matters and is the aid's, not ours: **all troop effects of the turn
first, then the base effects, in the order the bases were occupied.** A Cap'n
chain therefore runs both placements and both troop effects before either base
fires, which changes what the extra placement could have been.

### La Croisette

The 2026 expansion, and it is **only a terrain**: its own rulebook says "there
are no changes to the gameplay or victory conditions." Confirmed from the
official PDF, and it needs no new mechanics at all -- its four special bases are
kinds this engine already has:

| On the board             | Effect                                          |
| ------------------------ | ----------------------------------------------- |
| Ice cream van            | `Draw` -- draw 1 from your reserve              |
| Police box               | `Suppress` -- Battlefield's blind pick          |
| Pier marked `4 - 7`      | `Gate` -- strengths 4, 5, 6, 7 **or the joker** |
| Pier marked with a cross | `Nullify` -- troop effects do not apply         |

Its medals objective is **5**, read off both badges.

Note the gate admits the **joker as well as 4-7**, which is the first hard
evidence of how a gate treats Kwak. `kProvingGround`'s gate was a guess made
before this was known and admits only 6 and 7; that is fine for a board of ours,
but the real Tropical Pool should be read the same careful way when it lands.

**The board itself is not implemented, because it is not yet read reliably.**
See "Open items".

## The shape of the implementation

### The board is the placement log

`Game` stores placements in the order they happened, not a per-base stack array.
The top of a base is the last entry naming it. That falls out of a rule rather
than being a trick: **every removal in this game pops a visible troop**, and
visible means top of stack, so the log only ever grows at the end or loses its
last entry for some base. Jumbo's discard, Castle Field's recall and Volcanic
Jungle's shove are all pops.

This matters because a stack that gets uncovered can **flip who occupies a
base**, which can sever a supply line three bases away. A design that stored
only the visible tile would be wrong, and wrong in a way that shows up rarely
and looks like a connection bug.

Cost is a scan to answer "who is on base b". At 48 placements over ~20 bases
that is nothing for the rules; the brain keeps its own derived top-of-base cache
rather than pushing one into the wire format.

### One description of a game

`Game` is the whole shared state **and** the LinkPlay wire format, the
Battleship and Jaipur discipline. Everything derivable is derived: connection,
legal moves, region control, the winner. No stored field can disagree with a
computed one because there is no stored field to disagree.

### The opponent cannot cheat by construction

`Observation` has no field for the enemy rack, only its size. The brain takes an
`Observation`, so a brain that peeks does not compile. Toy Battle hides less
than it looks: every placement is public and permanent, the discard is face up,
so the only genuine unknowns are the 4 troops each player set aside unseen and
the order of the reserve. The belief state is a small multiset and it is exact.

### The opponent

Built before any pixel, because it is the part that decides whether this is fun.

**The threat that matters is exact, not searched.** Sudden death by touching an
H.Q. looked like it would force a real two-ply search. It does not, and the
reason is a rule: _any_ troop captures an H.Q., so there is no question of which
one they hold. Whether they can take yours next turn is therefore a property of
the board alone -- is your H.Q. in their reachable set, and can they place at
all -- and it is computed exactly from public information. `hqIsExposed` is that
question, and both skills consult it on every candidate move. The expensive
search the game seemed to demand was the wrong tool for the one threat it has.

**It cannot cheat, because its input holds no secret.** `chooseMove` takes an
`Observation`: the board, the discards, both rack sizes, and the multiset the
enemy rack must have been drawn from. Two things a player also cannot know are
modelled rather than simulated -- the enemy rack's composition, rebuilt from the
public multiset so positions can be played forward, and the order of either
reserve, which is why `observe` clears the seed and a draw is worth tempo rather
than a known troop. Carrying the seed would have handed over both reserves in
reading order; that is the one leak this design had to be careful about.

**Two skills, and the split is where it measured rather than where it was
designed.** Both keep the same reflexes: take the win in front of you, never
leave your own H.Q. open. Recruit has nothing else and plays freely among the
safe moves. General weighs medals, territory, reach and tempo.

The first version put the split somewhere else -- General also raced for
regions, valued pressure on the enemy H.Q., and looked one turn ahead at the
threat a Cap'n's second placement makes. Over 600 games each of those was worth
**nothing**: 48%, 48% and 50% against the same opponent without them, where 50%
is a coin toss. They were deleted rather than tuned. The gap that does exist is
between having an evaluation at all and not having one, and that one is 99%
(598/600). Reflexes alone still beat a random player 77%, so Recruit is a real
opponent and not a stooge.

The lesson worth keeping: **the first head-to-head ran 60 games and read 53%,
and the bound was set to 53%.** That is a bound tuned to its observation, inside
a noise band of about +/- 6%, and it certified a difference that did not exist.
The bands in `test_brain.cpp` are now well below what was measured, and the
sample is 600.

## Open items

- **La Croisette is traced but not implemented, and this is the honest gap.**
  Its rules are settled (above) and its objective is 5. Its topology is not:
  unlike Castle Field it is irregular -- diagonal roads, an asymmetric
  coastline, structures at the board edge -- and from the one screenshot I
  cannot tell reliably which of the right-hand structures are bases, how the
  diagonals join, or which bases fence which region. Roughly 13 bases and 2
  H.Q., but "roughly" is not something to write into a terrain table. What
  would settle it: the board with each base tapped or highlighted, or simply a
  second look together. Committing a guess here would be worse than the gap,
  because a wrong path is invisible until someone cannot make a move they
  should be able to make.
- **No other real terrain is implemented yet, on purpose.** `kProvingGround` is ours
  and is named so: a 5x3 lattice with an H.Q. at each end, its eight cells as
  regions, and every special base kind on the one board (no terrain Repos
  printed mixes them; this one exists so the tests have somewhere to run).
- **Nobody has published clean scans of all eight boards.** Searched 2026-08-10
  and this is what exists:
  - Repos' own fan render, on rprod.com as
    `elements/toy-graphics-boxbottom-boards-*.png` and on BGG as image 8785448.
    All eight, overlapping, and only Battlefield fully visible.
  - The rulebook's Castle Field photo, at an angle.
  - BGG image 9404840 ("Map 5"), a near-top-down of Battlefield with pieces on
    it: topology readable, one board.
  - 180 images in the BGG gallery, surveyed. The rest are angled table shots,
    component photos, DIY boards, and the Polish edition's marketing sheets.
  - The BGA doc wiki holds the eight troop icons and no boards.
  - **Board Game Arena has all eight clean, plus a Winter skin.** Its assets
    live behind a game table, which needs an account, so this is Mario's to
    capture: one screenshot per terrain settles the whole question.
- Medals objective per terrain: unknown, same reason. `kProvingGround` uses 7,
  which is the number on the rulebook's own illustration of the objective.
- **There is a 2026 expansion, La Croisette**, rules downloaded and out of scope.
