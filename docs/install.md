# Installing CrossPlay

The README covers the one-click browser install, which is what almost
everybody wants. This file is the rest: installing by hand, updating a device
you already flashed, and reflashing with no cable at all.

Everything here is for the **Xteink X4 Pro** and the **Seeed reTerminal
Sticky**, and only those. Both are ESP32-S3. The plain X4 and the X3 are
ESP32-C3, these binaries are not for them, and flashing one there is a
cross-chip flash. Install [CrossPoint](https://crosspointreader.com/) on those
instead: it is excellent, and it is what this is built on.

Between the two S3 devices the firmware protects you. Every image carries its
board name and both updaters refuse an image built for the other board.

You do not need to have installed CrossPoint first.

## By hand, over USB

1. Download your device's full image from the
   [releases page](https://github.com/ma-r-s/crossplay/releases):
   `crossplay-<version>-x4pro-full.bin` for the X4 Pro,
   `crossplay-<version>-sticky-full.bin` for the Sticky. Each is the whole
   firmware: second-stage bootloader at `0x0`, partition table at `0x8000`,
   application at `0x10000`, in one file.
2. Plug the device into a computer over USB.
3. Install [esptool](https://github.com/espressif/esptool) if you have not
   (`pip install esptool`, same on Windows, macOS and Linux) and run the line
   for your device:

   ```bash
   esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 crossplay-<version>-x4pro-full.bin
   ```

   ```bash
   esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 crossplay-<version>-sticky-full.bin
   ```

If a flash goes wrong, [fix-bricked-xteink.md](fix-bricked-xteink.md) is the
way back.

## Updating an install you already have

The release also carries `firmware.bin`, which is the application on its own.
That is the file for a device that already has a bootloader, and it needs no
cable: **Settings > Check for updates** fetches it over Wi-Fi, or you can copy
it onto the SD card and choose it from the same screen. The updater matches
that exact filename, so do not rename it.

`-full.bin` is for the USB install only. Do not hand it to the on-device
updater: that would be writing a bootloader into a slot meant for the
application.

**Do not follow the install steps on the v1.0.0 or v1.0.1 release pages.** Those
two releases published the application image alone and told you to write it to
`0x0`, which on an ESP32-S3 is where the second-stage bootloader lives. Every
release since publishes a `-full.bin`, and the steps above are the current ones.

**A device first installed before v1.5.3 has smaller firmware slots, and may
refuse the update.** The partition table is written by a USB flash and never by
an update over the air, so those devices keep 6.25MB slots for life while newer
installs get 7.94MB. Whether a given release still fits the smaller slot
changes release to release, and [open-items.md](open-items.md) carries the
current measurement rather than this file. When it does not fit, the device
says so and stops before erasing anything. The remedy is one USB flash using
the steps above, which rewrites the table and moves the device to the larger
slots permanently.

## A USB-locked X4 Pro

Some X4 Pros sold through third-party stores (AliExpress among them) ship
with USB flashing locked. The browser installer and `esptool` cannot reach
one: the serial device picker never shows it. Units bought from xteink.com
are not locked.

The route that works, reported by a user on
[issue #206](https://github.com/ma-r-s/crossplay/issues/206) (2026-09-23):

1. Install stock CrossPoint with the
   [Xteink Unlocker](https://crosspointreader.com/unlocker), which delivers
   it through the device's own update mechanism over Wi-Fi. Use CrossPoint's
   stable release.
2. Download CrossPlay's **`firmware.bin`** from the
   [releases page](https://github.com/ma-r-s/crossplay/releases). The
   app-only file, exactly that name: not the `-full.bin`, not the `.elf`.
3. Copy it to the root of the SD card, then on the device:
   **Settings > System > SD Card Firmware Update**, and pick it.

From then on **Settings > Check for updates** works over Wi-Fi like on any
other unit, and the same SD Card Firmware Update screen takes CrossPoint's
own `firmware.bin` if you ever want to go back.

What the updater's refusals mean, on CrossPoint and on CrossPlay alike:

- **Invalid firmware file**: the file is not an application image. The
  `.elf` from the releases page, a download that stopped early, or a file
  that was renamed from something else. Download `firmware.bin` again.
- **Firmware is for a different device**: the Sticky's `firmware-sticky.bin`
  on an X4 Pro, or the other way round.
- **Firmware too large for partition**: the device's partition table has
  smaller application slots than the image needs (CrossPlay's `firmware.bin`
  passed 6.25MB at v1.12.51). A USB flash would rewrite the table, and on a
  USB-locked unit that is the one thing nobody can do, so that release does
  not fit that device.

Read CrossPoint's own warning before you start: the unlock tool officially
supports only CrossPoint and CrossInk, and a USB-locked device has no way
back except over the air or from the card. CrossPlay keeps both doors open
(the update check and the SD updater are CrossPoint's), but a firmware that
loses them would be the end of the road on that unit.

## Reflashing without a cable

**Settings > System > Developer Mode** turns any device into one you can flash
over Wi-Fi, including a device that has only ever run shipped releases: it is a
setting, not a build flag. Pair once with the six-digit code the screen shows,
then every flash after that is one command.

```bash
./scripts_local/wifi-flash.sh --pair 123456
./scripts_local/wifi-flash.sh
./scripts_local/wifi-flash.sh --disable
```

It is off until you turn it on, it says on the panel that the device will not
sleep while it is on, and `--disable` closes it again. It also serves the last
panic, its backtrace and the log lines from before the reset at
`GET /api/dev/crash`, which otherwise needs a cable to read. A device in a
multiplayer match is off Wi-Fi and cannot be flashed.

[developer-mode.md](developer-mode.md) has the rest, including what protects
it.

## What a flash does not touch

Flashing replaces the firmware, not the SD card. Your library, your reading
positions and your fonts are files on that card and are left alone. Installing
stock CrossPoint over the top puts the device back where it was, which makes
this cheap to try.
