# Connect Four

The rules, exactly, and the two state machines the app is built on. Read this
before touching `src/apps_local/connectfour/`.

---

## 1. The rules

Seven columns, six rows, forty-two places. Drop a disc into a column and it
falls to the lowest free place in it. Four of yours in a line, in any of the
four directions, wins. A full board with no line is a draw.

That is the whole game. There is no [house] section, because there is nothing
the standard rules leave for a handheld to decide: no draw-by-repetition
question (the board only ever fills), no time control, no arbitration.

### The board is stored bottom-up

`cell[column][row]`, **row 0 at the bottom**, because that is the direction
gravity works in. `landingRow` is then a scan from zero, and the screen does the
flip in one line (`drawRow = kRows - 1 - row`).

`testRowZeroIsDrawnAtTheBottom` exists because if that flip ever inverts,
discs pile downward from the ceiling and nothing else in the app notices: every
rule, every test and every packet still agrees with itself.

### Nothing can float

The one property built in rather than checked for. A caller names a **column**,
never a row; the row is computed by `landingRow` and there is no other way in.
A floating disc is not a bug that is prevented, it is a state that cannot be
expressed.

`plausible()` re-checks it anyway on anything arriving over the wire, because a
corrupt packet does not go through `drop()`.

### Light opens

And that matters more than it looks. Connect Four is **solved**: the first
player wins with perfect play. Randomising who opens would not make the game
fair, it would only hide the asymmetry from whoever lost. So light always opens
and `goesFirst()` decides who is light.

---

## 2. The game state machine

`connectfour::Phase`, in `ConnectFourFlow.h`. Three states, none of them stored:

```
Yours ---- you drop ----> Theirs
Theirs --- they drop ---> Yours
either --- four, or a full board ---> Settled
```

`phaseFor(over, yourTurn)` derives it. There is no `phase_` member, so there is
nothing that can disagree with the board.

**A win keeps the turn.** `drop()` settles the outcome _before_ handing the turn
over, so `game.turn` still names the winner when the game ends. Everything reads
"whose" from that field, and a winner that passed the turn on would announce the
wrong name on the result screen.

---

## 3. Multiplayer

`GameId::ConnectFour`. The wire state is the `Game` struct itself: 46 bytes,
trivially copyable, well inside the 192-byte cap. The whole board travels on
every drop rather than the column, because it is smaller than a delta protocol's
worth of code and it cannot desynchronise.

**Both sides call `start()`.** There is no randomness, so the opening board is
empty on any device and there is nothing to wait for. This is not a free choice:
Checkers shipped with only the leader starting, and the follower's zeroed board
read as a _finished game_, announcing a winner before the first move. An empty
Connect Four board is a legal position, so the same mistake would be quieter
here and no less wrong: the two devices would disagree about `lastColumn` until
the first packet arrived, and `lastColumn` is what the board points at.

---

## 4. The opponent

`ConnectFourBrain.h`. Negamax with alpha-beta, seven plies deep, deterministic
and pure.

Connect Four has no hidden information, so "it cannot cheat by looking at what
you hold" is free here in a way it was not in Jaipur or Knucklebones. What is
not free is the other half of that promise.

- **It only ever plays a legal column.** Its choice comes from the same
  `canDrop` the board uses.
- **It is deterministic.** Same position, same column, and it does not touch the
  board while thinking. Every screenshot recipe in this repo replays a game by
  repeating taps, so a brain that rolled a die would make the site
  unreproducible.
- **It takes a win and blocks a loss**, tested from both colours so neither is
  an artefact of the other.
- **It does not hand over a win** it could have refused, which is the first
  thing a purely material evaluation gets wrong.
- **It beats a random mover 40-0 of 40**, with the sides alternating so the
  first-player advantage is not doing the work.

The last one is the test that matters, and the reason it is stated as
`randomWins == 0` rather than "more than half": Checkers had a brain whose
evaluation had been negated, and _legal_ and _deterministic_ both stayed green.

Three details worth keeping:

- **Deeper wins score lower** (`kWinScore + depth`), so a mate in one beats a
  mate in five and a loss is postponed as long as possible. Without it, every
  winning line scores the same and the brain finds a win and then wanders.
- **`kLinesThrough`** is the number of possible fours passing through each
  column: `{3, 4, 5, 7, 5, 4, 3}`. The centre preference is a fact about the
  board rather than a hand-tuned bonus, and it is also what breaks ties.
- **`kSearchOrder` is middle-outward** purely so alpha-beta prunes more. It does
  not change which column is chosen, because ties are broken explicitly.

---

## 5. The menu state machine

```
Menu ----> Board ----> Result
  |          |            |
  +-> HowTo  +--- Back ---+---> Menu
```

`back()` is an exhaustive switch with no `default`. `leavesApp()` is true only
for `Menu`: the first Back means stop playing, the second means leave.

### The lip

A shallow channel across the top of the board, inside the frame and on the same
dithered slab, holding the disc you are about to drop.

It exists because a Connect Four board has no "here" until a disc lands. The
grid is the history; the lip is the move. It also makes the column read as the
target, which it is: `columnAt` covers lip and grid alike, top to bottom, so
each column is one 64px strip about 480px tall. That is the most forgiving
target this panel can offer, and aiming at a single cell would be the only hard
tap in the game for nothing in return, because the row a disc lands on is not
the player's to choose.

It took two goes to get right, and both failures were invisible in source:

- **On white paper above the board it did not read at all.** A light disc is a
  white fill with a black rim, so on paper it is a rim around nothing, which is
  exactly what an empty hole looks like. Seven of them above a board of holes
  read as one more row of holes. Inside the frame, on the slab, a waiting disc
  reads as the same piece it is about to become. That is also physically what a
  Connect Four frame is: you drop from the top of the object.
- **At full cell height it was a row.** Pixel-identical to a row of played light
  pieces, with a 3px rule as the only separator, which fails precisely when it
  matters because the top row will one day be full of exactly those pieces. So
  the lip is 42px deep with 16px discs, against 64px cells and 26px discs. The
  rule stays.

**A slot you cannot use is DIMMED, not removed and not reshaped.** It is a
dithered disc of the same radius in the same place: dither is this fork's
disabled treatment, so the slot keeps its size and its place and loses only its
solidity.

Both halves of that took a bug to learn. It first showed **plain slab**, on the
argument that a full column is visibly full to the brim -- but a control that
disappears takes its space with it, and six rings and a gap in a row of seven is
not something anyone notices. Then it showed a dithered **square**, which is a
change of shape rather than of state. That one is only occasionally visible on
your own turn, where at most a column or two is full at a time; on the
opponent's turn NO column has its waiting bit set, so all seven changed at once
and the lip read as a mode change rather than a state. Mario, card 247: "I dont
get the change of the top row on connect 4 from circles to some kind of squares
when the cpu is thinking, looks weird."

**On their turn every slot is dimmed.** It says "not yours" with the same ink
the capsule spends words on, and the row is still a row of discs.

### Empty places are drawn

Every one of the forty-two, as a white disc punched into the dithered slab -- a
hole, not an outline. A
Connect Four board is a thing with holes in it; a grid that only appears where
discs have landed is a different object.

The slab also does the work that ring weights could not. Three states, three
textures:

| state | drawn as                       |
| ----- | ------------------------------ |
| empty | white disc on dithered ground  |
| light | white disc with a 4px black rim |
| dark  | solid black                    |

The first version distinguished them by ring weight instead: 2px empty, 4px
light, solid dark. Nothing at 220ppi tells a 2px ring from a 4px one.

### The last disc is ringed

Inside the disc, not around the cell. Forty-two identical discs do not say what
just changed, and "what did they do" is the first thing a player asks on their
turn. Inside rather than around so it cannot be confused with the winning-line
mark, which is corner brackets.

### The result screen is the board

Not a score. Connect Four has no score, and the question at the end is _where
was it_: which four, and how long had it been sitting there. So the result
screen draws the final position with the winning four bracketed, and the four
cells it marks come from the rules' own `winningLine` rather than from a second
search that could disagree.

The grid sits higher there than on the board screen, because there is no slot
row to reserve room for and the screen has gained two buttons. The board moving
is right: the header, the furniture and the marks all changed, so it is a
different screen rather than the same one twitching.

### Geometry is one pure function

`boardGridTop(device)` takes the device and nothing else. `cellRect`, `slotRect`,
`columnAt` and the drawing all call it, so a tap lands on the column the player
is looking at **by construction** rather than by two pieces of arithmetic
happening to agree. Deriving it from the laid-out body rect instead would give
the hit-test a different answer from the drawing the moment either ran at a
different point in the layout.
