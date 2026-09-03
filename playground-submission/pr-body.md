## Contribution type

- [ ] Firmware: new community firmware
- [x] Firmware: new partner firmware coordinated with the platform owner
- [ ] Firmware: official Sticky firmware update maintained by Seeed
- [ ] Firmware: update to an existing firmware
- [ ] 3D printable design: new design or update

## Common verification

- [x] `npm test` passes locally. (34 pass, 0 fail)
- [x] `npm run validate` passes locally. (`Registry validation passed (13 firmware(s), 5 printable(s)).`)
- [x] The directory name and the `id` in the metadata file are the same lowercase kebab-case identifier.
- [x] All links use HTTPS and open the intended page. (all four fetched, 200)
- [x] The submitted files contain no user-specific credentials, API keys, tokens, passwords, or private keys.

---

## Firmware

- Firmware name: **CrossPlay**
- Directory: `firmwares/crossplay`
- Firmware version: **1.12.11**
- Upstream project: https://github.com/ma-r-s/crossplay (a fork of https://github.com/crosspoint-reader/crosspoint-reader)
- Source license: **MIT**
- Firmware artifact origin and SHA-256:
  - https://github.com/ma-r-s/crossplay/releases/download/v1.12.11/crossplay-v1.12.11-sticky-full.bin
  - `c07efd799ee3d520ff5ff7e407b3401f5e2dfd271fc226d3e248c93f848b21cc` (6,643,296 bytes)

### Package type

- [ ] Source contribution built by GitHub Actions
- [x] Firmware-only package

### What this firmware provides

CrossPlay keeps the whole CrossPoint e-reader and adds the other things a screen
that holds still is good at.

Nineteen games you think about rather than react to: Chess, Checkers, Connect
Four, Yahtzee, Knucklebones, Battleship, Solitaire, Sudoku, Minesweeper,
Connections, Murdle, Jaipur, Sea Salt, Toy Battle, D&Diagrams, Insider,
Forehead, Trivia and Wavelength.

Five apps: Study (Anki decks reviewed offline on the device, scheduled with
FSRS-5 and Anki's learning steps), Hacker News, the xkcd archive, Get Books (an
OPDS catalog browser that downloads straight to the card) and Instapaper.

Nine of the games can be played between two nearby devices over ESP-NOW, with
nothing to pair, no account and no network. Reading is unchanged: the EPUB
engine, sync and file browser are CrossPoint's.

A microSD card is required. Without one the firmware stops at a full-screen
"SD card error" and goes no further, and the Sticky does not ship with a card,
so the entry says this in `compatibility.notes`, in a flash note and in the
README.

### Firmware verification

- [x] `firmware.json` uses the `group`, `catalogSection`, `mode`, and `category` documented in docs/contributing-firmware.md for the selected type.
- [x] The README identifies the package origin, firmware version, and tested hardware.
- [x] Community entries include author attribution and a real Sticky preview; partner entries use the official logo as identity artwork.

Firmware-only package:

- [x] `source.url` points to the maintained upstream project and `source.license` names its license.
- [x] Every required `.bin` file is committed under `firmware/<version>/`.
- [x] The local manifest records every firmware file, flash offset, byte size, and SHA-256.

The package is one merged image at offset 0 rather than four parts, which the
firmware guide allows ("A single merged image starts at offset `0`"). It is the
artifact the project's release workflow publishes, so anyone can download it
from the release and check it against the SHA-256 above.

`flashSize` (16MB), `flashMode` (dio) and `flashFreq` (80m) were read out of the
image header and the partition table in the submitted binary rather than copied
from another entry. The image contains the bootloader at 0x0 (ESP32-S3, chip id
0x09), the partition table at 0x8000, erased OTA-selection data at 0xe000 and
the application at 0x10000; the partition table spans exactly 16MB with two
7.94MB OTA slots.

### Physical-device test

- Device and hardware revision: reTerminal Sticky, the production sample Seeed supplied
- Installation method: serial bridge (`esptool`), **not this package**
- Tested firmware version: **1.4.0** (2026-08-26), not the submitted 1.12.11
- Main workflow tested: boot to Home; shelf paging on the two side keys; a full game of Chess against the on-device engine; Solitaire in landscape with the orientation-mapped Back swipe; Study's empty state; Settings; PLAY NEARBY radio init and discovery, and a chess opening exchanged in both directions with an Xteink X4 Pro
- Reboot and saved-state result: pass. A blank microSD formatted to FAT32 mounts at 40MHz on the shared display bus; a game save and the player identity both survived a reboot. Heap approximately 243KB free idle, 216KB with the radio up.
- USB reconnection and repeated-install result: not recorded

- [ ] The submitted firmware-only package or a local build of the submitted source was installed on a physical reTerminal Sticky.
- [ ] First boot and the main user workflow passed.
- [ ] Touch and hardware buttons used by the firmware passed.
- [ ] Reboot, saved state, USB reconnection, and repeated installation were tested where applicable.

**These four are deliberately left unticked, and I would rather say so than tick
them.** The verification above is real but it is at version 1.4.0. Versions
after that are built for the Sticky by CI on every release and are covered by
the project's host test suite and stack-budget checks, but no 1.12.11 install
onto a Sticky is recorded, so I cannot honestly claim the submitted bytes have
run on the device.

If you would like that closed before the entry is published, say so here and I
will flash this exact package to the Sticky and report back with the result. I
am equally happy for the entry to wait until then.

---

## Additional context

**On `"group": "partner"`, said out loud rather than slipped in.** The
contributing guide is direct that normal third-party submissions use
`"community"`, so this needs explaining rather than assuming. Seeed approached
this project, supplied the reTerminal Sticky hardware, and asked for a Playground
listing; the top-level README describes `partner` as "coordinated with the
platform owner", and that is the basis for the label here. It is not a claim of
any formal partnership beyond that. **If the maintainers would rather this sat
in `community`, please just say so** and I will change `group`, `catalogSection`
and add a `category` (it would be `fun`, or `ereader` if you would rather lead
with the reader). No argument from me either way.

**Assets.** `assets/preview.png` is the project's official identity artwork,
generated from CrossPlay's own mark and typefaces by a script committed in the
upstream repository. It deliberately carries no screenshot, so nothing in it can
be mistaken for a photograph of a Sticky. `assets/logo.svg` is the same mark with
the colour resolved rather than inherited, since it renders standalone here. If
you would prefer a real photo of the device running CrossPlay once the device
test above is done, I can supply one.

**Support.** Issues are enabled and watched at
https://github.com/ma-r-s/crossplay/issues. The firmware is MIT, and both the
upstream CrossPoint copyright and this fork's are carried in `LICENSE`.
