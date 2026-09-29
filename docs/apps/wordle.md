# Wordle

Six guesses for one five-letter word, the same word as the New York Times'
Wordle that day. Asked for twice: the report box (#497) and GitHub #202
(hantnor). Card #609. Every choice below was agreed with Mario before it was
built.

## Connections' shape

One button downloads every past answer and the list of accepted guesses. After
that, playing never touches the network. The menu, the archive calendar and
the download screen are Connections', and the calendar and download screen are
literally its code (`connectionsui::buildCalendar`, `buildImport`), drawn in its
faces. There is no free play: a day is a day's word.

- **Menu.** Today's date in the biggest type, which is also the way into
  today's game; how today went; the record; a chart of guesses per win; then
  HOW TO PLAY, ARCHIVE and GET PUZZLES (ALL CAUGHT UP when today is on the
  card). On an empty card the date line reads NONE YET, as in Connections.
- **The record** counts only games finished on their own date, as NYT's does.
  The streak runs from today, or from yesterday while today is still open. A
  past day played from the archive is marked on the calendar (a sparkle when
  solved, a cross when not) and counts for nothing else. The first finish of a
  day is the one kept.
- **Every game is kept**, one line per day in `/.crosspoint/wordle.games`, so
  a day reopened from the archive shows what was put down.

## The game

Layout 1 of three rendered (classic QWERTY, big keys with a footer bar, A-Z),
with absent letters grey. The marks, on tiles and keys alike:

- right letter, right spot: solid black, white letter;
- right letter, wrong spot: heavy border and a dot in the corner;
- not in the word: the light grey, one dot in four.

The first proposal marked "not in the word" with a thin border. The render
showed it was the same mark as a key nobody had pressed, so C, R, E and S read
exactly like W and Y. Grey is what the original uses for the same reason.

- ENTER is a tick, greyed until five letters are in. Delete is a backspace key.
- A guess that is not in the list stays in its row, and the line under the
  header says "Not in the word list."
- **The end.** The answer replaces WORDLE in the header and the right label
  says how it went ("4 / 6", "X / 6"). Any tap then goes back to the menu.
- **The keyboard is one hit region.** Twenty-eight keys are more than the
  24-slot interaction table holds, so the tap is resolved by `keyAt()` against
  the same `KeyboardLayout` the keys were drawn from. host-tests/ui taps the
  centre of every drawn key and requires its own letter back.

## Data

| File                        | What                                                                            |
| --------------------------- | ------------------------------------------------------------------------------- |
| `/.crosspoint/wordle.ans`   | Five bytes per day from day 0 (2021-06-19), `.` for a day not known.            |
| `/.crosspoint/wordle.words` | The accepted guesses, sorted, five bytes each, binary-searched in memory.       |
| `/.crosspoint/wordle.res`   | One byte per day: 1-6 solved, 7 lost, 8 started; 0x80 when finished on the day. |
| `/.crosspoint/wordle.games` | `day ANSWER GUESS GUESS...` per played day.                                     |

The answers come from [mfilej/wrdl](https://github.com/mfilej/wrdl), a mirror
that appends NYT's answer daily: `solutions-era1.txt` and `solutions.txt`
(`YYYY-MM-DD WORD`) and `valid.txt` (14,855 words, sorted, every answer among
them). It lands a day behind, so the days between its newest and today come
from NYT's own `svc/wordle/v2/YYYY-MM-DD.json`, one request per day, at most
fourteen, and never for a date after the device's today, so nothing on the
card is a word nobody has played yet. An update when the card is only a few
days behind is those requests alone.

Measured when it was built: 1,927 days from 2021-06-19, no gaps, no dates that
disagree between the two files. Day 1926 is 2026-09-27, NYT's own
`days_since_launch`.

"Today" is the device's local date, which follows Settings' time zone. The
simulator runs on UTC.

## Not built

- Hard mode, a share grid, free play, other word lengths.
