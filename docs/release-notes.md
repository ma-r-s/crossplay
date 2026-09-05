What was new in every tagged release from 1.12.1 onward, newest first.
Anything older is on the GitHub releases page and was never in this file.

**1.12.14 and 1.12.15 are absent because their tags are.** They are the two
releases that published a `-full.bin` with `bootloader.bin`, `firmware.bin` and
`partitions.bin` missing; no `v1.12.14` or `v1.12.15` ref exists in this
repository or on the remote, so there is nothing here to quote them from.

This file is NOT the release page: `docs/release-body.md` is what a tag
publishes, and it carries this release only plus a link back here. They were
one file until 2026-09-04, which is why v1.12.21's release page was 20,402
characters and opened with six previous releases.

Entries from 1.12.13 down to 1.12.16 were backfilled out of the tags
themselves, verbatim, including the "Plus N changes nothing on the device can
see." lines those releases actually published. A history that edits the past to
match today's rules stops being one.

`scripts_local/release_notes.py --write` prepends a block below the marker.
Everything above the marker is written by hand.

<!-- releases, newest first -->

### 1.12.34

- Tell the reader the device is working before it blocks (#306)

### 1.12.33

- Wallpapers: a sleep-screen picker on the device, and a browser to make wallpapers

### 1.12.32

- Move the US-centric toggle out of the device's Settings (#311)

### 1.12.31

- Connections' GET PUZZLES says it is working before it blocks (#244)
- Bound the two wire tokens where the index row is written

### 1.12.30

- Two glance-read icons -- pass arrow + peek eye (#261)
- Derive layout from content top; honest disabled score label (#262)

### 1.12.29

- Make the header band absolute in headerBand, so every screen's is 76
- Trivia: a settings toggle for US-centric questions (#191/#223)
- Draw battleship's waiting capsule solid, not dithered (#243)

### 1.12.28

- Draw the header rule in headerBand, and clear it in Yahtzee
- Keep the reader in place on a full card, and give its band a real shrink ladder (#40, #268)
- Persist Toy Battle & Jaipur record lines; make Toy Battle's HOW IT ENDED tappable
- Move the refusal band between the grid and the key (#293)
- Instapaper: disconnect the account from the reader (with a wipe confirm)

### 1.12.27

- Size 22 snprintf buffers from their formats, and delete the flag that hid them

### 1.12.26

- Three visible fixes -- murdle's refusal band, connect four's lip, the Seeed tile
- Ship the chess pieces' MIT licence and correct Solitaire's attribution
- Back swipe exits Trivia, and a guard so the next app cannot forget
- One type ladder, and every real title measured against the cut that draws it
- Connections: nothing is ever shortened, and nothing is broken that could be shrunk
- nothing changes on your device. Releases are built with the same pinned toolchain every other build here uses, instead of whatever PlatformIO version PyPI published that day, and a tag can no longer be built and published twice at once.

### 1.12.25

- nothing changes on the device. The release build now checks its own output is on disk before packaging it, so the fault that published v1.12.14 and v1.12.15 without a working image cannot reach you silently again.

### 1.12.24

- Settings: CrossPlay's System rows move below CrossPoint's, and the consent line shrinks

### 1.12.23

- Reserve the murdle refusal band so the grid never moves
- The release page now says what changed in this release, and nothing else.

### 1.12.22

- Wt: prune must not delete a tree somebody is working in
- Count a link match, and show the board that ended it (#35, #36)
- Plus 2 changes nothing on the device can see.

### 1.12.21

- Trivia: a blind panel drawn from the local rater, and what it found
- Plus 1 change nothing on the device can see.

### 1.12.20

- Check: --committed works in any argument position
- Trivia: the parts of the difficulty work that outlived the ratings
- Plus 2 changes nothing on the device can see.

### 1.12.19

- The notes list only what a device can see
- Plus 3 changes nothing on the device can see.

### 1.12.18

- Ci: group the pio run invocations, so the stack checks stop depending on order
- Ci: let postgres take longer than 60s, and say why when it does not
- Trivia: a pack the rating run can actually produce
- Forehead: PLACES is geography again
- Instapaper: a fileless row deadlocked its own download
- Marginguard: a folded margin handed to plain setContentMargin() fails a suite
- Review means merge on green; a hold is a card state, not a message
- Board: a sync run that opened a pull request is a card in review
- Autorelease: the token and trigger relationship at the top, and the publish step run under test
- Two firmware builds in the committed gate, in one pio invocation

### 1.12.17

- Ci: clang-format, unit tests and cppcheck run in the fork's own workflow
- The release watcher: the thing that would have noticed
- Readbridge: the running service can be asked which commit it is
- Ci: one release build per tag, and assert both halves of why
- Guard: quotes are stripped before the command is split
- Sync CrossPoint develop (6 commits) and FreeInk SDK
- Build both devices in one pio run, and stop misdescribing why

### 1.12.16

- Capture each device's artefacts before the next build removes them
- Trivia: rate the pack locally, on a scale that means something
- Make the repo presentable: front page, dead links, stale facts
- The release takes the bootloader from the framework package
- Release: the notes live in docs/release-notes.md, so the bump touches no workflow file
- Reading: wrap an article once, not on every page turn
- Autorelease: an emulator rebuild does not count as the tip moving
- Finish the upstream sync: adopt upstream's margin API, merge the SDK fork with Free-Ink main
- The black header band reaches the panel's top row
- Instapaper: parse the article list the API actually sends
- Ci: xteink queues its runs instead of cancelling them, so releases fire
- The device never makes a request of its own to report
- Guard: a write verb counts only as a command word, outside quotes
- The Anki and Instapaper sign-in pages look like CrossPlay, and say what to do
- The upstream sync carries the FreeInk SDK and tells the board when it stops
- Inbox: a desktop layout from 900px, and a fixture to look at it without a passphrase
- Autorelease: a merge that cannot reach a device is not a release
- Board: a re-registration keeps the claim's app id
- Site: the report box is a dialog behind a corner button, not a page
- Devices report on the requests they already make; services post the events
- Heartbeat: the device makes no request of its own for reporting
- The pulse runs on the board, not on GitHub's cron
- Board: a nightly check opens a card if no device heartbeat ever arrives
- Daily heartbeat and crash report to the board (cards 18, 103)
- Assemble the Sticky Playground submission
- A plural and an -ism are the same option twice
- Wt.sh prune drops every merged, clean, idle tree at once

### 1.12.13

**The box around the books icon is gone.** 1.12.12 gave real buttons
an outline so they would look like buttons, and that outline reached
one icon that is not a button.

**BACK works while a book cover is loading**, once the connection is
made. The cover was fetched in a way that read no input at all, so
the screen could not be left. A cover left over from a previously
opened book could also be shown for the wrong one; that is fixed.

**INSTAPAPER works now, from the device, with nothing to set up
first.** Open it, press SYNC, and the screen tells you where to sign
in and shows a code to scan. It could not work before: the address on
that screen pointed at a host that did not exist, and could never
have existed -- it was one level too deep for the certificate the
domain carries, so it would have failed even once created. The
service is live at the corrected address.

**GET BOOKS says when a download finished.** It shows SAVED, the
filename actually written to the card, and which folder it went to.
Before, a finished download looked exactly like one that gave up:
the screen simply vanished. The message waits for five seconds you
could actually have seen it -- the clock does not start until the
page has drawn, and stops while a finger is on the glass.

### 1.12.12

**GET BOOKS: BACK no longer downloads the book.** Pressing back on a
book's page started the download instead of leaving it.

**And tapping a book no longer looks dead.** The page waited for the
cover before drawing anything, so a tap seemed to do nothing until
the picture arrived. It draws first and fills the cover in after.

### 1.12.11

**SUDOKU no longer throws away your saved puzzle.** Opening the
difficulty menu, looking through the levels and coming back to your
own used to leave the game offering a NEW PUZZLE over the board you
were still looking at. Tapping it started fresh and your game was
gone, with nothing asked. Saves already left in that state are
repaired rather than discarded.

Sudoku could also overwrite your game with no tap at all: leaving
the menu while a new puzzle was being generated did not stop it, and
it saved itself over your game a moment later.

**Every page you read costs less.** The reader was preparing the next
page's lettering after each turn and then throwing the work away
before it could be used, twice over. Removing it takes real work off
the moment you turn a page. There is more to find here and the
firmware can now report its own timings, which it previously could
not do in a released build at all.

**HACKER NEWS opens without Wi-Fi.** It used to put the network
picker in front of itself, and backing out of that closed the app --
so the saved-articles shelf, the half that exists for when you have
no signal, could not be reached without a signal. It now opens on
your list with the radio off and only asks for a network when you tap
something that needs one. Four screens that had no way forward now
have one.

**Typographic quotes and dashes no longer vanish.** Text taken from
the internet carries punctuation the fonts do not have, and those
characters were silently dropped, leaving holes mid-sentence.

**WAVELENGTH asks before resuming somebody else's game.** A session
left on the card used to greet a new table with the previous group's
round and score. It now says a game was left unfinished, shows what
it was, and asks whether this is the same group.

**A single press no longer does two things.** Leaving the Wi-Fi
picker registered once as you let go and again in the app underneath,
which is what made Hacker News exit when you cancelled. Nine screens
handed control back that way and fifty-five could receive the stray
press.

### 1.12.10

**A tap can no longer land on a screen you have not seen yet.** The
panel takes up to two seconds to redraw, and until now the new
screen's buttons were already listening underneath the old picture.
A finger resting where the last screen's button was would press
whatever the new one had put there. In Battleship that meant your
winning shot could restart the match while the screen still read
FIRE, and you would never learn you had won. This is fixed for every
game at once, in the layer they all share, rather than one game at a
time.

A tap on a screen that did NOT change still works immediately, so
nothing feels slower: holding to flag a mine and holding to peek in
Wavelength are untouched.

**Two taps that were being read as the wrong answer entirely are
fixed too.** A tap that landed on nothing looked identical to a tap
on empty space, so a menu could read it as "close this" and Murdle
could read it as striking out a clue you never touched.

**LEAVE THE MATCH has moved off the button every game trains you to
press.** On a two-player game it sat exactly where PLAY AGAIN sits,
so the thumb already resting there after the last move would kill the
connection instead of starting the next game.

### 1.12.9

**WAVELENGTH's lock is an ordinary button now.** It used to ask you
to HOLD, without ever saying for how long, and the lock fired while
your finger was still down -- which is how the result screen came to
be drawn underneath the thumb that earned it. One tap locks the
guess, and the reveal appears with your finger already off the glass.

The stray tap that the hold was guarding against is handled by
moving the button instead of timing it: the lock now occupies only
the number column, and the strip below the board is dead space
rather than a smaller target.

### 1.12.8

**WAVELENGTH's result screen no longer disappears behind the thumb
that earned it.** Locking a guess fired while your finger was still
down, the score drew underneath it, and lifting off landed on the
button occupying that exact spot, which moved the game on. The
reveal is the whole point of the round, so this was a round played
and never seen. Four testers watched their own score exist for
under a second and vanish.

**The device asks to be passed on again.** A quick second tap used
to skip past PASS THE DEVICE entirely, which let the same player
give two clues in a row without anyone noticing.

**The dial ignores taps outside it.** Touching the left margin used
to move the guess, so a stray thumb changed a number the table had
agreed on.

**A button that does nothing is no longer shown.** Two testers
tapped it in separate rounds, got no response, and concluded the
device had frozen.

All four were fixed once already and lost when two versions of the
game were reconciled. They are verified by name on the shipped
build this time, rather than assumed to have survived.

### 1.12.7

**WAVELENGTH no longer asks a question nobody can answer.** It used
to ask whether the number was nearer one end or the other, after the
table had already committed to a number. In the original game that
bet belongs to the opposing team; this version is co-operative and
has none, so the question had nobody to put it to. It is gone, and
the round scores on distance alone.

**The game now says who is doing what.** It opens by naming the role
rather than telling whoever just picked the device up to pass it on,
and it uses one word for each thing throughout instead of four names
for two roles.

**A round in progress survives being interrupted.** Pressing home,
or letting the device sleep, used to destroy the round, the hidden
number and the session's score. It comes back where you left it now,
with the number still hidden.

**Several ways to lose a round without being told are closed**,
including the one where lifting your thumb off HOLD TO LOCK pressed
the button underneath and dismissed the score before the table had
seen it. Locking a guess needs a deliberate hold, and the buttons
that end or continue a session say which is which.

**Seven spectrum cards a table cannot actually play were removed.**

**Trivia's wrong answers are built properly now.** The engine that
picks them was reading the wrong word as the subject of the
question, so "this musical river" drew from musicals rather than
rivers. **This changes nothing until the question pack itself is
republished** -- the questions live on the SD card, not in the
firmware.

### 1.12.6

**CHESS no longer flashes a wrong board while the computer is
thinking.** After you moved, the board showed a position that was
not yours and not the computer's for about half a second, then
settled on the real one. The engine was working on the very board
being drawn on screen, so what you were shown was the middle of its
search: pieces part-way through moves it was considering and would
take back. It now thinks on its own copy, so between your move and
its reply the board only ever shows the position you played.

**A game of CHESS can end in a draw.** Until now the only endings
were checkmate and stalemate, so a repeated position or a dead
drawn ending simply went on for ever. A position reached three
times is now a draw, and so is a hundred half-moves with no capture
and no pawn move. The board says which one it was rather than just
stopping.

**Taking back a move in a resumed game no longer damages the
board.** Leaving CHESS and coming back, then using TAKE BACK, moved
nothing, silently took away your right to castle and deleted a move
from the score sheet. The save file keeps the position and the
written moves but not what is needed to unwind them, so take-back
now stops at the point you resumed from instead of guessing.

### 1.12.5

**Trivia's wrong answers no longer give the game away**, its em
dashes render as hyphens instead of vanishing mid-word, the front
door was rebuilt, and it now tracks how far through the pack you
are. Your difficulty setting also survives leaving the app.

**If you have used Trivia before, delete `/trivia/pack.dat` from the
SD card.** The questions live in that file, not in the firmware, and
the app keeps whichever pack is already there -- so updating alone
leaves you on the old questions. Delete it and the app fetches the
new one on the next run.

**A WAVELENGTH round in progress now survives leaving the app.**
Pressing the home key mid-round used to destroy the round, the
hidden number and the whole session's score, with no warning and no
way back -- and letting the device fall asleep did the same, because
waking it restarts the firmware. The round is written down as you
play now, so you come back to the dial exactly where it was.

**Its two end-of-session buttons said the opposite of what they
did.** "Finish and see the score" did not finish, and the only way
to actually end a session was a button labelled "start over", which
started nothing and wiped the score without asking. They now say
what they do, and the destructive one looks destructive.

**The games list pages up and down, and stops at the ends.** Swiping
sideways used to page one way and leave the shelf the other, which
meant paging forward off the last page wrapped around to the first
and opened a game you had not chosen. Up is the next page, down is
the previous, the header says which page you are on, and there is no
wrap to fall through.

**A folder also comes back to the page you left it on**, rather than
the page holding whatever you played last -- so browsing to page two
and going away to read no longer drops you somewhere else. And no row
is highlighted any more: nothing on this device can move or open a
highlight, so it was a cursor for something that does not exist.

### 1.12.4

**The games list comes back to the page you left it on.** It used to
reopen on whichever page held the game you last played, which is a
different thing and looked like a fault: browse to page two, go read
a book, come back, and you were somewhere else. It now remembers the
page.

**No row is highlighted any more.** A folder used to open with one
game or app marked, and on APPS that was whichever app you opened
last -- forever. There is no key on this device that can move a
highlight or open one, so it was a cursor for something that does
not exist, and it read as one.

**WAVELENGTH says one thing per name.** It had four names for two
roles, four for one scoring event and three for the guess bar. It is
now CLUE-GIVER, GUESSERS, SIDE CALL and the guess, everywhere. The
front door and the end-of-session summary also used to report the
same session with different numbers -- one counted the round about
to start, the other left out the practice round -- and they now
agree.

**The read-later app's ARCHIVE has moved and can be undone.** It sat
between the two page-turn buttons with no confirmation and no way
back. It is now at the far left, out of the way of paging, and PUT
BACK appears next to SYNC after you use it. A sync that uploads
something now says what it sent instead of reporting only what came
down, the last page of an article no longer says it is the
second-to-last, and an article you finished reopens where you left
it rather than at page one.

**Hacker News' bookmark was backwards.** The bright chip meant NOT
saved. It now says SAVE and SAVED in words, a story whose page will
not load can be kept by its discussion instead, and a swipe pages
the list.

### 1.12.3

**The games list stopped opening the wrong game.** Reaching page 2
or 3 was unreliable and about two attempts in three landed in a game
you had not chosen. The cause was not the key: the folder reopens on
the page holding the game you played last, and nothing said so. It
now marks the row it resumed on, a swipe turns the page, and the
page dots look like the controls they always were.

**Hacker News could open a different article than the one you
tapped.** Switching to SAVED and back left the front page drawing
your saved list under the front-page heading, so a tap opened
whatever sat at that position in the other list. With nothing saved
the same fault read as "nothing to read", which is why it went
unnoticed. Backing out of the Wi-Fi picker also threw you out of the
app; on a device that has never joined a network that made the
saved-articles shelf unreachable.

**Trivia stopped asking you the same question twelve times.** Within
one round nearly every answer was the same, because the chooser
walked forward through a pack stored in category order. It now
starts from a fresh point each time. Quizmaster also gained an exit;
it had none.

**An exact guess in WAVELENGTH now pays 5, not 6**, which is what
every screen already said. Records set before this update were
scored on the old ceiling and have deliberately been left alone. The
round can also be paused now -- Back opens a screen carrying the
scoring table, so "how many points is one off?" no longer costs you
the round -- and abandoning a round is counted and shown to the
table instead of being silent.

**Murdle no longer takes away marks you made.** On a full grid, one
tap could rewrite up to three other squares, two of them answers,
with no undo. A tap now writes its own square and no other, and the
crosses the grid worked out for you are drawn lighter than your own.

**Jaipur's running score was crediting the camel bonus only to
you**, so the strip could say you were ahead while you were behind.

**Connections draws all sixteen tiles at one size.** A word too long
for its tile used to shrink alone, in a grid whose whole premise is
that the sixteen are comparable.

**Get Books says what it is doing while it downloads**, instead of
showing a still screen that was indistinguishable from a hang, and
it no longer loses a book's title when saving it to the card.

### 1.12.2

**Trivia asks whether there is room before it downloads.** The
question pack is about 6 MB, and until now the app wrote it without
checking. On a nearly full card that fails somewhere in the middle,
and the app that suffers is not Trivia: it is Study, whose review
log then cannot be written, and which loses answers rather than
refusing. Trivia now checks first and stops with a message instead,
and it tells you the two cases apart. If the card is genuinely too
small it says how much it needs and how much you have. If the card
would not answer at all it says that instead, rather than pretending
to know.

**A frontlight that fails to start now says so.** It could not
before, which is most of why 1.11.1 and 1.12.0 shipped with a light
that would not turn on: the firmware reported success either way.

### 1.12.1

**The front light works again on the X4 Pro.** If you installed
1.11.1 or 1.12.0, the light stopped turning on: the panel opened,
the sun filled in, the setting remembered you had asked, and nothing
lit up.

**After updating, open the light panel and both switch it on and
raise the brightness.** Those two settings live on the SD card, not
in the firmware, so if you spent a while trying to get the light to
respond, the card may have kept the switch off and the slider near
the bottom from those attempts.
