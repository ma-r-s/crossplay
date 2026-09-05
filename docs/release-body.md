Games and small tools for the **Xteink X4 Pro** and the **Seeed reTerminal
Sticky**, on top of [CrossPoint](https://crosspointreader.com/): a shelf of
games, spaced-repetition flashcards, Hacker News, the xkcd archive, a
read-later queue and a catalog browser for downloading books. Many of the
games play over **PLAY NEARBY** -- two devices next to each other find one
another with nothing to type, and they do not have to be the same device.

### What is new in 1.12.22

- Wt: prune must not delete a tree somebody is working in
- Count a link match, and show the board that ended it (#35, #36)
- Plus 2 changes nothing on the device can see.

### Every release before this one

**[docs/release-notes.md](https://github.com/ma-r-s/crossplay/blob/xteink/docs/release-notes.md)**
is the history: what was new in every tagged release back to 1.12.1.

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
