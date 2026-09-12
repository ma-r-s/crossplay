# Go

Nine by nine, area scoring, komi 7.5, situational superko. Two people on one
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

**Area scoring (Chinese), situational superko, komi 7.5 in half points.** Every
engine plays this because a finished position is scorable by counting alone: no
prisoners to remember, no dame to haggle over, no seki exception.

Komi is **7.5 and not 7.0**, and the half point is load-bearing rather than
traditional. On an odd board a flat 7 ties on a 44/37 split, which is an
ordinary result; an odd number of half points cannot. **This game therefore has
no draw and needs no draw screen**, and `settlesEveryGame()` holds every komi
the level ladder can set to that promise. The first version had 7.0 and the
suite found the tie.

`kMoveLimit` is 400 moves, and it is a **[house rule]**. Chinese rules with full
superko terminate on their own, but the ring in `Game` remembers
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
superko ring, dead-stone marks, tallies, and a whole board copied to answer one
question. The search plays on `Fast`, which has none of that. Two
implementations of one rulebook is exactly the shape that drifts, so
`testTheFastBoardIsTheSameGame` plays **over a million positions** through both
and asserts they agree point for point, printing the count it reached, with the single licensed exception that the
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

## Agreeing the count is TWO agreements

Both seats have to agree which stones are dead, and then both have to agree
they are finished. `Game::accepted` is a bit a colour and it lives in the
**game**, not in the activity, because it has to cross the wire: an agreement
held only on the device that made it is not an agreement.

The first version kept it in the activity, and one seat pressing ACCEPT ended
the match for both while the button it pressed relabelled itself to WAITING.
The screen promised a negotiation the code did not hold. Changing any mark
withdraws both agreements, because a count that moved is a count nobody has
read.

Solo there is nobody to wait for, and two people sharing one device are sitting
together and can say so out loud, so one tap settles it in both of those.

## Multiplayer

`linkplay::LinkActivity`, `GameId::Go = 0x0A01`. The shared state is
`go::Game` itself, comfortably inside the layer's 192-byte ceiling, which the
suite asserts. The exact size is deliberately written down nowhere: it was, as
140, and adding one byte for `accepted` made four copies of that number wrong at
once. Whole states travel, so
a lost packet is a stale frame the next one corrects.

The counting phase crosses the wire like any other move: a dead-stone mark is a
state change, so flipping one hands the turn over and the other seat has to look
again and say yes again.

## How strong it actually is

Measured, on a laptop, alternating colours, Tromp-Taylor scored by a GTP
referee. Hard, which is 8,000 playouts a move.

| Opponent | Games | Won | Elo |
| --- | --- | --- | --- |
| GNU Go 3.8 `--level 1` | 64 | 29 | -33 |
| GNU Go 3.8 `--level 10` | 64 | 5 | -429 |

So: level with GNU Go at its lowest setting, and well below its highest. For
scale, michi-c2 at **500** playouts is level with `--level 10`, so there are two
or three stones still on the table and they are the knowledge this engine does
not have rather than search it cannot afford.

**Three traps in measuring this, all of which cost a wrong conclusion first:**

- **A 24-game match cannot tell 37% from 56%.** Both of those are the same
  engine against the same opponent, measured twice. The interval on 24 games is
  about twenty points wide, which is wider than every change worth making. Do
  not quote a number from fewer than about sixty games, and do not act on one.
- **GNU Go 3.8 is DETERMINISTIC.** Playing it against itself at two levels
  produces the same game every time: a 48-game match between `--level 1` and
  `--level 10` is two distinct games played twenty-four times each, and its
  confidence interval is a fiction. It is a valid opponent for a randomised
  engine and a useless one for itself.
- **Homebrew's `gnugo` crashes on `genmove` at every level on arm64.** It
  answers `boardsize` and `clear_board` happily and then dies silently, so a
  match reports every game as an error rather than as a crash. GNU Go assumes a
  signed `char`; building it with `-fsigned-char` fixes it, and that trap
  belongs to this whole generation of 2000s C.

**And one change that measured much worse and was reverted**: a prior favouring
the middle of the board and penalising the first two lines. It looked obviously
right, it fixed a visibly bad opening move, and it took the engine from 45% to
4% against `--level 1`. On nine by nine the edge is where the endgame is
decided, and telling the search to ignore it permanently is fatal. Two changes
went in together and only the pair was measured, which is the other half of the
lesson.

## Six things a cold reviewer found

Written down because each is a class rather than an incident, and this repo has
seen every one of them before.

- **A round-trip test that names fields by hand cannot see a field nobody
  wrote.** `GoSave` gained `komiHalves` and `handicap` when the level ladder did
  and `pack()` did not, so every resumed game scored with komi 0. The test named
  fourteen fields and omitted exactly those two. It compares the whole struct
  now, which cannot rot the same way.
- **"Which stone is on this point" is not "who owns this point".** The
  dead-stone guess asked the first and a captured group leaves its points EMPTY,
  so a lone dead stone was never marked and a dead pair was marked half.
- **A match must put the solo game back.** `onMatchStart` resets the board over
  it, so leaving a match without reloading left the front door offering RESUME
  for a game that had been overwritten.
- **Every screen that sends has a turn**, not just the board. The counting
  screen took taps from the seat that could not send them and dropped the
  refusal.
- **A `sizeof` written into prose is a number that rots.** 140 was in four
  files; one added byte falsified all four. It is written down nowhere now.
- **This is situational superko, not positional.** `positionKey` mixes the side
  to move. That is the AGA's rule and it errs toward permissiveness, so no legal
  move is refused -- but it was labelled wrong in four places.

And a note on the test that caught the second one: getting its POSITION right
took three attempts. The first put the dead group on an empty board, where
whether it lives is genuinely open. The second filled the rest with black and
put black's own eighty-stone group in atari, so white answered by capturing the
entire board: the playouts were right and the fixture was wrong.

## What is not done

- **No measurement on hardware.** Every playout rate here is a laptop number
  scaled by a published CoreMark ratio, and the spread in that estimate is a
  rank and a half. The first thing to do with a device is time a real move.
- **Two or three stones short of michi-c2**, which is the engine the research
  recommended porting. It was not ported because it is 33 to 45KB of flash
  against roughly 50KB spare on the oldest partition table in the field, where
  this one is twelve. If the gap matters more than the bytes, that port is the
  fallback and it is a measured one.
- **No 3x3 shape patterns in the playouts, no RAVE, no priors.** The research
  measures these at +512, +250 and +398 Elo respectively on top of what is here,
  and the pattern table is **961 bytes**, not megabytes. This is the single
  biggest improvement available and it is the next work.
- **No resignation.** The engine plays every game to the count.
- **No board coordinates.** The star points are how you read where you are.
