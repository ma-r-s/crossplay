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
  from nowhere. `FrontlightManager.cpp` is ours, so this is our dead code, and
  `Frontlight.begin()` still does a `gpio_hold_dis` on every boot that would
  clear exactly the hold `park()` sets. **Still open.**

  The other half of this paragraph is fixed. It predicted that "a Live wake
  would also switch the light on at 4am with nobody there", and that is exactly
  what shipped: Mario reported it on hardware on 2026-09-21, "the backlight is
  turning on on refresh". `Frontlight.begin()` was handed the restored
  on/off state and ran BEFORE the switch on the wake reason, so every scheduled
  check lit the panel for the length of a radio join whether or not it found
  anything. The light is now brought up dark and turned on, if at all, only
  after that switch, and only for a boot `util/WakePolicy.h` calls
  attended. `host-tests/wakepolicy` holds both halves of the rule.

  The light was only the half that could be named from across a room. The
  same wake also booted the whole reader whenever it found a picture:
  `isSleepWake` is PowerButton-only, so a timer wake fell to
  `BootResume::Splash` and drew the CrossPlay splash, then Home or whatever
  book was last open, and only then the picture. Three full e-ink repaints
  and an EPUB load where one repaint is meant. An unattended boot now takes
  its own exit straight after the wake switch: display and fonts up, sleep
  screen repainted, back down. It captures no state on the way -- the book
  that was open, the app the shelf must reopen, the Quick Resume frame all
  belong to the user's own sleep and would otherwise be overwritten with the
  empty answers of a boot that never created an activity.

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

**Taking the upload route ends with the uploaded picture on the sleep screen.**
Mario, 2026-09-21: _"when I click live and then I click to upload a new
wallpaper is weird that then live is what gets selected if what I really wanted
was to upload a new wallpaper."_ The route existed to put a person's own photo
on the glass and it used to end with the photo merely filed in the library, so
on a reader with Live running the panel went on showing what the website sends.
`pollAddArrivals()` now commits the arrival through `commitSelection()`, which
is the only writer of `/sleep.bmp` and the one place the Live-or-a-wallpaper
exclusivity holds -- so choosing the arrival is also the whole of "and do not
leave Live selected". Nothing on this route turns Live ON, and the pairing is
untouched either way.

**And the ADD A WALLPAPER screen becomes the picture.** Chosen from three
rendered arrangements; the reason is a fact about the route rather than a
preference. `CrossPointWebServer::nextWallpaperPath()` renames every upload
`w0001.bmp`, `w0002.bmp` and so on and discards the name the phone sent -- on
purpose, since a name off a phone is an unvalidated path component. So the
screen can never tell a person "kids-on-the-beach is on your sleep screen"; the
best it can ever say is "w0007", which confirms nothing. **The picture is the
only honest confirmation this route can offer**, so it takes the square the code
had, and `SEND ANOTHER` puts the code back. The name is still drawn, small,
because it is what the grid's caption will say.

**Reproducing that render**, because the first set of them could not be. The
build flag was typed on a command line (which is the convention
`docs/building-apps.md` prescribes, and it is why `WALLPAPERS_FLOW_VARIANT`
appears in no build configuration) but the SD card was seeded from a script in
an agent's scratchpad, so nothing in the tree could produce the images again --
and one of the files that script wrote was named `kids-on-the-beach.bmp`, which
this route cannot emit at all. Both halves are in the repository now:

```bash
tools_local/wallpapers/seed_sim_card.py fs_agent
PLATFORMIO_BUILD_FLAGS="-DWALLPAPERS_ADD_ARRIVED=1" ./scripts_local/sim-shot.sh \
  '1800:TAP:150,723;3400:TAP:240,480;7000:TAP:152,290;10000:TAP:240,246;15000:QUIT' \
  '13500:./qa-artifacts/winner.bmp'
```

`WALLPAPERS_ADD_ARRIVED` is the screenshot harness's only way into this screen:
reaching it for real needs a phone posting to a server the simulator does not
compile. It picks the first upload-shaped file on the card and **refuses, with a
log line, when there is none** rather than substituting a built-in -- which is
what the first version did, and why the render showed a name no reader can
produce.

The shape lives in `wallpapers::uploadFileName()` / `isUploadName()` rather than
in the web server, because three things need it: the server mints it, the
screenshot harness picks an arrival by it, and `host-tests/wallpapers` builds
its corpus from it. The first version of that test walked names a phone might
send, which this route cannot emit -- a corpus of impossible inputs passes
without testing anything.

**Which arrival, when two land inside one poll: the HIGHEST slot.**
`nextWallpaperPath()` scans from 1 upward and takes the lowest free slot, so two
sends in one window number ascending and the zero-padding makes them sort that
way. `wallpapers::lastNewName()` takes the last, which is the most recent send.
An earlier version took the first and showed the person the older picture under
a comment claiming nothing recorded arrival order.

**Choosing a wallpaper really does stop Live, and it now says so.** The device
half was already a mechanism rather than a comment: `commitSelection()` writes
`on: false` through `live::save`, and `live::decide` returns `fetchNow false,
timerSeconds 0` for a reader that is off -- no timer at all, so the reader
genuinely stops waking. What was missing was the other half. The service learns
what a reader is doing only when the reader speaks, and a reader that has
stopped waking never speaks again, so the website counted down to a check that
would not happen and then reported a reader it had not heard from -- which is
also what a flat battery and a router that moved look like. It has an honest
state for exactly this ("Live is off on the reader. Your drawing is saved and
appears the moment Live is switched back on.") and nothing on this path could
reach it. `commitSelection()` now queues the same `POST /api/off` the screen's
own toggle sends.

**That destination was not built, and its absence removed a whole feature.**
The tile went straight to the Live screen on the reasoning that the local
upload server kept its own way in through the offer screen's USE MY OWN PHOTO.
That second half is false: the offer screen only exists while the built-in set
is missing, so on every reader past its first fetch there was no route to the
upload server at all. Mario found it by looking for it: "We lost the option to
upload a regular wallpaper!" It is `wallpapersui::buildPhone` now, two routes
with a sentence each, and `host-tests/wallcaption` fails if either goes
missing. Two of the 24 interaction slots.

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

## The board: one screen on a phone, a grid on a desktop

Connected, `/live/` is an application and not a page of prose. On a phone it is
the whole viewport and **nothing scrolls**: the countdown, the three tabs, the
canvas, the brush sizes, the four tones, undo, clear, send and the history rail
are all reachable without leaving the drawing. Before this, every control sat
below the fold and changing a brush meant scrolling past the drawing and back.

Four things hold it together, and each one is a trap somebody will re-set:

- **`100svh`, never `vh`.** `vh` is the viewport with the iOS address bar
  hidden, so a board sized in it is taller than the screen for as long as the
  bar is showing, which is most of the time.
- **A definite width on `.lv-main`.** The rail's twelve thumbnails set the
  min-content width of every ancestor, `margin: 0 auto` turned off the flex
  stretch that would have pinned it to the body, and a phone browser answered by
  **zooming the whole page out to 58%** to fit a 676px layout viewport. The
  board did not overflow, it shrank, and every control came out a size nobody
  chose.
- **`container-type: size` sizes the panel**, so the canvas is `60cqh` wide and
  lands exactly as tall as the space the controls left. Nothing inside a size
  container can be laid out from its own content, which is why the zoom hint is
  a sibling of the box rather than a child, and why an `auto` grid column
  measures the panel at zero unless it is given a width.
- **`padding: 0` on `.lv-history`.** It is a `<section>`, and `styles.css` gives
  every section on the site 3.5rem top and bottom: 112px of nothing around the
  rail, taken from the canvas, by a rule the page's own stylesheet never
  mentions.

**Undo and clear are icons, and clear is not confirmed.** It pushes onto the
undo stack, undo pulses, and the line under the rail says "Cleared. Undo puts it
back." A confirm would cost a second tap on the common case, and on a phone it
would be the one dialog this layout exists to remove. The drawing is also
persisted to `localStorage` on every stroke, packed to the same two-bit form the
reader is sent, so "undo puts it back" survives the tab being discarded.

**The canvas is not text.** `user-select`, `-webkit-touch-callout` and
`-webkit-tap-highlight-color` are off across the board, `touch-action: none` on
the stage takes the pinch and the drag before the browser can, and every
`pointerdown` calls `preventDefault`. A `pointercancel` ends the stroke: left
set, the next move drew a line from wherever the finger had got to.

**Zoom is a view, never the document and never the drawing.** `view = {s, x, y}`
says which rectangle of the 480x800 panel is on screen; the canvas keeps its
480x800 backing store and is magnified with a transform, so a stroke drawn at 6x
is the same width on the reader as one drawn at 1x. One finger draws, two
pinch and pan, the wheel zooms about the cursor, shift-drag and middle-drag
move, and a minimap in the corner says where you are whenever that can be wrong.

**The countdown carries seconds and ticks.** Mario asked for it knowing it is
not exact, so the figure is spelled precisely and hedged immediately: "about"
sits in front of it and "left" behind, both in the muted grey. It is recomputed
from the clock rather than decremented, so a phone that slept for an hour comes
back right, and crossing into another band repaints the whole block rather than
letting a countdown reach zero and sit there.

**History is shared and it is the record of the reader, not of you.** Every
drawing, message and picture ever sent, newest first, each with when it was sent
and which phone sent it. Sending appends and picks; picking an older one
re-points the reader without making a copy; deleting asks first, in place, and
says so when it moves the pick. Picking is one tap because it is reversible and
costs the reader nothing until its next wake. Deleting is not, so the only
control that does it sits in the line under the rail, a long way from the tiles.

## When it looks: two shapes, and the service owns the calendar

The schedule is one control with two shapes: **repeat every so often**, or
**once a day at a time somebody picked**, in a named timezone.

**The device needs no change for the second one, and this is why.** A reader has
no wall clock worth trusting: waking is a chip reset and the timer is an RC
oscillator. It never needs one. Every `/api/pull` answers `X-Next-Wake` in
SECONDS, `live::clampInterval` bounds it to 15 minutes..7 days, and the reader
sleeps for exactly that. "Every day at 07:00" is therefore the service working
out how many seconds are left until the next 07:00 in that timezone, which is
arithmetic the device never hears about.

**The service sends `X-Cadence` beside `X-Next-Wake`, and one device change is
owed.** `live::scheduleNote` composes the panel's "Every N hours" from the same
figure the reader slept for. Under a daily schedule that figure is a part-day
whenever the schedule changed or a check was missed: set 07:00 at four in the
morning and a reader reading `X-Next-Wake` announces "Every 3 hours" forever
after. So `/api/pull` now answers both, `X-Next-Wake` for how long to sleep and
`X-Cadence` for what to say, and **`scheduleNote` has to read `X-Cadence`,
falling back to `X-Next-Wake` when the header is absent.** The service half is
done and proved in `host-tests/fridge`; `LiveEngine` is held by another session,
so the device half is written down here rather than made.

**How good the hour is, in the page's own words:** it aims for the time and
lands within about a quarter of an hour either side, every check puts it back on
time so the error never accumulates, and a reader in somebody's hands at 07:00
catches up when they put it down. Not "07:00 sharp", and not hedged until it
reads as broken.

**A schedule may only be one of six intervals, or a clock time.**
`store.ALLOWED_INTERVALS` is a finite tuple rather than the 15-minutes-to-a-week
range `X-Next-Wake` is clamped to, and the two are different questions: the
clamp bounds what the reader will believe, the tuple bounds what somebody may
choose. It is finite so the sentences below are a corpus that can be enumerated
and measured rather than sampled.

### `pending`: the window where the reader disagrees with the schedule

A reader is asleep on the cadence it last picked up. Change the schedule from
the website and the two disagree until it next wakes, which on a weekly cadence
is a week. `pending` is the one field that names that window, and the reader
shows a line only when it is present.

**Shape.** `GET /api/senders` (the reader's own call, bearer token) and
`GET /api/state` (the browser's) both carry it:

```json
{ "senders": [...], "max": 4, "pending": "Changing to 07:00 daily after the next check." }
```

**It is ABSENT when nothing is pending.** Not `null`, not `""`, and never equal
to the current cadence. The key missing is the whole signal; a reader that had
to compare two strings to decide whether to draw a line would be a reader
deciding something the service already knows.

**The sentence is the service's, and the reader draws it verbatim.** The reader
never invents wording for a decision this service made (`BridgeHttp.h`), so this
is a sentence, not a value to format. It is
`app.PENDING_TEMPLATE.format(store.cadence_words(schedule))`, which is exactly:

| schedule         | sentence                                             |
| ---------------- | ---------------------------------------------------- |
| every 15 minutes | `Changing to every 15 minutes after the next check.` |
| every 6 hours    | `Changing to every 6 hours after the next check.`    |
| every 12 hours   | `Changing to every 12 hours after the next check.`   |
| every 24 hours   | `Changing to every 24 hours after the next check.`   |
| every 2 days     | `Changing to every 2 days after the next check.`     |
| every 7 days     | `Changing to every 7 days after the next check.`     |
| a clock time     | `Changing to 07:00 daily after the next check.`      |

`host-tests/wallcaption` generates that whole cross product out of
`ALLOWED_INTERVALS`, `INTERVAL_WORDS` and `PENDING_TEMPLATE` and measures every
member in the device's real cuts, so a sentence nobody looked at cannot reach
the panel too wide. The generator stops the run rather than thinning the corpus
if any of the three is renamed or widened into a range.

**The same sentence, on both surfaces.** `/api/state` carries the identical
string and the website prints it verbatim, because two surfaces describing one
reader two ways is the failure this whole feature has spent its life fighting.
`store.cadence_words` is also what the website's schedule chip says, so "07:00
daily" is one phrase with one source.

**When it is present.** `store.Fridge.pending_cadence()` returns the cadence
words when `schedule` and `armed` name different things, and `None` otherwise.
`armed` is the schedule the reader last picked up; it moves in `touch_checkin`,
which is the moment the reader is handed a reply, because a pull it got an
answer to makes it adopt that reply. Nothing pending survives a check-in by
construction rather than by a second write somewhere else.

Three cases that are deliberately NOT pending:

- **Before the first check-in.** `armed` starts at `DEFAULT_SCHEDULE`, which is
  what the reader seeds itself with (`live::kDefaultIntervalSeconds` equals
  `DEFAULT_INTERVAL_S`, and `host-tests/live` regenerates the check from
  `store.py` so the two cannot drift). A reader that was paired and immediately
  moved to "every 15 minutes" is therefore correctly pending, and
  `next_expected` is measured against `armed` rather than against what somebody
  has since chosen.
- **The same schedule chosen again**, or a timezone change that leaves the local
  time alone. `same_cadence` compares the WORDS, so a pending line nobody could
  explain never appears.
- **Live switched off on the reader.** There is no next check for anything to
  take effect after, and a promise about one would be the fake number this field
  exists to avoid.

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

Since 2026-09-21 "mints" means the id and the device token are drawn and held
in memory by `Pairings`; the `state.json` is written by `/api/claim`. Before
that it was written here, and because the reader asks for a code every time
the Live screen opens unpaired, again when a code expires on screen and again
on a 401 -- with nothing ever deleting the unclaimed ones -- the service held
67 fridges from nineteen hours of one person testing. Waiting for the claim
costs nothing: the reader does not receive its token from `pair/start`, only
from `pair/poll`, which answers only once somebody has claimed, so a token
never exists for a fridge that does not.
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
  the toggle is off, `When it sleeps` while a check is due, `Any moment` inside
  three minutes, otherwise `In 45 minutes` / `In an hour` / `In 5 hours` /
  `In 2 days`, minutes rounded to five.
  - **`Soon` is gone, and its absence is the point.** It was the answer for a
    reader that had never checked in, which is every reader for the seconds
    after it pairs and the whole of one that cannot reach the service, and it
    names no moment, no mechanism and no gesture. All three ways a check can be
    due -- nothing asked yet, no clock, the moment gone by -- have one honest
    answer, because they have one mechanism: the fetch happens on the way into
    sleep and nowhere else. `host-tests/live` asserts the word cannot come
    back.
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

## What the first real user hit (card #552)

Three things, and none of them was visible from inside the code.

- **The QR encoded a code nobody had minted.** The Live screen fell back to
  `kLiveCode = "482 160"` whenever `liveCode_` was empty, behind a comment
  claiming a build flag guarded it; nothing checked any flag. Empty is the
  ordinary state of that screen from the moment it opens until the service
  answers, and the whole state of a reader that cannot reach the service. So
  the panel showed six plausible digits and the square beside it encoded them.
  Mario scanned it, the website told him the code did not work, and he typed
  the real one in by hand. There is no placeholder code now, no square, and no
  fallback: the screen says what it is waiting for.
  - Two more from the same journey. A browser **already connected** to another
    reader swallowed a scanned code's refusal whole and carried on showing the
    reader it had, so the next drawing went to the wrong fridge with nothing
    anywhere saying so. And the code's ten-minute expiry was parsed, logged and
    read by nothing, so a screen left up for eleven minutes showed six digits
    that could not be claimed under a line promising they last ten. The reader
    re-mints at the service's own `expiresIn` now.
- **The next check was missing on both surfaces.** See below.
- **The upload route had no way in.** See the destination, above.

## When the next check is, and who owns the number

**The service stamps it once, at the check-in**, from the interval in that
reply, and never recomputes it. The reader sends one header on the pull,
`X-Live-On`, which is the only schedule fact the service cannot derive: it can
work out _when_ the next check is, because it is the one handing out the
interval, but not whether somebody has switched Live off since the last one.

**The reader cannot report its own alarm**, which a version of this tried. The
headers are composed before the reply is read, and a pull that gets an answer
clears the reader's failures and makes it adopt that reply's interval, so any
figure it sends is the alarm for the state it was in _before_ the request. One
failed check was enough to make the website count down to a moment eighteen
hours early. The one state whose alarm the service could not derive -- a reader
in backoff -- is exactly the state whose pulls never arrive.

The service used to derive the countdown from `last_checkin + interval_s`, live
on every `/api/state`, and that was wrong twice over: changing the schedule from
the website moved the countdown while the reader was still asleep on its old
alarm, and a reader that had never checked in produced `0 + interval`, a moment
in 1970 -- which is why the page could only say "the reader has not checked in
yet" for the whole minute after pairing. Mario: _"shouldn't refresh time should
start since sync? Makes little sense if it's never shown at the start"_.

Before the first check-in the anchor is the pairing instant (`created +
interval_s`), so there is always a figure. `created` is the moment the code was
CLAIMED, which is when the record is written; it was the moment the code was
shown until 2026-09-21, a difference of however long somebody took to type it.

**The two seed intervals are now equal on purpose.** The reader seeded six hours
and the service a day, and the reader's report is composed BEFORE it reads the
reply -- so the first report of every new pairing named a moment eighteen hours
early. `host-tests/live` generates the service's three limits out of `store.py`
and fails if either side moves.

**Five states, expressed on both surfaces:**

|                               | the website                                    | the panel                                              |
| ----------------------------- | ---------------------------------------------- | ------------------------------------------------------ |
| just synced, never heard from | `First check in about a day.`                  | `When it sleeps`                                       |
| waiting                       | `Next check in about 5 hours.`                 | `In 5 hours`                                           |
| due now                       | `the next time the reader is put down`         | `When it sleeps`                                       |
| late                          | `The reader last checked in about 4 days ago.` | the backoff: `In 15 minutes` over `Last check failed.` |
| off on the reader             | `Live is off on the reader.`                   | `Paused` over `Every 6 hours when on`                  |

**Late is twelve hours or one whole interval, whichever is longer**, and it is
deliberately generous in both terms: the reader only fetches on its way into
sleep, so one somebody picked up in the morning and put down at night is half a
day past due with nothing wrong with it, and on a weekly cadence being a day
late is nothing. The RC oscillator's ~1% drift is the small term, not the large
one. The page states the fact and stops: a flat battery, a router that moved and
Live switched off without a word out are identical from the service, so naming
one would be a diagnosis it cannot make. That is what `POST /api/off` exists
for -- the reader says so on its way out, fire and forget.

**The rounding bands are shared.** `live::roughSpan` on the panel and `roughSpan`
in `site/live/live.js` are the same edges and the same rounding, ported rather
than invented; the page puts "in about" in front because it has the room and the
panel does not ("In about 45 minutes" is 464px at the display cut against a
448px body).

**One honest disagreement remains.** Before the first check-in the service
counts from the pairing instant while the reader's own rule treats "never
asked" as due right now, so the reader will normally beat the page's first
countdown by checking in at its very next sleep -- which, straight after
pairing, is seconds. The page's first figure is therefore an upper bound, and
the two agree from the first check-in onwards.

### Two cadences, and the window where they differ -- NOT BUILT

Mario, 2026-09-21: _"should we let user see the actual refresh time that is on
the device and what will it be after it syncs? Can we do so in a minimalistic
way?"_

**The question is real and the answer is not built.** There are two facts. **The
alarm** is what the reader will actually do: the interval the service sent at
the last check-in, the one the RTC timer was armed from, and what `nextCheck`
and `cadence` on the panel already come off. **The schedule** is what the
service has configured now. Change it from the website and the reader is asleep
on the old alarm and learns nothing until its next check-in. For that window the
two disagree and the panel shows only the first.

The service already models both -- `next_wake` is the alarm the reader reported,
`interval_s` is the schedule, and `next_expected()` returns the former for
exactly this reason. What is missing is any way for the READER to see the
latter: `/api/pull` sends `X-Next-Wake`, which the reader adopts on arrival, so
a pull can never show it a schedule it is not already on.

**A first attempt was cut rather than shipped** (card #552, 2026-09-21). The
reader-side half was built -- a `pending` field parsed off `/api/senders`, a
line on the paired screen, three rendered arrangements -- and `GET /api/senders`
in `app.py` was never changed, so the field was one nobody sent and the screens
drew a state no reader could reach. That is the stub-QR shape one round earlier
in this same feature: something plausible standing in for data the real system
cannot produce. It is all removed. What it would take, written down so the next
attempt starts from here rather than from the idea:

1. `/api/senders` gains `"pending"`, present only while `interval_s` disagrees
   with the interval behind the stored `next_wake` (the reader's adopted
   interval is `next_wake - last_checkin`), and absent whenever they agree --
   which is every ordinary call, and also what an older service gives, so
   deploying in either order cannot make the reader claim anything.
2. **The service writes the words and the reader draws them verbatim**, the rule
   its refusals already travel under. How much it may say depends on the
   arrangement: a full-width line takes a sentence, a labelled column takes a
   phrase. That choice has to be made before the contract is written.
3. The reader clears it on any successful check, because the check is the moment
   the pending cadence stops being pending.
4. **The page has to say the same thing**, in the same words. `/api/state`
   already carries both numbers (`nextExpected` from the alarm,
   `intervalSeconds` from the schedule).
5. Neither figure may read as exact: the alarm runs off an RC oscillator that
   drifts about a percent and the reader only fetches on its way into sleep.
   `roughSpan` bands both sides already.

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
