# Live

Leave a handwritten note, a drawing or a photo on a device that is asleep on
somebody's fridge, from a phone, anywhere in the world.

Card #552, branch `app/fridge`. Design settled 2026-09-20; the Wallpapers
tile is built, nothing else is.

## What it is for

Mario's words: a device on his mother's fridge showing a message he wrote the
night before, and a daily message his long-distance girlfriend wakes up to.
Those two cases are the test of every decision below: **the message has to be
there in the morning.**

## The fact the design rests on

**E-ink holds its image at zero power.** The energy budget is not "showing a
message"; it is only the periodic check for a new one. `display.deepSleep()`
delegates to `syncPendingAsync()` before the chip sleeps, so a pending waveform
is waited out and the image is physically on the glass.

## One rule covers every case

**On every sleep: if a refresh is due, fetch it; otherwise arm the timer for
when it will be.**

That single check handles the three situations that look different and are not:

- a fridge that is never touched (wakes on the timer, fetches, sleeps);
- a device in daily use (refreshes on the way into sleep, no timer needed);
- a device picked up and put back down (the idle timeout takes the same path).

It also closes the hole a cold review found: nothing today arms a timer on an
ordinary sleep, so a Live device that was picked up and put down would never
have woken again.

**Paint the sleep screen first, fetch behind it.** If the fetch came first, the
user would press power and watch a live screen for several seconds. The screen
sleeps immediately as it always has, the radio work happens after they have
looked away, and the panel repaints only if something actually arrived. There
is no "connecting" screen because there is nothing to show.

**Live off arms no timer at all**, so the battery cost is not small, it is
identical to today. That is why it is a real toggle and not a buried setting.

## Phase 0, still not optional: measure the sleep floor

**Nobody has ever measured this device's deep-sleep current.** The only figure
anywhere in the repository is the cell size, ~1100 mAh
(`docs/building-apps.md:281`). Two known effects push the floor up and neither
is quantified:

- `power.latch0` (GPIO1) is deliberately held HIGH through deep sleep
  (`HalPowerManager.cpp:92-111`) so the next press fast-wakes, which leaves the
  peripheral rail powered all night, including the frontlight driver IC.
- `FrontlightManager::park()` exists to fix the resulting leakage and is called
  from nowhere. `FrontlightManager.cpp` is ours, so this is our dead code.
  Worse, `Frontlight.begin()` runs unconditionally on every wake
  (`main.cpp:535-537`) and does a `gpio_hold_dis` that clears exactly the hold
  `park()` would set, so a Live wake would also switch the light on at 4am with
  nobody there.

A plausible floor spans ~150 uA to ~1.5 mA, and the frontlight owns almost all
of that spread. At 150 uA a fridge lasts most of a year; at 1.5 mA it lasts a
month and the feature is dead. **The go/no-go threshold sits around 300-500 uA.**

The fuel gauge reads whole percent (`BatteryMonitor.cpp:90`), so on a 1100 mAh
cell one step is 11 mAh. That makes a charged-and-left-alone test a **go/no-go
instrument, not a battery-life instrument**, and it needs about two weeks to
separate 150 from 500 uA. A ~25 GBP USB inline power meter settles the same
question in a minute and also gives the `park()` A/B, which is the
decision-relevant number.

## On the device

### Where Live lives: the combined tile

Mario chose this from three arrangements rendered in the simulator
(`qa-artifacts/live-variants.png`). Cell 0 of the grid, which was already the
`+ Add a wallpaper` special tile, becomes one tile captioned **Your phone**
that leads to a destination offering both intents: add a wallpaper, or set up
Live. A double frame (3px outer, 1px inner inset 5) marks it as not a picture
you own.

When Live is on, the tile takes the ordinary selection marker, exactly as a
chosen wallpaper does, and the wallpapers deselect. No new vocabulary.

**The weakness to solve on the destination, not the tile:** before Live is set
up nothing in the grid says it exists. The destination names both intents; the
tile names only where you are going. Once the device-hosted upload server is
retired both actions genuinely are "use your phone".

### Multi-select is untouched

The header chip (now an icon, still outline, because a filled chip would read
as state) enters CHOOSE A SET. Live and the special tile are not selectable
there. Leaving that mode with a set chosen turns Live off, and the hint strip
says so.

### Time

`esp_sleep_enable_timer_wakeup()` is relative and runs off the internal RC
oscillator, so a daily wake drifts roughly a quarter of an hour. For a fridge
nobody cares, and the design deliberately does not try to correct it.

**Do not assume the board has a battery-backed clock.** An earlier draft of
this document claimed the BM8563 at 0x51 is battery-backed; the cited lines say
nothing of the kind, the only such string in the tree belongs to a different
board, and `WavelengthSave.h:138` states the opposite outright. The RTC is only
ever set by an NTP sync over Wi-Fi.

### Verified TLS

`bridge::Endpoint` + `bridge::streamToFile` (`src/apps_local/bridge/`), the
client Study and Instapaper already use: verified against the baked root
bundle, an SD-card root override so a CA rotation is a file copy, the heap
floor enforced before any TLS attempt, device-identity headers attached free.
Not `HttpDownloader`, which calls `setInsecure()` on every device build.

### Wi-Fi

There is no headless join today; `DevMode::startJoin()` is the template. The
interactive path forces `WIFI_ALL_CHANNEL_SCAN` on every connect, which is
right for a person standing there and wrong for a fridge at 4am.

Failure needs capped exponential backoff, with numbers. The headless join burns
20s of radio before giving up (`DevMode.cpp:87`); a naive 15-minute retry is
roughly 50 mAh/day and kills the device in three weeks.

## The service

**`fridge.ma-r-s.com` on the Orange Pi**, Cloudflare Tunnel, copying
`server/read-bridge/` wholesale including `bridge/ratelimit.py`. Mario's call,
made knowing card #548: the box hard-reboots uncleanly every day or two and
took all three bridges down 14 times in 17 days. He is fixing that separately.

The hostname must be exactly one label below the apex. The zone is on
Cloudflare's free plan, whose Universal SSL covers `ma-r-s.com` and
`*.ma-r-s.com` and nothing deeper.

Measured 2026-09-20, not assumed: the Pi's hosts serve `CN=ma-r-s.com` chaining
GTS WE1 -> GTS Root R4, which is in the device bundle. Vercel serves Let's
Encrypt chaining to ISRG Root YR, which is not, and verifies today only through
a cross-signature. That is a second reason the Pi is the right origin.

Register the host in `pulse_targets` so an outage opens a card by itself.

### The countdown is the service's, never the device's

A sleeping device is unreachable by construction. The server stamps every
pull, and since the server also hands out the interval,
`next = last_checkin + interval` is arithmetic it can do alone.

That figure is an **estimate**: the RC drift moves it, a refresh-on-sleep during
user activity shifts the schedule until the next check-in, and a missed wake off
Wi-Fi is invisible until the following one. So the site says "in about 5 hours",
never "5h 12m 03s", and once the window has passed it stops counting down and
says "hasn't checked in since Tuesday". The device's own countdown and the
site's may differ; neither may be phrased as exact.

After sending, the copy is "she'll see this tomorrow morning", not a duration.

## Pairing

A six-digit code, readable down a telephone. A QR can only be scanned by
somebody holding the device, and the fridge is in another country; the day the
Wi-Fi changes, a QR means a plane ticket. The QR stays as a convenience
underneath. The three-legged flow is `server/read-bridge/bridge/pairing.py`
(`/api/pair/start`, `/api/pair/claim`, `/api/pair/poll`) with an explicit
confirmation on the device before anything is stored.

Several senders per device, revocable on the device. A single-sender model
would lock the owner out the moment they changed phones, and re-pairing needs
physical presence.

## The image

480x800, 1-bit, Floyd-Steinberg, exactly 48062 bytes:
`site/wallpapers/convert.js`, already shared with the firmware's own upload
page through a symlink at `src/network/html/js/wallconvert.js`. No new format,
no new validation.

## The sleep screen

Full bleed, with one hairline and a single line: when it arrived. **No "LIVE"
wordmark on the glass** - the person looking at a fridge does not need our
vocabulary, only to know the message is today's. The word belongs in the
owner's UI.

It never blanks. A failed or empty wake leaves yesterday's message, which is
the correct failure state. The footer date going stale is the signal, and after
several days it says so outright.

## Built so far

- The tile, three variants behind `WALLPAPERS_LIVE_VARIANT` (default 2), the
  chip as an icon, and the empty state. Real captures in `qa-artifacts/`.
- `drawGetSetTile` had a latent bug: its caption was pinned to
  `captionRect(geom, 1)`, so a second special tile would have printed its label
  under the neighbour. It takes its slot now.
- The hint strip was never centred: it hung off `kBodyTop`, spending the whole
  36px body gutter above the line and reserving only `kHintGap` below it. It is
  now `kChromeHeight + (kBodyGutter + kHintGap) / 2`, the same slack split
  evenly, derived rather than restated so it cannot drift from `gridTop`. The
  grid does not move. Measured after: 32px above, 28px below, from 42/18.5.

## Still to build

1. The destination screen: code, countdown, interval, refresh now, who can
   send, turn off.
2. The hint strip does not know Live exists - it still says "Tap one to set
   your sleep screen" while Live carries the marker. Card #354 is the same
   contradiction.
3. Arming the timer on every sleep, and the boot path for a timer wake.
4. `SETTINGS.sleepScreen` defaults to DARK, and `WallpapersCore.h:189-195`
   lists five ways `/sleep.bmp` never reaches the glass. Live must decide
   whether it paints itself or goes through `SleepActivity`, and if the latter,
   what forces the setting. These are different features and it is not decided.
5. The service, the website, the headless join, backoff, and `park()`.

## Not verified

Deep-sleep current, Wi-Fi-active current, battery runtime, real association
time, and whether `park()` materially lowers the floor. Hardware always counts
as not verified.
