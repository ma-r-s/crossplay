# Chess: the screen graph

Every screen, every input, every destination. Written down because the flow was
built by bolting screens onto each other and three separate defects came from
edges nobody had drawn. If you change navigation in `ChessActivity`, change this
first and make the code match it.

Chess does not know that `Games` is its parent. It calls `shelf::leave()` and
the shelf decides; the edge is drawn here because that is where you end up, not
because chess names it. See [shelf.md](../shelf.md).

```mermaid
stateDiagram-v2
    direction TB
    [*] --> Menu

    state "MENU" as Menu
    state "BOARD (solo)" as BoardSolo
    state "BOARD (match)" as BoardMatch
    state "SETTINGS (from menu)" as SetMenu
    state "SETTINGS (from gear)" as SetBoard
    state "LINK: searching" as Searching
    state "LINK: rematch / over" as Rematch
    state "GAMES folder" as Games
    state "asleep / app switched" as Away

    Menu --> Games: Back (shelf::leave)
    Menu --> BoardSolo: CONTINUE
    Menu --> BoardSolo: NEW GAME
    Menu --> Searching: PLAY NEARBY
    Menu --> SetMenu: SETTINGS

    BoardSolo --> Menu: Back (saves)
    BoardSolo --> SetBoard: gear
    BoardSolo --> BoardSolo: PLAY AGAIN (resets)
    BoardSolo --> Away: sleep (saves)

    SetMenu --> Menu: BACK TO MENU / Back / off-target
    SetMenu --> Menu: START NEW GAME (resets)
    SetMenu --> SetMenu: toggle a preference

    SetBoard --> BoardSolo: BACK TO BOARD / Back / off-target
    SetBoard --> BoardSolo: START NEW GAME (resets)
    SetBoard --> BoardSolo: NEW GAME / TAKE BACK
    SetBoard --> Rematch: NEW GAME (in a match, asks)
    SetBoard --> SetBoard: toggle a preference

    Searching --> Menu: Back / BACK
    Searching --> BoardMatch: opponent found

    BoardMatch --> Menu: Back (tells them, stops, restores solo game)
    BoardMatch --> SetBoard: gear
    BoardMatch --> Rematch: game ends
    BoardMatch --> Rematch: they leave or go silent
    BoardMatch --> Rematch: they ask for a new game
    BoardMatch --> Away: sleep (does NOT save)

    Rematch --> Menu: Back / BACK (tells them, stops)
    Rematch --> Rematch: PLAY AGAIN (waits for their answer)
    Rematch --> BoardMatch: both said yes
```

## The rules the graph encodes

**Multiplayer is an action, not a setting.** Nothing about `PLAY NEARBY`
persists. Re-entering chess always starts at MENU with the radio off.

**Settings has two doors and they are not the same screen.** The door decides
the row set and the close label, not just the exit. `NEW GAME` and `TAKE BACK`
act on a board you are looking at, so they only exist behind the gear.

**Three things must never happen during a match**, and each was a real defect
found separately:

- Saving. The position on screen is the shared game, and writing it over the
  single-player save loses the game you were keeping. Blocked in `Back`,
  in `onExit` (sleep and app-switch), and nowhere else writes.
- Resetting on your own. You cannot restart a shared board without telling the
  other device. Every path that would (`NEW GAME` in settings, the board's
  `PLAY AGAIN` capsule, a pending restart on close) asks instead.
- Leaving silently. Every exit sends a note first, so the other device says
  `LEFT` rather than sitting on `DECIDING` until the silence timeout notices.

**The zone under the seats belongs to the game.** `buildLink()` returns it and
chess draws the position it has just finished there, which is what turned that
space from slack into the thing worth looking at while you decide. The link
layer cannot draw it -- a board is chess's material, not the link's -- so it
hands over a slot and stays out, the same split the board screen uses.

**One door into the MENU.** `goToMenu()` is the only way in, so the `CONTINUE`
row cannot describe a position from two games ago -- which it did, when three
routes each kept their own copy of the bookkeeping.

**A game ends four ways, and two of them are facts about the GAME rather than
the board.** Checkmate and stalemate are read off the position, so any code
holding a `Position` can find them. Threefold repetition and the fifty-move rule
cannot be: they need the history behind the position. `refreshLegalMoves()` owns
all four and is the only writer of `gameOver`, which is why `takeBack()` no
longer clears that flag itself.

The repetition window is the key of every position since the last capture or
pawn move, and it is bounded by the halfmove clock for correctness, not for
speed: a position from before an irreversible move belongs to a game that can no
longer be reached, and counting it would draw a live game. Every transition
pushes one key -- your move, the engine's, and the opponent's arriving over the
radio -- so a path that changes the board without pushing is a silent bug.

**A resumed game cannot look behind its own start.** The save holds the position
and the SAN move sheet, never the per-ply undo data, so `undoableFrom` marks
where real history begins. Take-back stops there rather than unmaking a
zero-initialised `Move`, which moved nothing, wiped castling rights and deleted
a row from the sheet. Repetition starts counting from the resumed position for
the same reason: keys we do not have cannot be compared, and guessing would end
a live game in a draw that never happened.

**The engine must never be handed the live board.** `search()` makes and unmakes
thousands of moves on whatever `Position` it is given, and the render task reads
`position` from another core. `playEngineMove()` therefore searches a copy. The
one-pass deferral in `handleSquareActivated()` reads like it prevents this and
does not: `requestUpdate()` only notifies the render task, so the repaint it
asks for is still in flight when the search begins. This is invisible in the
simulator, where a depth-4 search finishes in under 150ms; on the device it was
half a second of a wrong position.

**`update()` runs before every early return in `loop()`.** The settings overlay
returns from that function, so a match that only ticked when no overlay was open
would die whenever somebody opened one for ten seconds.
