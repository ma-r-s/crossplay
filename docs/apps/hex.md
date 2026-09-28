# Hex

Eleven by eleven, no captures, no draws. Black joins the top edge to the bottom,
White joins the left to the right, and whoever gets there first has won. Two
people on one device, two devices in a room, or one person against a Monte Carlo
search at three levels.

Hex was invented by Piet Hein in 1942 and independently by John Nash in 1948. It
is in the public domain; this implementation claims no affiliation with anyone
who has ever sold a set.

**The rules are one sentence and they never grow.** That is the whole reason it
is on this shelf beside Go: a connection game has no capture rule, no scoring
pass and no dead-stone negotiation, so a player who has never seen it can be
playing within a tap of opening it -- and it still has enough in it that the
hard level is worth beating.

## Three things that are decided, not configurable

**One board size.** Eleven is the size Hex is played at and the size its
first-player advantage was measured on. A second size would mean a setting, a
saved field, a wire field and two sets of geometry constants, for a variant
nobody asked for.

**No swap rule.** The opening move is not stealable. The pie rule is how
tournament Hex answers Black's advantage, and it costs two more states to draw
(offered, taken), a turn belonging to neither player, and an explanation on the
front door. What it buys is fairness between two players of equal strength who
both know the game, which is not who is opening this app. Against the computer
the YOU PLAY row settles it instead: take Black for the advantage, take White to
be made to work.

**The board never flips.** It is drawn one way up for both players, in a match
and on a shared device alike. This is not the same decision chess and checkers
make, and the reason is that Hex's two players own DIFFERENT edges: rotating the
board to face whoever is to move would not merely move the stones, it would move
the goal, and two people sharing one device would each be told they were playing
top to bottom. So there is no face-to-face mode here, and nothing to pass-and-
play beyond passing the device across.

## The files

The shape every game here uses. The first five are freestanding and
host-tested; only the activity needs hardware.

| File                 | Holds                                                       |
| -------------------- | ----------------------------------------------------------- |
| `HexCore.h/.cpp`     | the rules, the position, the win detection. No heap.        |
| `HexFlow.h`          | the screens, the settings, what a tap means                 |
| `HexBrain.h/.cpp`    | UCT/MCTS, the playout policies, the level ladder            |
| `HexSave.h/.cpp`     | what is written to the card, and what a bad file costs      |
| `HexScreens.h/.cpp`  | every screen, as free functions over plain models           |
| `HexActivity.h/.cpp` | renderer, input, shelf, link, storage                       |

`host-tests/hex/run.sh` runs the rules, the brain and the save file on a laptop;
`host-tests/ui/run.sh` runs the screens; `host-tests/link/run.sh` plays a whole
match between two fake devices on a link that drops, duplicates and reorders.

## Win detection is union-find, and it lives in the game

Placing a stone unions it with its same-coloured neighbours and, when it sits on
one of its owner's borders, with a virtual node for that border. Black has won
when the TOP and BOTTOM nodes share a root. It is 125 bytes, it is incremental,
and it costs nothing per move -- the alternative is a flood fill on every
placement, on a device where the search wants every cycle it can get.

**The forest is a field of `hex::Game`**, so it crosses the wire and lands in
the save file with the position it describes. A receiver that rebuilt it would
be a second implementation of the one fact the whole structure is about, and the
two would only have to disagree once -- silently, with both devices agreeing
about every stone and one of them believing the game was still running.
`find()` is bounded rather than trusting the forest it walks, because a packet
is bytes from another device.

The whole game is **162 bytes** of the link layer's 192.

## The board fills the panel, and that is a geometry decision

Hexagons are drawn **flat-top**: a cell's centre is at `(2a + 3a*col, h + 2h*row
+ h*col)`, where `a` is half the flat edge and `h` is half the vertical pitch.
The rhombus then runs corner to corner from the panel's top left to its bottom
right, and the board box is `34a` by `32h` -- taller than it is wide, which is
what a portrait panel wants.

The conventional pointy-top drawing is the other way round and is width-bound:
the same board comes out with a 28px cell on a 480px panel, against **48px**
this way. Both extents come from `DeviceContext`, never from 480 and 800.

Keeping the hexagons axis-aligned costs about twelve per cent against the true
best fit, which is an arbitrary rotation of roughly 57 degrees. It is not worth
it: every edge would pick up stair-stepping on a one-bit panel, and the board is
nothing but edges.

**The rhombus leaves two big notches**, at the top right and the bottom left of
its box, and those are where the two seat cards go -- a stone, a name, and the
pair of edges that colour is joining. That space would otherwise be the price of
this layout; it is the thing that pays for it.

The notch is a TRIANGLE, so how far left a control may start depends on how tall
it is: row 0's cell `c` has ink from `21c - strip` downward, so an 84-pixel card
clears everything left of column five and a 52-pixel button band clears column
three. The result screen's two doors are stacked for that reason and each takes
the width its own row allows -- PLAY AGAIN the wide one, DONE the short one
underneath. Both at the narrow width and the component elides the label to
"PLAY AG...": drawn, tappable, and saying the wrong thing, which is what a
screenshot catches and no assertion did.

The four borders are drawn as strips outside the board: Black's are solid ink
and White's are paper with a rail along the outside, which is the same
filled-versus-outlined pair the stones themselves use.

**A stone goes down in one tap.** Go aims first and commits second because its
intersections are a 49px pitch with dead gutters between them. Hex's cells tile
the panel with no gutters at all, so the target a finger gets is the whole cell,
and a second tap would buy accuracy the geometry already has.

`cellCentre()` and `cellAt()` are exact inverses over all 121 cells, and
`host-tests/ui` walks every one of them plus six probes inside each hexagon.
The inverse is a fractional axial coordinate put through **cube rounding**;
rounding the row and the column independently instead claims the rhombus of four
centres rather than the hexagon, which is wrong by up to a third of a cell along
every slanted edge.

## The brain

UCT/MCTS. It exploits the property that makes Hex cheap to search: a full board
has exactly one winner, always, so a playout needs no legality check, no
mid-playout terminal test and no scoring pass. Fill every empty cell in a random
order, then read the winner off one flood fill.

**Difficulty scales the policy, not just the budget.** Cazenave and Saffidine
measured the bridge pattern in the playout policy at about +105 Elo over naive
UCT, and AMAF/RAVE on top of it (with UCT exploration off) at a further +181 --
together roughly what a 250-fold increase in compute buys. The gain is
affordable on this chip and the compute is not.

| Level  | Policy                                   | Budget                 |
| ------ | ---------------------------------------- | ---------------------- |
| EASY   | plain UCT, plain playouts                | 1,500 sims / 0.8s      |
| NORMAL | bridge-aware playouts                    | 8,000 sims / 2.5s      |
| HARD   | bridge + RAVE, exploration off           | 30,000 sims / 4.5s     |

Whichever bound binds first wins. The simulation count is what keeps the host
tests deterministic -- they lend no clock at all -- and the clock is what keeps
the device under five seconds a move.

**Every level takes an immediate win and blocks an immediate loss before it
searches.** A machine that walks past a winning stone is not an easy opponent,
it is a broken one, and at EASY's budget the search genuinely can miss both.

**The bridge response is reactive**, which is the consequence worth planning
for: from NORMAL up a playout cannot be one shuffle-and-fill pass. The shuffled
order is walked instead, and when a stone lands in one carrier of an opponent's
bridge the other carrier goes on that opponent's queue and is played before they
take anything else from the shuffle. It is O(1) a move against a bridge table
built at compile time from the neighbour cycle.

Virtual connections and H-search are the next tier up -- MoHex territory -- and
are deliberately out of scope: they need a solver and a pattern database, and
what they buy is invisible to anybody who is not already a Hex player.

The search runs on a **task of its own, pinned to core 1**, so a four-second
think cannot starve the core the system watchdog looks at. `thinking` is
returned from `preventAutoSleep()`, and what that covers is narrower than it
sounds: the loop task is blocked on the search task's notification while the
search runs, so nothing polls the sleep guard during it. The flag is true across
the repaint that announces THINKING -- the pass before the search starts, and
the last pass the sleep timer can see -- so it stops the device sleeping INTO a
search rather than during one. A four-second think is short enough that the
timer cannot expire inside it anyway. Its node pool is 49KB, taken in `onEnter()` and
freed in `onExit()`; with no pool the app still plays, with a centre-weighted
legal move and a line in the log.

## What is written down

`/.crosspoint/hex.sav`, one line, written to a temp file and renamed **only if
the temp write succeeded**. That second half is the half a temp file is useless
without: removing the live save before knowing the replacement exists turns a
full card into a lost record and a lost game, which is the total loss the whole
dance exists to avoid. The line's length is `hexsave::kMaxLineBytes`, stated
once, because a buffer that drifted below it would not fail loudly -- `pack()`
would return nothing and the format's own short-line tolerance would quietly
accept a save that had stopped carrying the game.

It holds the record, the settings, the last finished board for the front door's
ornament, and the game in progress.

The front door draws whichever of those two boards it has, and an EMPTY one when
it has neither. Empty rather than nothing: a fresh device showed a four hundred
pixel hole in the middle of its own front door, which reads as a screen that
failed to load -- and the shape of the board is the one thing about Hex a
stranger has to see before the rules mean anything.

**A short line is accepted; a wrong version is not.** The two failures are not
the same shape. A file from a build with fewer fields is missing a TAIL, so
everything it does carry is where this build looks for it -- which is how a
player who chose HARD once keeps it across a release that adds a field. A file
with a different version is a file whose fields may have MOVED, and reading one
of those as far as it goes shifts a whole board along by one. That is a game
that loads and is wrong, which is worse than one that does not load.

The file is never written during a match: the position on screen is then the
shared game, and this file is what the solo game resumes from when the match
ends.

## Nearby

`GameId::Hex` is `0x0B01`. A match is the solo game with a different source of
the opponent's move: `LinkActivity` owns the searching screen, the disconnect,
the rematch conversation and the tick, and Hex supplies the board.

The result is recorded in `onMatchEnded()`, never at the end of `gameLoop()` --
the link layer stops giving the game the pass the moment a match ends, so
anything after that point is unreachable in multiplayer. Five games in this fork
shipped counting zero matches for exactly that reason.

**And it has to survive the teardown**, which is the same zero one door further
along. `onMatchEnded()` counts the match in memory and cannot write it -- the
save is refused for the whole length of a match -- and then `onLinkEnded()`
reloads the card to put the solo game back. Reload the record with it and the
match is gone, silently. `hexsave::recordAfterLink()` is that decision, kept
freestanding so `host-tests/hex` can drive the whole sequence: a card, a match
counted on top of it, the teardown, the write teardown is the first moment for,
and the reload after it.
