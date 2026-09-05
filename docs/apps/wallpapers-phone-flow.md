# Wallpapers: from a picture on a phone to the sleep screen

Design for card #349, revised after a cold critic. Covers three things Mario
asked for in one conversation on 2026-09-05, which are one design and not three:

1. Replacing the "+ Add a wallpaper" screen. It currently tells you to go to a
   website **on a computer** and copy a file across with File Transfer. He was
   plain about it: _"asking to connect to the computer is not a good UX. It
   needs to be seamless and cheap to implement, as close as we can get to scan
   QR code -> select image on phone -> have it on the device."_
2. Preview and delete, reached by holding a wallpaper.
3. Multi-select and shuffle (card #305).

They land on the same grid and compete for the same 24 interaction slots, so
designing any one of them alone produces a picker with three bolted-on modes.

**The critic changed this design in four places rather than polishing it.** Each
is marked below. The largest is Part 0, which did not exist in the first draft
and blocks two of the three asks.

---

## Part 0: a hold can arrive as a tap, silently, and the tap is destructive

**This is a prerequisite, not a detail, and it was missing from the first
draft.** Verified in the SDK directly rather than taken on report:

`InputManager::wasTouchTap`
(`freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:643`) gates on
four things -- a release event, not suppressed, not a multi-contact sequence,
and not moved beyond the release slop. **There is no duration gate anywhere in
it.** A stationary hold of any length releases as a tap.

The long press is a separate, weaker signal: `touchLongPressEvent` is set only
when an `update()` happens to run while the finger is still down and
`now - touchDownPoint.timestamp >= TOUCH_LONG_PRESS_MS` (`:1220`), and it is
cleared at the top of every `update()` (`:507`). The wallpapers grid blocks its
own loop to decode four full 480x800 BMPs per page (`ensureThumbsForPage`,
`WallpapersActivity.cpp:300`) and then blocks again for an e-ink repaint. A hold
begun across either window never gets its 500 ms evaluated.

So on today's grid, a hold that misses its window falls through to
`wasScreenTapped` -> `setWallpaper(idx)`, which overwrites `/sleep.bmp` and sets
`SETTINGS.sleepScreen = CUSTOM` with **no confirmation and no undo**. A user
reaching for "delete this" would silently get "this is now your sleep screen"
instead, and nothing on the panel would say why.

The first draft answered the lesser objection at length -- that a hold has no
affordance on e-ink -- and never named this one. Every hold-based interaction in
Parts 1 and 2 is unsafe until it is fixed.

**The fix is one bound, and the number it needs is already in hand.**
`MappedInputManager::wasScreenTapped` (`src/MappedInputManager.cpp:158`) already
calls `rememberTouchHeldTime()`, which captures `gpio.lastTouchHeldMs()`. A tap
held longer than the long-press threshold is not a tap and must be refused, so
that a hold which missed its window does nothing at all rather than doing the
wrong thing. Silence is the correct failure here: the user holds again.

This ships first, on its own, with a host test that drives a 900 ms stationary
contact and asserts no tap is routed (`a-test-not-seen-fail`: revert the bound
and watch it go red).

---

## Part 1: the picker's interaction model

### What it does today

`WallpapersActivity::loop()` hit-tests the grid against geometry. A tap on a
cell calls `setWallpaper(idx)`, copying the file to `/sleep.bmp`. That is the
whole vocabulary: one tap, one meaning, no preview, no undo, no delete. Cell 0
of page 0 is a `+` tile opening the help screen being replaced.

### The constraint that decides this

**A destructive action must never land where a remembered tap goes.** This fork
has destroyed user data exactly that way (`same-pixel-different-action`).

That rules out turning tap into "open this wallpaper" and putting DELETE on the
screen it opens: the pixel that used to pin would then lead, in two taps, to a
button that erases.

### Decision: tap keeps its meaning, hold earns the rest -- once Part 0 lands

- **Tap a cell**: unchanged. That wallpaper becomes the sleep screen.
- **Hold a cell**: an action sheet for that one wallpaper. PREVIEW, the shuffle
  toggle, DELETE.
- **DELETE always confirms**, on its own screen, naming the wallpaper.

This is what Mario proposed (_"Maybe when holding we have a button to preview and
one to delete?"_). The affordance objection is answered by the hint strip the
grid already reserves (`kHintH`, 30px, drawn whether used or not so the grid's
top does not jump): it becomes a permanent line naming both gestures.

### Why preview is worth a screen of its own

The thumbnail in the grid is **not** what the sleep screen looks like and cannot
be. `decodeThumb()` box-downscales the 480x800 source into a ~170x283 cell and
re-dithers against an 8x8 Bayer matrix. A 1-bit image resampled to a fifth of its
size and re-dithered loses the fine hatching the engravings are made of and gains
moire the original does not have.

Preview draws the BMP at 1:1 through `renderer.drawBitmap(...)`, the same call
`SleepActivity::renderBitmapSleepScreen` makes, with the same
`calculateBitmapPlacement`. It answers Mario's actual question -- _"a way to
preview the wallpaper without turning off the device"_ -- which is a request to
see the sleep screen without sleeping.

### CHANGED: deleting a pack wallpaper is recoverable, but not cheaply

The first draft said recovery was a matter of `unpackSet()` re-fetching the
missing file. That was wrong, and the correction matters because the claim was
being used to justify deleting built-ins with no extra friction.

`unpackSet()` fetches nothing. `runSetDownload()` calls
`HttpDownloader::downloadToFile(kPackUrl, ...)` for the **whole ~1MB pack,
unconditionally** (`WallpapersActivity.cpp:651`), with no Range support, behind
`kPackFloorBytes` (12MB + the pack) and behind the Wi-Fi picker. Only the
_unpack_ skips files already present at the right length.

So recovering one deleted Duerer costs a full download on a 13MB free-space
check. The confirm screen must say that, in those terms, rather than implying an
undo button exists.

### CHANGED: the grid renumbers under the user

Two ways, both of which put a different picture under a remembered tap:

- `scanLibrary()` sorts (`WallpapersActivity.cpp:87`), so every upload inserts
  mid-list and shifts later cells across pages.
- Deleting a built-in flips `specialTiles()` from 1 to 2 (`:424`) -- the "get the
  set" tile returns -- shifting **every** wallpaper by one cell.

This is not new to this design; it exists today. But this design adds deletion
and uploads, which are the two things that trigger it. The picker must repaint
on any change to the library, and the delete confirm must be the last screen
before a shift, never a screen the user can tap through blind.

---

## Part 2: multi-select and shuffle (#305)

### The trap, restated

`renderCustomSleepScreen()` checks `/sleep.bmp` **first**, then `/.sleep`, then
`/sleep`. A pinned single silently shadows any shuffle set.

### The audit, and the thing it found that is not a design question

PR 3's prerequisite -- a written audit of `/sleep`, `/.sleep` and every
`SLEEP_SCREEN_MODE` -- is done, and it moved two things from "design risk" to
"already broken".

**A wallpaper reaches the glass in two of eight modes.** `CUSTOM`, and
`COVER_CUSTOM` (which shows the cover when sleep came from the reader and the
wallpaper otherwise, and also falls back to the wallpaper whenever cover
generation fails). In `DARK`, `LIGHT`, `COVER`, `BLANK`, `QUICK_RESUME` and
`TRANSPARENT_CUSTOM` it is silently ignored -- and `TRANSPARENT_CUSTOM` does not
read `/sleep.bmp` or `/.sleep` at all, only the four `sleep-overlay` slots.

**And even `CUSTOM` is not sufficient**, which is card #354 and not this card's
to fix: `quickResumeSleepScreen == QUICK_RESUME_AFTER_TIMEOUT` short-circuits
*above* the mode switch (`SleepActivity.cpp:500-507`) whenever `fromTimeout`,
which is the ordinary idle sleep. So the wallpaper is only seen when the device
is slept by hand. There is a reachable path where that flag sticks on
permanently, and `setWallpaper` forcing `sleepScreen = CUSTOM` without the sync
call is part of it.

That is what the hint strip is for, and it is why the strip carries a sentence
rather than a marker: no per-cell mark can express "your settings mean none of
these reach the sleep screen".

The audit also disproved a comment in `WallpapersCore.h` claiming this app's
file filter and `findNextValidSleepImage` "cannot drift, because they are the
same rule". They are two rules and they disagree six ways; the comment now lists
them. The one that matters to PR 1: **a valid BMP whose size is not exactly
48062 bytes is listed in the grid and cannot be set** -- `setWallpaper` refuses
and the caller shows nothing at all. The browser converter emits exactly that
size, so the upload path is safe, but any other route in is not.

### CHANGED: the single-marker promise cannot be kept, so it is not made

The first draft claimed the corner brackets could simply mean "your sleep screen
shows this", on the grounds that a single and a set are mutually exclusive. The
critic showed that is false in two ways, both verifiable in
`src/activities/boot_sleep/SleepActivity.cpp`:

- There is a **third** source. The fallback chain is `/sleep.bmp` -> `/.sleep` ->
  **`/sleep`** (`:569-571`), and `/sleep` is a folder the app neither writes nor
  knows about.
- `renderCustomSleepScreen()` runs only in `SLEEP_SCREEN_MODE::CUSTOM`, or
  `COVER_CUSTOM` when not arriving from the reader (`:534-546`), and is
  short-circuited entirely by quick-resume. In BLANK, COVER, or
  `TRANSPARENT_CUSTOM`, nothing the picker shows reaches the glass at all.

The existing code already knows this -- `loadActive()` returns early unless the
mode is CUSTOM (`WallpapersActivity.cpp:103`) -- and the first draft silently
dropped that guard.

**So the marker means "chosen here", which is all the app can honestly claim**,
and the screen says the rest: when `SETTINGS.sleepScreen` is not a mode that
reads these files, the hint strip carries a plain sentence saying so. A grid full
of confident brackets over a sleep screen set to COVER is
`a-silent-screen-reads-as-a-crash` with extra steps.

One bracket vocabulary is still enough for both states -- one cell wearing them
is a single, several are a set -- with the header's right label carrying the
count (`SHUFFLE 5`) so a user on page 3 of 6 knows the two cells they can see are
part of a larger set and not the whole of it.

### CHANGED: never empty `/.sleep`

The first draft said entering single mode empties `/.sleep`. **That destroys
files the app does not own.** `/.sleep` is an upstream folder users fill
themselves through File Transfer; `SdCardFontRegistry.cpp:200` calls it "the
sleep-folder pattern". Today `setWallpaper()` never touches it, so a user with
their own rotation there can pin and unpin without loss. Making an unconfirmed
tap on a grid cell wipe that directory is the exact shape of
`a-fix-must-repair-existing-data`.

The state on the card becomes:

- `/wallpapers/.shuffle` -- the user's set, one file name per line. Persistent
  and independent of which mode is active, so a stray tap cannot destroy a set.
- **`/wallpapers/.placed`** -- a manifest of exactly which files this app copied
  into `/.sleep`. Leaving shuffle removes **only** those, one by one, by name. A
  file in `/.sleep` that is not in the manifest is somebody else's and is never
  touched.
- Mode is **derived, never stored**: `/sleep.bmp` exists -> single; otherwise the
  manifest is non-empty -> shuffle. A stored mode flag that disagreed with the
  card would give a screen that is right on one boot and wrong on the next
  (`invisible-saved-state-reads-as-nondeterminism`).

FAT has no symlinks, so a set costs `48062 x N` bytes duplicated. `roomFor()`'s
three outcomes are sized for N files, not one, and `Unknown` refuses.

### CHANGED: multi-select is a mode, not N holds

The first draft built a set one long-press at a time. Five wallpapers meant five
_successful_ holds -- each of which can silently fail as a tap (Part 0) and each
costing an e-ink repaint. Mario asked to _"select multiple ones at the same
time"_, and that is not it.

So: the hold sheet's shuffle button on any wallpaper **enters selection mode**
for the whole grid. In that mode a tap adds or removes a cell instead of pinning
it, the header says `CHOOSING 3`, and a DONE control commits. Tap keeps exactly
one meaning per mode, which is what makes the mode safe; entering it is the
deliberate act, and it is the only place a tap means something new.

Tapping a bracketed cell outside selection mode -- the case the first draft left
undefined -- is a no-op, extending the guard `setWallpaper` already has for the
pinned single (`:897`).

### A wallpaper in the set that gets deleted

The set is a list of names; the sweep that rebuilds `/.sleep` drops names with no
file behind them and rewrites the list. Note the sleep side is **stricter** than
the picker: `findNextValidSleepImage` also requires
`Bitmap::parseHeaders() == Ok` (`:400-407`), so a truncated file lists in the
grid and is skipped at sleep. A set that falls to zero must remove its
`/sleep.bmp` and its manifest entries and say so, rather than leaving an empty
`/.sleep` -- which does not fall through to the logo, as the first draft
claimed, but to `/sleep`.

---

## Part 3: getting a photo from a phone

### The trap, measured rather than assumed

The tempting hybrid is: point the QR at the HTTPS site so the converter costs no
flash, convert on the phone, then `fetch()` the finished BMP to the device's
`http://` LAN address. Probed from the real origin
`https://crossplay.ma-r-s.com` in Chrome 148:

| From an HTTPS page                       | Result                                                                                                                                            |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `fetch('http://192.168.1.50/probe')`     | **blocked** -- "Mixed Content: ... This request has been blocked"                                                                                 |
| `fetch('http://10.0.0.5/probe')`         | **blocked**, same                                                                                                                                 |
| `fetch('http://crosspoint.local/probe')` | **blocked**, same -- `.local` earns no exemption                                                                                                  |
| `new WebSocket('ws://192.168.1.50:81/')` | **blocked** -- SecurityError                                                                                                                      |
| `fetch('http://127.0.0.1:9/')`           | failed as `ERR_BLOCKED_BY_CLIENT`, **not** as mixed content -- the potentially-trustworthy exemption is real and does not extend to LAN addresses |

Two corrections, both cutting the same way:

**Chrome fixed this, and it does not help.** Chrome 142 (Oct 2025) shipped Local
Network Access, which amends the mixed-content algorithm to _allow_ an HTTPS page
to reach a `local` address space once the user grants a permission prompt --
built for exactly this case. Firefox shipped it in 151. The probe got a hard
block rather than a prompt because an automated browser grants nothing.

**Safari does not have it, and on iOS every browser is Safari.** WebKit's
`LocalNetworkAccessEnabled` is `status: unstable`, `default: false`, with
implementation bugs still NEW as of September 2026. Apple's TN3179 confirms the
iOS local-network prompt is a _native app_ permission that Safari traffic
"doesn't require", so there is no prompt to grant.

**That is the fact that decides this, and it is bigger than the flash number: a
site-hosted page talking to the device is not degraded on an iPhone, it is dead,
with no user-visible explanation and no workaround.**

Worth retiring at the same time, since it points the wrong way twice:
`Access-Control-Allow-Private-Network` is vestigial (PNA preflight enforcement
went on hold in Oct 2024 and never shipped), and private IPs have not become
secure contexts (webappsec-secure-contexts #60, open and ignored since 2018). A
publicly-trusted certificate for a private IP or `.local` name is not hard but
_prohibited_: CA/B Forum BR 4.2.2 forbids Internal Names and Reserved IP
Addresses.

**One thing does get through.** Mixed content covers subresources, not top-level
navigation, and a form POST is a navigation:

```
[warn] ... contains a form that targets an insecure endpoint 'http://192.0.2.1/upload'.
       This endpoint SHOULD be made available over a secure connection.
```

A warning, not a block -- proven attempted rather than refused by the tab hanging
45s on the TCP connect to that unroutable address. `DataTransfer` also lets a
page put a `File` it built itself into a file input. **And it still loses on the
one thing this feature is for**: Chrome puts a full-page interstitial in front of
an insecure form submission ("The information you're about to submit is not
secure"), with no private-IP exemption in the predicate. The cheapest route ends
with a security warning the user must click through to put a photo on their own
reader in their own house.

### Route A: the device serves the page

QR carries `http://<device-ip>/w`. The phone opens a page served **by the
device** over plain HTTP -- no HTTPS anywhere, so nothing to escape -- picks a
photo, converts it in the browser, and PUTs the finished 48,062-byte BMP.

Every capability the converter needs works on a plain-http LAN origin: canvas
2D + `getImageData`, `createImageBitmap`, `OffscreenCanvas`,
`<input type=file>` including `capture` (which is not `getUserMedia` and is not
gated), Web Workers, WebAssembly, `fetch`. `FilesPage.html` already proves the
whole pipeline there. **One trap to record: `crypto.randomUUID()` IS gated** and
will be `undefined` on the device page while working on `http://localhost` in
dev -- so filenames must not use it. Gate on `window.isSecureContext`, never on
hostname.

Almost all of it exists:

- **The converter is written and tested.** `site/wallpapers/convert.js` (5,576 B)
  -- BT.601 luma, Floyd-Steinberg to 1 bit, a 1-bpp BMP encoder emitting exactly
  the 62-byte header and 48,062-byte file the sleep screen accepts. DOM-free, so
  `host-tests/wpupload/test_convert.mjs` runs it under bun.
- **The upload endpoint exists.** `WebDAVHandler::raw` accepts `HTTP_PUT` on any
  URI, streams to `<path>.davtmp` and renames. Verified by the critic through the
  Arduino dispatch: no `on()` route matches, `Parsing.cpp:182` takes the raw path
  for a non-multipart body, and `isProtectedPath("/wallpapers/mine.bmp")` is
  false. Same-origin PUT needs no preflight. (`POST /upload` would be wrong: it
  **refuses on collision** and takes `upload.filename` with no traversal check.)
- **The device cannot convert and must not try.** `JpegToBmpConverter` caps at
  2048x3072; a 12MP phone photo is 4032x3024 and is rejected outright.

**Flash cost**, measured with the repo's own minify+gzip-9, inlined into one
asset: **8,643 bytes**, or 6,925 with JS comments stripped. `FontsPage.html`
costs 3,267 and `FilesPage.html` 51,423. `gh_release_x4pro` last measured
6,599,674 against an 8,323,072-byte slot, so this is 0.5% of the 1.65MB spare.

That figure is the PUBLIC site's four files inlined, and the device page is not
quite those files: it ends in a `PUT` to the card rather than a download, and it
does not need the site's chrome. So treat 8,643 as an upper bound with the right
order of magnitude, not as the number this will cost -- and re-measure the real
asset before quoting it anywhere, because a byte count that came from a
different artefact is exactly the kind of derived fact that rots
(`derived-facts-written-as-literals`).

### CHANGED: route A as first drafted opened the whole card

The first draft called "no new C++ route" a win. It is not, and this is the
finding that changes the most code.

A `CrossPointWebServer(devOnly=false)` registers `/files`, `/download`,
`/delete`, `/rename`, `/move`, `POST /api/settings`, `/api/wifi` (**the saved
network list**) and WebDAV across the entire card. The header's own comment says
that surface is acceptable only because it is "a deliberate, temporary thing you
open from a screen and close again" -- which is precisely the property removed by
putting it behind a photo picker reached from a QR.

So the design needs **a third server mode**, `wallpapersOnly`, mirroring the
existing `devOnly` gate in `begin()`: `GET /w`, and `PUT` restricted to
`/wallpapers/*.bmp`. Nothing else registered, WebDAV not installed. That is a
handful of lines in a function already shaped for it, and it makes this flow
narrower than the File Transfer screen rather than wider.

### CHANGED: it cannot be a second server, and Mario's own device proves it

File-scope statics are shared by all instances (`wsInstance`, `wsUploadFile`,
`wsUploadPath`, `wsUploadInProgress`, `wsUploadClientNum`), the ports are fixed
(80, 81, UDP 8134), and `WebServer::begin()` returns void and swallows a bind
failure while `CrossPointWebServer::begin()` sets `running = true`
unconditionally -- **so `isRunning()` lies on a port collision** and the app
would show a QR for a server that never answers, with nothing on screen.

This is not hypothetical here. **Mario keeps a device in Developer Mode**, and
dev mode owns its own `CrossPointWebServer(devOnly=true)` on those same ports.
The app must take the `devmode::pause()` / `resume()` latch that
`WifiSelectionActivity`, `CrossPointWebServerActivity` and `LinkRadio` already
hold, and it must treat a bind failure as a failure the screen reports.

### CHANGED: three more gaps on the upload path

- **No free-space precondition exists anywhere on a PUT.** `roomFor()` is used
  only in `runSetDownload`; the WebDAV raw path streams `Content-Length` bytes to
  SD unconditionally. A phone can push straight through `kCardFloorBytes` -- the
  12MB floor whose own comment says it exists so Study does not lose its review
  log. Part 2 remembered `roomFor` for shuffle and Part 3 forgot it for the
  feature that actually adds bytes.
- **Torn uploads orphan forever.** `raw()` writes `<path>.davtmp`;
  `sweepPartFiles()` only matches `.part`. A dropped connection leaves a 48KB
  file invisible to both the picker and the sweep.
- **Collisions overwrite silently.** `bmpName` is a pure function of the source
  name, so two photos both called `IMG_0001` produce one target and the PUT
  answers 204. Uploads need a unique suffix -- and not from
  `crypto.randomUUID()`, which is unavailable here.

### CHANGED: the Wi-Fi step as drafted drops a live connection

`startSetDownload()` launches `WifiSelectionActivity` unconditionally, and
`WifiSelectionActivity::startWifiScan()` does
`WiFi.mode(WIFI_STA); WiFi.disconnect();` on every path. **There is no
already-connected short-circuit anywhere.** So the drafted step 2 ("if Wi-Fi is
not up...") describes a conditional that does not exist: an already-online device
gets a redundant picker _and_ loses its association. The app must check
`WiFi.status()` first and skip the picker -- while remembering that
`WiFi.status()` is not ownership (`radio-has-one-owner`), so a device in a
Linkplay match must be refused rather than fought.

### Route B, and the one place it wins

A bridge (`server/read-bridge`, `server/study-bridge` are the precedent, and
their `pairing.py` is 90 stateless lines) costs a container or a function, a new
`*.ma-r-s.com` label, a `deploy.sh`, an entry in `server/attacks.py`, a section
in `docs/bridge-security.md`, and on the device a pairing screen, a poll loop and
a download -- not less firmware, merely different. And the wallpaper case needs
no upstream account, so the entire sign-in half of both existing bridges would be
copied for nothing.

**Route B wins exactly one scenario: the phone is not on the same Wi-Fi as the
device.** Guest networks, a phone that dropped to cellular. Route A's answer is a
sentence on the screen, not a fallback, and the existing "download the BMP and
copy it across" path still exists for it -- it just stops being the headline.

### The flow

1. **`+ Add a wallpaper`** on the grid.
2. If `WiFi.status()` is already connected, **skip the picker**. If the radio is
   held by a match, refuse and say so. Otherwise `WifiSelectionActivity`.
3. Take the `devmode` latch, start the server in `wallpapersOnly` mode, and
   **verify it bound** before drawing anything.
4. Draw the QR for `http://<ip>/w` -- 24 bytes at the longest IPv4 form, well
   inside the budget below -- with the address printed underneath for anyone who
   would rather type it. Station mode only: the hotspot has no NAT and a
   captive-portal DNS answering every name with the device, so a phone joined to
   it has no internet and both iOS and Android offer to drop back to cellular,
   mid-upload. (HTTPS-First does not threaten this: Chromium's own adoption guide
   exempts "non-unique hostnames, local IP addresses, and single-label
   hostnames". The QR carries the IP rather than `crossplay.local` because
   Android only gained mDNS `.local` resolution in the Nov 2021 Mainline update
   -- Android 12+ -- and it is dead on 11 and earlier.)
5. The phone picks a photo, sees the 1-bit preview, sends. The device checks
   `roomFor` before accepting and refuses with a sentence the page shows.
6. The device polls its own library at 1500 ms and, when a file appears, redraws
   with the new wallpaper: SET IT / ADD ANOTHER / DONE.
7. Back stops the server, releases the latch, and sweeps any `.davtmp`.

### The QR budget, which is a live bug and not a guideline

`QrUtils.cpp:28-32` picks its version from the **alphanumeric** capacity table.
Every URL with a lowercase letter encodes in **byte** mode, whose version-4 ECC-L
capacity is **78 bytes, not 114**, and nothing catches the overrun:
`qrcode_initBytes` carries a `@TODO` where the error return should be,
`bb_appendBits` has no bounds check, and the `LOG_ERR` at `:64` is unreachable
for this failure. 79-99 bytes gives a QR that looks drawn and does not scan;
100-114 writes past a 101-byte stack buffer.

`http://192.168.100.100/w` is 24 bytes, so this design is nowhere near it.
Recorded because the next person to put a token in a QR will be, and filed as
card #352.

---

## Part 4: the cut

Four pull requests, in this order. The first is a prerequisite for the third.

| PR  | What                                                                                                                                                                                                                              | Blocked by |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- |
| 0   | **The tap/hold gate.** Refuse a tap held longer than the long-press threshold, with a host test that fails when the bound is reverted.                                                                                            | nothing    |
| 1   | **QR + upload.** The new screen, `wallpapersOnly` server mode, the devmode latch, a real bind-failure path, `roomFor` on upload, `.davtmp` sweeping, collision naming, the Wi-Fi short-circuit. Grid stays tap-only.              | nothing    |
| 2   | **Hold sheet: preview, delete, confirm.** Including the honest "getting it back means the whole pack again" wording and the `specialTiles()` renumbering.                                                                         | PR 0       |
| 3   | **Shuffle (#305).** Selection mode, the `.placed` manifest, the sleep-mode honesty line. Its prerequisite audit is DONE (Part 2), and #354 should land first or the honesty line is telling half the truth. | PR 0, PR 2 |

PR 1 is the whole of Mario's first ask and the only piece with a security
surface. It is also the only one that needs his eyes on a screen before it is
built, which is why the three arrangements below exist.

---

## The three arrangements

Rendered at native 480x800 through the device path, composed in
`qa-artifacts/add-variants.png`. All three carry the same two load-bearing facts:
the address in words (a QR tells a person nothing, and the one failure this
screen has is a phone on the wrong network) and that Back stops it.

1. **The pairing twin** -- Instapaper's screen, which Mario named as the
   precedent, with the address where its 8-character code sits. QR 232px.
2. **The three steps** -- the site's numbered rail, on the panel. QR 200px.
3. **The code, mostly** -- the largest square the body will take. QR 300px.

Two defects the renders caught that no test could, both fixed before the compose:
"SCAN WITH YOUR PHONE" came out `SCAN WITH YOUR...`, and the footer came out
`PHONE MUST BE ON THE SAM...` -- the failure explanation, truncated. Both now go
through `toybox::fittedTitle`, and the Wi-Fi requirement moved out of the footer
into the prose, where it is information rather than chrome.

### What the UI review changed

Mario asked for a dedicated UI review by name, and it found two things a render
I had already looked at did not show me. Both are fixed and the numbers are
measured against the real cuts, not the ui suite's ten-pixels-a-character target.

**The screen had one type size, not three.** `drawAddress` and `drawHeadline`
both asked for the display cut and **neither can ever have it**: "SCAN WITH YOUR
PHONE" measures 579 against a 448px body at toybox_30, and a worst-case IPv4 URL
measures 632. So `fittedTitle` stepped both down to reading_serif_14 -- the same
cut as the prose and the footer -- and the one line a person has to read off the
glass and type was the third line of a four-line paragraph. Nothing looked
broken. That is what made it survive.

Fixed by `readingAddressFaces()`, which rebinds the SMALL slot (bound on this
screen and never drawn) to the bold reading cut: the longest possible address
measures 399 against 448. The headline keeps the display cut by being short
enough to hold it -- `SCAN THIS CODE`.

**The footer drew outside the body.** A 24px box for a 40px line box:
`text()` clamps the negative centring offset to zero, so the line ran 758..798,
below `body.bottom()` at 784 and eleven pixels from the panel edge. Measured in
the render before the fix: ink at 769..789. After: 755..775. It came verbatim
from `InstapaperScreens.cpp:527`, where the same box is correct because it draws
in toybox_10 (line box 21) -- **the box came across from the twin and the font
did not.** Every text height on this screen is now asked of the face that will
draw it.

**The header right label is gone.** Any label costs the band its display cut:
"ADD A WALLPAPER" needs 433 of 448, and even "1 ADDED" at the smallest cut
leaves 380. The title would have dropped from a 38px Jersey cap to a 21px serif
one the moment a wallpaper arrived. It was also a literal "1 ADDED" for any
count, on a field nothing assigns -- so the state it existed for was the one
state no render had ever shown.

**One correction to a standing assumption**, verified in the renderer rather
than taken on report: `truncatedText` (`GfxRenderer.cpp:1834`) asks the RESOLVED
face for U+2026 and falls back to a literal `"..."`, so overflow **through that
path** is visible, not silent -- as the first render's own `SCAN WITH YOUR...`
showed. The defensive comments elsewhere claiming silent truncation are stale
for it.

The review recommends **variant 1**, for the headline: it is the only
arrangement that says what the black square is before you see it, on a screen
whose whole failure mode is "my phone is on the wrong network". Variant 3 has
the best code (9px modules against v1's 7 and v2's 6) and no sentence attached
to it; variant 2 has the best content and the worst page. The pick is Mario's.
