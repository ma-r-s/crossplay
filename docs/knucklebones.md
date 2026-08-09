# Knucklebones

The dice game from Cult of the Lamb. Two players, a three by three grid each,
one die. It is the ninth game in this fork and the first built to find out what
a repeatable game cycle actually needs, rather than to guess at it.

## The rules, exactly

On your turn you roll a d6 and drop it into one of your three columns. Columns
fill from your near end outward.

**Placing a value destroys every copy of that value in the opponent's facing
column.** Only that column, only that value. This is what makes a column a
contest rather than two solitaire boards side by side.

**A column scores each distinct value as `v * n * n`**, where `n` is how many
copies of `v` it holds. Three fours are 36, not 12. That multiplier is the whole
game: it is why stacking is worth more than spreading, and why the destruction
rule bites.

**The match ends the instant either grid fills**, with no final turn for the
other player. The higher total wins. You can end it by filling your own grid and
still lose, which is a real decision late on.

Matching dice score together wherever they sit in a column. Gravity is a layout
rule, not a scoring one -- `[3,5,3]` scores 17, and that position is reachable
because destruction removes dice from the middle and the survivors compact.

## The three layers

| Layer      | File                       | Knows about                           |
| ---------- | -------------------------- | ------------------------------------- |
| Rules      | `KnucklebonesCore.h`       | nothing -- freestanding C++17         |
| Opponent   | `KnucklebonesBrain.h`      | the rules, and nothing else           |
| Navigation | `KnucklebonesFlow.h`       | nothing -- two enums and a table      |
| Screens    | `KnucklebonesScreens.h`    | FreeInkUI and Toybox tokens           |
| Activity   | `KnucklebonesActivity.cpp` | renderer, storage, input, shelf, link |

The first four are host-tested with no device. `host-tests/knucklebones/run.sh`
runs the rules, the opponent and the flow; the screens have a block in
`host-tests/ui/test_ui.cpp`.

## The state is the wire format

`Game` is 24 bytes, trivially copyable, and travels whole on every move. There
is one description of a match, so two devices cannot drift, and
`takeOpponentState()` is a straight copy rather than a merge. Battleship needs a
merge because its state holds two fleets; this holds one board both sides agree
on.

Consequences worth knowing:

- **Only the first mover deals.** The follower starts from a zeroed game and
  receives the real one with the opponent's first move. Dealing locally would
  put a different die on its screen for the half second before that arrives,
  which is why its board is briefly empty and says so.
- **`plausible()` guards the seam.** `turn` indexes straight into `grid[2]`, and
  the layer beneath checks payload length only with no CRC, so a corrupt byte
  would be memory corruption rather than a rejected move.
- **`GameId` is `0x0501`.** Bump it if the layout ever changes, or an old build
  and a new one will match happily and misread each other.

## The two state machines

`Screen` is the shell -- menu, how to play, board, result -- and is the only one
Back navigates. `Phase` is the game. They are separate because mixing them is
how "Back went to the wrong screen" happens: if "am I in the how-to" and "is it
my turn" share a set of flags, Back has to know about turns.

`back()` is an exhaustive switch with no default, so adding a screen without
deciding where its Back goes fails the build. The tests assert properties rather
than a transcript: every screen reaches the top, no pair can Back into each
other, exactly one leaves the app.

## The opponent

One ply, greedy on `my score - their score`, deterministic. There are at most
three legal moves and a die nobody can predict, so depth buys very little and
costs a slow panel its responsiveness.

Two things about it are load-bearing:

- **It cannot cheat, structurally.** The `Game` it is handed carries `rng`, and
  `rng` advances once per placement, so that field is the complete future die
  sequence. The trial copy is blinded, and a test plays two positions differing
  only in the die stream and requires the same move.
- **Its tiebreak is 43% of how it plays.** Breaking toward the lowest column put
  its first three dice in column 0 in every single game, burying the opening
  where it could not start a second stack. It now prefers room, then a rotation
  derived from the visible position. Still deterministic, which is what keeps a
  seeded match replayable.

## What the critics found

Two cold reviewers, one on the rules and one on the look, after the game was
"finished". Recorded because the pattern is more useful than the list.

The rules critic found three defects and four holes, and proved every one by
compiling a mutant that survived the existing suite. The suite had 1.85M
assertions at the time. Its score assertion was verbatim `score()`'s own body,
so it could not fail; every hand-built literal put matching dice in adjacent
slots, so a `columnScore` counting only adjacent runs passed everything.

The look critic measured this board at 7% ink against chess's 17.8% and murdle's
17.7%, found the die spending 7396 solid black pixels on every repaint against
an explicit ghosting rule, and found the how-to's pages contradicting their own
captions.

**The single most useful thing either of them did was compile something.** Every
finding that mattered came with a mutant or a number, not an opinion.

## Two traps this game paid for

**A test that asserts a thing was drawn cannot tell you it was visible.** The
how-to's page counter went in as the header's `rightLabel`; the host test
asserting it was drawn passed, and the panel showed nothing, because it renders
black on the black header band. Same class as the known-failing `paperOnTheBand`
test. It is drawn in the body now.

**Arithmetic that overflows a panel does not complain.** The first board layout
totalled 812px against 800: the opponent's column scores drew behind the header
band and mine ran off the bottom. Nothing failed. A test now pins the extremes
of the drawn board to the screen.
