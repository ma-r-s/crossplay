# CrossPlay

CrossPlay is a fork of [CrossPoint](https://crosspointreader.com/) that keeps the
whole e-reader and adds the other things a screen that holds still is good at.
The same build runs on reTerminal Sticky and the Xteink X4 Pro.

- Project: https://github.com/ma-r-s/crossplay
- Site: https://crossplay.ma-r-s.com
- Issues: https://github.com/ma-r-s/crossplay/issues
- Upstream: https://github.com/crosspoint-reader/crosspoint-reader
- License: MIT

## What it adds

Nineteen games you think about rather than react to: Chess, Checkers, Connect
Four, Yahtzee, Knucklebones, Battleship, Solitaire, Sudoku, Minesweeper,
Connections, Murdle, Jaipur, Sea Salt, Toy Battle, D&Diagrams, Insider,
Forehead, Trivia and Wavelength.

Five apps: Study (Anki decks reviewed offline on the device, scheduled with
FSRS-5 and Anki's learning steps), Hacker News, the xkcd archive, Get Books (an
OPDS catalog browser that downloads straight to the card) and Instapaper.

Nine of the games (Chess, Checkers, Connect Four, Yahtzee, Knucklebones,
Battleship, Jaipur, Sea Salt and Toy Battle) can be played between two nearby
devices over PLAY NEARBY, which finds the other device by itself over ESP-NOW.
There is nothing to pair, no account and no network.

Reading is unchanged. The EPUB engine, sync and file browser are CrossPoint's.

## Controls

Touch throughout. The two side keys (GPIO5, GPIO6) page the shelf and the
reader; the shared Confirm/Power key (GPIO4) confirms on a click and sleeps on a
hold. Back is a left-to-right swipe starting from the left edge of the panel,
not a button. The games are driven by touch and do not use the side keys.

## Setup

1. Flash from the Playground card.
2. Insert a microSD card before first boot. Saves, player identity, books and
   flashcard decks all live on the card, and the Sticky does not ship with one.
   Without a card the firmware stops at a full-screen "SD card error" and goes
   no further.
3. Open the shelf and pick an app.

## Package origin

The `firmware/1.12.11/` package is the published release artifact for
[v1.12.11](https://github.com/ma-r-s/crossplay/releases/tag/v1.12.11), built by
the project's GitHub Actions release workflow. It is a single merged image
written at offset 0, not a four-part package.

- File: `crossplay-v1.12.11-sticky-full.bin`
- Origin: https://github.com/ma-r-s/crossplay/releases/download/v1.12.11/crossplay-v1.12.11-sticky-full.bin
- Size: 6,643,296 bytes
- SHA-256: `c07efd799ee3d520ff5ff7e407b3401f5e2dfd271fc226d3e248c93f848b21cc`
- MD5: `d7a2f89dcff2fb41fe52836a96ca3d60`

The merged image contains the bootloader at 0x0 (ESP32-S3, chip id 0x09), the
partition table at 0x8000, erased OTA-selection data at 0xe000 and the
application at 0x10000. The partition table spans exactly 16MB, with two 7.94MB
OTA slots, which is where the manifest's `flashSize`, `flashMode` (dio) and
`flashFreq` (80m) come from: all four were read back out of the image header
rather than copied from another entry.

## Hardware test record

**Read this before reviewing: the submitted 1.12.11 package has not itself been
flashed to a reTerminal Sticky.** What follows is the verification that does
exist, and it is at an earlier version.

Verified on reTerminal Sticky production hardware (the sample Seeed supplied) on
2026-08-26, running **version 1.4.0**, driven over a serial bridge with
screenshots read back:

- boot to Home, and shelf paging with the two side keys;
- a full game of Chess against the on-device engine;
- Solitaire in landscape, with the orientation-mapped Back swipe;
- Study's empty state, and Settings;
- PLAY NEARBY radio init and the LOOKING screen;
- PLAY NEARBY between the Sticky and an Xteink X4 Pro: discovery, pairing, and a
  chess opening exchanged correctly in both directions;
- microSD: a blank card formatted to FAT32, mounted at 40MHz on the shared
  display bus; a game save and the player identity both survived a reboot;
- heap approximately 243KB free idle, 216KB with the radio up.

Not verified:

- **This 1.12.11 package on hardware.** Versions after 1.4.0 are built for the
  Sticky by CI on every release and are covered by the project's host test suite
  and stack-budget checks, but no 1.12.11 install onto the Sticky is recorded.
- **PLAY NEARBY between two Stickys.** Only one Sticky exists here, and
  two-device play needs two. Sticky-to-X4-Pro is the verified path.

I would rather say this plainly than tick the box. If you want the device test
completed against this exact package before the entry is published, say so on
the pull request and I will flash it and report back.
