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

**The web server running on the device.** Settings -> Network -> Web server.
This is the part that is still manual, and the reason is below.

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

## What is deliberately not here yet

**The device does not come back online by itself after a flash.** There is no
boot-time Wi-Fi connect anywhere in this firmware -- `WiFi.begin` appears in
exactly one place, `WifiSelectionActivity` -- and the web server only runs
while its settings screen is open. So after each flash you walk to the device
and reopen that screen for the next one, which blunts most of the point.

Fixing it means connecting from `WifiCredentialStore` at boot and keeping the
server up in the background, both dev-build-only. That was left out of this
version on purpose: it changes app lifecycle, heap and power behaviour, and at
the time it was written both desk units were mid hands-on-test, so it could not
have been verified on hardware. A lifecycle change that has never run on a
device is not something to hand someone as working.

The endpoint below is the half that is verifiable without a device, so it
shipped first.

## Errors it reports

| status | means |
|---|---|
| 404 | device is on a release build; no dev route exists |
| 422 `TOO_LARGE` | the device's partition table predates the spiffs reclaim; it needs one USB flash |
| 422 `WRONG_BOARD` | this is the other device's image (sticky vs x4pro) |
| 422 `OPEN_FAIL` | the upload never landed on the card |
| 500 | the write itself failed; the running firmware is still intact |
