# TRMNL

The reader as a [TRMNL](https://usetrmnl.com) screen. It speaks the device API
a TRMNL panel speaks, so anything that drives a panel drives this: trmnl.app,
[Terminus](https://github.com/usetrmnl/byos_hanami),
[LaraPaper](https://github.com/usetrmnl/byos_laravel), or anything else that
answers the two calls below.

## The exchange

```
GET /api/setup    ID: <device id>                       -> api_key, friendly_id
GET /api/display  ID, Access-Token, Width, Height, ...  -> image_url, filename, refresh_rate
GET <image_url>                                         -> a BMP or a PNG
```

- With no API key saved, the reader asks `/api/setup` once and keeps the key it
  gets. On trmnl.app that needs the device registered first (BYOD); on
  Terminus and LaraPaper, auto-join or a device added by MAC does it.
- The device ID is the reader's Wi-Fi MAC unless the phone page names another,
  which is how a reader takes over a panel that is already set up on a server.
- `image_url` may be absolute or a path; paths are resolved against the server.
- An unchanged `filename` is not downloaded again.
- A BMP is kept as it came. A PNG is converted to a 1-bit BMP fitted to the
  requested size, on the card, before it replaces the old picture.
- `reset_firmware: true` drops the saved key, so the next exchange runs setup.
- A key belongs to the device ID it was issued for (`key_for` in `state.txt`).
  A key held under any other ID is dropped before use, and a phone save that
  changes the server or the ID without bringing a new key drops it too.
- The MAC is read from eFuse, not from the radio, which reports all zeros until
  it has started. An all-zero ID is refused rather than sent.
- Back stops a fetch that is waiting on the network.

`TrmnlCore` holds all of that apart from the radio and is what
`host-tests/trmnl` checks.

## Settings

Everything is set on the phone page (QR code on the app's home screen, the
phone icon), stored as `key=value` lines in `/.crosspoint/trmnl/config.txt`:

| Key | Default | Meaning |
| --- | --- | --- |
| `server` | `https://trmnl.app` | Base URL. `https://` is added when missing; a trailing `/api` is dropped. |
| `device_id` | the MAC | What the reader sends as `ID`. |
| `api_key` | empty | The `Access-Token`. Empty means run setup. |
| `refresh_minutes` | 0 | Minutes between pictures; 0 follows the server's `refresh_rate`. Clamped to 1 minute .. 1 day. |
| `orientation` | 0 | 0 sideways, 1 sideways turned over, 2 upright, 3 upside down. Upright asks for 480x800. |
| `clean_every` | 1 | A full (flashing) refresh every N pictures, fast ones between. |
| `wifi_off` | 1 | Bring Wi-Fi down between pictures. |
| `keep_awake` | 1 | Keep the reader awake while the picture is up. |

A failed exchange retries after one minute, doubling, capped at thirty minutes
and at the refresh interval itself.

## What it does not do yet

- **No refresh while the reader sleeps.** It fetches only while the app is
  open; with `keep_awake` off, the reader's own sleep timeout ends it. A wake
  timer in the sleep path would fix that, and the sleep path belongs to Live.
- **Not a sleep-screen source.** The picture is a normal BMP on the card at
  `/.crosspoint/trmnl/screen.bmp`, so wiring it into the sleep screen is a
  small change, but it touches the same settings enum other branches extend.
