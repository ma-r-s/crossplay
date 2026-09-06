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

### On the network, a device that is off looks exactly like one that is asleep

`--disable` leaves no code on the panel, no route, and no other visible sign
that anything changed. That is correct -- switching Developer Mode off should
leave an ordinary reader -- and it is completely indistinguishable from the
outside from a unit that has gone to sleep, lost its network, or been powered
down. All four give the same silence ON THE NETWORK: no HTTP, no discovery
answer, nothing.

**Over USB they are not the same, and that is the tell.** Measured 2026-08-28
with one of each on the desk at once:

| symptom | meaning |
|---|---|
| no `/dev/cu.usbmodem*` at all | asleep, or unplugged |
| port present, serial silent, **Wi-Fi answering** | the CABLE is wedged; the device is fine |
| port present, serial silent, Wi-Fi silent too | wedged AND off-network (a match, or a screen holding the radio) |

So check Wi-Fi before reaching for the power button. The X4 Pro's native USB CDC
wedges under sustained serial driving and `esptool` cannot rescue it either --
there is no auto-reset circuit -- but the wedge takes the CABLE down, not the
device. One unit served HTTP throughout an entire night of it.

Standing in front of the device: a sleeping one paints the sleep screen, a
wedged one still shows whatever app it was on. E-ink retains either after power,
so trust the screen's CONTENT, not that there is an image.

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
| `GET /api/dev/serial` | | the cable's TX counters (see below) |

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

**On an X4 Pro there is never a backtrace at all.** `HalSystem`'s frame capture
sits under the `#else` of `#if !__riscv` (`lib/hal/HalSystem.cpp`), and the X4
Pro is Xtensa, so `Stack memory:` is empty for *every* crash on this board. It
reads like evidence that failed to record; it is a feature that was never
compiled in for this target. The Sticky is Xtensa too.

**An empty `Panic reason:` is a finding, not a gap.** `panicMessage` is written
only by `__wrap_panic_abort`, so empty means `panic_abort` never ran: not an
assert, not an `abort()`, and not a failed allocation under `-fno-exceptions`
(which aborts). Together with `isRebootFromPanic()` that leaves a CPU exception
or a lockup -- a bad memory access -- which is most of the way to an answer.

### When the report names nothing, read the coredump partition

`partitions.csv` carries `coredump, data, coredump, 0xFF0000, 0x10000`, and
nothing above reads it. It holds a real ELF core: every task's registers and
call stack, the crashed task's name, and the exception. Card #398 was three
crash reports with empty panic reasons and empty stacks, and one read of this
partition named the task, the exception and the exact frame.

**Read it before flashing anything -- a flash destroys it.**

```bash
# 1. off the device (esptool.py from tool-esptoolpy fails on `import rich_click`)
~/.platformio/penv/bin/python -m esptool --port /dev/cu.usbmodemXXXX \
    --chip esp32s3 read-flash 0xFF0000 0x10000 coredump.bin

# 2. the MATCHING elf -- the version that crashed, not the current tip
gh release download vX.Y.Z --repo ma-r-s/crossplay --pattern "crossplay-vX.Y.Z-x4pro.elf"

# 3. gdb, which the toolchain does not install beside the compiler
pio pkg install --global --tool platformio/tool-xtensa-esp-elf-gdb

# 4. decode. --chip goes BEFORE the subcommand.
PATH="$HOME/.platformio/packages/tool-xtensa-esp-elf-gdb/bin:$PATH" \
  ~/.platformio/penv/bin/python -m esp_coredump --chip esp32s3 \
    info_corefile --core coredump.bin --core-format raw <elf>
```

Without gdb it still reports, silently, with no stacks -- so check for
`ERROR: GDB executable not found` before believing an empty result.

**Do not trust the decoded report's own `STACK USED/FREE` column.** For card
#398 it said `6432/1756` for a task that had overflowed. It is a high-water
heuristic. The answer is `readelf -l core.elf`: find the crashed task's stack
segment and compare the stack pointer against it.

**Reading is non-destructive.** Two people debugging one device would otherwise
race, and the first `curl` would win while the second saw a healthy device.
Clearing stays on the on-device crash screen, when a human dismisses it. A
corrupt ring is reported as `logsValid: false` rather than shown as empty --
"nothing was logged" and "RTC memory was garbage" are different findings.

## Driving the device

Since `app/linkradio`, a paired device takes synthetic input and hands back its
screen, so a session can navigate the UI and see the result without anybody
touching the hardware.

```bash
# one tool, two transports -- --port for the cable, --ip for Developer Mode
uv run --with pillow tools_local/device/drive.py --ip 192.168.68.78 \
    shot before.png, tap 240 400, sleep 1, shot after.png
```

- `POST /api/dev/input` -- body is one command line, the same four verbs the
  serial bridge takes: `TAP x y [holdMs]`, `LONG x y`,
  `SWIPE x0 y0 x1 y1 [ms]`, `BTN UP|DOWN|CONFIRM|BACK|LEFT|RIGHT|POWER [holdMs]`.
  Coordinates are panel-native pixels; `drive.py --view` converts from the
  portrait frame the PNGs are saved in. Answers `200` with the device's `OK`
  line, `400` for bad arguments, `409` when an event of the same kind is still
  playing. 409 is a retry, not a mistake: the command was well formed and will
  work once the previous gesture finishes, and `drive.py` waits it out rather
  than reporting failure.
- `GET /api/dev/screen` -- the framebuffer as it stands: 1bpp, row-major, MSB
  leftmost, `X-Panel-Width`/`X-Panel-Height` headers. The same bytes the serial
  bridge streams, so one decoder serves both.
- `GET /api/dev/serial` -- what the USB cable's transmit path has been doing.
  One line: `mac` (which unit this is), `plugged` and `connected` (is a host
  attached, and does it have the port open), `short` and `zero` (writes the ring
  took only part of, and writes it refused outright), `retryMs` and
  `worstStallMsLifetime` (time spent waiting on a full ring, in total and at its
  worst), `timeouts`, and `logDrops` (log lines discarded rather than blocked
  on). `drive.py cdcstat` reads it, and reads `CMD:CDCSTAT` down the cable for
  the same numbers.

  `mac` leads, and it is read from efuse rather than from `WiFi`, so it answers
  with the radio off -- the state a device in a link match is in. Over the cable
  it duplicates what `ioreg` already knows; over Wi-Fi it is the only way to
  tell two identical desk units apart, and identifying a unit immediately before
  writing to it is the standing rule here.

  `plugged` and `connected` come next because without them the rest is
  ambiguous: `HWCDC::write` returns the full size when no host has the port
  open, so a device discarding every byte reports `short=0 zero=0 timeouts=0`,
  byte-identical to a healthy idle one.

  **Those two fields mean nothing on the Sticky.** Its `serialTransport()` is
  `Serial0`, a UART behind a WCH bridge, while `HWCDC::isPlugged()` reports the
  USB-Serial-JTAG peripheral the log does not use, and a `HardwareSerial` is
  always truthy. Read them on the X4 Pro; ignore them on the Sticky. `logDrops`
  is structurally zero there too, because that board logs through
  `esp_rom_printf`, which never reaches the drop path. The rest of the counters
  are honest on both.

  `logDrops` is not an error count. Serial logging is deliberately lossy now: a
  line that does not fit the 256-byte ring at the instant it is written is
  dropped rather than waited on, because waiting is what took the cable down in
  the first place. Under a burst -- boot at `LOG_LEVEL=2` especially -- expect
  to lose lines on a perfectly healthy device. The RTC ring behind
  `/api/dev/log` still receives every line, but it is only sixteen deep, so
  neither channel is a complete transcript.

  It exists over Wi-Fi because the question it answers -- what did the cable do
  -- is unaskable over the cable once the cable is the thing that stopped
  answering, and the RTC log ring is only sixteen lines. A dev build, unlike the
  rest of the table.

**The verbs live in one place**, `lib/DevInput/DevInputCommands.cpp`, and both
transports call it. A device that answers `TAP` down a cable but not over Wi-Fi,
or that takes the arguments in a different order on each, is a trap that only
springs while somebody is already debugging something else.

**Dev builds only**, unlike the rest of Developer Mode. That is not about
secrecy -- anyone who can pair can already replace the firmware, which is
strictly more powerful -- but about cost: the injector overlays every HalGPIO
read, and a shipped reader should not pay for a frame hook nobody will use.

## Playing and flashing are exclusive

A link match takes the radio outright: `LinkRadio::begin()` calls
`devmode::pause()` and `end()` calls `devmode::resume()`. While you are playing,
the device is off Wi-Fi and cannot be flashed or logged. Leave the game and it
comes back on its own within a few seconds.

This is not a policy choice, it is the hardware. An AP association pins the
radio to that AP's channel; ESP-NOW here is fixed to channel 1. Both cannot hold
the radio, so one of them has to yield, and the one the user is looking at wins.

**The bug this replaces is worth knowing, because the shape recurs.** Developer
Mode decided nobody else was using the radio by asking
`WiFi.status() == WL_CONNECTED`. ESP-NOW never associates, so a live match read
as "my connection dropped" -- and dev mode rejoined the house AP
`kMinBackoffMs` (5s) later, dragging the radio from channel 1 to channel 9 with
a game running on it. The symptom was precise and misleading: pairing worked,
the first move landed, and the match died `kPeerTimeoutMs` (10s) after that with
"connection lost". The 5s backoff is exactly the width of the window in which
the first move fit.

`WiFi.status()` answers "am I associated", never "is this radio busy". Anything
that reads it as ownership will miss every non-associating user of the radio.

## Known limits

- **A device left in a match is unreachable and stays awake.** Both halves are
  deliberate and they compound: the link holds the radio (so no Wi-Fi) and
  `wantsAwake()` is true for the whole match (so no deep sleep, because an
  opponent thinking for five minutes is indistinguishable from an idle device).
  Walk away mid-game and the device sits there, off the network, until somebody
  presses a button. Deep sleep would otherwise have been the recovery path,
  since waking is a reset and dev mode rejoins on boot.

  Do not "fix" this by making `inhibitsSleep()` yield-aware -- that changes
  nothing here, because it is `LinkActivity::preventAutoSleep()` holding the
  device awake, not Developer Mode.

- **An unauthenticated client can freeze the UI by trickling a body.** A `PUT
  /api/dev/upload` is refused at `RAW_START`, but this HTTP core drains the body
  on the loop task before the 401 goes out, and `client.readBytes` blocks per
  chunk -- so while it drains there is no button input and no `devmode::update()`.
  A freeze, not a reset: nothing here subscribes the loop task to the task
  watchdog, so `resetTaskWatchdogIfSubscribed()` is a no-op across this entire
  firmware. Pre-dates this branch; not fixable inside the handler.

- **A six-digit code on an open endpoint is worth about four months to a
  determined flood.** MEASURED, not derived: driving `pairing::decide()` with a
  24-hour flood at 1kHz gives 8,497 evaluated guesses a day, so ~118 days to
  walk 10^6 with replacement. `host-tests/devpair` pins that rate, so it cannot
  drift without failing.

  What bounds it is the gate in `pair()` refusing to EVALUATE a guess inside the
  backoff window -- one evaluated guess per interval. Two earlier versions of
  that gate bounded nothing at all, because they compared the code before
  consulting the timer, and a limiter you have already answered limits nothing.

  It is 118 days rather than the 347 the ceiling alone would give, because
  ROTATION RESTARTS THE LADDER: zeroing the failure count sends the backoff back
  to 1s, so it never settles at the 30s ceiling. That is a deliberate trade and
  it favours the owner -- since the gate now applies to a correct code too, a
  permanently-pinned ceiling would mean the owner waiting up to 30s to pair on a
  device nobody is even attacking. Rotation is not what makes grinding
  expensive; it makes it about three times cheaper, and buys back the owner's
  latency.

  If this ever needs to be genuinely hard rather than adequate for a home LAN,
  the answers are per-source-IP limiting or more digits. Rotation is not one.

- **One cached token and code**, in `~/.crossplay-devtoken` and
  `~/.crossplay-devcode`. With two devices you re-pair when you switch between
  them.
- **The radio is still not arbitrated,** but the two places that take it
  outright now say so. Developer Mode will not take a radio already in use, only
  puts down a connection it raised, and `host-tests/release` checks 10 to 13
  discover -- rather than list -- the files that put the radio out of service
  and the files that yield. 13 tests the comment scanner those checks depend on,
  and named canaries assert that specific files are still being discovered:
  three times on that branch a check quietly stopped examining files and went on
  reporting green, and the only tell was the check count.

  **One file is still outside that net,** and note the checks would not catch it
  either -- see below. `StudyActivity` tears the radio down twice: `onExit` does
  it behind its own `wifiActivated_` flag, which is set whenever the app wants
  Wi-Fi rather than when it actually raised the radio, and `endSyncSession` does
  it behind no flag at all. Either way, with Developer Mode holding an
  association it will drop it, and dev mode rejoins ~5s later.

  **Closed on `app/studyradio`, not yet merged** (2026-08-28): `beginSync()`
  sets `wifiActivated_` only when Wi-Fi was down, and both teardowns ask
  `holdsRadio()`. Delete this paragraph when that branch lands.
  Annoying, not fatal, and it predates this feature. Check 10 does not catch it
  because the pattern deliberately excludes `WiFi.disconnect(`: a self-owned
  teardown is a legitimate use of it, and `ClockSyncActivity` is the example of
  doing that correctly (its flag is only set when Wi-Fi was NOT already up).
  `ConnectionsActivity` and `KOReaderSyncActivity` had the same hole through
  `esp_wifi_stop()`, which is unambiguous; both now ask `holdsRadio()` and the
  check covers that call.

  **What the gate does NOT cover, stated plainly:** an ownership flag that is
  set unconditionally. `StudyActivity`'s pattern -- `WiFi.mode(WIFI_STA)` plus
  `wifiActivated_ = true` regardless of whether the radio was already up -- is
  invisible to every check here, because the pattern deliberately excludes
  `WiFi.disconnect(` and says nothing about flags. Nothing would catch a
  regression of it. `ClockSyncActivity` is the shape that is correct by
  construction and worth copying: it only sets its flag when Wi-Fi was NOT
  already connected. There is still no general ownership protocol.

  What the convention missed, and what shipped in v1.6.1: Developer Mode's test
  for "is somebody else using this?" is `WiFi.status() == WL_CONNECTED`, so an
  owner that never associates is invisible to it. ESP-NOW never associates. See
  "Playing and flashing are exclusive" above.
- **The unauthenticated web UI can still overwrite
  `/.crosspoint/settings.json`** by basename, which is another route to enabling
  Developer Mode. It predates this feature and is not caused by it, but this
  feature is what makes it worth more.
- **Remote input and screenshots are dev builds only.** They are gated on
  `CROSSPOINT_DEV_SERIAL_BRIDGE`, the same flag as the injector they schedule
  onto, so a shipped release carries neither the routes nor the per-frame input
  overlay. A release device in Developer Mode can be flashed and read, but not
  driven. See "Driving the device" above.
- **The simulator does none of this.** Two call sites are guarded on `SIMULATOR`
  because the host's `WebServer` shim has no raw-body API. A platform gate, not
  a feature gate.
