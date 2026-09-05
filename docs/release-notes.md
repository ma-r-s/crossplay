Games and small tools for the **Xteink X4 Pro** and the **Seeed
reTerminal Sticky**, on top of
[CrossPoint](https://crosspointreader.com/). Nineteen games --
Chess, Checkers, Connect Four, Yahtzee, Knucklebones, Minesweeper,
Sudoku, Toy Battle, Forehead, Battleship, Connections, Solitaire,
D&Diagrams, Insider, Jaipur, Murdle, Sea Salt & Paper, Trivia and
Wavelength -- plus spaced-repetition flashcards, Hacker News, the
xkcd archive, a read-later queue and a catalog browser for
downloading books. Nine of them play over
**PLAY NEARBY**: Chess, Checkers, Connect Four, Yahtzee,
Knucklebones, Battleship, Jaipur, Sea Salt and Toy Battle. Two
devices next to each other find one another with nothing to type --
and they do not have to be the same device.

### What is new in 1.12.22

- Wt: prune must not delete a tree somebody is working in
- Count a link match, and show the board that ended it (#35, #36)
- Plus 2 changes nothing on the device can see.

### What WAS new in 1.12.12

**GET BOOKS: BACK no longer downloads the book.** Pressing back on a
book's page started the download instead of leaving it.

**And tapping a book no longer looks dead.** The page waited for the
cover before drawing anything, so a tap seemed to do nothing until
the picture arrived. It draws first and fills the cover in after.

### What WAS new in 1.12.11

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

### What WAS new in 1.12.10

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

### What WAS new in 1.12.9

**WAVELENGTH's lock is an ordinary button now.** It used to ask you
to HOLD, without ever saying for how long, and the lock fired while
your finger was still down -- which is how the result screen came to
be drawn underneath the thumb that earned it. One tap locks the
guess, and the reveal appears with your finger already off the glass.

The stray tap that the hold was guarding against is handled by
moving the button instead of timing it: the lock now occupies only
the number column, and the strip below the board is dead space
rather than a smaller target.

### What WAS new in 1.12.8

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

### What WAS new in 1.12.7

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

### What was new in 1.12.6

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

### What was new in 1.12.5

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

### What was new in 1.12.4

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

### What was new in 1.12.3

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

### What was new in 1.12.2

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

### From 1.12.1

**The front light works again on the X4 Pro.** If you installed
1.11.1 or 1.12.0, the light stopped turning on: the panel opened,
the sun filled in, the setting remembered you had asked, and nothing
lit up.

**After updating, open the light panel and both switch it on and
raise the brightness.** Those two settings live on the SD card, not
in the firmware, so if you spent a while trying to get the light to
respond, the card may have kept the switch off and the slider near
the bottom from those attempts.

### If you have not plugged your X4 Pro into a computer since August

**This update will refuse to install over the air, and one USB flash
fixes it permanently.**

Devices flashed before v1.5.3 have a 6.25MB app slot. This release
is about 45KB over it. The device checks the size before downloading
and refuses cleanly -- nothing is damaged, and it now tells you the
remedy rather than only the problem.

<!-- Approximate on purpose. The exact byte count is a property of
the CI build, not of any local one: v1.11.1 came out 506 bytes above
the local measurement and v1.12.0 502 bytes above, same tree,
different toolchain instance. A precise figure written here from a
local gate is wrong by the time anyone reads it. Quote the ceiling,
which is fixed, not the image, which is not. -->

The fix is a one-time flash of the `-full.bin` below, which carries
the partition table that the over-the-air updater never writes.
After it, updates work from the device forever. Sticky owners are
unaffected.

### The short way: press Install

**[crossplay.ma-r-s.com/#get](https://crossplay.ma-r-s.com/#get)**
installs this release for you. Open it in Chrome or Edge on a
computer, plug the device in, wake it, and press Install: the page
downloads the right image and writes it over USB itself, with
nothing to install first and no command to type. Safari, Firefox and
every phone have no Web Serial, and the page says so rather than
failing when pressed.

The files below are for doing it by hand, and for the on-device
updater.

### Which file to download

Each device has its own pair of images; the board name is in the
filename, and the firmware refuses an image built for the other
board.

**`crossplay-<version>-x4pro-full.bin`** /
**`crossplay-<version>-sticky-full.bin`** are the ones to flash
over USB. Each is the whole firmware -- bootloader, partition table
and application in one image -- so it installs on a device that has
never run CrossPoint.

**`firmware.bin`** (X4 Pro) and **`firmware-sticky.bin`** (Sticky)
are the application alone, for a device that is already running
this. Settings -> Check for updates fetches the right one over
Wi-Fi, or you can copy it to the SD card and pick it there. Keep
the filenames: each updater matches its own name exactly.

v1.0.0 and v1.0.1 published only the application image and told you
to write it to `0x0`, which on an ESP32-S3 is where the bootloader
lives. Do not follow the install steps from those two releases.

Try the whole thing in a browser first, without owning either:
**[crossplay.ma-r-s.com](https://crossplay.ma-r-s.com)** runs this
same firmware compiled to WebAssembly.

### Before you flash this

Releases are flashed and booted on the author's own X4 Pro and
Sticky before they ship. PLAY NEARBY between two Stickys is
untested -- one Sticky exists here, and two-device play needs two.

**The X4 and X3 are ESP32-C3**; these images are S3. Flashing them
there is a cross-chip flash. Install
[CrossPoint](https://crosspointreader.com/) on those instead.

**Flashing replaces the firmware, not the SD card.** Your library,
your reading positions and your fonts are files on that card and are
left alone. Installing stock CrossPoint over the top puts the device
back where it was, which is what makes this cheap to try. If a flash
goes wrong,
[docs/fix-bricked-xteink.md](https://github.com/ma-r-s/crossplay/blob/xteink/docs/fix-bricked-xteink.md)
is the way back.

Full install steps are in the
[README](https://github.com/ma-r-s/crossplay#install-it). The short
version, once `pip install esptool` has run -- pick your device's
file:

```
esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 crossplay-<version>-x4pro-full.bin
esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 crossplay-<version>-sticky-full.bin
```
