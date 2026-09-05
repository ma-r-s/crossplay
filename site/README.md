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

The emulator rebuild CI commits after a firmware merge DOES touch `site/` --
it writes `site/emulator-manifest.json` -- so it still deploys. That is correct:
the page really did change.

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

Point Vercel at this directory as the project root; the framework preset is
**Other**. There is one build step and it does not build the site: it downloads
the browser emulator, which is no longer committed. `vercel.json` carries the
command, the output directory and an empty install command, and **How the
emulator reaches production** below says why each of the three is there.

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
stored as brotli -- the last two committed that way, the emulator published
that way (see below) -- with `Content-Encoding: br` declared for those paths in
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

## How the emulator reaches production

`site/emulator/` is 3.7MB of generated wasm that changes on every firmware
merge. It used to be committed, and the arithmetic is the whole story: 111
distinct revisions of `crossplay.wasm`, ~357MB of blobs, 56 revisions and 159MB
in one week, in a repository whose checkout is ~104MB and whose clone was ~497MB
and rising by ~20MB a day. Every clone and every CI checkout in the fork paid
for it.

It cannot simply be ignored either, and `.gitignore` said so for months: **Vercel
has no Emscripten**, so a deploy that cannot see these files ships a site whose
headline feature 404s. The browser demo is the first thing the README offers a
stranger, before install instructions.

So the bytes go where a clone does not follow, and the repository keeps a
pointer. Four files, in the order they run:

|                                             |                                                                                                                |
| ------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| `tools_local/site/publish_emulator.py`      | uploads each file to the **`emulator`** GitHub release under a content-addressed name, and writes the manifest |
| `site/emulator-manifest.json`               | ~1KB, committed. Names the assets, their sha256s, and the source revision                                      |
| `site/fetch-emulator.mjs`                   | `vercel.json`'s `buildCommand`. Downloads them during the build and verifies every hash                        |
| `.github/workflows/crossplay-site-live.yml` | asks the live origin whether it worked                                                                         |

`.github/workflows/crossplay-emulator.yml` drives the first two after every push
to `xteink` whose sources are newer than the artefact.

**`vercel.json` cannot carry comments, so its three build keys are explained
here.** All three were added together and none of them is decoration:

- `"buildCommand": "node fetch-emulator.mjs"` — the fetch itself. `node` and not
  `curl` or `python3`: only Node is on Vercel's documented build image.
- `"outputDirectory": "."` — Preset "Other" already resolves the output to `.`
  when there is no `public/`, so this changes nothing about what is served. It
  is here because an **Output Directory override left on and empty in the Vercel
  dashboard skips the build step entirely**, and that setting is invisible from
  the repository. Declaring one takes the decision away from it.
- `"installCommand": ""` — there is no `package.json`, so there is nothing to
  install and no reason to let Vercel guess a package manager.

**Three things that are load-bearing, each of which has an obvious-looking
wrong version:**

- **Asset names are the content's hash, and old assets are never replaced.** A
  rolling filename updated in place would break every redeploy of an older
  commit — Vercel's Redeploy button and its rollbacks both rebuild an old tree,
  whose manifest would name bytes that no longer exist under that name.
- **A failed fetch fails the build, deliberately.** A failed Vercel build
  promotes nothing, so the previous deployment keeps serving. A broken publish
  must cost a stale site, never a broken one — and it must never be survivable,
  because a green deploy with no emulator is invisible to every check that looks
  at the repository.
- **The files are published brotli and hashed brotli.** Everything under
  `emulator/` is served with `Content-Encoding: br` for the reason spelled out
  above, so `publish_emulator.py` refuses to publish anything that is not, and
  the manifest carries both the encoded and the decoded hash.

**Rebuilding by hand** is unchanged, and it no longer produces something to
commit:

```bash
pio run -e simulator_x4_pro -t compiledb
source ../.emsdk/emsdk_env.sh && python3 tools_local/wasm/build.py
```

The files land in `site/emulator/` and `serve.py` picks them up. Do not commit
them; land the source change and let CI publish.

**If you only want to look at the page**, you do not need Emscripten at all --
fetch the published build the same way Vercel does:

```bash
node site/fetch-emulator.mjs
```

If you do need to publish one by hand:

```bash
python3 tools_local/site/publish_emulator.py            # upload + rewrite the manifest
python3 tools_local/site/verify_live_emulator.py        # and then ask the live site
```

**The files under `site/emulator/` are still tracked**, frozen at the last
revision CI ever committed. They are the fallback for the one failure the build
cannot catch — a `buildCommand` that never runs at all — and while they are
there, a fetch finds the right hashes on disk only until the next rebuild
changes the manifest. Removing them from the index is a separate change, and the
live check above is what says it is safe to make: it compares the origin against
the manifest, so a fallback being served instead of the published bytes shows up
as a failure with the reason named.

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

**It is not committed any more.** It used to be, and that cost 111 revisions of
`crossplay.wasm`, ~357MB of history and about 20MB a day forever, on a
repository whose working tree is ~104MB. See **How the emulator reaches
production** below before changing anything about how it is built or shipped.

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

## The top bar on a phone

Five labels do not share one line at 320px, and the bar is one line high. Until
2026-09-05 the stylesheet answered that below 720px by hiding `THE SHELF` and
`PLAY NEARBY` -- the two links that lead to what the site is for -- and keeping
`ANKI DECKS`, which then wrapped to two 49px lines inside a 50px bar at 320,
390 and 414 anyway. `/study/` did the same with `INSTALL THE FIRMWARE`.

So below 720px the bar carries the wordmark and a `Menu` button, and every link
moves into a panel under it with a line each. Three files have to agree and
none of them imports another:

- `index.html` and `study/index.html` carry the `.topnav-toggle` button, give
  the `<nav>` the id its `aria-controls` names, and load the script;
- `assets/topnav.js` adds `.has-menu` to the bar and toggles `.is-open` on the
  nav;
- `styles.css` decides what those two classes mean.

`.has-menu` comes from the script rather than the markup on purpose: a page
whose scripts did not run keeps the plain inline bar instead of a button that
opens nothing. **`white-space: nowrap` is scoped to `.topbar.has-menu` for the
same reason.** Unscoped it made the no-script bar worse rather than leaving it
alone: links still inline and no longer allowed to wrap, the last one running
to x=339 past a 320px viewport, and `.topbar` is `position: fixed`, so there
was no scrollbar to reach it with. Wrapping is ugly and reachable; overflowing
a fixed bar is neither.

The close listener is on the **bar**, not the panel. The wordmark is an anchor
inside `.topbar` and outside `.topnav`, so a listener on the panel alone let it
jump the page and leave the panel covering the heading it had jumped to. Escape
hands focus back to the toggle, because `display: none` on the nav drops the
caret on `BODY` and the next Tab restarts at the top of the document; a close
caused by a click or a jump deliberately does not, since the person is already
somewhere else.

`host-tests/site/run.sh` checks both pages against the script, `aria-controls`
against the element it names, and every class the script writes against the
stylesheet's **selectors** -- `host-tests/site/css_selectors.py`, comments and
`:not()` contents stripped. Grepping the whole file was answered YES by the
sentence " * .has-menu is added by assets/topnav.js" in a comment, and a
reviewer renamed all four real rules with the suite still green and the bar
broken. What the suite cannot see is behaviour: the wordmark case and the focus
return were measured in a browser at 390x844.

The breakpoint lives only in `styles.css`. The script closes the panel by
asking whether the button still has an `offsetParent`, not by comparing a width
it would have to keep in step.

## One release request per page

The Install button names the version and the report form uses it as the version
field's placeholder. Both asked GitHub themselves for a day, which put two
identical requests on every front-page load against an unauthenticated limit of
60 an hour per IP -- the very limit `assets/install.js`'s own comment gives as
the reason it asks from the visitor's browser rather than from `/api/firmware`.

`assets/release.js` is the single asker: it publishes `window
.crossplayLatestRelease()`, memoised, never rejecting. Load it **before** the
scripts that call it. The suite checks that neither caller has gone back to
fetching for itself, and that every page carrying one loads the helper first.

## The study wizard is one step at a time, not one screen

`study/study.css` sizes the wizard to the window when the step fits and lets
the page scroll when it does not. It used to clamp to `100vh` and clip:
`.wiz-step` was `position: absolute` inside a `min-height: 0` row, so a step
taller than the window was cropped by the row rather than growing the page.
At 1440x900 the step-2 `Next` button hung 29 of its 45 pixels below the cut --
`document.elementFromPoint` at the button's own centre returned `.wiz-foot` --
and at 1280x720 none of it was on screen while `scrollHeight` equalled
`innerHeight`, so the page reported nothing more to see and looked finished
with the form cut mid-select.

The steps stack in one grid cell now and stay in flow. Two consequences worth
knowing before editing:

- The emulator panel's height is capped by `--preview-chrome`, not by the box
  around it. That cap used to be irrelevant, because the box cropped the panel
  first; it is now the only thing sizing it. It is **not** a constant: the
  foot's one sentence wraps on a narrower window, so the chrome measures 274px
  where it fits on one line and 285px at 1100x700 where it does not. The token
  is the worst case (15rem), which is why 12.5rem and then 14rem were both
  wrong. Re-run the sweep if the foot's wording changes. Below 650px tall the
  step scrolls whatever this is set to, because the side column's own text is
  taller than the window -- do not chase those sizes with the token.
- A step change resets the page scroll, in `study.js`'s `goTo()`. The page is
  the scroll container now; the step box was, and its own `scrollTop` reset
  went quietly dead when the box stopped clipping.
- Reachability is a browser question. The suite checks the mechanism -- that
  the step is in flow and the page may grow -- and cannot see whether a control
  can be clicked. `host-tests/site/study_layout.py` says so in its own header.
  It asserts PROPERTIES, not spellings: the first version forbade three exact
  declarations and a reviewer restored the identical breakage through three
  others with nothing reported. Reachability itself is measured with
  `elementFromPoint` and a real click, the way the bug was found.

## The rules this page follows

`docs/identity.md` governs the copy and the look, and it is worth reading before
changing either. The short version: black and white on purpose, screenshots are
the artwork, say the thing rather than the category, and never fake the panel.
