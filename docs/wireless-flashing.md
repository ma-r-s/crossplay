# Flashing a desk device without a cable

`scripts_local/wifi-flash.sh` puts this tree's build on a desk device over
Wi-Fi. The loop is about a minute and needs no USB.

```bash
./scripts_local/wifi-flash.sh                 # x4pro build, find the device
./scripts_local/wifi-flash.sh --env sticky
./scripts_local/wifi-flash.sh --ip 192.168.1.42 --build
```

## What it needs

**A dev build already on the device.** The route it drives exists only under
`-DCROSSPOINT_DEV_WIFI_FLASH`, set in `[env:x4pro]` and `[env:sticky]` and
never in a `gh_release` env. A device running a release answers 404, and the
script says so. The first flash onto a release device is still a USB flash;
every one after it is wireless. `host-tests/release` fails if that flag ever
appears in a section a tag builds, including the `*_common` sections the
release envs inherit from.

**The device awake and on a known network.** A dev build joins the
last-connected network at boot and keeps the server up by itself, so after the
first setup there is nothing to press. See "Staying reachable between flashes".

## How it works

Two ordinary requests. There is no new protocol and no second flashing path:

1. `POST /upload` writes `firmware.bin` to the SD card. Same route that uploads
   books, no size cap, streams in 4KB chunks.
2. `POST /api/dev/flash` runs `firmware_flash::validateImageFile` and then
   `flashFromSdPath`, which is the same code the SD-card update and the
   over-the-air update use.

That reuse is the point. The device checks image magic, the segment table, the
XOR checksum, the SHA trailer, the chip id and the embedded board tag before it
writes anything, and it only switches `otadata` at the very end. A failure at
any point leaves the running firmware untouched. Streaming straight into the
OTA partition would have saved one SD write and cost a second, separately
maintained path into `esp_ota_write` that would not share those checks.

The script's last step is not optimism: it records the device's uptime before
flashing and waits for uptime to go *backwards*, which is the only evidence
that the new image actually booted rather than the flash merely reporting
success.

## Staying reachable between flashes

A flash ends in a reboot, and stock behaviour would bring the device back with
no Wi-Fi and no server: `WiFi.begin()` lives in `WifiSelectionActivity` and
nowhere else, and the web server runs only while its settings screen is open.
That is right for a reader in a bag and useless for a desk device, because
every flash would end with a walk over to reopen the screen.

So dev builds also carry `src/DevWifiFlash.{h,cpp}`, which joins the
last-connected network at boot and keeps the server up:

- **Non-blocking.** `devwifi::begin()` runs last in `setup()`, after storage is
  mounted, and only starts the attempt. Boot never waits on an access point.
- **No saved network is not an error.** If nothing has ever been connected
  there is nothing to join; it logs that once and stops, rather than retrying
  forever while a dev wonders why the script finds nothing.
- **Backoff.** A failed join retries at 5s doubling to a 60s ceiling, so a
  device left on a desk near a dead AP is not spinning.
- **It yields the ports.** `CrossPointWebServerActivity` binds the same 80, 81
  and 8134. Opening that screen calls `devwifi::pause()`, closing it calls
  `resume()`. Two servers on one port is a bind failure that reads as "the web
  screen is broken", which is a bad hour.
- **It rejoins rather than assumes on resume.** The screen may have switched to
  AP mode or joined a different network, so what was connected before is not
  necessarily connected after.

Release builds contain none of this.

### The limit worth knowing

**Sleep still wins.** If the device sleeps, Wi-Fi goes with it and the server
dies; `update()` notices the drop, tears the server down and rejoins on wake.
So a device that has been idle long enough to sleep is not reachable until
something wakes it. This is not worked around on purpose: a dev flag that
quietly stopped a device sleeping would change battery behaviour on the exact
builds used to judge battery behaviour.

## Turning dev mode off

There is no switch on the device. The whole feature is compile-time, so a
device leaves dev mode the only way it entered it: by being flashed with a
different build. The good news is that the last flash can itself go over Wi-Fi.

```bash
./scripts_local/wifi-flash.sh --env gh_release_x4pro
```

That sends a release image through the dev route the device still has. When it
reboots it no longer joins Wi-Fi by itself, no longer runs the web server at
boot, and answers neither `/api/dev/flash` nor the serial bridge. **It is
one-way: the next flash needs a USB cable.** The script says so before it
uploads and confirms it afterwards.

Do this when you are done working on a device, particularly before taking it
anywhere that is not your own network -- see below.

### Why you would bother

While dev mode is on, anything on the same network can replace the firmware.
`/api/dev/flash` has no authentication; the validation it does is for
*correctness* (right chip, right board, intact image), not *authorisation*. On
a home network that is a reasonable trade for a desk device. On a shared or
public one it is not.

Release builds are unaffected in every case -- they contain none of this, which
is the reason the gate is compile-time rather than a setting someone could
leave switched on by accident.

## Errors it reports

| status | means |
|---|---|
| 404 | device is on a release build; no dev route exists |
| 422 `TOO_LARGE` | the device's partition table predates the spiffs reclaim; it needs one USB flash |
| 422 `WRONG_BOARD` | this is the other device's image (sticky vs x4pro) |
| 422 `OPEN_FAIL` | the upload never landed on the card |
| 500 | the write itself failed; the running firmware is still intact |
