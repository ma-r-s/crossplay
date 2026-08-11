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

### Everything public is on screen, in 88 pixels

The table version leaks nothing the screen may hide, so the action bar carries
the numbers a player would read across a table: troops in hand, troops still to
draw, troops out of the game, for both seats. Two rows of three, top row theirs,
no headings and no labels -- a solid tile, a down triangle and a cross, the same
marks the cards and the bases already use.

The labels were the whole cost. Spelled out (`THEM  HAND 4  LEFT 16  OUT 0`) it
was 194px and still growing per revision; the rows do not need naming because
the board says which end is which, their H.Q. is at the top and your own rack is
the row directly beneath the bar. Two marks were tried and rejected at this
size: an outlined tile is a zero, and two overlapping tiles are a smudge. The
hand also gets a narrower column than the other two, because eight is the rack
limit and a single digit in a two-digit box reads as a detached group.

### The opponent

Built before any pixel, because it is the part that decides whether this is fun.

**It got hard by looking one move further, and by nothing else.** The brain
that first shipped was greedy: it applied every legal move, scored the position
each one left, and played the best. Depth one. Against that brain, the same
evaluation with a shallow reply search wins **71% of 6400 games**, and it wins
on both boards with special bases on and off.

Everything else tried on this brain has measured as nothing. Four evaluation
terms across two sittings -- a region race, pressure on the enemy H.Q., a Cap'n
threat, and later a region race plus a medal-threat penalty -- were each worth
zero or slightly negative, and the last pair made the searching brain *worse*
(53.5% against 57.1% for the same search without them). The reason is
structural. The engine banks a region's medals inside `apply`, so a greedy brain
already sees closing one; a term telling it "I am close to a region" says what
the medal count is about to say anyway. What it could not see was the reply --
nothing stopped it handing over a three-medal region, because the only threat it
ever computed was its own H.Q. A term buys one threat; the search closes the
class.

**How the search is shaped, and why those numbers.** Score every move greedily,
keep the best 8, and for each of those compute the opponent's best reply and
then your own best answer to it: three plies, on a beam of 8. The greedy pass is
the move ordering, so a move has to survive being answered rather than merely
look good before anyone answers. Beam 12 and beam 24 are level with beam 8 head
to head (51.5% and 50.8%, both inside one and a half standard errors) at two and
four times the cost, so 8 is where width stops paying. The opponent inside the
search plays greedily, because a model with a beam of its own squares the cost
for a reply that is only ever a guess about a rack the brain may not see.

Terminal scores are discounted by the ply they were found at. Without that, a
win three plies deep scores the same as a win available this turn and the
tie-break picks between them by hash -- which the suite caught as "with a win on
the table, the brain takes it" failing the moment depth 3 was switched on.

**It costs 1248 positions in the worst move measured**, against 361 for the
greedy brain, and adds 3.4KB of DRAM. All the search's working positions are
static: a `Game` is 148 bytes and an `Observation` 160, and holding six live
would have put about 900 bytes of locals on a stack this project caps at 256.
The last time this file allocated on the stack it took 17KB and the simulator
never noticed.

**The tier list is the evidence, and it is transitive.** The policies are
competed against each other in `host-tests/toybattle/tournament.sh`, seats
alternating, every pairing on the same seeds, on both boards with special bases
on and off. A ranking is only believed if no policy loses head to head to one it
outranks by more than two standard errors -- a first version of that check
flagged three "cycles" that were simply ties being force-ranked, which is the
same mistake as a bound tuned to its observation wearing different clothes.

    recruit 0.1% < greedy 47.6% < d3b4 61.1% < SHIPPED 70.1% ~ d3b12 71.1%

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
safe moves. General weighs medals, territory, reach and tempo, and searches.

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

## The shell

Eight screens: Menu, Setup, MapPick, Lobby, HowTo, Board, Brief, Result.
`back()` is an exhaustive switch over the screen alone, with no default, so a
new screen without a decided Back fails the build. It is deliberately **not** a
function of the mode: leaving Setup or the Board in a link game also ends the
session, but that is the activity carrying out a consequence rather than a
second destination. A Back that read the mode would be the shell machine reading
the game machine, which is the coupling the two-machine split exists to prevent.

Two screens hang off another rather than off the menu -- the briefing off the
board, the map list off setup -- and the flow test asserts nothing is ever three
presses from the top, that exactly one screen leaves the app, and that no pair
can Back into each other. It also asserts its own screen list is complete
against `kScreenCount`, which is what caught both new screens.

**Three treatments were built and photographed rather than described**, and
Mario picked the front door on 2026-08-11: the documented band order, plainly.
The other two -- SLAB, which carried the board's own material up into the menus,
and BRIEFING, which kept the setup on screen so starting was one tap -- were
deleted the same day rather than left behind a flag. The renders are still in
`qa-artifacts/` if the question comes back, and `scripts_local/shoot-shell.sh`
is the recipe that produced them.

Every picture in the shell is `miniBoard()` drawing the real terrain from its own
normalised coordinates -- the menu ornament carries the position you would
resume, the map list draws each board, and the rules teach on Castle Field
itself. A diagram would be a second thing to learn before you can learn the game.

**Four defects came from looking at the renders, not from reasoning.** The menu
caption read "14 OF 7 MEDALS" backwards; map names truncated to "CASTLE FIEL" in
the display cut; the difficulty rungs drew their name through their own blurb;
and a rules caption struck itself through its own rule. The last two are one
mistake: a text box sized by guess rather than by `lineHeight()`. **A box shorter
than the cut does not clip, it overlaps whatever is under it.**

## Three rungs, and they are the tier list

`Recruit` is the two reflexes, `Sergeant` the greedy brain, `General` the reply
search. `test_brain` asserts the ladder IS a ladder -- General 71% over Sergeant
across 400 games, Sergeant 100% over Recruit across 300 -- so a change that makes
two rungs the same opponent fails there rather than being noticed by a player.
Against a person the row is not on the screen at all, because a difficulty
setting with nobody to apply it to is worse than no row.

## Two devices

Toy Battle is the eighth game on `src/apps_local/link/`. GameId `0x0901`, and the
shared state is `Game` itself: 148 bytes of a 192-byte payload, already the save
format, already carrying an exact-size assert. Nothing about a position is
described twice anywhere in this app, so there is no second description to drift.

**Everything two devices must agree on lives inside the state**, because the
layer has no settings channel and its one-byte note vocabulary belongs to the
layer. `Game` holds the seed, the terrain and the special-bases flag, so the
dealer's choices ride along with the opening. PLAY NEARBY still lets you pick a
map; the coin toss decides whose pick is dealt.

**The dealer deals and passes at once.** The first version dealt and kept the
turn, so the opening did not leave the device until the dealer had chosen a move
-- and two simulators side by side showed the other player sitting on an empty
rack and a board with nothing on it for that whole time. Correct by every test,
obviously wrong the moment you looked. The deal now names the other seat as
starter and goes out immediately: the coin toss decides who deals, not who moves.

**The follower must not deal, and the shape of that trap here is worse than the
one the layer documents.** A zeroed `Game` reports Playing, has no legal
placement, and **believes it can draw** -- a reserve is whatever has not been
seen yet, and nothing has. An undealt follower is not idle, it is playable, and a
tap would draw from an imaginary reserve and send that board as a move. The
`dealt` flag is load-bearing, and `test_toybattlelink.cpp` asserts all three
facts so it cannot quietly stop being so.

Turn gating asks whether the rules **and** the link both say yes. Asking whether
they agree is a different question and is also true on the opponent's turn.

The link suite plays a full game across 20% loss, 10% duplication and 60ms
jitter, soaks twelve more, checks the dealer's map and bases reach the follower,
and flips every bit of a position to confirm each one is either refused or still
playable.

## Continuing

`/.crosspoint/toybattle.sav`, loaded in `onEnter` so CONTINUE means something the
moment the app appears, written in `onExit`. The guard against writing during a
match lives inside `saveGame()` rather than at each call site, because the call
that matters is the one sleep makes when the player does nothing.

The format is `Options` plus `Game` plus a seat, with a magic, a version, both
struct sizes and a rotating checksum. Solo only: a link game cannot be resumed
with the other device gone, and one that quietly restarted against the brain
would be worse than no save. The flow test flips all 1272 bits of a valid save
and requires every one refused, then truncates it at every length. It also
refuses a terrain index this build does not have, which is the ordinary way a
good save goes bad -- written after Mario adds a map, opened by a build from
before it.

## City of Clouds

Traced by Mario 2026-08-11, the second real terrain. Fourteen bases in two
columns of five with four **Draw** bases down the middle, each of those joined
to two bases on either side, so every face of the board is a triangle or a quad
-- thirteen of them, paying sixteen medals against an objective of eight. That
is the same half-the-board ratio Castle Field uses (7 of 14), which is the only
corroboration available for an objective nobody has published.

Its two end regions are fenced by three bases and an H.Q. An H.Q. is not a base
and cannot be in a region mask, so those regions list the three bases and are
taken by holding those three. That falls out of the rule rather than being a
decision: the rulebook says occupy every **base** surrounding a region.

**Alignment and symmetry are the generator's job, not the tracer's.** A hand
trace puts a column at 0, 8, 13, 13, 16 when it means one column, and puts the
left half and the right half in visibly different places. `to_cpp.py` now snaps
near-equal coordinates onto shared levels and, for a board that declares
`"symmetry"`, mirrors those levels about the midline. It is opt-in per board
because not every terrain is symmetric -- Caribbean Sea is deliberately lopsided,
2 H.Q. against 1. Both traced boards declare `"both"`.

**The tolerance is 2% and was 4%, and the difference is a board's shape.** 4% is
wider than hand jitter but not narrower than every real distinction: Castle
Field's wells sit 35 units inside its corner bases on purpose, and 4% pulled them
onto the same column and quietly reshaped the board. 2% closes every gap either
traced board actually has -- the widest was 16 -- and leaves the wells alone. It
is also what puts Castle Field's H.Q. on the same row as the two bases beside it,
which they missed by 10 units: about five pixels, and a visible kink in the path
that runs straight through all three.

**Medals are anchored, not averaged.** The centre of a region's fence bases is
the obvious place to draw its medals and it is wrong the same way for every thin
region: a triangle of two column bases and one centre base puts its centroid a
third of the way across, hard against the column. `Region` now carries an anchor
computed at authoring time -- the roomiest point inside the region, the pole of
inaccessibility -- so the device pays nothing and the dots sit where the eye says
centre. **An H.Q. can be part of a region's fence even though it can never be part of
the mask**, because the region really does extend to it -- the rulebook says
occupy every *base* surrounding a region, and an H.Q. is not a base. Without it,
City of Clouds' end regions computed their anchor from a flat triangle and landed
it exactly on the top edge of a base.

Which regions those are is **said by the board, not guessed from adjacency**. The
generator inferred it at first, from any H.Q. joined to two of the fence bases;
Mario's point was that the person tracing can see it and the tool cannot, and an
irregular board would eventually make the guess wrong. The region tool now picks
an H.Q. like any other slot, and a fence entry at or above `baseCount` is an
H.Q.: it shapes the region and centres its medals, and it never enters the mask.
Migrating the three traced boards to say it explicitly reproduced all three
generated sources byte for byte, which is what says the migration was faithful.

**It also found a gap in the pipeline.** Castle Field happened to be traced edge
to edge and this one did not, so its coordinates spanned 117..872 of 1000 and the
board drew inside a margin while Castle Field filled the panel. That is a
property of how the reference picture was loaded, not of the terrain, so
`to_cpp.py` now normalises every board to fill 0..1000 in both axes. Castle
Field's generated source is byte-identical after the change, which is what says
the normalisation is a no-op where it should be.

## Volcanic Jungle

Traced by Mario 2026-08-11, the third real terrain and the first that is not
mirror-symmetric. It is a **complete 3x5 lattice** -- thirteen bases and both
H.Q. filling every cell -- with 180-degree ROTATIONAL symmetry: every slot has a
partner under a half turn, including the two H.Q. (seat 0 bottom-left, seat 1
top-right), the two Shove bases, and the two diagonal paths, which are each
other's partner. Six regions, two of them pentagons that close across a
diagonal. Fourteen medals, objective 8.

Point symmetry needs no new machinery: on a complete grid, making the x levels
and the y levels each symmetric about the midline is exactly what a half turn
requires, so `"symmetry": "both"` is right for it.

**Its objective was traced as 8 and is 7.** It came in as the only one of the
three that did not sit at exactly half the medals on the board, which is what
made it worth asking about; Mario checked and confirmed 7. Every real terrain so
far is half: Castle Field 7 of 14, City of Clouds 8 of 16, Volcanic Jungle 7 of
14. **That is now a usable check on the five still to be traced** -- an objective
that is not half the medals is not necessarily wrong, but it is worth a second
look at the printed board before it lands.

**No single alignment tolerance can work, and this board is why.** Castle Field's
wells sit 35 units inside its corner bases and must stay there; Volcanic Jungle's
centre column is 40 units wide and is all one column. The real distinction on one
board is smaller than the hand jitter on another, so the ranges overlap and any
global number is wrong for somebody. Boards declare `"alignTolerance"` (default
20, this one 30), and `to_cpp.py` prints how many levels it merged on each axis
so a snap that folds two real columns into one cannot pass unseen.

**A rectangular region has a line of equally roomy points, not one.** The medal
anchor search kept the first maximum it met, and on a point-symmetric board the
two halves get scanned in different orders -- so partner regions disagreed by
eleven units, five pixels, visible. Ties within half a unit of clearance now go
to the point nearest the region's centre, which is both symmetric and where the
eye expects it. The search also refines four times rather than twice, because
landing in the right region is not the same as converging tighter than the eye.

## Cursed Cemetery

Traced by Mario 2026-08-11, the fourth real terrain. Fifteen bases in an
irregular arrangement with four Exhume graves, ten regions, fourteen medals,
objective 7. Point-symmetric under a half turn like Volcanic Jungle, and the
first board that is point-symmetric **without being a grid**.

That distinction is why the generator grew a `"rotational"` symmetry mode.
Mirroring the x levels and the y levels independently is equivalent to a half
turn only when every slot sits on a row-and-column crossing, which is true of a
complete lattice and false here: two slots can share a column while their
partners do not. The rotational mode instead pairs each slot with the one
nearest its rotated position and gives the pair mirrored coordinates outright.
It refuses rather than guesses when the pairing is not an involution, which is
what an asymmetric board looks like from inside it.

Mirrored **outright**, not averaged: rounding each of a pair independently left
partners a unit apart, which is nothing on the panel and is still not what
symmetric means. Every slot's exact half-turn image is another slot.

Its two H.Q. fence nothing. The triangles they sit in are real faces of the
board but pay no medals, which is the case that made explicit H.Q. fences
necessary -- see above.

## Battlefield, and a frozen card

Fifth real terrain, traced 2026-08-11: fourteen bases either side of a central
spine, four **Suppress** bases, nine regions including two six-base ones fenced
partly by an H.Q., sixteen medals, objective 8. Symmetric across both midlines.

Suppression needed a look. The rack drew two states -- a light dither for
playable, plain white for anything else -- so "there is nowhere legal to put
this" and "Battlefield pointed at it" were indistinguishable. A frozen troop now
takes the darker of the only two dithers the device has, because it is a state
done TO you rather than a shape of the board. **It is asserted rather than
photographed**: an ordinary game does not happen to contain a suppressed troop,
so the ui fake target records fill paints as well as rects and the test checks
that no rack tile wears the dark dither until one is frozen and then exactly one
does. A state expressed only as a ground is otherwise untestable.

**The map list was dropping maps.** Cards were a fixed 104px, five fitted, and
everything after was silently not drawn -- with no scrollbar, no arrow and
nothing in the log.

The first fix sized the card to the terrain count so all nine would fit one
screen, which avoided the problem rather than solving it: nine cards at 63px are
small enough to be useless. **The card is 104 because that is what makes a map's
shape readable, and how many fit follows from that.** Seven maps is two pages,
stepped with the side buttons -- pointing is fingers, stepping is buttons -- with
dots at the foot showing which page, drawn only when there is more than one. No
on-screen hint names the buttons, per the design language.

**Nothing on the map list is highlighted.** It used to invert the currently
configured map, which reads as a cursor, and this device has no cursor: a tap
picks a map and leaves, so there is nothing for a highlight to mean.

The ui test now walks the pages and requires every terrain to turn up on one of
them. Asserting that every name is *drawn* was not enough -- that is what passed
while the sixth map was unreachable.

## Caribbean Sea

The lopsided one, traced 2026-08-11: twelve bases, **no special bases at all**,
and **three H.Q. -- two for seat 0 against one for seat 1**, which is what
`kMaxHq` was sized for. Six regions, eleven medals, objective 5. Its lone right
H.Q. hangs off a single path and fences nothing.

It has no symmetry of any kind, and it is the board that showed alignment and
symmetry were wrongly tied together: the generator only aligned a board that
declared a symmetry, so an asymmetric one got no jitter removal at all. They are
independent -- alignment takes out the hand, symmetry mirrors the design -- and
alignment now runs unless a board sets its tolerance to 0. Regenerating all five
earlier boards after the change reproduced every one byte for byte.

The tidying it asks for is exactly four groups, named by Mario: top row level,
bottom row level, left edge plumb, right edge plumb -- where the right edge is
the H.Q., base 7 and base 3, and explicitly **not** base 2, which sits further
in. `"alignTolerance": 30` produces precisely those four and leaves base 2 out.

**The tolerance is in normalised units, not traced ones.** The board is spread to
0..1000 before aligning, so a raw gap is scaled by 1000/extent first: Caribbean
Sea's top row is 21 apart as traced and 26 once spread, and a tolerance of 25
missed it by one.

## Tropical Pool

Traced 2026-08-11, the sixth real terrain and the only one where a special
**restricts what may be placed** rather than firing after a troop lands.
Thirteen bases, five gated, and **four H.Q. -- two per player, one of each pair
gated to 6 and 7**, which is exactly what `kMaxHq` allows and what `gate` being
indexed by slot was always for. Eight regions, twelve medals, objective 6.
Point-symmetric under a half turn, gate values and all.

**"Symmetrical over the diagonal" has meant rotational every time.** Volcanic
Jungle was the same. Tested here against all five candidates: only the half turn
is an involution with matching kinds and swapped seats (worst miss 18 units);
neither diagonal is even an involution (119 and 128). Detect it, do not read it.

**The trace was missing one path, and the board's own symmetry named it.** Two
regions failed to close, both at bases 1 and 4, and the edge list was
rotationally symmetric in every place but one: `(4,10)` existed and its half-turn
partner `(1,4)` did not. Base 1's partner is base 10 and base 4 maps to itself,
so `1-4` was determined rather than chosen. Added, and every region closes.

**It broke two things that had encoded "only a base can be gated".**

The structural test read `specialAt(slot) == Gate` for every slot, and
`specialAt` returns None for anything that is not a base -- so it asserted that
no H.Q. could ever be gated, and passed for as long as no board had one. It now
states the base case and the H.Q. case separately, and additionally requires any
gate bitmask to stay inside the eight troop kinds.

The board drew the silhouette from `specialAt` too, so Tropical Pool's two gated
H.Q. came out round: the one thing you must bring a 6 or a 7 to looked like the
two you can take with anything. **A gate is per slot, so the silhouette is too.**

**The joker passes every gate.** The first trace listed only printed values,
which would have meant Kwak could never enter a gated base nor take a gated
H.Q.; Mario corrected it, and it agrees with La Croisette's gate listing the
joker alongside 4-7. So a gate admits its printed values plus Kwak, everywhere.

**A gated slot says what it admits, on a tab under it.** The silhouette says
there is a rule; the tab says what the rule is, and a gate you have to remember
is a gate you misplay. Written as a range when the values run consecutively --
`*1-2`, `*3-5`, `*6-7` -- with the star for the joker, which needs no legend
because `*` is already Kwak's pip on the rack and in the how-to.

It sits under the slot rather than inside it. Inside was tried first and clipped
its own digits: the slot is 52px, the small cut is nearly twenty, and the strip
fought a six-pixel border. Under it, the whole slot stays free for the troop and
a knocked-out white plate keeps the values legible where a path runs beneath --
the same trick the medals already use.

## Open items

- **The "Winter board" is not a ninth terrain.** BGG image 9252799, posted by
  Repos themselves and captioned "skin on BGA": it is Castle Field reskinned for
  a seasonal Board Game Arena event. Same 15 bases, same paths, same four wells,
  same 14 medals in the same regions, same objective of 7. Noted because it is
  the most distinctive-looking image in the gallery and the obvious thing to
  trace by mistake. If the winter look is ever wanted it is a theme over an
  existing terrain, not new data.
## Castle Field, and why it was traced twice

The board in the tree is the merge of two independent readings of the same
board, and each caught an error in the other. That is the whole argument for the
editor in one paragraph.

**Mario's trace corrected mine on the wells.** I had each well joining the
*outer* base of its shoulder; it joins the *inner* one. The outer bases have
exactly two paths, to their H.Q. and to their inner neighbour, and nothing runs
down the outside of the board at all. That one edge, mirrored four times,
changes the shoulder regions from four-base quadrilaterals into three-base
triangles -- so it is a third cheaper to take a shoulder than I had it, and the
connectivity through the middle of the board is different.

**My reading corrected his on the river.** His trace has no region for the four
medals sitting in the water: two between the left and centre bridges, two
between the centre and the right. That water is a closed zone fenced by exactly
those two bridges, with the bridge parapets closing each end, so it is a region
of two bases -- the cheapest medals on the board, and what makes the objective
reachable in three region captures rather than four.

Both readings agreed on everything else, including the part I was least sure of:
the centre regions really are fenced by five bases, because their far edge is
the road that runs through an H.Q., and an H.Q. is part of a boundary without
being part of the price.

The arithmetic that falls out: 4 shoulders at 1, 2 river gaps at 2, 2 centres at
3, so 14 medals against an objective of 7. `testCastleFieldMatchesTheBoard`
asserts that shape rather than three region indices, precisely because the board
has now been retraced once and could be again.

- **Boards are traced in `tools_local/terrain-editor`, not by eye.** Reading a
  board off a photograph is the one part of this game that does not survive
  being done by inspection: Castle Field is regular and came out fine, La
  Croisette is irregular and did not. The editor takes the tracing, runs the
  same structural checks the suite asserts, and its `to_cpp.py` generates the
  terrain source, so no index is ever copied by hand. Castle Field in the tree
  **is** that generator's output, which is what proves the path works.
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
