# Go

Nine by nine, area scoring, komi 7.5, positional superko. Two people on one
device, two devices in a room, or one person against a machine at three levels.

Nine by nine **only**, and that is a decision rather than a first step.
Nineteen lines on a 480px panel is a 24px pitch with a 20px stone, which is
below the fingertip this device is driven with; nine gives 49px and a game that
finishes on one train journey.

## The files

The shape every game here uses. The first four are freestanding and
host-tested; only the activity needs hardware.

| File            | Holds                                                     |
| --------------- | --------------------------------------------------------- |
| `GoCore.h/.cpp` | the rules, the position, scoring. No renderer, no heap.   |
| `GoFlow.h`      | two state machines, the aim, the cautions                 |
| `GoEngine.h/.cpp` | the opponent: MCTS, the level ladder, dead-stone guessing |
| `GoSave.h/.cpp` | what is written to the card, and what a bad file costs    |
| `GoScreens.h/.cpp` | every screen, as free functions over plain models      |
| `GoActivity.h/.cpp` | renderer, input, shelf, link, storage                 |

`host-tests/go/run.sh` runs the lot on a laptop.

## The front door has three doors

PLAY, PLAY NEARBY, SETTINGS, and an ornament in the middle that is the final
position of your last game. Everything configurable is behind the third door:
six rows on a front door, three of them settings, is a settings screen with a
PLAY button on it.

There is **no how-to**. The board explains itself instead: the status capsule
names the phase, a stone is aimed before it is placed, and the two warnings
(that fills your own eye, that stone would be in atari) arrive at the moment
they are about to matter rather than on a page nobody reads twice.

**Icons carry the value, not the label.** The opponent row's mark is a machine
or two people. A graded mark for the level was the first choice and had to go:
at 32px in one bit, Lucide's three signal strengths are 3px bars in the bottom
third of the box and the weakest is a single speck that reads as a rendering
fault. `tools_local/toybox/icons.txt` records that, because the next person will
reach for the same three.

**A value row's icon has to LEAD.** The front door's icons sit at the right,
like the shelf's, because those rows are label-only. A settings row carries a
value there, and an icon drawn on top of it lands ON the value: the first
version squeezed the third row's label off the screen entirely.

## The ruleset, and the one thing it is for

**Area scoring (Chinese), positional superko, komi 7.5 in half points.** Every
engine plays this because a finished position is scorable by counting alone: no
prisoners to remember, no dame to haggle over, no seki exception.

Komi is **7.5 and not 7.0**, and the half point is load-bearing rather than
traditional. On an odd board a flat 7 ties on a 44/37 split, which is an
ordinary result; an odd number of half points cannot. **This game therefore has
no draw and needs no draw screen**, and `settlesEveryGame()` holds every komi
the level ladder can set to that promise. The first version had 7.0 and the
suite found the tie.

`kMoveLimit` is 400 moves, and it is a **[house rule]**. Chinese rules with full
positional superko terminate on their own, but the ring in `Game` remembers
eight positions rather than every one, so a long enough cycle is not forbidden.
An opponent that refuses to pass while losing (which is correct, below) will
happily play into one, and a self-play game ran past four hundred moves during
testing. A real game is forty to ninety.

## Four rules and four traps

Each of these was wrong first and is pinned by a test that a deliberate mutation
made fail.

- **A liberty is a POINT, counted once**, not a contact counted per stone. The
  oldest bug in every implementation of this game; it makes big groups immortal.
- **Suicide is judged AFTER captures resolve.** The move that fills its own last
  liberty while taking the group around it is legal, and is how half of all
  life-and-death problems are solved.
- **Ko arms only on the shape that can repeat**: one stone taken, by a stone now
  alone with one liberty. Arming it on any single capture passes the obvious
  test and silently refuses legal moves, which on the panel is a tap that does
  nothing. That mutation survived the first suite.
- **An edge eye tolerates NO hostile diagonal** where a centre eye tolerates
  one. Getting it wrong fills false eyes on the second line and kills the group
  the playout was keeping alive.

## A stone goes down in two taps

The first tap aims, the second commits, and tapping elsewhere moves the aim.
It costs one tap on a move you were sure of and saves a game on the one you were
not: the pitch is 49px, which is under a fingertip, and a stone cannot be taken
back in a match.

The pause is also the only place a warning can live. `go::cautionFor` returns
`FillsOwnEye` or `SelfAtari` for a move that is legal and almost certainly a
mistake, and the capsule says so before the stone exists. Without the pause, a
beginner's commonest way of losing a group they had already won happens in
silence.

## The opponent

**Monte Carlo tree search, not alpha-beta.** Go has no usable hand-written
evaluation function; that is why computer Go was stuck at beginner level for
thirty years. Playing the position out at random a few thousand times and
counting who won needs no knowledge, no opening book and no weights, which is
also why it fits in a device with fifty kilobytes of flash to spare.

**There are two boards, and that is deliberate.** `go::Game` is the game: a
superko ring, dead-stone marks, tallies, 140 bytes copied to answer one
question. The search plays on `Fast`, which has none of that. Two
implementations of one rulebook is exactly the shape that drifts, so
`testTheFastBoardIsTheSameGame` plays **3.6 million positions** through both and
asserts they agree point for point, with the single licensed exception that the
fast board knows simple ko where the game knows superko.

**The playout policy is LOCAL, and that is the whole design.** The first version
asked every chain on the board whether it was in atari, once per move: correct,
and fourteen times slower than a uniform playout. At roughly 113 Elo per
doubling of playouts, that is four ranks handed back to buy one. Go is a local
game and an atari is caused by the move just played, so only the four points
around the last move can have started one. The local version runs at 34,000
playouts a second on a laptop against 58,000 uniform: 1.7x, not 14x.

Measured on the same laptop: Medium's 3,000 playouts is 57ms a move. The
research (below) puts the device at 26x slower, so about 1.5s, and Hard's 8,000
at about 4s. **Those are scaled, not measured on hardware.**

### The three levels are three different players

The measurement that decides this: across the entire playout budget this device
can reach, strength moves about 113 Elo per doubling, so the whole feasible
range is **three and a half ranks and it bottoms out in single-digit kyu**.
Thinking time alone therefore cannot produce a level a beginner can beat.

Handicap can. On nine by nine a stone is worth about three ranks among
single-digit kyu and more below, so two stones is a bigger step than every
doubling this device can afford put together. Handicap is also the only
mechanism that **cannot make the opponent look broken**, because it never plays
a deliberately worse move.

|        | Opening                | Playouts | Blind | Move choice        |
| ------ | ---------------------- | -------- | ----- | ------------------ |
| Easy   | you take Black + 2 stones, komi 0.5 | 1,200 | 60% | most visited |
| Medium | even, komi 7.5         | 3,000    | none  | weighted, floored  |
| Hard   | even, komi 7.5         | 8,000    | none  | most visited       |

- **Easy is blind and spotted.** It searches a random 40% of the board each
  turn and misses things elsewhere, which is what being a beginner actually is:
  a beginner does not weigh a capture and decide against it, they do not see it.
  Every move it plays is still one it thought about, so it never looks insane.
  The exclusion is floored and overridden for a move that takes two stones or
  saves one of its own chains, or "blind" becomes "brain-damaged".
- **Medium is indecisive.** It sees everything and searches properly but picks
  among the moves it looked hardest at, weighted by how hard, with a floor at
  half the top move's visits. A club player not concentrating. The floor is what
  stops "not concentrating" becoming "occasionally insane".
- **Hard is thorough.** Everything on, most visits wins.

**In a handicap game the weaker player takes Black**, so YOU PLAY is not a
choice at Easy. That is what a handicap is; the alternative is placing White
stones and letting Black open, which is not a game anybody plays.

**What was deliberately NOT done, and must not be re-added**: blunder
injection. Making a strong engine occasionally play a move it knows is bad
produces a player who is excellent and then insane, which reads as a fault
rather than as a weaker opponent. So does disabling the playout policy: that
makes the bot alien, not weak. The policy is on at every level.

### It never passes a won game away

The Leela Zero rule: if the search wants to pass, count the board as it stands
with every stone alive. Pass only if passing wins; otherwise play the best move
that is not a pass. This stops the two behaviours that make a Go program look
broken, and it is why the fallbacks in `chooseMove` reach for another legal move
rather than for a pass.

Filling the neutral points is **not** stupid, which is the correction worth
carrying: under area scoring a dame is worth exactly one point. The engine is
collecting points a territory-trained human was taught are worthless. The fix is
not to suppress it but to stop playing once the game is decided, which this rule
does exactly.

## The endgame is an agreement, not a computation

Two passes end play and the board is counted immediately: dead stones are
guessed, territory is shaded, the score is shown with komi. **Nobody is ever
made to fill dame.**

The guess comes from playing the position out a couple of hundred times and
asking who owned each point at the end. That is the strong programs' method and
it reuses machinery that already exists; a hand-written life-and-death analyser
gets seki and bent-four wrong in ways nobody can debug on a device.

Tapping any group flips it, and the whole group flips, never one stone of it.
The score moves as you do it. **PLAY ON** puts the stones back for the player
who passed too early, which is the common beginner mistake. Against the computer
the human's marking is simply accepted: there is no rating to protect, and an
app that argues with you about which of your stones are dead is worse than an
app that is occasionally wrong.

## Multiplayer

`linkplay::LinkActivity`, `GameId::Go = 0x0A01`. The shared state is
`go::Game` itself: 140 bytes against the layer's 192-byte ceiling, asserted in
the suite rather than discovered when a field is added. Whole states travel, so
a lost packet is a stale frame the next one corrects.

The counting phase crosses the wire like any other move: a dead-stone mark is a
state change, so flipping one hands the turn over and the other seat has to look
again and say yes again.

## What is not done

- **No measurement on hardware.** Every playout rate here is a laptop number
  scaled by a published CoreMark ratio, and the spread in that estimate is a
  rank and a half. The first thing to do with a device is time a real move.
- **No 3x3 shape patterns in the playouts, no RAVE, no priors.** The research
  measures these at +512, +250 and +398 Elo respectively on top of what is here,
  and the pattern table is **961 bytes**, not megabytes. This is the single
  biggest improvement available and it is the next work.
- **No resignation.** The engine plays every game to the count.
- **No board coordinates.** The star points are how you read where you are.
