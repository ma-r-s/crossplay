# Reading Stats

How much you read, and what you finished. Card #762, asked for by report box
#525 and #684 and approved by Mario on 2026-11-17. This file is the design as
built, and the things the code will not tell a reader.

## What it is

- **On the shelf** as READING STATS, the last row of the Apps folder (appended,
  so no row moved under a thumb that knew where it was).
- **One screen**, top to bottom: this week's reading time as the headline
  (Sunday through today, the week the fork's other calendars use), the current
  daily streak beside THIS WEEK, a rule, then two weeks of minutes a day as a
  bracketed bar chart, then the books finished this year, newest first, each
  with the day it was finished.
- **The chart** runs from thirteen days ago on the left to today on the right.
  Every column with reading in it carries its minutes; this week's columns are
  solid and last week's dithered, so the headline points at the bars it adds
  up. Today's day letter is knocked out of a black square.
- **The finished list** shows as many rows as the screen holds. When the year
  has more, the last row becomes ALL N BOOKS, which opens the whole year a page
  at a time: the side keys or a vertical swipe step a page, the band says 2/3,
  nothing wraps, and Back returns to the main screen.
- **No yearly goal**, nothing on Home or the sleep screen, nothing in the
  reader's own screens. Those were decided against for this version.

## Where the numbers come from

The reader already measures a session (`src/activities/reader/ReaderSession.h`):
the seconds between forward page turns (two seconds to thirty minutes a page,
so a book left open on the desk does not count), a start time from the trusted
clock, and the progress it ended at. It used to hand that only to an SD plugin
subscribed to `reader.session`, and drop it otherwise.

**The seam is one call.** `ReaderActivity::flushReaderSession()` begins with
`readingstats::recordSession(bookPath, getBookTitle(), readerSession,
getScreenshotInfo().endProgressPercent)`, ahead of the early return that drops a session nobody
subscribes to. It is marked `fork-local seam`, listed in `LOCAL_SCOPE.md`, and
`host-tests/readingstats` fails if `ReaderActivity.cpp` differs from upstream's
by anything but that include, that comment and that call. Everything else is in
`src/apps_local/readingstats/`. Other firmwares put this in the reader (one
added about 354 lines there, another about 7,600 across eight upstream files),
which is what stops a fork syncing.

flushReaderSession() runs when the reader exits, before the device sleeps and
before a KOReader sync, and resets the session after, so a session is recorded
once.

**What counts.** The reader's own rule, `ReaderSession::isEmitWorthy()`: ten
seconds of reading against a trusted clock. A session read while the clock has
never been set cannot be dated and is not recorded; the app says so instead of
showing numbers (NO DATE YET, JOIN WI-FI TO SET IT).

**Finished** means the reader reached the end, as the reader reports it: its
own end-of-page progress, the `end_percent` its `reader.exit` event carries, is
100 on the book's last page and on the end-of-book screen (`isAtEndOfBook()`).
Closing the book on its last page therefore finishes it; it does not take a
turn past the end. It counts only after a page turned in that session was read,
so opening a finished book and closing it again finishes nothing, while a last
session too short to count as reading time still finishes the book
(`readingstats::outcomeOf()`). A book is finished once a year: the first time
this year is the date the list shows, and reaching the end again the same year
writes nothing. One edge, the reader's and not this app's: jumping to the last
page from the contents after reading something counts, because the reader then
says 100 too.

## The two files

Both under `/.crosspoint/`, outside every book's cache, so Settings > Clear
cache does not touch them. Both are runs of fixed-size, little-endian records
with a magic byte, a version and a CRC-16/CCITT-FALSE.

| file                   | record    | holds                                                                                                      |
| ---------------------- | --------- | ---------------------------------------------------------------------------------------------------------- |
| `reading-sessions.dat` | 24 bytes  | the local day, the UTC start, reading seconds, the book (FNV-1a of its path), end progress, a finished bit |
| `reading-finished.dat` | 144 bytes | the local day, the book, its title (up to 128 bytes of cleaned UTF-8)                                      |

So a session costs 24 bytes: five sessions a day is about 44KB a year.

**The day is decided when the record is written**, under the time zone in
force at that moment, and it is stored. A session is the local day it STARTED
on; a finished book is the day its record is written, which is the day the end
was reached (so a book finished just after midnight on New Year's Day is this
year's). Nothing re-derives either, so a session read across midnight stays on
the day it began, and moving to another time zone afterwards changes what
"today" is and nothing about the past.

**A damaged card costs the damage.** A file whose length is not a whole number
of records has a torn tail (power cut mid-append); the partial record is not
read, and the next append cuts it off first (or pads it out, on a card that
will not truncate) so the new record starts on a boundary. A whole record that
fails its CRC is skipped, and so is a chunk the card cannot read back; neither
is ever taken for the end of the file. The rules live in `ReadingStatsCore` behind a `ByteFile` interface,
so the host suite tears and corrupts byte vectors through the exact code the
device runs.

The sessions file is read newest first and the read stops as soon as nothing
older can change the screen: once the fortnight is complete and the streak has
ended (`Tally::settledBefore`), with three days of slack for a record a move
west wrote out of order. A reader with a ten-day streak reads about three weeks
back however many years the file holds; only a streak itself as long as the
window reads the window. The finished file is a few KB a year and is read
whole.

## The rules

- **Minutes a day**: a day's sessions added up.
- **Streak**: consecutive days holding at least a whole minute -- the minute the
  chart shows, so a day drawn as nothing never props a streak up -- ending
  today, or yesterday while today has not been read yet. It can see a thousand
  days back; past that it says 999+.
- **This week**: Sunday through today.
- **Finished this year**: the calendar year of today, one row per book, newest
  first.

## Titles

Titles are somebody else's text. `cleanTitle()` runs where they enter, when the
finished record is written: invalid UTF-8 and control characters out, the
typographic fold every app applies (`utf8FoldTypography`), whitespace collapsed
and trimmed, the file's name when the book has no title. A title past 128 bytes
is cut at a word and ends in "...". 128 is not arbitrary: it is the most
ordinary text -- English, accented Spanish, German compounds -- that always
fits the row, measured in the real face by the suite.

They are drawn in the system face (ubuntu 12, 10 and 8, the cuts Home and
Recent Books use), not Jersey, because Jersey carries ASCII alone. That face
draws Latin and its extensions, Cyrillic and Hebrew. **What it lacks, Greek and
CJK, draws as replacement boxes**, exactly as the same title does on Home,
unless an SD font is installed, which the renderer's fallback then reaches by
the same font id.

The row is two lines at 12, then two at 10, then three at 8, breaking between
words and, in CJK, between characters. Every line is measured before it is
drawn, so no title reaches past its box whatever it is; only a title no real
book has (128 bytes of W) can still end in "...". The screen is drawn in two
passes over one frame for this -- the app's own words in Jersey, then the
titles in the system face -- from one geometry, so a title cannot land beside
another book's date.

## How the layout was chosen

Three complete layouts were rendered from the same seeded history and compared
side by side: this front door (Study's and Connections' shape), two outlined
tiles over a full-width chart, and a two-week calendar of filled days with each
week's total. The front door won on fitting the fork's other front doors and
on showing a true fortnight; the tiles' solid-against-dithered bars were the
one element worth keeping, and it carries them. The calendar showed fewer than
fourteen days early in a week and could not fit its labels.

## Tests

- `host-tests/readingstats`: the record format, the day rule across midnight and
  a time-zone change, the streak, the week, the yearly list, the torn and
  corrupt files, the title cleaner; the titles in the real ubuntu cuts; and the
  seam, held to one call.
- `host-tests/ui`: what the screens draw and make tappable -- the door, the
  first run, the missing clock, the paging, the chart's numbers.
- `scripts_local/shoot-readingstats.sh <real|few|empty> <theme> <out>` seeds the
  simulator card through the same encoder and photographs the app.
