# Wallpapers: from a picture on a phone to the sleep screen

Design for card #349. Covers three things Mario asked for in one conversation
on 2026-09-05, which are one design and not three:

1. Replacing the "+ Add a wallpaper" screen. It currently tells you to go to a
   website **on a computer** and copy a file across with File Transfer. He was
   plain about it: _"asking to connect to the computer is not a good UX. It
   needs to be seamless and cheap to implement, as close as we can get to scan
   QR code -> select image on phone -> have it on the device."_
2. Preview and delete, reached by holding a wallpaper.
3. Multi-select and shuffle (card #305).

They land on the same grid and compete for the same 24 interaction slots, so
designing any one of them alone produces a picker with three bolted-on modes.

---

## Part 1: the picker's interaction model

### What it does today

`WallpapersActivity::loop()` hit-tests the grid against geometry. A tap on a
cell calls `setWallpaper(idx)`, which copies the file to `/sleep.bmp` and
switches the sleep mode to CUSTOM. That is the whole vocabulary: one tap, one
meaning, no preview, no undo, no delete. Cell 0 of page 0 is a `+` tile that
opens the help screen being replaced.

### The constraint that decides this

**A destructive action must never land where a remembered tap goes.** This fork
has destroyed user data exactly that way (`same-pixel-different-action`). A tap
on a grid cell means "make this my sleep screen" today, and 21 built-in
wallpapers plus a user's own means people will build muscle memory for it.

That rules out the obvious shape -- turning tap into "open this wallpaper" and
putting DELETE on the screen it opens -- not because preview-then-act is wrong
in the abstract, but because the pixel that used to pin now leads, in two taps,
to a button that erases. It also rules out any layout where DELETE sits in the
grid.

### Decision: tap keeps its meaning, hold earns the rest

- **Tap a cell**: unchanged. That wallpaper becomes the sleep screen.
- **Hold a cell** (`mappedInput.wasScreenLongPress`, already wired through
  `UiAppHelpers.h` and already used by `WifiSelectionActivity`): opens an
  action sheet for that one wallpaper. PREVIEW, the shuffle toggle, DELETE.
- **DELETE always confirms**, on its own screen, naming the wallpaper.

This is what Mario proposed (_"Maybe when holding we have a button to preview
and one to delete?"_) and it survives the constraint, which the alternative
does not.

### The objection to it, and the answer

A hold has no affordance on e-ink. There is no ripple, no press state; the
panel will not redraw until the gesture has already been decided. A gesture
nobody can see is a feature nobody finds.

The answer is that the grid already reserves a 30px hint strip
(`kHintH`, drawn whether or not it is used, so the grid's top does not jump).
Today it says "Tap one to set your sleep screen." when nothing is pinned and
nothing otherwise. It becomes a permanent line that names both gestures. The
cost is one string in space already paid for.

### Why preview is worth a screen of its own

Not as UI garnish. The thumbnail in the grid is **not** what the sleep screen
looks like, and cannot be. `decodeThumb()` box-downscales the 480x800 source
into a ~170x283 cell and re-dithers the result against an 8x8 Bayer matrix. A
1-bit image resampled to a fifth of its size and re-dithered loses exactly the
fine hatching that the built-in engravings are made of, and gains moire that
the original does not have. Look at "Celestial Chart" in the current grid: it
reads as grey noise at thumbnail size and as a star chart at full size.

So preview is the only way to see what you are choosing. It draws the BMP at
1:1 through `renderer.drawBitmap(bitmap, x, y, w, h, cropX, cropY)`, the same
call `SleepActivity::renderBitmapSleepScreen` makes, with the same
`calculateBitmapPlacement`. Full-bleed and honest.

It also answers Mario's actual question -- _"a way to preview the wallpaper
without turning off the device"_ -- which is a request to see the sleep screen
without sleeping.

### Deleting a pack wallpaper must be recoverable

`WallpapersActivity::pickView()` derives the view from the card alone, and
`builtInsPresent()` counts how many of the 21 built-ins are on it. Delete one
and `builtInsMissing_` becomes 1, which brings back the "get the set" tile and
makes `unpackSet()` re-fetch only the missing file (it skips any target already
on the card at exactly `kWallpaperFileBytes`). So the recovery path exists and
is already resumable: **deleting a built-in is undoable by re-downloading, and
the app already says so on screen.** The confirm screen says which kind it is,
because deleting a photo you put there yourself is not recoverable and deleting
a Duerer is.

---

## Part 2: multi-select and shuffle (#305)

### The trap, restated as a UI rule

`renderCustomSleepScreen()` checks `/sleep.bmp` **first**, and only then falls
back to `/.sleep`. A pinned single wallpaper therefore silently shadows any
shuffle set: you would pick five wallpapers, keep seeing one, and nothing on
screen would explain it.

### Decision: one marker, because the two states are mutually exclusive

Card #305 asks how the grid should distinguish "in the shuffle set" from "the
pinned one", and warns that two different meanings of _selected_ on one grid is
the ambiguity Mario already rejected once.

The way out is to notice that the device has no state in which both exist.
The sleep screen shows either one fixed image or a rotating set, never both --
that is precisely what the `/sleep.bmp`-first check enforces. So the marker
does not need to carry two meanings. It carries one:

> **The corner brackets mean: your sleep screen shows this.**

One cell wearing brackets is a pinned single. Five cells wearing brackets is a
shuffle set. No second marker, no new vocabulary, and the grid becomes a direct
picture of what the sleep system will actually do -- which makes the trap
unrepresentable rather than merely handled.

The header's right-hand label says which mode is on, since a five-bracket page
and a one-bracket page differ only by a count you would have to page around to
find: `SHUFFLE 5` instead of `21 SAVED` / `PAGE 2 / 6`.

### The state on the card

- `/wallpapers/.shuffle` -- the user's set, a list of file names, one per line.
  Persistent and independent of which mode is active, so a stray tap cannot
  destroy a set the user built up.
- Mode is **derived, never stored**: `/sleep.bmp` exists -> single;
  otherwise `/.sleep` has files -> shuffle. `invisible-saved-state-reads-as-
nondeterminism` applies -- a stored "mode" flag that disagreed with the card
  would produce a screen that is right on one boot and wrong on the next.
- Entering shuffle: delete `/sleep.bmp`, copy the set's files into `/.sleep`.
- Entering single: delete the contents of `/.sleep`, write `/sleep.bmp`.
  Emptying `/.sleep` rather than leaving it shadowed is the point: a directory
  that exists but never shows is the bug #305 is about.

FAT has no symlinks, so a set costs `48062 x N` bytes duplicated into
`/.sleep`. `WallpapersCore::roomFor()` already returns three outcomes and
refuses on `Unknown`; the precondition is sized for N files, not one.

### A wallpaper in the active set that gets deleted

The set is a list of names; the sweep that rebuilds `/.sleep` drops names with
no file behind them and rewrites the list. A set that falls to zero drops to no
custom sleep screen at all rather than to an empty `/.sleep`, which would sleep
to the default logo with no explanation.

---

## Part 3: getting a photo from a phone

### The trap, measured rather than assumed

The tempting hybrid is: point the QR at the HTTPS site so the converter costs no
flash, convert the photo on the phone, then `fetch()` the finished BMP straight
to the device's `http://` LAN address. It does not work, and here is the actual
browser saying so rather than a recollection that it does not. Probed from the
real origin `https://crossplay.ma-r-s.com` in Chrome 148:

| From an HTTPS page                       | Result                                                                                                                                             |
| ---------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `fetch('http://192.168.1.50/probe')`     | **blocked** -- "Mixed Content: ... This request has been blocked"                                                                                  |
| `fetch('http://10.0.0.5/probe')`         | **blocked**, same                                                                                                                                  |
| `fetch('http://crosspoint.local/probe')` | **blocked**, same -- `.local` earns no exemption                                                                                                   |
| `new WebSocket('ws://192.168.1.50:81/')` | **blocked** -- SecurityError, "may not be initiated from a page loaded over HTTPS"                                                                 |
| `fetch('http://127.0.0.1:9/')`           | failed as `ERR_BLOCKED_BY_CLIENT`, **not** as mixed content -- the potentially-trustworthy exemption is real, and does not extend to LAN addresses |

So every route that keeps the phone on an HTTPS page and talks to the device as
a subresource is dead. That kills the cheap hybrid.

**One thing does get through, and it is worth recording because it is not
folklore either way.** Mixed-content blocking covers subresources, not top-level
navigation, and a form POST is a navigation:

```
[warn] ... contains a form that targets an insecure endpoint 'http://192.0.2.1/upload'.
       This endpoint SHOULD be made available over a secure connection.
```

A warning, not a block -- and the proof it was attempted rather than refused is
that the tab then hung for 45 seconds on the TCP connect to 192.0.2.1
(TEST-NET-1, unroutable) and had to be force-navigated away. `DataTransfer` also
lets a page put a `File` it built itself into a file input, so a
browser-converted BMP can ride that form. An HTTPS page therefore _can_ hand
bytes to a plaintext device. It just cannot stay on the page while doing it.

That route is real, and it still loses, for reasons below.

### Route A: the device serves the page

QR carries `http://<device-ip>/w`. The phone opens a page served **by the
device** over plain HTTP -- no HTTPS anywhere, so no mixed content to escape --
picks a photo, converts it in the browser, and PUTs the finished 48,062-byte BMP
into `/wallpapers`.

Almost all of it already exists:

- **The converter is written and tested.** `site/wallpapers/convert.js` (5,576 B)
  is the whole pipeline -- BT.601 luma, Floyd-Steinberg to 1 bit, a hand-rolled
  1-bpp BMP encoder emitting exactly the 62-byte header and 48,062-byte file
  `WallpapersCore.h` and `SleepActivity` already accept. DOM-free on purpose, so
  `host-tests/wpupload/test_convert.mjs` runs it under bun.
- **The upload endpoint already exists.** `WebDAVHandler::raw`
  (`src/network/WebDAVHandler.cpp:41`) accepts `HTTP_PUT` on any URI, streams the
  raw body to `<path>.davtmp` and renames it over the target. So
  `fetch('/wallpapers/mine.bmp', {method:'PUT', body: blob})` works today, with
  no new C++ route. (`POST /upload` would be the wrong choice: it **refuses on
  collision** at `CrossPointWebServer.cpp:761`, contradicting its own docs, and
  takes `upload.filename` into a path with no traversal check.)
- **The QR renderer, the Wi-Fi chooser and the server are all wired.** The app
  already calls `WifiSelectionActivity` for the pack download
  (`WallpapersActivity.cpp:562`), and `CrossPointWebServerActivity` shows the
  pattern for owning a `CrossPointWebServer` and pumping `handleClient()`.
- **The device cannot do the conversion and must not try.** `JpegToBmpConverter`
  caps at 2048x3072 (`JpegToBmpConverter.cpp:552`); a stock 12MP phone photo is
  4032x3024 and is **rejected outright**. Conversion in the browser is not a
  preference, it is the only thing that works.

**Flash cost, measured with the repo's own minify+gzip-9 pipeline**, inlining the
four site files into one HTML asset:

| form                                   | raw    | flash (gzip) |
| -------------------------------------- | ------ | ------------ |
| four separate `.generated.h` assets    | 24,063 | 9,362        |
| inlined into one file, one gzip stream | 24,063 | **8,643**    |
| same, JS `//` comments stripped        | 20,202 | **6,925**    |

For scale: `FontsPage.html` -- a complete single-purpose page with a list, an
upload form, CSS and JS -- costs 3,267 bytes. `FilesPage.html` costs 51,423.
`gh_release_x4pro` last measured 6,599,674 bytes against a 8,323,072-byte slot,
so **~8KB is 0.5% of the 1.65MB spare** and 0.1% of the slot.

### Route B: the site hosts the page, a bridge relays

QR carries a pairing token to `crossplay.ma-r-s.com`; the phone converts
client-side and POSTs to a bridge; the device polls and downloads.

The precedent is real -- `server/read-bridge/` and `server/study-bridge/` are
FastAPI containers on the Orange Pi behind Cloudflare tunnels, and their
`pairing.py` is 90 stateless lines. But **the wallpaper case needs no upstream
account**, so the entire reason those bridges exist -- signing in to Instapaper
or AnkiWeb, the Fernet credential store, the session cookie, the CSRF plumbing,
the login limiters -- would be copied for nothing.

What it actually costs: a fifth container or a new Vercel function, a new
`*.ma-r-s.com` label (the free-plan cert stops at one level, so no
`wall.crossplay.ma-r-s.com`), a `deploy.sh`, an entry in `server/attacks.py`, a
section in `docs/bridge-security.md`, and on the device a pairing screen, a poll
loop and a download -- which is not less firmware than route A's page, merely
different firmware. And every wallpaper a user adds then travels through a
machine in Mario's flat for no reason.

### The ranking, and the one place B wins

|                        | A: device serves it                    | B: site + bridge                                          | Hybrid: site page, form-POST to device |
| ---------------------- | -------------------------------------- | --------------------------------------------------------- | -------------------------------------- |
| Flash                  | ~7-9 KB                                | comparable (pairing + poll + download)                    | ~0.3 KB (a receipt page)               |
| New service surface    | **none**                               | a container or a function, a subdomain, a security review | none                                   |
| Account                | **none**                               | none needed, but the copied scaffolding assumes one       | none                                   |
| Works with no internet | **yes** (LAN only)                     | no                                                        | no                                     |
| Works on mobile data   | no                                     | **yes**                                                   | no                                     |
| Taps after the scan    | pick, done                             | pick, done, then wait for the device to poll              | pick, done, land on a device page      |
| Privacy                | **photo never leaves the LAN**         | photo crosses a third machine                             | photo never leaves the LAN             |
| When it fails          | phone says it cannot reach the address | bridge down, token expired, poll stuck                    | two networks must both work            |

**Route A wins.** It is the cheapest thing that achieves scan -> pick -> done, it
creates no service to run, and it is the only one of the three where the picture
never leaves the room. The hybrid's flash saving is ~8KB and it buys that by
requiring the phone to have working internet _and_ be on the right LAN, plus a
top-level navigation whose error handling is a plain device page. That is a worse
product for a rounding error of flash.

**Route B wins exactly one scenario: the phone is not on the same Wi-Fi as the
device.** That is a real scenario (guest networks, a phone that dropped to
cellular) and route A's answer to it is a sentence on the screen, not a fallback.
Named here rather than discovered later. The existing "download the BMP and copy
it across with File Transfer" path still exists for it, and stops being the
headline.

### The flow

1. **`+ Add a wallpaper`** on the grid. No longer a help card.
2. If Wi-Fi is not up, `WifiSelectionActivity` -- the same call the pack download
   already makes. Station mode, never the hotspot: `WiFi.mode(WIFI_AP)` is
   exclusive, there is no NAT anywhere in the tree, and the captive-portal DNS
   answers every name with the device, so a phone joined to the `CrossPlay` AP
   has no internet and both iOS and Android will offer to drop back to cellular
   mid-upload. Station mode keeps the phone online and is where the IP QR already
   lives.
3. The app starts its own `CrossPointWebServer` and draws the QR for
   `http://<ip>/w` -- 21 bytes, comfortably inside the QR budget (see below) --
   with the same address printed underneath in text for anyone who would rather
   type it.
4. The phone opens the page, picks a photo, sees the 1-bit preview, and taps
   send. The browser PUTs 48,062 bytes to `/wallpapers/<name>.bmp`.
5. The device polls its own library directory at the bridges' cadence (1500 ms)
   and, the moment a file appears, redraws: the new wallpaper's thumbnail, its
   name, and SET IT / ADD ANOTHER / DONE.
6. Back at any point stops the server and drops Wi-Fi.

### The QR budget, which is a live bug and not a guideline

`QrUtils.cpp:28-32` picks its version from the **alphanumeric** capacity table:

```cpp
int version = 4;
if (len > 114) version = 10;
```

Every URL with a lowercase letter encodes in **byte** mode, whose version-4
ECC-L capacity is **78 bytes, not 114**. And nothing catches the overrun:
`qrcode_initBytes` carries `// @TODO: Return error if data is too big.`,
`bb_appendBits` has no bounds check, and the `LOG_ERR` at `QrUtils.cpp:64` is
unreachable for this failure. So a payload of 79-99 bytes produces a QR that
looks drawn and does not scan, and 100-114 bytes writes past a 101-byte stack
buffer.

`http://192.168.100.100/w` is 24 bytes, so this design is nowhere near it. It is
recorded because the next person to put a token in a QR will be.
