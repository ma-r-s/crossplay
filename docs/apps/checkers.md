# Checkers

The rules, exactly, and the two state machines the app is built on. Read this
before touching `src/apps_local/checkers/`.

The rules are English draughts (American checkers) as the World Checkers
Draughts Federation states them. Where that body is silent about something a
handheld has to decide, the gap is marked **[house]** and says what we chose and
why.

---

## 1. The rules

### The board

Eight by eight, thirty-two playable squares, played on the dark ones only. Each
side starts with twelve men on the three rows nearest them. Light moves first.

`kSize = 8`, `kCells = 64`, `kMen = 12`. The board is stored as all sixty-four
squares even though half can never be occupied, because index arithmetic that
matches the drawing is worth more than the thirty-two bytes: `squareRect` and
`squareAt` are exact inverses of each other, and a numbering that skipped the
light squares would make that pair the only interesting code in the file.

### Men

A man steps one square diagonally **forward**, and jumps an adjacent enemy
diagonally forward into the empty square beyond it. Never backwards, either way.

### Kings

A man that ends its move on the far row is crowned. A king moves and jumps one
square diagonally in **any** direction. It is not a flying king: this is English
draughts, not international, and a king does not slide.

### Capture is compulsory

If any capture is available to the side to move, then **every** legal move is a
capture. Not "you should"; there is no other move on the board. This is one rule
and it is the reason the game is not a shuffle, and it is also the single hardest
thing about the app, because it makes most of your pieces unliftable without
saying so. Section 5 is mostly about that.

`captureAvailable()` is a promise about the whole move list, and the test
`testMovableSetAndMustTakeAgreeWithCanPick` asserts exactly that across four
hundred plies of real play: when it is true, every move in the list captures;
when it is false, none does.

### A jump chain is one move

If the piece that just jumped can jump again from where it landed, it must, and
the whole chain is a single move. The player never releases the piece midway.

Two consequences that cost real bugs:

- **The maximum chain is nine captures**, not three. `Move::taken` is a
  `uint64_t` bitmask over the sixty-four squares for that reason. The first
  version used `uint8_t taken[3]`, and when a chain wanted a fourth capture the
  generator recorded the three-capture prefix as a finished move. That is a
  half-completed jump offered as legal, and it happened in 13 of 282,558
  positions from random play.
- **A chain can end where it started.** Four jumps around a square return the
  piece home. `play()` therefore lifts the moving piece _before_ clearing the
  captured squares, or it erases the piece that just moved. That bug was
  unreachable only because of the three-capture cap: fixing the cap alone would
  have created it.

### Promotion ends a chain

A man crowned mid-jump stops there, even if the new king could jump again. WCDF
rule; it is also the only place where the piece's own type changes during a move.

### Winning

You win when your opponent has no legal move, whether because every piece is
captured or because everything left is blocked. Those are the same condition and
the code treats them as one: `over()` asks the move list, never the piece count.

### Draws **[house]**

The federation resolves stalemate-ish positions by arbitration and move counters
that assume a human is present. A handheld cannot. Two kings shuffling in an
empty corner is a real position and it does not end.

So: **eighty plies (forty moves each) with no capture and no promotion is a
draw.** `kIdleLimit = 80`. The counter resets on any capture or any crowning,
which are the only two irreversible events in checkers, so a game that is making
progress can never be cut off by it. Before this rule existed, the random-play
test ran until the harness gave up.

---

## 2. The game state machine

`checkers::Phase`, in `CheckersFlow.h`. Three states, and the app never stores
which one it is in:

```
Yours ---- you play a move ----> Theirs
Theirs --- they play a move ---> Yours
either ---- no legal move ------> Settled
```

`phaseFor(over, yourTurn)` derives it from the game and the seat. There is no
`phase_` member anywhere, so there is nothing that can disagree with the board.
This is the same discipline as `over()` asking the move list: a stored tally that
contradicts the position is a bug class that does not exist here.

The board still draws in `Settled` (a finished game is the thing you want to look
at), it just stops accepting.

---

## 3. Multiplayer

Checkers is a `linkplay` game, `GameId::Checkers`. The wire state is the `Game`
struct itself, 67 bytes, trivially copyable, well inside the 192-byte cap.

### Both sides deal

Unlike Knucklebones, **both devices call `ck::start()`**. Checkers has no
randomness at all, so the opening position is a fact rather than a deal, and
there is nothing to synchronise.

The first version had only the leader start, and the bug it caused is worth
keeping written down: the follower began on a zeroed `Game`, a board with no
legal move, so `over()` was true and `outcome()` read `DarkWins`. The follower
displayed **YOU WIN** before the leader had moved, and `LinkActivity` latched a
rematch flag it never clears, so `gameLoop()` stopped being called for the rest
of the match. A screen that is confidently wrong and then stops updating.

### Seats

The leader takes light and moves first, which is `goesFirst()`. The board is
always drawn from the playing side's end (`squareRect` flips file and rank on
`seat`), so your own men are nearest you on both devices and neither player is
reading the board upside down.

### What crosses the wire

The whole `Game` after each move, not the move. It is smaller than a delta
protocol's worth of code and it cannot desynchronise. `plausible()` validates an
arriving state (piece counts, turn, idle counter) before it is accepted, because
a corrupt packet must not be able to put the board in a state the rules cannot
produce.

---

## 4. The opponent

`CheckersBrain.h`. Minimax over the legal move list with a material and position
evaluation. Deterministic and pure: the same position always produces the same
move, and the brain never sees anything the player cannot.

What is proven:

- **It only ever plays a legal move.** Its choice is taken from `moves()`, so an
  illegal one cannot be expressed.
- **It is deterministic.** Same position, same move, asserted across whole games.
- **It is actually good at the game.** `testTheOpponentBeatsARandomMoverConvincingly`
  requires it to beat a random legal mover four to one; it manages **56-2 of 60**.

That last test exists because the suite was green without it. Negating the
evaluation function, so the brain played to lose, took it from 200-0 against
random to 0-195 with every other assertion still passing. Legal and
deterministic are not the same as competent, and only one of the three was being
checked.

---

## 5. The menu state machine

`checkers::Screen`, in `CheckersFlow.h`:

```
Menu ----> Board ----> Result
  |          |            |
  +-> HowTo  +--- Back ---+---> Menu
```

`back()` is an exhaustive switch with no `default`, so a new screen without a
decided Back fails the build rather than falling through to something plausible.
`leavesApp()` is true only for `Menu`: the first Back means stop playing, the
second means leave the app. Back meaning two different things depending on depth
is how players get lost.

Abandoning a board returns to the menu, not out of the app.

### Picking a piece up

One activation path: tap a piece, tap a destination. `canPick()` asks the move
list rather than ownership, so a piece with no legal move cannot be lifted at
all, and `moveBetween()` will not construct a move the rules did not offer. A
destination that was never marked cannot be expressed at the boundary either.

### Making compulsory capture visible

This is the interaction problem the whole game has, and it is worth being
explicit about how it is solved, because the obvious version is wrong.

Under a compulsory capture, seven of your twelve men typically go dead at once,
with nothing on screen having changed. A player taps one and it does not lift.
The first version called `requestUpdate()` anyway, so the e-ink panel visibly
blinked and came back identical, which is _worse_ than doing nothing: a refresh
that changes nothing is what a bug looks like.

Three things fix it, and all three are the same data drawn differently:

1. **Corner marks on every piece that can move**, whenever nothing is in hand.
   `movableSquares()` returns them as a bitmask from the same move list
   `canPick()` consults, so a marked piece always lifts and an unmarked one never
   does. When a capture is compulsory, that set collapses from seven squares to
   one, and the board _demonstrates_ the rule instead of describing it.
2. **The capsule reads YOU MUST TAKE**, which is the caption for those marks: the
   marks say which pieces, the capsule says why there are so few.
3. **A rejected tap does not repaint.** Nothing changed, so nothing refreshes.

### Showing what a jump takes

When a piece is in hand, each landing square gets a filled dot, and every enemy
disc that landing captures gets corner marks. The mask comes straight from
`Move::taken`, which the rules already computed.

Without it, a jump drew a dot two squares away with nothing saying which enemy
died, and a chain drew one six squares away with the route invisible. In
checkers the chain is the game, so that was the least legible move on the board.

### The board, and why it looks like chess

Same panel, same problem, so it deliberately reuses chess's answers:

- **56px squares**, eight of them, exactly the 448px between the margins.
- **Only the dark squares are dithered**, so it reads as a board before a single
  piece is drawn.
- **A 9px frame**, flush against the squares, heavier than anything inside it, so
  the playing surface reads as one object. The selection frame is `kFrame` (4)
  and drawn _outside_ its square: an inset frame eats the piece and makes its
  square look smaller than its neighbours.
- **Pieces are round** because everything else on this board is square, and at
  56px that difference does more work than any detail inside them would. Filled
  means dark, the same convention chess uses here: whose, never whose turn.
- **A king carries a concentric ring**, the stacked second piece of the physical
  game. Not a crown, and specifically not a square, which is what the first
  version drew inside a shape whose whole argument is that it is not square.

### The material strips

Two rows of small discs between the board and the capsule: their losses nearest
the board, yours nearest you, matching the ends the two sides are drawn from.
Empty slots stay drawn, so the strip has a length before anything is taken and
the marks do not shift as they fill.

They exist because that band was a hundred and seventy-five pixels of nothing,
which is a real defect on this panel, and because material is the fact a checkers
player most wants and can otherwise only get by counting twenty-four discs. The
numbers were already computed. They were being shown only on the result screen,
which is when they stop mattering.
