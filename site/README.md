# The CrossPlay site

Static. No build step, no framework, no dependencies. `index.html`, one
stylesheet, and the assets it names.

One exception, and it is deliberate: `api/firmware.js`, a single Vercel
function that exists so the Install button can work at all. See **The Install
button** below before touching it.

## When it deploys, and when it does not

`vercel.json` carries `"ignoreCommand": "git diff --quiet HEAD^ HEAD ./"`.
Vercel runs it from this directory: **exit 0 skips the build, non-zero builds**.
`git diff --quiet` exits 0 when nothing here changed, so a commit that does not
touch `site/` never deploys.

This is not tidiness. On 2026-09-04 the account hit its deployment rate limit
and every pull request in the fork went red on a Vercel check, including ones
whose diff was a word list or a shell script. Of the 305 commits on `xteink`
that day, **57 touched `site/`** -- so four deploys in five built nothing new
and the fifth could not run.

Two properties worth keeping if you edit it:

- **It fails towards building.** If the command errors -- a shallow clone with
  no `HEAD^`, a git that is not there -- it exits non-zero and the deploy
  proceeds. Never skip when unsure.
- **`./` means this directory, not the repository root**, because the Vercel
  project root is `site/`. Changing the project root without changing this
  path silently stops every deploy.

The emulator rebuild CI commits after a firmware merge DOES touch `site/`, so
it still deploys. That is correct: the page really did change.

## Before it deploys

Two steps, both of which fail silently if skipped:

```bash
python3 site/set-host.py https://your-domain     # og:image and og:url must be absolute
uv run --with playwright python site/make-og.py  # only if the mark, headline or shelf shot changed
```

Nothing on the page shows you the share card, so it is the one asset that goes
stale without anyone noticing.

## Deploying

Vercel builds `xteink` to production on every push. That branch has to be
named in the project's Production Branch setting, not just be the repo's
default: this repository is a GitHub fork, and Vercel took `master` from the
fork network rather than the branch GitHub reports. The API rejects
`productionBranch` as an unknown field, so it is a dashboard setting only,
and getting it wrong makes every push a preview while the live domain
silently keeps serving the last manual deploy.

Point Vercel at this directory as the project root. There is nothing to build,
so the framework preset is **Other** and the build command is empty.

`vercel.json` sets `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. Those are there for the in-browser
simulator, which needs `SharedArrayBuffer` for its thread pool. They also mean
**every subresource must be same-origin or explicitly CORP-marked**: the page
loads no third-party fonts, scripts or images, and it must stay that way. Adding
an analytics snippet or a Google Fonts link will silently fail to load under
these headers rather than warn.

The one sanctioned exception is the study installer's Pyodide runtime, which
`site/study/worker.js` loads from `cdn.jsdelivr.net` first and from the
same-origin `/pyodide/` mirror only as fallback. Vercel's platform attack
mitigation intermittently challenges worker fetches (it stranded two real
installs on 2026-08-25) and has no off switch, while jsDelivr sends both CORS
and `Cross-Origin-Resource-Policy: cross-origin` on every file, which is what
makes it legal under the headers above; that was verified per-file before the
exception was made. Any new third-party source must clear the same bar.

**The big binaries ship already brotli-compressed, and that is not an
optimisation you may quietly undo.** Vercel compresses static files at its
edge on every cache MISS, and on a multi-megabyte binary it streams that
output at roughly 35 KB/s. Measured on the live site, same file same minute:
`crossplay.data` took **34.3s** compressed and **1.2s** uncompressed. Sending
three times the bytes is thirty times faster, so it is compression CPU, not
bandwidth. Edge caches are per region and every deploy empties all of them,
which is how a page that "used to load in under a second" starts taking
minutes for everyone who is not near the region you last tested from.

So `site/emulator/`, `site/pyodide/` and `site/study/NotoSansCJK.otf` are
committed as brotli, with `Content-Encoding: br` declared for those paths in
`vercel.json`; Vercel sees an encoded body and passes it through (34.3s ->
0.99s, decoding byte-identically). `tools_local/site/precompress.py` does the
compressing, `tools_local/wasm/build.py` and `fetch_pyodide.py` call it so a
rebuild cannot leave raw bytes behind a header promising brotli, `serve.py`
sends the same header locally, and `check.sh`'s `encoding` gate fails when a
file served as `br` is not. **Adding a big binary to the site means adding it
to that script and to `vercel.json` together** -- one without the other is
either a slow site or a broken one.

One second-order catch, already met once: `content-length` on these responses
is the size of the **compressed** body, while a `getReader()` loop counts the
bytes the browser has **decoded**. Dividing one by the other made the study
page's download counter announce "154%" on its way to 354%. Anything measuring
progress over a pre-compressed file has to notice `content-encoding` and stop
claiming a percentage; there is no header carrying the decoded size.

**Only `/assets/fonts/` is cached `immutable`, and nothing else may join it
without a content hash in its filename.** `immutable` is a promise that a URL's
bytes will never change, and it is enforced by the browser refusing to ask
again -- for a year, with no revalidation, whatever the server now holds.
`/assets/(.*)` carried that rule until 2026-08-09, over files that change
constantly: `emulator.js` alone changed eight times in the preceding month, and
every one of those changes was invisible to anyone who had loaded the page
before. It surfaced as a landscape fix that worked for new visitors and not for
the author, because `styles.css` sits outside `/assets/` and revalidates, so he
got the new stylesheet and the old script. Fonts keep the rule because a font
that changes gets a new filename anyway. Everything else falls through to
Vercel's default, `max-age=0, must-revalidate`, which costs a 304 and is worth
it.

Note that `curl` cannot catch this class of bug and neither can a fresh
incognito window: both start with an empty cache and always see the new file.
The check that would have caught it is loading the page normally, twice.

**Narrowing the rule did not release the browsers already holding a promise.**
Anyone who loaded the page before 2026-08-09 keeps that copy until 2027, no
matter what the server now says, and on 2026-08-12 that bit the author: his
`emulator.js` predated the root-absolute `/emulator/crossplay.js` fix, so the
study preview died with `Could not load emulator/crossplay.js` while the live
file was correct. The only reach into a cache that refuses to revalidate is a
different URL, so both pages load `assets/emulator.js?v=2`. The HTML itself
revalidates, which is what makes that work. Keep the query on both pages until
2027, and bump it rather than removing it if this ever recurs.

## The Install button

`index.html` carries an Install panel; `assets/install.js` drives it and writes
firmware to a device over Web Serial; `assets/esptool.bundle.js` is the library
that talks the protocol. Someone said on Reddit that they could not flash the
device and had been looking for a tutorial, and a longer tutorial was not the
answer.

**The one server-side thing on this site is `api/firmware.js`, and it is not
optional.** GitHub serves release assets from
`release-assets.githubusercontent.com`, which sends **no**
`Access-Control-Allow-Origin` header at all -- so a page cannot `fetch()` a
release asset, on this site or any other. (The site-wide COEP `require-corp`
would refuse it a second time.) The function fetches the file server-side and
streams it back on our own origin. Three things about it are load-bearing:

- **It streams, and must not declare a `Content-Length`.** A _buffered_ Vercel
  function response is capped at 4.5MB; these images are ~6.3MB. Streaming is
  exempt, and setting a length is what turns one into the other. The size the
  progress bar needs rides in `X-Firmware-Size`, which also happens to be the
  decoded size -- so the progress counter cannot repeat the Study page's
  "154% downloaded".
- **The browser passes the release tag; the function does not look one up.**
  The GitHub API's unauthenticated rate limit is per IP. Asking it from the
  function means every visitor shares a handful of Vercel egress addresses and
  the button starts answering 403 under exactly the traffic it was built for.
  The page already asked the API for the latest tag before this feature
  existed, to print "Latest release", and that limit is per visitor.
- **Therefore the tag is client-supplied, and is validated before it becomes a
  URL.** `^v\d{1,3}\.\d{1,3}\.\d{1,3}$` and a fixed device -> filename map:
  no host, no scheme, no traversal. The worst a hostile caller can do is
  download a different version of our own firmware.

The filename template (`crossplay-{tag}-x4pro-full.bin`) is a literal copy of
what `.github/workflows/crossplay-release.yml` publishes. Nothing links the
two, and a rename there leaves the site rendering perfectly while the button
404s, so `host-tests/release/run.sh` asserts them against each other and
`host-tests/site/run.sh` asserts every element id `install.js` reaches for.
`serve.py` answers the same endpoint locally -- without it the button is
untestable off Vercel, and its failure looks like a broken endpoint rather than
a missing one.

**What it writes is the merged image at offset 0**, not the app into a spare
OTA slot the way CrossPoint's flasher does. That is on purpose: this fork
changed `partitions.csv` (7.94MB slots), and only a write at 0 lays the new
table down. An OTA-slot write would leave every install this button makes
capped at the old 6.25MB forever.

To rebuild the library after an esptool-js release:

```bash
bash tools_local/site/build_esptool.sh
```

It pins the version deliberately. **Flash a real device before committing a
bump** -- this file writes a bootloader to somebody else's hardware, and no
check here can tell you it still does.

## Looking at it

A plain `python3 -m http.server` will serve the page but **not** the emulator:
without COOP/COEP the module has no `SharedArrayBuffer` and never starts, and
what you see is a stuck canvas rather than an error. Serve it with the headers:

```bash
python3 site/serve.py 8099
```

**Browse it as `http://127.0.0.1:8099`, not `localhost`.** It binds IPv4 only,
and Chrome resolves `localhost` to `::1` first, so the localhost spelling gives
you Chrome's own "site can't be reached" page -- which reads like the server
failed to start when it is running perfectly.

`serve.py` needs nothing but the standard library -- the `uv run --with
playwright` this used to say is `pageshot.py`'s dependency, not its own, and
made a plain static server look like it needed a toolchain. It serves the
directory it lives in, so run it from whichever worktree you want to look at
and give each one its own port. That is how to see a change before it deploys,
which beats finding out in production.

The inbox page (`/inbox/`) needs a passphrase and a board, and a layout
change needs neither. `serve.py` answers `POST /api/inbox` from a JSON file
when `INBOX_FIXTURE` names one, whatever passphrase is typed:

```bash
INBOX_FIXTURE=site/inbox/fixture.json python3 site/serve.py 8099
```

`site/inbox/fixture.json` holds three open asks, forty cards and every table
the Numbers section reads; `host-tests/site/run.sh` fails when the page starts
reading a key the fixture lacks. Dev only: production is `api/inbox.js` and
never runs `serve.py`, so the fixture cannot leak.

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
hero screenshot can _become_ the device without navigating anywhere.

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
pio run -e simulator_x4_pro -t compiledb && source ../.emsdk/emsdk_env.sh && python3 tools_local/wasm/build.py
```

It is checked in because Vercel does not have Emscripten.

## Assets

`assets/shots/` is generated, not drawn. Every screenshot is the real firmware
running in the simulator against a seeded SD card, captured with
`scripts_local/sim-shot.sh` and downsampled from the simulator's 2x output to
native panel pixels: 480x800, or 800x480 for the landscape ones. Regenerate them
rather than editing them, and never retouch one: the claim the page makes is
that this is what the device shows.

**The downsample is the step that gets skipped**, because skipping it looks like
nothing. `index.html` declares the 1x width and height, so a 2x file has the
right aspect and the card renders perfectly at four times the bytes -- on a page
that lazy-loads two dozen of them. The `shoot-*.sh` scripts copy the simulator's
output straight across, and on 2026-09-01 all four shots they produce (trivia,
wavelength, toybattle, forehead) were 2x. `host-tests/site/page_structure.py`
now compares every shot against the size the page declares, so the next one
fails the site suite instead of shipping.

`assets/fonts/` is Jersey 25 and Instrument Serif, the two faces the device
itself draws, converted to woff2. Both are SIL OFL and their licences ship
beside them; keep them there.

`assets/esptool.bundle.js` is generated too -- esptool-js, Apache-2.0, bundled
by `tools_local/site/build_esptool.sh`. Edit the script, never the file. At
175KB it is the largest script here, so nothing loads it until someone actually
presses Install; it sits under the ~1MB line where `precompress.py` starts
mattering, and does not want a `Content-Encoding` of its own.

## The rules this page follows

`docs/identity.md` governs the copy and the look, and it is worth reading before
changing either. The short version: black and white on purpose, screenshots are
the artwork, say the thing rather than the category, and never fake the panel.
