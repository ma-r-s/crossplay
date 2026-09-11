# Wikipedia on the reader: the experience first, then the machinery

Status: v1 design, written 2026-09-10 before any code. The research behind
every number is in the workspace's `WIKIPEDIA-RESEARCH.md` and
`wikipedia-research/` (sources, prior art, on-device format, reader pipeline).
Card #468.

## The bar Mario set

"The best experience possible, and that should be the center of it." Reading,
getting the data onto the card, updating and navigating, all seamless enough
that a five-year-old can install it: open the app, it shows a QR code, the
website says plug the reader in and press a button, and that is it. Readable
first; it must look natively made for this device, not hacked together. An
article in a markdown-like form that reads comfortably end to end, comfortable
navigation inside long articles, links that open other pages.

Every decision below is measured against that, and the machinery section
exists only to make the flows in the next section true.

## What it looks like

### First open, nothing on the card yet

**The rule: nothing in this flow takes more than twenty minutes.** Mario,
2026-09-10: "six or even three hours is way too much. Anything that takes
more than twenty minutes is way too much." The page measures before it
promises, and if the measured rate cannot finish inside twenty minutes it
says so before starting and offers the route that can.

That rule plus two physical facts decide the shape. The ESP32-S3's USB port
is Full Speed (12 Mbit/s) and the card behind it runs 1-bit at 20 MHz, so
writing through the device tops out near 1 MB/s: twenty minutes through the
cable is about one gigabyte, and all of Wikipedia (11.5 GB) can never go
that way. A card in the computer writes at 10 to 20 MB/s, so all of
Wikipedia is ten to twenty minutes there, bounded by the internet connection
rather than the card (11.5 GB is 15 minutes at 100 Mbit/s). So there are
two routes, and the pack is built so both write the same files in the same
order.

The shelf tile says WIKIPEDIA. Opening it with no pack on the card shows one
screen: a QR code in the middle, under it "Scan this with your phone. It
takes about ten minutes.", and the address in small type for people without
a scanner. Nothing to configure, nothing to choose on the device.

Behind that screen the device has already put its card on the USB port, the
way Settings > USB Drive does (the same `Storage.beginUsbDrive()` call; the
QR renderer is `QrUtils::drawQrCode`, already used by Study, Instapaper and
Wallpapers), so the cable route needs nothing more from the user than the
cable. When the cable goes in the screen changes to "Connected. Follow the
page on the computer." When the computer ejects the drive, or the cable
comes out, the device restarts (USB drive mode always ends in a restart,
`restartToHomeAfterStorageHandoff`) and lands back in Wikipedia on the
search screen, not on Home; that landing is a small addition to the restart
target mechanism that already knows how to land in the reader. If nothing
connects for a few minutes the screen times out back to the shelf, exactly
like the USB Drive screen does. If the card was taken out for the other
route, the app simply finds the pack on the next open.

### The page: crossplay.ma-r-s.com/wikipedia

One page. The first thing on it is the choice of route, two big cards with a
picture each, the first one selected:

- **The essentials, with the cable. About ten minutes.** "Plug the reader
  into the computer with its cable." You get the 50,000 articles Wikipedia
  itself ranks as vital, full text, about 450 MB. (If the hardware spike
  measures the device's port at 1 MB/s or better, this card also carries
  the first paragraph of every other article, another 1 GB, and the wording
  becomes "every article, and the 50,000 most important ones in full".
  Under 1 MB/s it does not, and the rule holds.)
- **All of Wikipedia, with the card in the computer. About fifteen minutes.**
  "Take the card out of the reader and put it in the computer, in its slot or
  in a card reader." 7.2 million articles, 11.5 GB. The page shows the
  measured time after the first seconds and says plainly if this connection
  needs longer than twenty minutes.

Then two steps, the same for both routes:

1. "Choose the reader" or "Choose the card": one button that opens the
   browser's folder picker. The page checks the chosen drive for the
   `/.crosspoint/` folder every CrossPlay card has and says "That is not the
   reader's card" if it is missing. Nothing else on the card is touched.
2. One progress bar with the measured time left. "You can start reading on
   the reader after part 1." Interruptions are fine: unplug, come back, it
   continues where it stopped, because the parts are files with hashes.

When it is done the page says the one thing left to do for the route taken:
"Eject the reader, then unplug it" (the eject is what makes the device
restart into Wikipedia, so the reader's screen changing is the signal that
it worked) or "Put the card back in the reader and open Wikipedia".

What makes the two routes one product rather than two:

- **The parts are ordered by importance.** Part 1 is the essentials; the
  parts after it are the rest of Wikipedia by readership. The two cards are
  the same pack cut at different points, so a partial copy is always the most
  useful subset, and "you can start reading after part 1" is literally true.
  Someone who took the cable route can add the rest later with the card in
  the computer, and the page copies only what is missing. An article whose
  part has not arrived yet says so on the device and offers to fetch it over
  Wi-Fi.
- **The page never prints a number it did not measure.** It writes the first
  part, measures the rate, projects, and stops to say so if the projection
  crosses twenty minutes.

The page is a static page: fetch a manifest, pick a folder, stream shards
into it with range requests and resume, verify hashes, done. No Pyodide.
It needs a Chromium browser for the folder picker, the same limit the Study
page lives with; Safari and Firefox users get the same files as a plain
download plus the card-in-the-computer instructions.

### Search, the app's home

Once a pack is on the card the app opens on search. A field at the top with
the fork's touch keyboard already up. As you type, up to eight titles appear
under the field; tap one and the article opens. Matching is by prefix, case
and accents folded, redirects included ("nyc" finds New York City, "colour"
finds Color). There is no full-text search; nobody who shipped on this class
of device had one, and the title index answers in one card read.

With the field empty the screen shows "Continue: <the article you were in>",
the last ten articles, and a RANDOM button, because a random article is half
the joy of Wikipedia and it costs one lookup. The footer says how many
articles are on the card and the date of the snapshot.

### The article

It looks like a page of a book, because it is laid out by the book engine:
the reader's serif at the reader's font size and margins, page turns by the
same tap zones and side buttons as a book, the same header band with the
title. The footer says "12 of 87" and the name of the section you are in.

Order on the page: the title, the lead paragraphs, then QUICK FACTS (the
infobox as a two-column list of key and value), then the sections. Every
top-level section starts on a fresh page; the layout engine does that natively
when told which headings are section anchors, and it is what gives a
40-page article its rhythm and makes a Contents jump land cleanly.

Links are underlined words. Tap one and that article opens; Back returns to
the exact page you left. The history is eight deep, like following a trail
of thought and coming back. A link into an article that is not on the card
yet (a partial copy, or a title the pack does not have) shows one line, "Not
on the card yet", with GET IT if Wi-Fi is reachable.

CONTENTS, top right in the header, opens an overlay list of the section
headings; tap one to jump. It also has TOP and QUICK FACTS at the head of the
list. On a 100-section article the list scrolls; it is windowed the way the
reader's chapter list is.

The article's menu has one more thing: GET THE LATEST VERSION. It brings up
the reader's Wi-Fi picker if needed, fetches the current article, and
replaces the copy; the footer date changes. That is the whole of "update"
for v1, and it is the version of update that matches how people read: the
article in front of you is current, the ones you never open do not matter.

### Getting a newer Wikipedia

The app's settings row says "Wikipedia from August 2026" and offers GET A
NEWER ONE, which brings back the QR screen: same page, same three steps, the
new pack copies over the old one part by part and the old one keeps working
until the new manifest is complete. No bulk update rides on Wi-Fi, because at
the measured 95 to 150 KB/s of TLS on this board a pack is a day of
download. A monthly patch overlay over Wi-Fi (per-article zstd patches,
measured at 300 to 500 MB a month) is the v2 of this screen.

## What is deliberately not there

- No full-text search. No images. No references, citations, navboxes,
  external links, coordinates, categories. Math keeps its TeX text. Complex
  tables are dropped with a one-line note; simple ones stay.
- No settings inside the app beyond the pack row. Font, size, margins are the
  reader's settings, so Wikipedia changes when the reader does.
- No account, no server of ours in the reading path. The pack is files on a
  card; the on-demand fetch talks to Wikipedia's own API with a proper
  User-Agent.

## The machinery

### The pack on the card: `/wikipedia/`

```
manifest.json        pack id, snapshot date, article count, tier cut points,
                     dictionary sha256, every shard's size and sha256
dict.zst             the 110 KB trained zstd dictionary
titles.idx           front-coded folded titles in 4 KB blocks, sampler at the
                     head; entry = suffix + locator (shard, block, slot);
                     redirects are entries pointing at the target's locator
blocks.dir           16 bytes per block: shard, offset, compressed size, raw size
shards/00.blk ...    <= 1 GB each, preallocated contiguous, in importance order
overlay/             single-article frames fetched on demand + their index
```

A block is 64 KB of raw article XHTML compressed as one zstd -19 frame with
the dictionary (measured on 3,000 real articles: ratio 3.45 at 64 KB with
the dictionary, against 3.03 without). A block holds about twelve articles
grouped by title within their shard, behind a slot table. Each article begins
with a header: display title, byte length, revision, and the list of
top-level headings with their anchor ids, which is what CONTENTS reads
without laying the article out.

Shard membership is by importance (Vital levels 1 to 5, then monthly
readership, then everything else); order inside a shard is by title, which is
what keeps the ratio. "The essentials" is shard 0; "first paragraph only" is
a separate small pack built from the same rows.

### The article format

XHTML, well-formed, `<html><body>` around it, produced at build time from
Enterprise Structured Contents (paragraphs with links, sections, infobox as
key/value, lists), with this subset and nothing else:

`h1 h2 h3 h4 p ul ol li b i a table tr th td`, plus `div`/`p` for the
infobox rows (the layout engine treats `dl/dt/dd` as inline, so the pack
never emits them). Every `h2` carries an `id`; the id list is the CONTENTS.
Internal links are `<a href="Title">`; the engine passes hrefs through
verbatim and the app resolves them against the title index. The
preprocessor is `tools_local/wikipedia/article_html.py`, prototyped tonight
as `wikipedia-research/measurements/sample_pack.py`.

### Reading: the book engine, not a new renderer

`Section` (`lib/Epub/Epub/Section.cpp`) is file in, file out: it reads HTML
from a path, streams laid-out pages to `sections/<n>.bin`, and never holds
the document in RAM. The only EPUB coupling is the zip inflate, and that call
is skipped when the HTML already sits at `<cache>/html/<n>.html`. The
dictionary already drives the same parser with a null `Epub` and no images;
the host test `test/chapter_html_slim_parser` constructs it that way too. So
the article path is:

1. look the title up (one card read), read its block (one card read, ~50 ms),
   decode into PSRAM (~15 ms), write the article's XHTML to the cache html
   path (one write);
2. build pages incrementally with `Section::startBuild` / `buildSomeMore(8)`
   exactly as `EpubReaderActivity::renderBook` does, first page on screen
   immediately, the rest trickling in the background under the same heap
   gate; the `h2` ids are passed as section anchors so each starts a fresh
   page;
3. render each page with the reader's two-pass glyph prewarm; hit-test taps
   with `EpubReaderUtils::linkAtPoint` over the page's link rectangles;
   CONTENTS jumps through `Section::findAnchor`.

A 250 KB article is inside the envelope: the reader already lays out
584 KB single-chapter novels through this code, page by page.

Two caps in the page format to measure on a link-dense lead paragraph:
32 links per page, and every internal link also recorded as a footnote entry
(288 bytes each). If Wikipedia's density trips them, both are one
section-format version bump away. The article cache under `/.crosspoint/`
is pruned to the last 32 articles.

### The device app: `src/apps_local/wikipedia/`

- `WikipediaCore` (freestanding, host-tested): title folding (ASCII fast
  path, a few KB of case and diacritic tables for the 8% of non-ASCII
  titles), index block decode, locator math, overlay shadowing, manifest
  parsing, tier state ("which parts are here").
- `WikipediaPack`: the card side: block read into an internal bounce buffer,
  zstd decode with the dictionary into PSRAM (`lib/zstd/`, the single-file
  decoder, 40 to 70 KB of flash; DCtx 96 KB and DDict 27 KB placed in PSRAM
  through static-init).
- `WikipediaActivity`: search home. `WikipediaArticleActivity`: the reader
  above, with the history stack and the CONTENTS overlay.
  `WikipediaInstallActivity`: the QR + USB drive screen and the restart
  target. Screens as free functions over plain models so `host-tests/ui`
  draws them.
- On-demand fetch: `HttpDownloader` to Wikipedia's API
  (`action=query&prop=extracts` for text, or the page HTML through the same
  preprocessor rules if we want links in fetched articles), written into the
  overlay as a single-article frame.

### The build: `tools_local/wikipedia/`

`build_pack.py` reads the `wikimedia/structured-wikipedia` parquet, the
Vital Articles JSON and one monthly `pageview_complete` file; `article_html.py`
makes the XHTML; `pack_format.py` is the writer and a reference reader (the
Trivia pack's shape, so the format is executable); `zstd --train` makes the
dictionary. One run writes the full pack and cuts the tiers. Publishing is a
directory of shards plus the manifest on Cloudflare R2 (no egress fees; the
`cf` CLI is already authenticated here), mirrored as release assets if we
want a second home.

### The site: `site/wikipedia/`

Static HTML and JS: fetch the manifest, folder picker, marker check, speed
probe, tier cards with measured estimates, streaming copy with resume (skip
shards whose size and hash already match), and the eject hint. The same
page serves "get a newer one".

## Order of work

0. **Spike on hardware, one day, before anything else**: zstd decode on the
   S3 (RAM, MB/s, PSRAM penalty); MSC write throughput through the device;
   SDMMC at 40 MHz (card #467); `Section` on a 250 KB article with a
   30-link paragraph. These four numbers decide block size, the page's
   honest time estimates, and whether the link caps need a format bump.
1. Build tool, pack format, host tests; the essentials pack built on this Mac.
2. Device: pack reading, search, article, CONTENTS, links, history. Verified
   in the simulator with a seeded card, then on Mario's Developer Mode unit.
3. Install flow: QR screen, USB drive, restart-to-app; the site page.
4. On-demand fetch: GET THE LATEST VERSION and the missing-article GET IT.
5. **Twenty random articles, screenshotted on the panel and looked at one by
   one.** Mario, 2026-09-11: this is the gate nobody remembers, and it is
   where the readability insights come from. Not a sample of the good ones:
   random locators, whatever comes out, every screenshot opened and judged
   for the pitch, "all the knowledge in the universe in your e-reader, in
   your pocket, no internet". Fix what looks wrong, render again.
6. Full pack build, R2, release.
7. Later: the monthly patch overlay.

## Open risks, stated

- The through-the-device copy speed. Measured nowhere yet; it decides only
  whether the cable card carries the first paragraphs too (needs 1 MB/s or
  better). The twenty-minute rule holds either way because the full pack
  never takes the cable route.
- The internet connection, which the card route is bounded by: 11.5 GB is
  15 minutes at 100 Mbit/s and 30 at 50. The page measures and says so; a
  "most-read million articles in full, first paragraph of the rest" cut
  (about 5 GB) is the fallback tier if that turns out to be most people.
- Flash budget: the app plus the zstd decoder against the legacy 6.25 MB
  slot; check `gh_release_x4pro` before merging.
- Free space: ask `HalStorage::freeBytes` once before staging an article and
  before an on-demand fetch; refuse on unknown with its own sentence.
- The cache directory is named `epub_<hash>` by `Epub`; either accept it or
  add the small explicit-path constructor to `Section`.
