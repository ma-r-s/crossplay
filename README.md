<h1 align="center">
  <img src="site/assets/logo.svg#gh-light-mode-only" width="72" alt=""><br>
  Crossplay
</h1>

<p align="center">
  <strong>E-ink is good at waiting.</strong><br>
  So is a chess position, a flashcard, a hand of cards you are counting.
</p>

<p align="center">
  <a href="https://github.com/ma-r-s/crossplay/releases">Releases</a> &middot;
  <a href="docs/shelf.md">What is on it</a> &middot;
  <a href="docs/identity.md">Design language</a> &middot;
  <a href="LOCAL_SCOPE.md">Scope</a>
</p>

![Crossplay on the Xteink X4 Pro](site/assets/shots/og.png)

Crossplay is a fork of [CrossPoint](https://crosspointreader.com/) for the
**Xteink X4 Pro**. CrossPoint turns the device into an excellent e-reader.
Crossplay keeps all of that and adds the other things a screen that holds still
is good at: games you think about rather than react to, spaced-repetition
flashcards, comics, and two devices that play together with nothing to set up.

It is a personal fork, built in the open. Every CrossPoint release is merged in
and the reading side is untouched.

## What is on it

| | |
|---|---|
| **Chess** | A full engine on the device, or a board between two of them. |
| **Battleship** | Lay out a fleet, then hunt someone else's. |
| **Connections** | The daily word grid, with an archive of past boards. |
| **Solitaire** | Klondike, turned sideways because that is the shape of a tableau. |
| **D&Diagrams** | A nonogram whose clues are a dungeon. 64 of them. |
| **Insider** | A party game for a table and one device. |
| **Jaipur** | The two-player trading game, solo or nearby. |
| **Murdle** | A logic grid built through the solver, so you never have to guess. |
| **Study** | Anki decks with the FSRS scheduler, offline. |
| **Hacker News** | The front page in a reading serif, articles kept on the card. |
| **xkcd** | The archive, packed for the card and drawn one to one. |

Chess, Battleship and Jaipur play over **PLAY NEARBY**: put two devices next to
each other and they find one another. No pairing screen, no room code, no
account, no router, no internet.

## Install it

Crossplay targets the **Xteink X4 Pro** only. For every other supported device,
CrossPoint upstream is the right answer and is excellent.

1. Download the latest [release](https://github.com/ma-r-s/crossplay/releases).
2. Flash it exactly the way you flashed CrossPoint: the same web flasher works,
   over USB, from Chrome or Edge.
3. Flashing replaces the firmware, not the SD card. Your library, your reading
   positions and your fonts are files on that card and are left alone.
4. Flashing stock CrossPoint over the top puts the device back where it was.

## Try it without a device

The site runs the real firmware in the browser: same sources as the device
build, compiled to WebAssembly, with an SD card of its own. Click the screen in
the hero, or ask for two devices and watch them find each other.

## Build it

```bash
pio run -e x4pro                 # the firmware
pio run -e simulator_x4_pro      # a desktop simulator, SDL2 + a FreeRTOS shim
./scripts_local/check.sh         # host tests and both builds
```

`ui:paperOnTheBand` is a known baseline failure; a tree where it passes is one
whose work fixes it.

Everything this fork owns lives in `src/apps_local/`, which is what keeps the
merge with upstream close to conflict-free. Read
[LOCAL_SCOPE.md](LOCAL_SCOPE.md) and [docs/shelf.md](docs/shelf.md) before
adding anything, and [docs/building-apps.md](docs/building-apps.md) for how an
app is put together.

## The website

`site/` is static: one HTML file, one stylesheet, the assets they name, and the
browser build under `site/emulator/`. See [site/README.md](site/README.md) for
how to serve it (a plain `http.server` will not do, the threads need COOP/COEP)
and what has to be true before it deploys.

## Credit and licence

Crossplay is MIT, like the project it forks. It stands on
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) and the
[FreeInk SDK](https://freeink.org/); upstream's own README is kept at
[docs/crosspoint-readme.md](docs/crosspoint-readme.md).

xkcd comics are by Randall Munroe, [CC BY-NC 2.5](https://xkcd.com/license.html),
fetched by the device from [xkcd.com](https://xkcd.com). Connections puzzles are
the New York Times'; Crossplay ships none of them and downloads only what you
ask for. Type is Jersey 25 and Instrument Serif, both SIL OFL.
