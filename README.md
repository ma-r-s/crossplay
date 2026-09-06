<h1 align="center">
  <img src="site/assets/logo.svg#gh-light-mode-only" width="72" alt="CrossPlay"><img src="site/assets/logo-dark.svg#gh-dark-mode-only" width="72" alt="CrossPlay"><br>
  CrossPlay
</h1>

<p align="center">
  <strong>E-ink is good at waiting.</strong><br>
  So is a chess position, a flashcard, a puzzle you are halfway through.
</p>

<p align="center">
  <a href="https://crossplay.ma-r-s.com">Try it in your browser</a> &middot;
  <a href="#what-is-on-it">What is on it</a> &middot;
  <a href="#install-it">Install it</a> &middot;
  <a href="https://github.com/ma-r-s/crossplay/releases">Releases</a>
</p>

<p align="center">
  <a href="https://github.com/ma-r-s/crossplay/actions/workflows/crossplay-ci.yml"><img src="https://github.com/ma-r-s/crossplay/actions/workflows/crossplay-ci.yml/badge.svg?branch=xteink" alt="Build status"></a>
</p>

![CrossPlay on the Xteink X4 Pro](site/assets/shots/og.png)

The **Xteink X4 Pro** and the **Seeed reTerminal Sticky** are cheap e-ink
devices with an 800x480 panel, capacitive touch and two physical buttons, and
[CrossPoint](https://crosspointreader.com/) already makes them good at reading.
CrossPlay is firmware that keeps all of that and adds the other things a screen
that holds still is good at: **19 games and 6 apps**,
spaced-repetition flashcards, comics, a read-later queue, and two devices that
play together with nothing to set up.

## Try it without a device

[**crossplay.ma-r-s.com**](https://crossplay.ma-r-s.com) runs the real firmware
in the browser: the same `src/` and `lib/` the device build compiles, put
through `em++` instead of `g++`, against an SD card of its own. Click the screen
in the hero, or ask for two devices and watch them find each other.

It is the real thing rather than a video, with three things faked, all listed
in [site/README.md](site/README.md): the network answers from a snapshot,
Study's headword font is a smaller cut standing in for the large one, and sleep
is off. The snapshot is why Connections is the one game that does not play in
the browser: it fetches the day's board and nothing can be canned for it.

## What is on it

### Games

|                  |                                                                              |
| ---------------- | ---------------------------------------------------------------------------- |
| **Chess**        | A full engine on the device, or a board between two of them.                 |
| **Battleship**   | Lay out a fleet, then hunt someone else's.                                   |
| **Connections**  | The daily word grid, with an archive of past boards.                         |
| **Solitaire**    | Klondike, turned sideways because that is the shape of a tableau.            |
| **D&Diagrams**   | A nonogram whose clues are a dungeon. 64 of them.                            |
| **Insider**      | A party game for a table and one device.                                     |
| **Jaipur**       | The two-player trading game, solo or nearby.                                 |
| **Murdle**       | A logic grid built through the solver, so you never have to guess.           |
| **Checkers**     | English draughts, where taking is compulsory and the board says so.          |
| **Connect Four** | Drop a disc, get four in a line. Seven columns, one tap each.                |
| **Yahtzee**      | Thirteen boxes, three rolls a turn, and the Joker rules in full.             |
| **Knucklebones** | Cult of the Lamb's dice game. Matching dice multiply; yours destroy theirs.  |
| **Minesweeper**  | Tap to dig, hold to flag, tap a finished number to chord its neighbours.     |
| **Sudoku**       | Generated on the device and graded by the technique it needs, not the clues. |
| **Sea Salt**     | Sea Salt & Paper: collect duos, bet on STOP or LAST CHANCE.                  |
| **Toy Battle**   | Nine boards of bases and paths. Hold regions, take medals, solo or nearby.   |
| **Forehead**     | Screen against your forehead, the room shouts clues, sixty seconds.          |
| **Trivia**       | 50,000 questions off 42 years of Jeopardy. Read them out, or play alone.     |
| **Wavelength**   | A hidden point on a spectrum, one clue, and the whole table arguing.         |

### Apps

|                 |                                                                          |
| --------------- | ------------------------------------------------------------------------ |
| **Study**       | Anki decks with the FSRS scheduler, offline.                             |
| **Hacker News** | The front page in a reading serif, articles kept on the card.            |
| **xkcd**        | The archive, packed for the card and drawn one to one.                   |
| **Get Books**   | Browse any OPDS catalog and download straight to the card, no computer.  |
| **Instapaper**  | Your read-later queue, synced both ways: reading position and archiving. |
| **Wallpapers**  | Pick an image on the card as the sleep screen, one tap to set it.        |

And the reader is still CrossPoint's reader: the EPUB engine, sync and the file
browser are theirs and stay theirs.

### Two devices, nothing to type

Nine of the games play over **PLAY NEARBY**: Chess, Checkers, Connect Four,
Yahtzee, Knucklebones, Battleship, Jaipur, Sea Salt and Toy Battle. Put two
devices next to each other and they find one another. No pairing screen, no room
code, no account, no router, no internet.

Per-game rules, state machines and the decisions behind them live in
[docs/apps/](docs/apps/). How the two physical buttons are used, and why there
are only two: [docs/buttons.md](docs/buttons.md).

## Install it

> **The X4 Pro is not the X4.** CrossPlay is for the Xteink **X4 Pro** and the
> Seeed reTerminal Sticky, both ESP32-S3. The plain **X4** and the **X3** are
> ESP32-C3, and writing an S3 image to one of those used to brick it. Install
> [CrossPoint](https://crosspointreader.com/) on those instead: it is excellent,
> and it is what this is built on.
>
> **You do not have to work out which you have.** The browser installer below
> reads the chip off the device before it writes anything, and stops with
> "Nothing was written" if it is a C3. Between the two S3 devices every image
> carries its board name and both updaters refuse an image built for the other
> board.

Open [**crossplay.ma-r-s.com/#get**](https://crossplay.ma-r-s.com/#get) in
Chrome or Edge on a computer, plug the device in, wake it, and press **Install**.
The page downloads the current release and writes it over USB itself; there is
nothing to install first and no command to type. Safari and Firefox have no Web
Serial, and no phone or tablet does either, so the button says so rather than
failing when pressed.

You do not need to have installed CrossPoint first. Flashing replaces the
firmware, not the SD card: your library, your reading positions and your fonts
are left alone, and installing stock CrossPoint over the top puts the device
back where it was.

[**docs/install.md**](docs/install.md) covers the rest: installing by hand with
esptool, updating a device you already flashed, and Developer Mode, which
reflashes over Wi-Fi with no cable. If a flash goes wrong,
[docs/fix-bricked-xteink.md](docs/fix-bricked-xteink.md) is the way back.

Once it is running, [USER_GUIDE.md](USER_GUIDE.md) is the guide to the device
itself: the controls, the reader, the web server and what to do when something
goes wrong.

Every release is flashed to a real X4 Pro and a real Sticky before it ships.
That is still a small field record, so if you install it, please
[say what happened](https://github.com/ma-r-s/crossplay/issues), either way.

## What it does over the network

Most of the shelf never touches the network. Of the parts that do:

- **Reading positions sync to CrossPoint's own server by default.** That is
  upstream's infrastructure rather than this fork's, inherited so that flashing
  CrossPlay over CrossPoint does not orphan an existing sync. The address is a
  setting and can be pointed at any KOSync server.
- **Connections, xkcd, Hacker News, Trivia, Get Books and Instapaper** fetch
  what you ask them for, when you ask. Connections downloads each board from
  the New York Times as you open it and CrossPlay ships none of them; Trivia's
  question pack and xkcd's comics are downloaded once onto the card.
- **Opening a Hacker News article sends its URL to a third party.** The story
  list comes from the public [Algolia API](https://hn.algolia.com/api), and
  opening an article proxies it through [r.jina.ai](https://r.jina.ai) to get
  readable text back, so that service learns what you read. It is a choice, not
  an accident, which is why it is written here.
- **Developer Mode is off** until you turn it on. While it is on the device
  will not deep-sleep, and the panel says so.

## Build it

```bash
git submodule update --init --recursive   # freeink-sdk, and nothing builds without it
pio run -e x4pro                          # the firmware
pio run -e simulator_x4_pro               # a desktop simulator, SDL2 + a FreeRTOS shim
./scripts_local/check.sh                  # host tests and both builds
```

Always name an environment. A bare `pio run` builds `[env:default]`, which is
upstream's ESP32-C3 target: the chip the install warning above says not to
write an S3 image to. [docs/contributing/](docs/contributing/) is the long
version of all of this -- prerequisites, the clone, the hooks, what CI checks
and how a branch lands.

The apps live in `src/apps_local/`, which is what keeps the merge with upstream
close to conflict-free. Read [LOCAL_SCOPE.md](LOCAL_SCOPE.md) for what the fork
owns and what it turns down, [docs/shelf.md](docs/shelf.md) for the shelf
contract, and [docs/building-apps.md](docs/building-apps.md) for how an app is
put together. `site/` is the static website and the browser build;
[site/README.md](site/README.md) says how to serve it.

## Credit and licence

CrossPlay is MIT, like the project it forks. This repository is a GitHub fork
of
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader),
shares its commit history, and merges upstream continuously rather than
re-implementing it. It stands on CrossPoint and the
[FreeInk SDK](https://freeink.org/); upstream's own README is kept at
[docs/crosspoint-readme.md](docs/crosspoint-readme.md).

xkcd comics are by Randall Munroe, [CC BY-NC 2.5](https://xkcd.com/license.html),
fetched by the device from [xkcd.com](https://xkcd.com). Connections puzzles are
the New York Times'; CrossPlay ships none of them and downloads only what you
ask for. Type is Jersey 25 and Instrument Serif, both SIL OFL.

Where a game on the shelf carries the name of a published game, that name is
its owner's trademark and is used to say what the thing is; game mechanics are
not copyrightable. Sea Salt & Paper was designed by Bruno Cathala and Theo
Riviere and is published by Bombyx. Toy Battle was designed by Paolo Mori and
Alessandro Zucchini and is published by Repos Production. CrossPlay implements
the games; it is not affiliated with, endorsed by or sponsored by any of their
owners.
