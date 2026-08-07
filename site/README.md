# The Crossplay site

Static. No build step, no framework, no dependencies. `index.html`, one
stylesheet, and the assets it names.

## Before it deploys

Two steps, both of which fail silently if skipped:

```bash
python3 site/set-host.py https://your-domain     # og:image and og:url must be absolute
uv run --with playwright python site/make-og.py  # only if the mark, headline or shelf shot changed
```

Nothing on the page shows you the share card, so it is the one asset that goes
stale without anyone noticing.

## Deploying

Point Vercel at this directory as the project root. There is nothing to build,
so the framework preset is **Other** and the build command is empty.

`vercel.json` sets `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Those are there for the in-browser
simulator, which needs `SharedArrayBuffer` for its thread pool. They also mean
**every subresource must be same-origin or explicitly CORP-marked**: the page
loads no third-party fonts, scripts or images, and it must stay that way. Adding
an analytics snippet or a Google Fonts link will silently fail to load under
these headers rather than warn.

## Looking at it

A plain `python3 -m http.server` will serve the page but **not** the emulator:
without COOP/COEP the module has no `SharedArrayBuffer` and never starts, and
what you see is a stuck canvas rather than an error. Serve it with the headers:

```bash
uv run --with playwright python site/serve.py 8099
```

Changes must be _looked at_, not reasoned about -- the same rule the device
apps follow. `pageshot.py` renders full-page and sliced captures at any width
and colour scheme through the Chrome already installed:

```bash
uv run --with playwright python site/pageshot.py http://localhost:8099/ /tmp/pageshots 1440 light
```

## What the browser build fakes

Three things, all under `tools_local/wasm/`, none of them reaching `src/` or
`lib/`. Worth knowing before anyone screenshots this build as if it were the
panel:

- **The network.** `src/http_canned.cpp` replaces `HttpDownloader` and answers
  from `/canned` on the preloaded card. The bodies are real, curled from the
  real endpoints on the day the card was built: the Algolia front page, one
  story and one article's text. Connections fetches its board at runtime and
  nothing is canned for it, so the daily grid does not work in the browser. A URL with
  no canned answer fails like an unreachable host and logs itself.
- **Study's font.** `StudyFonts` wants KaiTi at 50pt and 17pt; the 50pt cut is
  5.5MB and the 17pt one is 714KB, so the card ships the small file under both
  names. The deck, the scheduler and the glyphs are genuine; the headword is
  set smaller than the device sets it.
- **Sleep.** `sleepTimeoutMinutes` is 31 (never) on this card. A browser tab
  has no power button to wake the device with, so sleeping is a dead end.

The card itself is assembled by hand: `pack_subset.py` cuts the xkcd pack, the
canned bodies are curled, the font is copied twice. There is no one script that
rebuilds it, which is the next thing to write.

## The device in the hero

`emulator/` holds three generated files -- `crossplay.{js,wasm,data}` -- and no
page of its own. `assets/emulator.js` is the entire front end: it creates a
canvas inside whatever element you hand it, reads the composited frame out of
the module's heap, and feeds pointer and key events back in. That is why the
hero screenshot can *become* the device without navigating anywhere.

Two things it must keep doing, both of which have already been got wrong once:

- **`locateFile`.** Emscripten resolves `crossplay.wasm` and `crossplay.data`
  against the page, not the script, so they have to be prefixed with
  `emulator/` or a boot from `index.html` 404s.
- **One release per press.** The firmware latches input edges per frame, so a
  duplicate release lands on the activity the first one just opened -- a single
  tap on Back walked back two screens. Every handler is guarded on the pointer
  id that went down.

The module is the real firmware -- same sources as the device build, same SD
card layout, its own seeded card under `tools_local/wasm/sdcard/`. Rebuild it
after any change to `src/` or `lib/` that the page should show, or it quietly
demonstrates an old version:

```bash
pio run -e simulator_x4_pro && source ../.emsdk/emsdk_env.sh && python3 tools_local/wasm/build.py
```

It is checked in because Vercel does not have Emscripten.

## Assets

`assets/shots/` is generated, not drawn. Every screenshot is the real firmware
running in the simulator against a seeded SD card, captured with
`scripts_local/sim-shot.sh` and downsampled from the simulator's 2x output to
native panel pixels (480x800, or 800x480 for the two landscape apps). Regenerate
them rather than editing them, and never retouch one: the claim the page makes
is that this is what the device shows.

`assets/fonts/` is Jersey 25 and Instrument Serif, the two faces the device
itself draws, converted to woff2. Both are SIL OFL and their licences ship
beside them; keep them there.

## The rules this page follows

`docs/identity.md` governs the copy and the look, and it is worth reading before
changing either. The short version: black and white on purpose, screenshots are
the artwork, say the thing rather than the category, and never fake the panel.
