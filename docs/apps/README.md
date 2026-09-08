# What is in docs/apps/

One directory, 34 files, and until this index they were a raw file listing. A
doc lands here when something about an app would otherwise be rediscovered the
hard way; not every app has one, and some have several.

**Read the status first.** Several of these are plans, and a plan describing
something that shipped six weeks ago reads like unfinished work if nothing says
otherwise. Where a file is a record rather than a description, it says so in its
own first paragraph.

## What an app is, and how to use it

The fork's only user-facing app docs. `USER_GUIDE.md` at the repository root is
upstream's and covers the reader, not these.

| | |
| --- | --- |
| [`study.md`](study.md) | Anki decks on the reader: the deck, the scheduler, and what a review does. |
| [`instapaper.md`](instapaper.md) | The read-later queue and how it syncs. |
| [`trivia.md`](trivia.md) | The question app, and where the questions come from. |

## The rules a game implements

What the game is, the decisions the rules forced, and what the screens do about
them. These are the ones worth a stranger's time.

| | |
| --- | --- |
| [`chess.md`](chess.md) | The screen graph rather than the rules of chess. |
| [`checkers.md`](checkers.md) | English draughts, and the compulsory capture the board has to say out loud. |
| [`connectfour.md`](connectfour.md) | Seven columns, one tap each. |
| [`yahtzee.md`](yahtzee.md) | The Joker rules are three rules, and conflating them is the bug. |
| [`knucklebones.md`](knucklebones.md) | The dice duel, and what the critic agents found after "finished". |
| [`minesweeper.md`](minesweeper.md) | Flagging without a right button. |
| [`sudoku.md`](sudoku.md) | Difficulty proved by a grader rather than estimated. |
| [`jaipur.md`](jaipur.md) | The two-player trading game, solo or nearby. |
| [`seasalt.md`](seasalt.md) | Sea Salt & Paper, and the rulebook contradiction Mario settled. |
| [`toybattle.md`](toybattle.md) | The fork's first graph board. |
| [`murdle.md`](murdle.md) | A logic grid built through the solver, so a case is never a guess. |
| [`wavelength.md`](wavelength.md) | The dial, and why the board is public. |
| [`forehead.md`](forehead.md) | The first game to make both physical buttons load-bearing. |
| [`trivia.md`](trivia.md) | Also the app doc, above. |

## Where the content came from

A game whose difficulty is a property of its bank, not its code.

| | |
| --- | --- |
| [`dungeon.md`](dungeon.md) | The 64 D&Diagrams puzzles and how they were transcribed. |
| [`picross.md`](picross.md) | Why no open nonogram corpus exists, and what was generated instead. |
| [`../trivia-curation.md`](../trivia-curation.md) | Outside this directory: how the Jeopardy pack is cut and ranked. |

## Formats on the card

The on-disk shapes. Read these before changing a writer.

| | |
| --- | --- |
| [`study-deck-format.md`](study-deck-format.md) | |
| [`study-anki-compatibility.md`](study-anki-compatibility.md) | What converts, what is reduced, what stays behind. |
| [`trivia-pack-format.md`](trivia-pack-format.md) | |
| [`xkcd-pack-format.md`](xkcd-pack-format.md) | |

## Decision records for things that shipped

Written before the code, kept for the reasoning. **All of these landed**, and
all are in the present tense of the day they were drafted, so read "not built
yet" here as "not built yet then". Only some carry a marker saying so at the
top; the rest is this row.

| | |
| --- | --- |
| [`hackernews-saved-port.md`](hackernews-saved-port.md) | The saved-articles shelf. |
| [`instapaper-plan.md`](instapaper-plan.md) | The queue, the sync, and what it costs. |
| [`study-installer-plan.md`](study-installer-plan.md) | The browser page that converts a deck. |
| [`study-sync-bridge-plan.md`](study-sync-bridge-plan.md) | Every device syncs, nobody runs a server. |
| [`study-syncflow-ui.md`](study-syncflow-ui.md) | The sync flow rework. |
| [`trivia-sync-design.md`](trivia-sync-design.md) | Report a question, filter what you get, sync the pack. |
| [`wallpapers-phone-flow.md`](wallpapers-phone-flow.md) | From a picture on a phone to the sleep screen. |
| [`wallpapers-shuffle.md`](wallpapers-shuffle.md) | Choosing a set and letting it take turns. |
| [`xkcd-viewing-plan.md`](xkcd-viewing-plan.md) | Reworking how comics are shown. |

## Generated, not written

[`study-quick-reference.pdf`](study-quick-reference.pdf) is the one file here
that is not prose: a printable page for the Study app, produced rather than
edited.

## Decided, and nobody is on it

| | |
| --- | --- |
| [`wavelength-teams.md`](wavelength-teams.md) | Teams mode. `Mode::Teams` exists and is tested, but `WavelengthActivity::kMode` is pinned to `CoOp`, so no player can select it. |

## Argued, and deliberately not built

| | |
| --- | --- |
| [`guesswho.md`](guesswho.md) | There is no `src/apps_local/guesswho/`. The doc talks the idea down to the reason it does not work: the faces are hashes, and hashes have no askable attributes. |

## Eight things on the shelf have no doc of their own

Battleship, Connections, Solitaire, Insider, Hacker News, xkcd, Wallpapers and
Get Books. Some have auxiliary records here (a format, a plan, a flow) and Get
Books has nothing at all; none has a file saying what the app is. That is a gap
rather than a decision, and the number is checkable: every name in
`Shelf.cpp`'s `kGames` and `kApps` should have a row somewhere above.
