# The Crossplay site

Static. No build step, no framework, no dependencies. `index.html`, one
stylesheet, and the assets it names.

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

```bash
cd site && python3 -m http.server 8099
```

Changes must be _looked at_, not reasoned about -- the same rule the device
apps follow. `scratchpad/pageshot.py` renders full-page and sliced captures at
any width and colour scheme through the Chrome already installed:

```bash
uv run --with playwright python pageshot.py http://localhost:8099/ /tmp/pageshots 1440 light
```

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
