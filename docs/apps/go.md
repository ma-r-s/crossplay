# Go

Nine by nine, thirteen by thirteen or nineteen by nineteen, area scoring, komi
7.5, situational superko. Two people on one device, two devices in a room, or one person against
michi-c2 at three levels.

**Three board sizes.** Nine gives a 49px pitch and a game that finishes on one
train journey; thirteen gives 33px; nineteen gives 23px with an 18px stone,
which is below the fingertip this device is driven with. The larger two are
playable at those pitches because a stone goes down in two taps and the first
one can be moved. Nine is the default. Nineteen was left out at first for that
pitch, and offered from 2026-10-03 because it is the board real Go is played on
(GitHub #282; Mario: "we should allow 19x19, at least allow it"). Twenty-three
is the widest pitch nineteen allows: at 24 the pad inside the square drops to
eight pixels, under the stone's radius, and the edge row touches the frame.

All three boards occupy the **same 448px square**, and that is what keeps the
seat bands, the buttons and the frame in one place across them: only the pitch
and the pad inside the square change.

**A point is two bytes (`go::Point`).** Nineteen by nineteen has 361 points
and a byte numbers 256, so the ko, the last move, every flood fill's stack and
every neighbour list hold a `Point`, never a `uint8_t`. The suite plays past
the 256th point on purpose (`testKoPastTheByteIsTheRightPoint`: in a byte the
ko at 325 was 69, which forbade an empty point at the top of the board and
allowed the immediate recapture).

## The files

The shape every game here uses. The first four are freestanding and
host-tested; only the activity needs hardware.

| File            | Holds                                                     |
| --------------- | --------------------------------------------------------- |
| `GoCore.h/.cpp` | the rules, the position, scoring. No renderer, no heap.   |
| `GoFlow.h`      | two state machines, the aim, the cautions                 |
| `GoEngine.h/.cpp` | playouts, the pass rule, dead-stone guessing               |
| `GoMichi.h/.cpp` | the bridge to michi-c2, the level ladder, when to pass      |
| `michi/`        | vendored michi-c2, plain C, behind `MichiBridge.h`          |
| `GoSave.h/.cpp` | what is written to the card, and what a bad file costs    |
| `GoScreens.h/.cpp` | every screen, as free functions over plain models      |
| `GoActivity.h/.cpp` | renderer, input, shelf, link, storage                 |

`host-tests/go/run.sh` runs the lot on a laptop.

## The front door has three doors

PLAY, PLAY NEARBY, SETTINGS, and the board in the middle. Everything
configurable is behind the third door: eight rows on a front door, five of them
settings, is a settings screen with a PLAY button on it.

**The board in the middle is the game you are IN**, drawn small, with the move
number under it. It falls back to the last finished game when there is none, and
to nothing at all on a device that has never played. It showed only finished
games first, which meant the screen a player reaches their half-played game
through was the one screen that did not show it.

**RESUME is a split row.** The row resumes; a square button on its end with a
trash mark throws the game away. It is deliberately not confirmed: what is being
discarded is drawn on the same screen, directly above the finger, which is a
better guard than a dialog nobody reads. It is absent entirely when there is
nothing to discard, so a destructive control never exists to be tapped by
mistake. The square is drawn AFTER the list so it wins the hit test, which runs
backwards through the interaction table; the row underneath it stays registered
at full width.

### The five settings

| Row | Values | Notes |
| --- | --- | --- |
| OPPONENT | COMPUTER / 2 PLAYERS | who holds the other seat |
| LEVEL | EASY / MEDIUM / HARD | how hard the machine thinks, and nothing else |
| HANDICAP | NONE / 2..5 STONES | stones spotted to the player, komi 0.5 |
| YOU PLAY | BLACK / WHITE | dim while a handicap is set: a handicap is Black's |
| BOARD | 9x9 / 13x13 / 19x19 | applies to the next NEW game |

**The level is strength alone.** It used to carry the opening as well, so EASY
meant "a weaker opponent AND two free stones" and neither half could be had
without the other. Those are three separate decisions and they are three
separate rows now. Changing any of them drops a game in progress, because most
of them cannot be applied to a position already under way and a list where one
row keeps your game and four throw it away is a list nobody can predict.

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

`moveLimit(size)` is five times the board -- 405 moves on nine, 845 on thirteen,
1805 on nineteen -- and it is a **[house rule]**. Chinese rules with full superko terminate on
their own, but the ring in `Game` remembers eight positions rather than every
one, so a long enough cycle is not forbidden. An opponent that refuses to pass
while losing (which is correct, below) will happily play into one, and a
self-play game ran past four hundred moves during testing. A real game is forty
to a hundred and fifty.

**The board is two bits a point.** A hundred and sixty nine points at a byte
each does not fit the link layer's 192-byte packet beside the superko ring and
the tallies; packed, the whole game is 116 bytes. That is why nothing indexes
`game.point[]` any more: it is `game.at(p)` and `game.put(p, c)`, and the
compiler finds every site that forgot. `game.size` is the board and every loop
bounds itself with `game.points()` rather than with the array length, because a
nine by nine game leaves the tail of every array untouched and a loop that reads
it sees stones that are not there.

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
not: the pitch is 49px on nine, 33px on thirteen and 23px on nineteen, which is
at or under a fingertip, and a stone cannot be taken back in a match. It is also
what makes the larger boards offerable at all.

The pause is also the only place a warning can live. `go::cautionFor` returns
`FillsOwnEye` or `SelfAtari` for a move that is legal and almost certainly a
mistake, and the capsule says so before the stone exists. Without the pause, a
beginner's commonest way of losing a group they had already won happens in
silence.

## The opponent is michi-c2

**Vendored, not written.** `src/apps_local/go/michi/` is Denis Blumstein's
michi-c2, a C recoding of Petr Baudis's michi, under MIT. The research said port
it; the first version of this app did not, on a flash budget that turned out to
be wrong, and wrote its own Monte Carlo tree search instead. That engine was two
or three stones weaker and Mario said so after one game on hardware.

What crosses the boundary is six functions over integers and byte arrays
(`MichiBridge.h`). michi's headers do **not** compile as C++ -- they do
arithmetic on enums and return string literals as `char*` -- so everything on
its side of that header is C and everything on this side is C++. Patching four
thousand lines of somebody else's engine to satisfy a compiler it was never
written for is a sync nobody wants to do twice.

**Every fork change is marked `FORK CHANGE:` in the source**, which is the list
a sync greps for rather than a count to keep in step. Some are ports to a
machine michi was not written for; the rest are bugs only a build like this one
reaches:

- `N` is the compile-time MAXIMUM, 19 (upstream's value; it was 13 until 19x19
  was offered); the size actually played is `pos->size`, so one build serves
  every board and a nine by nine game sits in a corner of the larger array.
- `log_fmt_s` tolerates a null sink. michi logs through a `FILE*` that `ui.c`
  opens, and `ui.c` is not vendored.
- Every allocation goes through `michi_malloc`/`michi_calloc`, and on ESP32
  those are `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` falling back to internal
  RAM. **The search tree lives in PSRAM**: it is hundreds of kilobytes at these
  simulation counts, internal SRAM does not have that to spare, and the tree is
  the least cache-sensitive thing in the app. The 3x3 pattern table stays where
  it was. `GoActivity::onExit` frees the tree, or it sits there for the rest of
  the boot while somebody reads a book.
- `mark1`, `mark2` and `buf` are **extern** in `michi.c`. Upstream defines each
  of them twice, which links only under `-fcommon`; GCC 10 turned that off, so
  both the host suite's compiler and the Xtensa one reject the duplicates.
- **No `<x86intrin.h>`.** `non_portable.h` includes it on any GCC that is not
  Apple's, and the include is vestigial: the two functions under it are
  compiler builtins. No Xtensa build can find that header.
- **`expand()` allocates one more child slot.** The array is NULL-terminated
  and `free_tree()` walks to the NULL, but the block that adds a PASS child
  when a node has two or fewer children writes it into the terminator's slot
  when every candidate was legal -- so the walk runs off the end and frees
  whatever it reads. Upstream never meets it because `genmove` passes out of a
  decided game before the board is down to two points; this fork deliberately
  does not, so it is the last two moves of nearly every game.
- **`mcplayout()` draws its random start from the board, not from `1..N`.**
  The playout picks a point and walks forward from it, so an off-board start
  still finds a move -- but it funnels every start in the border into the same
  few entry points. With N=13 playing nine by nine more than half of all starts
  are in the border, and the playout's random move stops being uniform, which is
  most of what a playout is. Upstream cannot meet this because it builds `N` to
  the size it plays.
- **`tree_search()` takes a deadline** and checks it once per simulation, so the
  move can end on a wall clock without the count it reasons about changing. See
  the budget section above for what the alternative cost.
- **`line_height()` subtracts `N - size`.** `empty_position()` lays a board of
  `size` out at array rows `N-size+1..N`, not `1..size`, so on a build where N
  is the maximum rather than the board being played the arithmetic read every
  row wrong. On the default nine by nine board it returned the first-line
  penalty for tengen and no penalty at all for the real first line, which
  inverts michi's opening priors.
- **`print_board()` tolerates a null sink**, which `michi_assert`'s failure
  path hands it. A crash inside the code that reports a crash reports nothing.

**Two things michi does are NOT used, and both are measurements rather than
preferences.**

- **`compute_all_status` faults on a nearly full board.** Reproduced in forty
  lines of plain C with no C++ anywhere near it: five empty points is fine,
  three is a segmentation fault. A counting screen is always a nearly full board
  -- that is what counting is -- so the one position this app would ask the
  question in is the one position michi cannot answer it in. Dead stones are
  `goengine::estimateDead` instead, which is tested and next door. The function
  that would have called it is deleted rather than left in the bridge.
- **`is_better_to_pass` is never called**, because it calls the above. It could
  not fire anyway: it wants the opponent's last move to have been a pass, and
  the position handed to michi records no moves at all -- the stones are PLACED,
  not played, because the position is already the result of every capture and ko
  in the game.

**The engine is told what it cannot see.** The bridge builds michi's position
by PLACING stones rather than playing them, which is right -- the position
handed over is already the result of every capture and ko in the game -- but
it means three things do not come with it, and `board_place_stone` quietly
leaves all three wrong:

- **the ko point.** Left at zero, michi sees the recapture as an ordinary
  capture of a stone in atari, which is usually the highest-value point on the
  board. The rules then refuse the move it offers.
- **the last move.** `board_place_stone` is `play_move` with the counter wound
  back, so after the loop `pos->last` is the bottom-right-most stone on the
  board. Every local heuristic in the playout, and the "play near the last
  move" prior at the root, then aim at the wrong part of the board.
- **the move count**, which is what makes a playout inheriting a pass start as
  though the opponent had just passed.

`gomichi::lastContext()` reads all three back out of michi's own position, so
"the engine was told" is a fact the suite asserts rather than something
inferred from the move that came back. That distinction is the whole test: the
app's fallbacks hold the guarantee -- never the ko, never a pass, always legal
-- whether or not anything was transferred, so a test watching only the move
passes with the ko dropped entirely.

**A refused move is answered with the next choice, never a pass.**
`chooseMove` asks the bridge for the search's ranked moves and takes the first
one our rules allow, then any legal move that is not filling our own eye. The
version that passed instead threw a move away in the middle of a ko fight, and
if the human passed back it ended the game.

**Passing is the app's decision, not the engine's.** michi will not pass while
there is a point left to take, and it is right not to: under area scoring a
neutral point is worth one. To a person it reads as the machine not knowing the
game is over. So `gomichi::chooseMove` applies the Leela Zero rule itself, and
`michi_bridge_genmove` never returns a pass -- when the search likes one it
hands back its best non-pass child instead, which is what michi's own
`best_move(tree, except)` is for. Without that, a search at sixty simulations
ended games this app was winning.

**The clock is inside the search, not around it.** Upstream's `genmove` runs its
whole simulation count in one call with no way in or out, which is fine for a
program with a GTP time control and wrong for a panel somebody is holding. The
fork gives `tree_search` a deadline it checks once per simulation
(`michi_set_deadline` in `michi.c`), so the move ends on time with at most one
simulation of overshoot and michi still reasons about the count it was asked
for.

It used to slice the search into a growing series of small `tree_search` calls
and read the clock between them, and that was a real defect rather than a
stylistic one. **Both** of `tree_search`'s early stops compare the simulations
done against the count *that call* was handed: a slice of eight is "twenty
percent read" after two simulations and stops itself there. The old code carried
a comment saying the early stops became relative to the chunk and that this
meant "it stops sooner, never wrong". Sooner was a fifth of the search.

Measured by running the engine against a clock scaled to a part twenty-six times
slower than the laptop, which is the only condition where the budget binds at
all. Six seeds a row, 13x13 Hard:

| | sliced | deadline |
| --- | --- | --- |
| playouts a move | 766-830 | 885-1,065 |
| worst single move | 2.7s | 3.1s |

The worst move goes UP, because the search now spends the budget it was given.
It stays inside the 4.0s budget and well inside the five second ceiling.

**Head to head under that clock the deadline build wins 75-45** over 120 games,
with GNU Go counting and playing neither side. Against GNU Go on the laptop's
own clock, where the budget never binds and only the first slice can hurt, it is
36% against 29%.

What is NOT safe is reimplementing `genmove`'s preamble. An earlier version did,
missed part of it, and produced a tree in which PASS won every playout and every
real move lost every one: the search was running on a position michi did not
consider set up. The preamble in the bridge is `genmove`'s, line for line, and
only the loop is the fork's.

**`init_large_board()` is called at init** even though large patterns are off.
`expand()` calls `copy_to_large_board()` unconditionally, and with the
coordinate map left zeroed that copy writes every point to `large_board[0]` and
trips its own assert. Upstream initialises it inside `init_large_patterns()`,
the function that loads two multi-megabyte pattern files this fork does not
vendor.

### The three levels are one knob

|        | Simulations | Budget | What it is |
| ------ | ----------- | ------ | ---------- |
| Easy   | 60          | 1.2s   | loses to GNU Go 3.8 `--level 1` essentially always |
| Medium | 500         | 2.5s   | 36% against it, level with michi-c2's own build |
| Hard   | 1,500       | 4.0s   | 69% against it |

One knob, because the other three -- handicap, komi and colour -- are the
player's rows now. A level that silently spotted stones made EASY mean two
things at once and neither could be adjusted.

**Whichever binds first wins.** The count keeps the host tests deterministic:
they lend no clock at all, so a result does not depend on how fast the machine
running them happens to be. The clock keeps the device under the five seconds
Mario set, on both boards -- and thirteen by thirteen is where it matters, since
the same simulation costs roughly twice as much there.

**What was deliberately NOT done, and must not be re-added**: blunder injection.
Making a strong engine occasionally play a move it knows is bad produces a
player who is excellent and then insane, which reads as a fault rather than as a
weaker opponent. So does disabling the playout policy: that makes the bot alien,
not weak.

### It never passes a won game away

The Leela Zero rule, and it has **two halves**:

- the opponent has passed, **and**
- passing wins the board as it stands with every stone alive.

Both are load-bearing. With only the second, White passes at move two of every
game: one black stone on an empty board surrounds the whole board under area
scoring, so "passing wins" is true for Black before anything has happened. The
third case is a board with nothing left but one's own eyes, where passing is the
only move whoever is ahead, and `hasUsefulMove` answers it.

Filling the neutral points is **not** stupid, which is the correction worth
carrying: under area scoring a dame is worth exactly one point. The engine is
collecting points a territory-trained human was taught are worthless. The fix is
not to suppress it but to stop playing once the game is decided, which this rule
does exactly.

`RESIGN_THRES` is set to 0, so michi never resigns. It would have to be reported
as a pass, which hands the opponent a free move every turn for the rest of a
lost game. Losing games are played out; when a game is over is the app's call.

## The endgame is an agreement, not a computation

Two passes end play and the board is counted immediately: dead stones are
guessed, territory is shaded, the score is shown with komi. **Nobody is ever
made to fill dame.**

The guess comes from playing the position out a couple of hundred times and
asking who owned each point at the end -- the OWNER map, not the stones, because
a dead group is captured during the playout and the points it stood on end
empty. That is the strong programs' method; a hand-written life-and-death
analyser
gets seki and bent-four wrong in ways nobody can debug on a device.

**There are two boards here, and that is deliberate.** `go::Game` is the game:
a superko ring, dead-stone marks, tallies, a packed position, and a whole board
copied to answer one question. `GoEngine`'s `Fast` has none of that, because a
playout plays a hundred moves and the estimator plays two hundred playouts. Two
implementations of one rulebook is exactly the shape that drifts, so
`testTheFastBoardIsTheSameGame` plays **over a million positions** through both
and asserts they agree point for point, printing the count it reached, with the
single licensed exception that the fast board knows simple ko where the game
knows superko.

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

Nine by nine, area scoring, komi 7.5, against GNU Go 3.8 at `--level 1`, 300
games a level, both engines seeded per game:

|                                  | wins |
| -------------------------------- | ---- |
| Easy, 60 simulations             | 0.3% |
| Medium, 500                      | 36%  |
| Hard, 1,500                      | 69%  |
| michi-c2's own build at N=9, 500 | 34%  |

The last row is the control. This ladder was measured with michi compiled for a
13x13 maximum and playing 9x9 inside it, and the row says that cost nothing,
which it did not always. Since 19x19 was offered michi is compiled for a 19x19
maximum, upstream's own value, and **the ladder has not been re-run at it**.

**What is not known is how any of this maps to a person.** Nobody has played
this build against a human of known rank. The rungs are ordered and well
separated, and that is the whole of the claim. An earlier version of this file
said Medium was level with GNU Go at `--level 10` and that 1,500 was "comfortably
above it"; those came from michi-c2's own published ladder rather than from
anything measured here, and the matches that appeared to confirm them could not
have failed. They are gone.

**Four traps in measuring this, each of which cost a wrong conclusion first:**

- **Seed BOTH engines, per game, or the match size is a fiction.** michi sets
  its generator to 1 in every process, so our engine replayed one game per
  opening; GNU Go varies little at `--level 1`. A 300-game match was six to
  twelve distinct games repeated, and two builds of *identical* code scored 42%
  and 53% on it. Every strength number taken before that was found is worthless,
  including several that were acted on. The harness sends
  `param_general random_seed <n>` to our side and `--seed <n>` to GNU Go, and
  prints how many distinct results a match produced; fewer than about thirty in
  three hundred means the seeding is not working.
- **A 24-game match cannot tell 37% from 56%.** Both of those are the same
  engine against the same opponent, measured twice. The interval on 24 games is
  about twenty points wide, which is wider than every change worth making. Do
  not quote a number from fewer than about sixty games, and do not act on one.
- **Both engines against a third is a blunt instrument for "is A better than
  B".** It answers "is either better than GNU Go". Play A against B directly,
  with a GNU Go that played neither side as the counter. The deadline change
  read three ways: 7 points through GNU Go, 55% head to head on the laptop's own
  clock, and 62% head to head under the device's. Only the last is the condition
  the device is in, and it is the only one of the three that is clear of its own
  error bar.
- **Homebrew's `gnugo` crashes on `genmove` at every level on arm64.** It
  answers `boardsize` and `clear_board` happily and then dies silently, so a
  match reports every game as an error rather than as a crash. GNU Go assumes a
  signed `char`; building it with `-fsigned-char` fixes it, and that trap
  belongs to this whole generation of 2000s C.

**And one change that measured much worse and was reverted**, from the engine
that came before this one: a prior favouring the middle of the board and
penalising the first two lines. It looked obviously right, it fixed a visibly
bad opening move, and it took that engine from 45% to 4% against `--level 1`. On
nine by nine the edge is where the endgame is decided. Two changes went in
together and only the pair was measured, which is the other half of the lesson.

### The move time is logged, on the device

`LOG_INF("GO", "search: level %d, %ux%u, %u ms of %u, %d of %u sims ...")` after
every move. It is the one line that turns the budget from a promise into a
measurement, and it is also the line somebody needs if a move ever takes long
enough to trip the watchdog. Read it with `tools_local/device/drive.py --ip`.

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

- **No 13x13 or 19x19 strength measurement.** The simulation counts come from
  michi-c2's nine by nine ladder. The larger boards are bigger boards for the
  same search, so every level is weaker there in a way nobody here has
  quantified, nineteen most of all: the clock, not the count, is what binds,
  and a 19x19 playout is about four times a 9x9 one. Expect Hard on nineteen to
  be a beginner's opponent.
- **19x19 has not run on hardware.** michi's position is about 16KB at N=19
  (5.7KB at 13), so the ladder reader's position stack, one slot per level the
  depth cap allows (13), is about 200KB of PSRAM; it was 128 deep, about 2MB,
  until review pointed out that twelve slots are the most ever in use.
  The search task's stack IS measured (`scripts_local/stack-budget.sh`, then
  the ladder recursion summed by hand at `MICHI_LADDER_MAX` in michi.c): 26,848
  of 32,768 bytes worst case. Raising N from 13 to 19 first took it to about
  46KB, because upstream copies a whole position into `fix_atari()`'s frame
  under the recursion; that copy and `expand()`'s board-sized arrays now live
  in PSRAM scratch blocks. That applies to every board size, since N is
  compile-time. Measured on DEVICE 2 (2026-10-04, the per-move `search:` log
  line carries the stack left): 25.5KB used by move 99 of a 13x13 game the
  machine was losing at Hard.
- **Where the search task's stack lives.** Internal RAM when a contiguous
  32KB block exists, otherwise PSRAM (`xTaskCreatePinnedToCoreWithCaps`). On
  DEVICE 2 after the reader and Wi-Fi there was never such a block, and the
  search used to fall back to the 24KB loop task, which the 25.5KB above does
  not fit: that was GitHub #279, a crash only when the player was winning,
  because a losing machine runs every simulation and reads the most ladders.
  The loop task is now the last resort, logged as an error. The PSRAM stack
  costs no measurable speed (about 170 simulations a move at 13x13 Hard
  either way).
- **Hard on the chip is far below its count.** 1,500 simulations is the
  ladder's number; on DEVICE 2 a 13x13 Hard move gets about 170 in its four
  seconds, and 76 late in a game. The laptop-times-twenty-six estimate the
  budgets were set from is off by several times, so every level plays weaker
  on the device than the ladder above says.
- The timing of a 19x19 move on the chip is unmeasured. The counting screen's dead-stone guess runs
  unclocked on the loop task: 29ms on the laptop at nineteen, so roughly three
  quarters of a second on the chip.
- **A match between two devices set to different boards** settles on the first
  seat's size as soon as its first move arrives. The size crosses the wire
  inside the game, and the second seat cannot place anything before that move,
  so nothing is ever misplaced -- but its empty board does visibly change size
  once.
- **michi aborts on a failed allocation.** `michi_malloc` calls `exit()`, which
  is upstream's behaviour and was not patched. With 8MB of PSRAM and a tree of a
  few hundred kilobytes it is not a path this app can reach, but it is a path.
- **No resignation.** The engine plays every game to the count, deliberately.
- **No board coordinates.** The star points are how you read where you are.
- **The large 3x3-plus pattern files are not vendored.** They are several
  megabytes; `large_patterns_loaded` stays 0 and michi falls back to its
  built-in 3x3 set, which is what the pattern priors here are.
