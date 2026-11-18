# BOOX Picco

**Experimental. Nobody working on CrossPlay owns one, and this build has not
been on a Picco.** Everything below is either read out of a firmware that
supports the Picco officially, or checked on this Mac without the device, and
each section says which.

The references, all official Picco support elsewhere:

- **CrossPoint 2.0**, which this fork is built on: the board's `picco` env and
  `partitions_picco.csv` (#3947), its panel variants (#4146), the slide switch
  as a hardware setting (#4234), the app-only upload (#4248, fixed in #4510),
  and the Picco cases in `main.cpp`, `SettingsList.cpp` and `SettingsActivity.cpp`.
- **The FreeInk SDK's `PICCO` profile** (`BoardConfig.h`), recovered from the
  retail firmware: pins, panel, TMA525C touch, frontlight, charger, insets.
- **CrossPoint's web flasher** (`crosspoint-tools`, `flasher.js`
  `flashAppOnly`), which installs onto a Picco by writing only the app partition
  the bootloader boots.
- **The CrossPoint simulator's Picco profile** (`docs/picco.md` in
  `crosspoint-simulator`), which this fork pins.

And two contributors: Miguel Schulz's #320, which ran on his Picco for about two
weeks and is the base of this port, and glintux's #327, whose hardware findings
(the ROM bootloader combination, TLS running out of internal RAM) are credited
where they are used.

## The partitions are the vendor's

The Picco keeps its maker's table (`partitions_picco.csv`, read from a retail
unit):

| Partition  | Offset     | Size       |                                               |
| ---------- | ---------- | ---------- | --------------------------------------------- |
| `nvs`      | `0x9000`   | `0xC000`   | the vendor's NVS                              |
| `otadata`  | `0x15000`  | `0x2000`   |                                               |
| `safeboot` | `0x20000`  | `0xC0000`  | factory app: the vendor's recovery image      |
| `app`      | `0xE0000`  | `0xE70000` | ota_0: the one slot CrossPlay is written into |
| `spiffs`   | `0xF50000` | `0x80000`  |                                               |
| `coredump` | `0xFD0000` | `0x10000`  |                                               |

Installing and updating CrossPlay write `app` and nothing else, as CrossPoint
does. (The running firmware is another matter, exactly as stock and CrossPoint
are: it keeps its settings, Wi-Fi and device id in the vendor's `nvs`, a panic
writes `coredump`, and the Arduino core erases `nvs` whole at boot if it is full
or in a newer format than the core reads. Whatever the vendor kept in `nvs` is
then gone; the backup below is the only copy.) Every other board
here is installed with a merged image at `0x0`, which lays this fork's own table
down; on a Picco that would replace the vendor's boot region and put the app
over `safeboot`. So the Picco never gets a merged image, and with one app slot
it cannot update itself either. Each path, and what it does with a Picco:

| Path                                              | Picco                                                                                                                                                                          |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| A release                                         | `firmware-picco.bin` and `crossplay-<version>-picco.elf`; no `-full.bin`. `ship.sh` refuses to publish a Picco image that is merged                                            |
| CrossPoint's web flasher, BOOX Picco, Custom .bin | Writes `firmware-picco.bin` into the app slot only. The supported way to install                                                                                               |
| `esptool ... write_flash 0xE0000`                 | Writes the app slot only ([install.md](install.md#boox-picco))                                                                                                                 |
| `pio run -e gh_release_picco -t upload`           | Upstream's `scripts/app_only_upload.py` strips the bootloader, table and boot_app0; `scripts_local/app_slot_guard.py` refuses anything left, an erase, a filesystem upload and `pio debug`, and deletes the build's `firmware.factory.bin` (0xFF up to `0xE0000`, then the app) |
| The site's Install button                         | Reads the device's partition table, finds `safeboot`, writes nothing and says where to go                                                                                      |
| `scripts_local/usb-flash.sh`                      | Refuses a `picco` env outright, and reads the device's table and refuses one that names `safeboot`                                                                             |
| Check for updates, SD-card update                 | Not offered on the Picco (upstream's `SettingsActivity`)                                                                                                                       |
| Boot into the SD firmware picker                  | Disabled on the Picco (upstream's `main.cpp`)                                                                                                                                  |
| Developer Mode's Wi-Fi flash                      | `wifi-flash.sh` and `check.sh --flash` refuse it; the device itself answers "no OTA partition", because `updateSlot()` will not hand any flash path the running slot           |

`updateSlot()` (`src/network/UpdateSlot.h`, from #320) is the device-side half.
esp-idf's `esp_ota_get_next_update_partition()` falls back to the first OTA slot
when no other follows the running one, so on this table it names the slot that
is running, and `FirmwareFlasher`'s raw erase would wipe it. All four flash
paths ask `updateSlot()` instead, which says there is none.

## What an app sees

No app in `src/apps_local` reads its board. Each lays out from the renderer's
size, `BoardConfig::ACTIVE.viewableInsets` and the theme's `uiScale`, and reads
Up and Down, taps, swipes and the left-edge Back gesture. On all of those the
Picco is the Sticky, a board every app has been built and photographed against:

- **Screen**: an 800x480 panel, 480x800 in the apps' portrait, `uiScale` 1.2,
  and the SDK's default bezel insets (top 9, sides and bottom 3), which are a
  guess on the Sticky and the Picco alike. Nobody has measured the Picco's glass.
- **Keys**: Up (GPIO21) and Down (GPIO9) page as on the X4 Pro; Power is GPIO2.
  GPIO5 is carried as Confirm with its physical role unconfirmed; if it is a
  key it is an alias, as the Sticky's third key is, and nothing needs it. There
  is no Back, Left or Right key. See [buttons.md](buttons.md).
- **Gestures**: Back is the left-edge swipe; there is no capacitive Home key, so
  Home is the up-swipe from the bottom edge, as on the Sticky.
- **The slide switch** (GPIO6) is upstream's setting, **Settings > Controls >
  Slide Switch**: the frontlight (the default), night mode, or the reader's
  touch controls. No app reads it.

`host-tests/picco/test_profile.cpp` compiles the SDK's `PICCO` profile and holds
it to that list, pins included, against `STICKY`, so a sync that measures the Picco's insets or
moves its keys fails there and sends someone back to the screens.

**Simulator.** The pinned `crosspoint-simulator` has a Picco profile, and
`CROSSPOINT_SIM_ENV=simulator_picco ./scripts_local/sim-shot.sh ...` runs this
firmware against it (T moves the slide switch, Return is the GPIO5 Confirm).
Every shelf app's first screen (25 games, 9 apps) was opened in both
`simulator_picco` and `simulator_x4_pro` on the same empty card, and all 34
screens are pixel-identical between the two profiles. The only two differing
captures were not apps: the player's random three-word name, made fresh on each
empty card, and a file list holding folders the earlier run's apps had made. So
nothing an app draws depends on which of the two boards it runs on, and no
screen overflows on the Picco that does not on the X4 Pro. What the simulator
cannot say: touch calibration, the real panel, the real keys.

## Memory and flash

- **wolfSSL's heap is in PSRAM**, on the Picco only
  (`src/network/WolfsslPsramAllocators.h`, from #320, from glintux's report in
  #327 of TLS handshakes failing on a real Picco). `malloc` sends only blocks
  above 4 KB to PSRAM, so a handshake's many smaller ones otherwise take the
  internal RAM the Wi-Fi driver also allocates from. Its effect on free internal
  RAM and handshake time has not been measured. #327's other changes (display
  buffers in PSRAM, Wi-Fi modem sleep off for the whole session) are not taken;
  #320 says why.
- **Flash**: the release image is 7,388,530 bytes of a 15,138,816-byte slot
  (48.8%), against 88.6% of the X4 Pro's 7.94 MB slot; static RAM 59.4%, the
  X4 Pro's figure within a kilobyte.

## Not verified, and what would verify it

Everything that needs the device:

1. **It boots at all**: panel, card, touch and PSRAM come up from this build,
   written at `0xE0000` with CrossPoint's web flasher. CrossPoint ships a Picco
   build from the same HAL; this one adds the fork's apps above it.
2. **Touch lands where it is drawn** in all four orientations, near every edge.
3. **Every app**: open Games and Apps, start a game, leave it with the back
   swipe, page a list with the side keys, go Home with the bottom-edge swipe.
4. **GPIO5**: what it is, and whether pressing it does anything unwanted.
5. **The slide switch** in each of its three bindings.
6. **Wi-Fi and TLS**: Hacker News, xkcd and a Study sync, watching free internal
   RAM; this is where #327 saw failures.
7. **Sleep and wake** from a game and from a book, on battery and on USB.
8. **The bezel**: whether the glass hides more than the default insets assume.
9. **The refusals on a real cable**: the site's Install button and
   `usb-flash.sh` reading a Picco's table and stopping, which nobody has
   watched happen.
10. **Forehead's landscape keys**: the X4 Pro's mapping (Up is the bottom key
    once the panel turns counter-clockwise) depends on where the keys sit, and
    the Picco's volume keys sit elsewhere.
11. **The vendor's `nvs`** surviving the first boot, see above.

One gap no check here closes: `pio run -e x4pro` (or `sticky`, `papermono`)
`-t upload` with a Picco on the cable writes this fork's layout over it, as it
would over any S3. Only the site's Install button and `usb-flash.sh` read the
device's table first.

Before any of it, back up the whole flash once and keep the file; it is the
only copy of the vendor's partitions:

```bash
esptool.py --chip esp32s3 read_flash 0 0x1000000 picco-stock.bin
```
