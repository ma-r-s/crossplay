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

## Two pieces, not one: the page is on the site

**The page is `crossplay.ma-r-s.com/live/`** (`site/live/`), built out of the
site's own `styles.css`, its top bar and its two faces, exactly as
`site/wallpapers/` and `site/wikipedia/` are. **The service is
`fridge.ma-r-s.com` and answers `/api/` only.**

It was one piece for a while: the service served both the API and a standalone
page, and that page shared nothing with the site -- not the palette, not the
bar, not the type -- so it was a design orphan that drifted further every time
the site changed.

The split is safe because both names sit under one registrable domain:

- the sender cookie is set with `domain=.ma-r-s.com`, which makes it
  **first-party for both names**. Safari's third-party cookie blocking would
  otherwise end this on an iPhone, and it would end it silently: the claim
  returns 200 and every request after it arrives anonymous;
- **`SameSite=Lax` is enough and stays.** SameSite is decided by the
  registrable domain, not the origin, so the page calling the service is
  same-site and the cookie rides an XHR. Verified in a browser against the
  deployed pair, not reasoned about;
- CORS allows **exactly `https://crossplay.ma-r-s.com`, with credentials**. A
  wildcard could not carry a cookie even if it were wanted, and a list of
  origins is a list of sites allowed to draw on somebody's reader;
- the page's fetches say `credentials: "include"`. `"same-origin"`, the
  default, sends nothing cross-origin and every call reads as "not connected".

**The QR points straight at the page.** It encodes
`https://crossplay.ma-r-s.com/live/?c=<code>`, built by `wallpapersui::liveLink`
from `kLiveAddress` -- the same constant the panel prints beside it, so the
address a person types and the address a phone scans can never name different
hosts. There is no redirect on the service: readers on v1.13.11, whose QR points
at the old `/p/<code>`, stop working until they update, which is the house rule.

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
underneath. The three-legged flow is `server/fridge-bridge/bridge/pairing.py`
(`/api/pair/start`, `/api/claim`, `/api/pair/poll`) with an explicit
confirmation on the device before anything is stored.

Several senders per device, revocable on the device. A single-sender model
would lock the owner out the moment they changed phones, and re-pairing needs
physical presence.

### Adding a phone is a different endpoint from setting one up

`/api/pair/start` mints a NEW fridge. Wiring ADD to it would have
handed the browser a different fridge and silently orphaned both the phone
already sending and the picture already on the glass. `/api/pair/join` takes
the reader's bearer token and mints a code against the fridge it already has.
Two endpoints rather than one with a flag, because a flag defaulted the wrong
way is the same bug back.

**Four phones, and the SERVICE is what decides.** It refuses the fifth with a
409 before a code is minted, in its own sentence, and answers its own cap on
every `/api/senders`. `live::kMaxSenders` and `LiveModel::kMaxSenders` are the
size of the array the reader can hold, kept equal to each other by a
`static_assert` in `WallpapersActivity.cpp`, and `live::listSenders` logs loudly
if the service ever says more: a fifth sender the service allowed would exist,
could write to this fridge, and would be invisible on the one screen that can
revoke it.

No FIFO. Dropping the oldest to make room takes a fridge away from whoever had
it first and tells nobody, and the person losing it is the one least able to
notice.

### Revoking is the reader's, and it is destructive at a distance

The person losing access is in another country and the service tells them
nothing. There is no undo and no apology to send. So it sits behind a confirm
that NAMES them, and the confirm is laid out against the list's own rectangles:
KEEP is hit over the whole band the four rows share, so a second press of
whichever row opened it cancels, and REMOVE lies wholly outside that band.
host-tests/wallcaption asserts both, and asserts it for every row rather than
for one.

A reader with no senders at all is RECOVERABLE, not broken: it says so in
words, keeps ADD, and the picture stays on the glass. Proved on the
live service and in `qa-artifacts/live-senders/08-empty.png`.

**A service sentence is drawn verbatim and the screen is built to take it.**
The report at the foot of the paired screen is one prose line when that fits
and three condensed ones when it does not. It was one line, and
"This reader already has 4 phones. Remove one first." reached the panel as
"This reader already has 4 phones...." with the only actionable half gone.
`host-tests/wallcaption` now GENERATES its corpus of refusals from
`server/fridge-bridge/bridge/app.py` at test time, so a sentence the service
edits is measured rather than a copy of the one it used to send.

## The image

480x800, made by `site/wallpapers/convert.js` -- already shared with the
firmware's own upload page through a symlink at
`src/network/html/js/wallconvert.js`. No new format.

**Not one bit.** The X4 Pro's panel driver declares AbsolutePlanes grayscale and
`renderCustomSleepScreen` takes the grayscale path, so the device's own format
is **2bpp four-level, 96070 bytes** (0 = black, 1 = dark gray, 2 = light gray,
3 = white, which is what `lib/GfxRenderer/Bitmap.cpp` calls NATIVE). The 1-bit
file (48062 bytes) is the other thing the same reader takes, and it takes 4, 8,
24 and 32 as well.

So the device does **not** bound the download by a size. `bridge::getToFile`
enforces a ceiling (`live::kMaxImageBytes`) so a runaway body cannot fill the
card, and `live::bmpIsComplete` judges what arrived from the BMP's own declared
length in bytes 2..5. That catches truncation at any depth, including depths
this firmware has not met -- a constant would have to be revisited every time
the website learned a new one, and the revision that gets forgotten ships a
torn picture to a fridge.

## The sleep screen

Full bleed, with one hairline and a single line: when it arrived. **No "LIVE"
wordmark on the glass** - the person looking at a fridge does not need our
vocabulary, only to know the message is today's. The word belongs in the
owner's UI.

It never blanks. A failed or empty wake leaves yesterday's message, which is
the correct failure state. The footer date going stale is the signal, and after
several days it says so outright.

## Built so far

- The tile, the chip as an icon, and the empty state. Real captures in
  `qa-artifacts/`. Three tile variants and three Live-screen arrangements were
  built behind `WALLPAPERS_LIVE_VARIANT` and `WALLPAPERS_LIVE_SCREEN` and
  rendered side by side; Mario picked the combined tile and the centred stack,
  and both macros went with the losers in the shipping commit.
- The Live screen itself: the pairing code, the address, the QR, and the paired
  half (next check, how often, who can send, and the three controls).
- **The engine, in `src/apps_local/live/`, and it is no longer a stub.** The
  screen mints a real code from `fridge.ma-r-s.com`, polls until a browser
  claims it, stores the device token on the card and pulls the image onto the
  sleep screen. `LiveCore` is the arithmetic with no card, radio or panel in it
  (interval clamp, capped backoff, clock floor, ETag, image completeness, the
  wake rule) and `host-tests/live` walks all of it. `LiveBridge` is the three
  calls, over `bridge::request` rather than `HttpDownloader` -- the latter calls
  `setInsecure()` on every device build. `LiveStore` is the card. `LiveEngine`
  is the radio and the wake.
- `bridge::Headers` and `bridge::getToFile` are new, because the whole design
  rests on two headers the transport could neither send nor read: If-None-Match
  out, `X-Next-Wake` back. `getToFile` opens its destination lazily, so a 304
  never touches the card at all.
- The wake rule, in one function: on every sleep, if a refresh is due, fetch it;
  otherwise arm the timer for when it will be. The sleep screen is painted
  first and the fetch runs behind it, so pressing power never waits on the
  radio, and the panel is repainted only when an image actually arrived. **Live
  off arms no timer at all**, so a device with it off costs what it cost before
  any of this existed.
- Live yields the radio to everyone. A connection already up is used as it
  stands; Developer Mode holding it means Live does not join at all.
- End-to-end against the running service, not mocked: code drawn on the panel,
  claimed from a shell, device paired, image PUT, image pulled (200 + ETag +
  `X-Next-Wake`), second check 304 with the card's mtime unchanged, four greys
  on the sleep screen. `qa-artifacts/live-e2e/`.
- **The sender list is real, and a row is a control.** The Live screen fetches
  `/api/senders` when it opens and draws name and date per phone; ADD mints a
  join code on the same screen the setup code uses; a tap on a row opens a
  confirm that names the person and revokes on the service. Proved end to end
  against the live service, not mocked: `qa-artifacts/live-senders/` walks a
  reader pairing, adding a phone claimed with curl from the shell, the phone
  appearing by name, being tapped, confirmed and gone, plus the four-phone list,
  the service's 409 at the fifth, and the empty list.
- **The paired screen is a headline, a row of three controls and a list.**
  It was NEXT CHECK, HOW OFTEN, CHECK NOW, TURN IT OFF, WHO CAN SEND, TAP TO
  REMOVE and ADD SOMEBODY: seven headings for three facts, and two of the pairs
  said the same thing twice. Now the next check is the display cut with nothing
  above it, the cadence is one small line under it, the three controls are a
  24px Lucide mark and one word each across one row (CHECK / STOP / START /
  ADD), and a sender row ends in an X. Before and after, four states side by
  side: `qa-artifacts/live-lean/before-after.png`.
  - A mark is always beside a WORD, never alone. There is no hover and no
    tooltip on this panel -- the same reason the Add screen draws its address in
    words next to the QR. The one exception is the X at the end of a row, where
    a word would be the word four times and the confirm behind it names the
    person anyway.
  - The band carries a STATE (`ON` / `OFF`) and the button a VERB (`STOP` /
    `START`). Two vocabularies on purpose: with one, both words are on the
    screen in both states and the assertion that each is drawn cannot fail.
  - Seven of the 24 interaction slots, four phones listed. Reported by
    `host-tests/wallcaption` on every run.
  - A sender row shows a bare date, and the confirm is the one place that says
    what it means: `Added` stacked over `12 Sep`. Stacked rather than inline,
    because "Added 12 Sep" on one line leaves 285px for the name and "Abuela
    phone" is 315px at the display cut -- the ladder would have shrunk the name
    on the one screen whose whole job is to name a person.
  - The empty list names its own recovery ("Press ADD to let a phone in"). With
    the list unheaded there is nothing else on the screen to say what ADD adds.
- **"In about 24 hours" over "Every 24 hours" was a bug, not a wording
  problem.** The next check was printed from the INTERVAL, so a reader checked
  one minute ago and one checked twenty-three hours ago said the same thing. It
  is `live::nextCheckPhrase` now, computed from `live::decide` -- the same
  arithmetic that arms the timer on the way into sleep, backoff included -- so
  the headline cannot promise a check the schedule is not making. `Paused` while
  the toggle is off, `Soon` with no clock, `Any moment` inside three minutes,
  otherwise `In 45 minutes` / `In an hour` / `In 5 hours` / `In 2 days`, minutes
  rounded to five.
  - The line under it is `live::scheduleNote`, and it takes the whole schedule
    because two of its three answers are not the interval: `Last check failed.`
    / `3 checks failed.` in backoff, and `Every 6 hours when on` while the
    toggle is off. Both were contradictions before. In backoff the headline is
    the RETRY, so `In 15 minutes` sat over `Every week` with nothing saying the
    reader could not reach the service; and `Paused` over `Every 6 hours` is the
    screen saying it is not checking and then naming how often it checks, which
    is the same defect this layout was built to remove, one line down.
  - `host-tests/live` walks every band, every interval, both toggle positions
    and the backoff. `host-tests/wallcaption` links `LiveCore` and drives the
    real screen with the phrases `live::` composes, **measured in the face that
    draws them** -- which is the only way to catch this screen's silent failure:
    `fittedTitle` does not refuse a headline too wide for its cut, it steps it
    DOWN a rung, and "In about 45 minutes" is 464px at the display cut against a
    448px body. A suite fed plausible-looking strings could not see it, and the
    one here was fed "Tomorrow, 6:00" and "Once a day" until it was.
- **Two staleness bugs the layout made visible.** The headline is derived, so it
  is wrong the moment it is not recomputed: pairing set the token and saved
  without recomputing, so the paired screen arrived with its largest element
  BLANK at the exact moment the feature succeeded (`nextCheckPhrase` answers ""
  for an unpaired schedule); and `openLive()` did not recompute either, so ten
  minutes in the grid was ten minutes of drift. Both call `refreshLiveLines()`
  now, and it keys off `liveConfigured()` rather than the store, because
  `WALLPAPERS_LIVE_CONFIGURED` makes those two disagree by design.
  Reproduced and fixed in renders rather than argued: `07-harness-forced.png`
  against `08-harness-before.png`, whose headline band holds zero ink.
- Four tappable rows at a finger each plus a full-width ADD SOMEBODY was 145px
  more than an 800px panel has, and the control that fell off the bottom was the
  one that adds a phone. That is what put the controls on one row; the headline
  spends what the third button gave back.
- The hint strip says when Live is the sleep screen ("Your phone is your sleep
  screen."). It sits third in the strip's order, below the sleep-screen note and
  the free-space advisory (both are news, and a standing line that outranked
  either would suppress it for a whole session) and above the two it makes
  false.
- `drawGetSetTile` had a latent bug: its caption was pinned to
  `captionRect(geom, 1)`, so a second special tile would have printed its label
  under the neighbour. It takes its slot now.
- The hint strip was never centred: it hung off `kBodyTop`, spending the whole
  36px body gutter above the line and reserving only `kHintGap` below it. It is
  now `kChromeHeight + (kBodyGutter + kHintGap) / 2`, the same slack split
  evenly, derived rather than restated so it cannot drift from `gridTop`. The
  grid does not move. Measured after: 32px above, 28px below, from 42/18.5.

## Still to build

1. Arming the timer on every sleep, and the boot path for a timer wake.
2. `SETTINGS.sleepScreen` defaults to DARK, and `WallpapersCore.h:189-195`
   lists five ways `/sleep.bmp` never reaches the glass. Live must decide
   whether it paints itself or goes through `SleepActivity`, and if the latter,
   what forces the setting. These are different features and it is not decided.
3. The headless join, backoff, and `park()`.

## Not verified

Deep-sleep current, Wi-Fi-active current, battery runtime, real association
time, and whether `park()` materially lowers the floor. Hardware always counts
as not verified.
