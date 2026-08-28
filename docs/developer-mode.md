# Developer Mode

Flash and inspect a device over Wi-Fi, with no cable.

**Settings > System > Developer Mode.** The screen shows an address and a
six-digit code. Pair once from your computer and the device is yours to reflash
until you turn it off again.

```bash
./scripts_local/wifi-flash.sh --pair 123456   # once, using the code on screen
./scripts_local/wifi-flash.sh                 # x4pro build, finds the device
./scripts_local/wifi-flash.sh --env sticky
./scripts_local/wifi-flash.sh --ip 192.168.1.42 --build
```

Turning it off is itself a wireless flash:

```bash
./scripts_local/wifi-flash.sh --env gh_release_x4pro
```

## It is a setting, not a build flag

This works on **any** build, including a shipped release. That is the whole
design, and it was not the first one: an earlier version gated the endpoints at
compile time so no release could contain them. It was secure and it was
useless, because the only way to get a device into Developer Mode was to flash
a dev build over the cable the feature exists to remove. Every device needed
one USB flash "first", forever.

A runtime setting has no such bootstrap. A reader that has only ever run
releases becomes a development device from its own menu.

## What protects it

Compile-time gating used to be the answer to "an unauthenticated LAN endpoint
that replaces firmware". Once any user can switch it on, three things replace
that argument.

**It is off by default**, asserted by `host-tests/release`.

**Pairing.** The device shows six digits from the hardware RNG; `POST
/api/dev/pair` exchanges them for a 32-hex token, and every other `/api/dev/`
route requires it. The code is regenerated every time Developer Mode is
switched on, so one glimpsed last week is dead. The token lives only in RAM: it
does not survive a reboot, so a stale token in a script cannot outlive the
session it belongs to. You will re-pair after every flash.

**A smaller surface than the reader's own web UI.** Developer Mode serves
`/api/status` and `/api/dev/*` and nothing else: no file manager, no WebDAV, no
settings API, no WebSocket port. The reader's web transfer screen is
unauthenticated by design, because it is a deliberate, temporary thing you open
and close. Developer Mode stays up for as long as the toggle is on, so serving
that same surface would have turned "I left it on" into "anyone here can browse
my card".

For the same reason `devMode` is excluded from the web settings API
(`isLocalOnlySetting`). Without that, anyone on the network could switch
Developer Mode on through the *unauthenticated* screen, using the temporary
surface to enable the permanent one. Turning a device into a development device
is a decision made at the device.

**With Developer Mode off the routes answer 404**, not 403, so they are
indistinguishable from a build that has none. This hides less than it sounds: a
device with it *on* answers `/api/status` unauthenticated and replies to UDP
discovery, so it is findable by design. The 404 only covers devices that were
never reachable anyway.

## The device will not sleep

Deep sleep on this chip is a full reset. A sleeping device does not idle, it
leaves the network, and nothing on the network can wake it. So Developer Mode
counts as activity and the device stays awake while it is on. The panel says so
rather than letting you discover it.

That is a real battery cost, confined to devices whose owner deliberately
switched it on. The alternative is worse: a development device that disappears
after the sleep timeout is not one.

### A device that is off looks exactly like a device that is asleep

`--disable` leaves no code on the panel, no route, and no other visible sign
that anything changed. That is correct -- switching Developer Mode off should
leave an ordinary reader -- and it is completely indistinguishable from the
outside from a unit that has gone to sleep, lost its network, or been powered
down. All four give the same silence: no HTTP, no discovery answer, nothing.

This matters when more than one person works on these devices. Twice on
2026-08-27 a device changed state under another session and produced a
confident, wrong instruction: "wake it and read the six-digit code" against a
unit whose Developer Mode had since been switched off, and a `curl` against an
already-dark device read as a rejected pairing. In both cases the device looked
broken rather than reconfigured.

So: before concluding a device is faulty, establish which of the four it is.
`ioreg` says whether it is on USB at all, and if a unit is silent on the network
the first question is whether anyone switched Developer Mode off, not what
broke.

## Endpoints

| Route | Takes | Returns |
|---|---|---|
| `POST /api/dev/pair` | `code` | `{token, device, version}` |
| `PUT /api/dev/upload` | raw body | streams an image to the card |
| `POST /api/dev/flash` | `path` | validates, installs, reboots |
| `GET /api/dev/crash` | | panic message, backtrace, log from before the reset |
| `GET /api/dev/log` | | the live log ring |

All but `pair` require `X-Dev-Token`.

### Why upload is PUT

Not taste. This ESP32 core's `FunctionRequestHandler` hands the **same**
callback to the upload path and the raw path with nothing passed in to
distinguish them, and calling `server->upload()` while the server is in raw
mode dereferences a null `_currentUpload` and **resets the device** --
unauthenticated, from anyone on the LAN. Registering the route as `HTTP_PUT`
makes `canUpload()` structurally unreachable, because it requires `HTTP_POST`.
One code path instead of two to tell apart and get wrong.

That reboot shipped twice before it was understood. `host-tests/release`
asserts the method so it cannot come back quietly.

## Crash retrieval

`GET /api/dev/crash` returns what the device remembers about the last time it
died. None of it is captured for this endpoint. `HalSystem` already kept the
panic message and backtrace in `RTC_NOINIT` memory, and `Logging` already kept
the last lines in a second `RTC_NOINIT` ring guarded by a magic word. Both
already survived the reset; all that was missing was a way to read them without
standing at the device.

The log tail is usually worth more than the backtrace: a backtrace says where it
died, the preceding lines say what it was doing.

**Reading is non-destructive.** Two people debugging one device would otherwise
race, and the first `curl` would win while the second saw a healthy device.
Clearing stays on the on-device crash screen, when a human dismisses it. A
corrupt ring is reported as `logsValid: false` rather than shown as empty --
"nothing was logged" and "RTC memory was garbage" are different findings.

## Known limits

- **One cached token and code**, in `~/.crossplay-devtoken` and
  `~/.crossplay-devcode`. With two devices you re-pair when you switch between
  them.
- **The radio is still not arbitrated.** Developer Mode will not take a radio
  already in use, only puts down a connection it raised, and every file that
  tears the radio down now either asks `devmode::holdsRadio()` or tracks its own
  ownership -- enforced by `host-tests/release`, which discovers those files
  rather than listing them. But there is no ownership protocol; `LinkRadio`'s
  "only one thing on the device may own the radio at a time" is still managed by
  convention, and link multiplayer with Developer Mode on is untested.
- **The unauthenticated web UI can still overwrite
  `/.crosspoint/settings.json`** by basename, which is another route to enabling
  Developer Mode. It predates this feature and is not caused by it, but this
  feature is what makes it worth more.
- **No remote input or screenshots yet.** The serial bridge already has
  TAP/SWIPE/BTN and SCREENSHOT; exposing them over this transport is the obvious
  next step and is not built.
- **The simulator does none of this.** Two call sites are guarded on `SIMULATOR`
  because the host's `WebServer` shim has no raw-body API. A platform gate, not
  a feature gate.
