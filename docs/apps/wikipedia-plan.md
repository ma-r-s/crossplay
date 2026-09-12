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
screen: the address in large type, `crossplay.ma-r-s.com/wikipedia`, then
"Open this in Chrome or Edge on a computer. About ten minutes.", and a QR
code under it for a phone. The phone page is one sentence and a SEND THIS TO
MY COMPUTER button (the Web Share API, mail as the fallback), because no
phone browser can write the card; the computer is where the copy happens,
and the page says "Chrome or Edge" as a requirement, first, not as a
footnote. Nothing to configure, nothing to choose on the device.

Behind that screen the device has already put its card on the USB port, the
way Settings > USB Drive does (the same `Storage.beginUsbDrive()` call; the
QR renderer is `QrUtils::drawQrCode`, already used by Study, Instapaper and
Wallpapers), so the cable route needs nothing more from the user than the
cable. Right before it hands the card over, the device writes
`/wikipedia/install.json`: firmware version, the pack already installed if
any and how many of its parts are there (not the free space: counting it
walks the FAT for seconds, and the page sizes the copy from the manifest). That
file is what the page looks for to know it has the right drive, what fits,
and whether this is a first install or "get a newer one". When the cable
goes in the screen changes to "Connected. Follow the page on the computer."
The screen keeps the device awake for as long as it is showing (the stock
USB Drive screen does not, and deep-sleeps ten minutes into a copy with the
host mid-write; ours overrides `preventAutoSleep`). When the computer
ejects the drive, or the cable comes out, the device restarts (USB drive
mode always ends in a restart, `restartToHomeAfterStorageHandoff`), checks
the manifest against the files, and lands back in Wikipedia on the search
screen, not on Home; that landing is a small addition to the restart target
mechanism that already knows how to land in the reader. Only the host
letting go lands back in the app: Back on the install screen, and the
thirty-minute wait running out with no host, restart to Home instead,
because restarting into an app with no pack would show this screen again
with the card handed over again, and Back would never reach Home. The
screen also declares `requiresExclusiveStorageLoop()`, as the stock USB
Drive screen does: otherwise ActivityManager's own home gesture exits it
through `onExit` with no restart, and since `endUsbDrive()` cannot remount
the card, every SD open on the device fails until a power cycle
(`host-tests/wikipedia/test_usb_exclusive.py` keeps the override in place).
"Wikipedia is
ready" is said by the device after that check, never by the page. If
nothing connects for thirty minutes the screen times out back to the shelf
(the stock five minutes is shorter than finding a cable). If the card was
taken out for the other route, the app simply finds the pack on the next
open.

### The page: crossplay.ma-r-s.com/wikipedia

One page. The first thing on it is the choice of route, two big cards with a
picture each, the first one selected:

- **The essentials, with the cable. About ten minutes.** "Plug the reader
  into the computer with its cable." You get the 50,000 articles Wikipedia
  itself ranks as vital, full text, about 460 MB including their own title
  index (the index is split by tier, so the essentials carry no dead
  weight for articles they do not have).
- **All of Wikipedia, with the card in the computer. About fifteen minutes.**
  "Take the card out of the reader and put it in the computer, in its slot or
  in a card reader." 7.2 million articles, 11.5 GB. The page shows the
  measured time after the first seconds and says plainly if this connection
  needs longer than twenty minutes.

Then two steps, the same for both routes:

1. "Choose the reader" or "Choose the card": one button that opens the
   browser's folder picker. The page names the drive to pick (the card's
   volume label, from `install.json` when the device wrote one), checks the
   chosen drive for `/wikipedia/install.json` or, failing that, the
   `/.crosspoint/` folder every CrossPlay card has, and says which drive to
   pick instead if it is neither. It greys out a tier that does not fit the
   free space the device reported. Nothing else on the card is touched, and
   it writes `.metadata_never_index` and `.fseventsd/no_log` at the root so
   the Mac does not index the card while the copy runs.
2. One progress bar with the measured time left. "You can start reading on
   the reader after part 1." Interruptions are fine: unplug, come back, it
   continues where it stopped. Resume is by size: a file that exists at its
   manifest size is complete, because the browser only reveals a file after
   its close succeeded; nothing is read back through the slow port. Hashes
   are computed on the download stream as it is written, and every block
   inside a shard carries zstd's own checksum, which the device verifies on
   decode, so a bad block is one article's error line and never a
   three-hour verification.

When it is done the page says the one thing left to do for the route taken,
with a picture for the operating system it is running on: on a Mac "click
the eject arrow next to the card in Finder, then unplug"; on Windows
"unplug it"; or "put the card back in the reader and open Wikipedia". The
page cannot eject a drive itself; the File System Access API has no such
call, and the plan used to pretend otherwise.

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

Once a pack is on the card the app opens on its home: a field at the top,
and under it the doors back in. Tap the field and the fork's touch keyboard
rises (the doors stay above it; a boxed X at the field's end closes the
search, text and keyboard both; GO puts the keyboard down and leaves the
matches). As you type, up to eight titles appear under the field, and KEEP
TYPING FOR MORE when there are more; tap one and the article opens.
Matching is by prefix, case and accents folded, redirects included ("nyc"
finds New York City, "colour" finds Color). The source rows carry no
redirect list, so the builder makes the one entry every printed index has:
"Mozart, Wolfgang Amadeus" for every person (a Born or Died in the
infobox) with a plain name, so "mozart", "beethoven" and "einstein" find
the people and not only the coefficients. There is no full-text search;
nobody who shipped on this class of device had one, and the title index
answers in one card read.

The doors: CONTINUE, a filled black card naming the article you were in,
the loudest thing on the page (dimmed, and not a target, before anything has
been opened: "Open any article and it waits here."); RANDOM ARTICLE, an
outlined bar, because a random article is half the joy of Wikipedia and it
costs one lookup; and RECENT, the trail of the last articles opened, as many
as fit above the count line, never the one CONTINUE already names. The state
keeps ten. The last line says how many articles are on the card and the
date of the snapshot, in the app's own caps. This arrangement was chosen
from three rendered ones (below, "After the second critic").

No title is ever cut. A match, a recent article or a section heading too
wide for its row wraps, and the row grows to hold it; the field's prompt
("SEARCH WIKIPEDIA") is in Jersey caps so an empty field and a typed one
never look alike.

### The article

It looks like a page of a book, because it is laid out by the book engine:
the reader's serif at the reader's font size and line spacing, page turns
by the same tap zones and side buttons as a book, the same header band with
the title. Two things are the app's and not the reader's setting: the prose
is ragged right (link-dense, name-dense text on a 28-character measure
makes rivers a fifth of the measure wide when justified, hyphenation on or
off; two cold reviews found them), and hyphenation is on. The footer is a
book's running foot: the section you are in at the left, "12 of 87" at the
right ("page 12" while a first layout is still counting), and under the
words, in the gutter above the glass, a hairline with a solid band along it
as far as the page has come.

The band has a chevron at the left, the way back along the trail (the
previous article, or the main page when there is none; the edge swipe is
the other way out, straight to the main page), and the list icon at the
right, which is CONTENTS (a word in
the reader's small cut took 151 of the band's 448 pixels and left a
one-word title cut). The title is bold in the reader's
own face: at the reading size when it fits one line, otherwise two lines of
the reader's 12, the most the band's 66 visible rows hold. A title longer
than that (past 56 bytes, "List of ..." territory) is the running head's
one permitted cut, and the page then keeps its own h1 so the whole title is
on the glass. Every title-bearing slot in the app is the reader's face,
because the toybox reading cuts stop at Latin-1 and "Chișinău" drew as
"Chiinu" in them; the pill is set in the small reader cut rather than
Jersey for the same reason, three slots being all a screen has.

Order on the page: the title, the lead paragraphs, then QUICK FACTS (the
infobox as a two-column grid of key and value) flowing on from the lead,
then the sections. In an article over about 24 KB every prose section
starts on a fresh page; the layout engine does that natively when told which
headings are section anchors, and it is what gives a 40-page article its
rhythm and makes a Contents jump land cleanly. Quick facts is not an anchor:
a grid on its own page left the first page turn a third empty, which reads
as the article having ended. Below 24 KB headings flow with the text,
because the median article would otherwise fan into near-empty pages.

Getting a newer pack: the count line at the foot of the main page
("49,715 ARTICLES, MAY 2026 · GET NEWER") is a door to the install screen,
so a complete pack is no dead end. The page then copies only the parts
that changed, and the reader, seeing a different `built`, drops its
article cache.

The twenty-page gate: `tools_local/wikipedia/twenty.sh 318 <out>` opens
the app in the simulator, photographs twenty random articles from whatever
pack is on `fs_agent/wikipedia`, and composes them on one sheet. Every text
rule the builder has (padded possessives, hyphens, respellings, dead links)
was found on that sheet and on nothing smaller.

Links are underlined words, and only words whose article the pack carries
are links: the builder drops the rest to plain text once it knows every
title in the pack, so the reader never taps a promise the card cannot keep.
Tap one and that article opens; the chevron returns to
the exact page you left. The history is eight deep, like following a trail
of thought and coming back; the edge swipe drops the trail and goes to the
main page. A link into an article that is not on the card
yet (a partial copy, or a title the pack does not have) shows one line, "Not
on the card yet", and BACK; fetching it over Wi-Fi is v2.

CONTENTS, top right in the header, opens an overlay list of the section
headings, each with the page it starts on once the layout has reached it;
tap one to jump. TOP (page 1) and QUICK FACTS head the list, and the section
the page is in is set bold with a bar in the margin. On a 100-section
article the list is windowed, as many rows as fit, and the line under it
says which rows these are and where the rest is.

There is no per-article refresh over Wi-Fi in v1 (GET THE LATEST VERSION
was in the first draft and is cut; see "After the critic"): a refreshed
article from the text API would lose its links and infobox.

### Getting a newer Wikipedia

The app's settings row says "Wikipedia from August 2026" and offers GET A
NEWER ONE, which brings back the install screen: same page, same steps, the
new pack copies over the old one part by part and the old one keeps working
until the new manifest is complete. In v1 that snapshot date is the whole
update story; the per-article refresh over Wi-Fi (GET THE LATEST VERSION,
GET IT) is cut from v1 (see "After the critic") because a refreshed article
from the text API would lose its links and infobox, and a faithful one
needs the preprocessor ported to the device. No bulk update rides on Wi-Fi, because at
the measured 95 to 150 KB/s of TLS on this board a pack is a day of
download. A monthly patch overlay over Wi-Fi (per-article zstd patches,
measured at 300 to 500 MB a month) is the v2 of this screen.

## What is deliberately not there

- No full-text search. No images. No references, citations, navboxes,
  external links, categories. Math keeps its words and loses its TeX. A
  table the panel's grid can hold stays a table; a wider one becomes one
  paragraph per row, each cell labelled by its column header; a table the
  row does not carry leaves a one-line note. A section with nothing left
  under it (its only content was an image or a navbox) has no heading.
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
dictionary. One run writes the full pack and cuts the tiers. Publishing is
`server/packs/scripts/publish_pack.sh`: the directory of shards plus the
manifest goes to the Orange Pi beside the packs already there, and the
stable name (`https://packs.ma-r-s.com/wikipedia/en/`) is flipped to it.
The host is the pi behind its own Cloudflare Tunnel, deployed like the
bridges (`server/packs/`); Mario's call on 2026-09-11, over R2, because it
is how every other service here runs and it costs nothing. The essentials'
files are under Cloudflare's 512 MB per-file cache limit, so with a cache
rule on the host the edge serves them; the full pack would be the one to
move to R2 if its downloads ever weigh on the uplink.

All of English Wikipedia does not fit that builder: 7.2 million articles'
XHTML is some forty gigabytes held in memory on a machine with 24.
`build_full.py` is the same build in three passes over the rows and a
working directory: titles, order and aliases first (from here on every
title's place is known, so links can be judged); then each row converted
in a worker pool and written into a bucket file by its place, with a
reservoir of records for the dictionary; then bucket by bucket, sorted in
memory, into the writer in order. `test_build_full.py` checks it writes
the pack `build_pack.py` writes from the same rows, byte for byte in the
articles; only the dictionary's sample differs.

**The content gate (2026-09-11).** Mario's standard is correct
information, not just legible text: a diaeresis, a Greek letter or a maths
sign removed can change what a sentence says. So the text rules are
written from counts, not from imagination, and every build is measured
before it ships:

- `census.py --rows <rows> --json <out> --md <report>` counts every code
  point above ASCII the converter feeds its strip step, across every
  article: occurrences, articles, share inside a parenthetical, what the
  pipeline does to it (drawn, spelled, folded, dropped) and real sentences
  before and after, plus the collateral (drawable letters lost with an
  undrawable run) and the articles that lost the most. The outcome column
  is decided by the same tables the converter uses, so it cannot drift.
- `quality.py <pack> --summary <summary.json> --report <report.md>
  --sample 30 --plain <sample.md>` runs eighty detectors over the built
  pack (structure, headings, words, balance, source remnants, encoding,
  formulas, tables, links), prints the shape percentiles with the named
  extremes, judges the census of removed characters by block, and writes
  thirty random articles as plain text for a reviewer to read. A detector
  firing is a thing to look at, not a verdict.
- `twenty.sh` opens the app in the simulator and photographs twenty
  random articles, the test no regex replaces.

**Maths (2026-09-11, Mario's constraint: nothing more in flash).** The
dump writes every formula twice, flattened words and TeX; the words lose
every index. `tex_text.py` renders the TeX into linear text the serif
draws (real superscript and subscript digits, "a/b", "sqrt(x)", "sum from
i = 1 to n of", "[a, b; c, d]") and replaces the words when they match
what MathML would have shown for that TeX; otherwise the words stay and
the TeX goes. Counted per build (formulas_rendered, formulas_unmatched);
96% of formulas render on the essentials.

Twenty-one rounds ran on the night of 2026-09-11/12 (commits bb0ad49df
onward): each one a census or a detector report or a cold reviewer's
read of thirty random articles, every finding checked against the dump's
own text before a rule was written, the essentials rebuilt and republished
after each. Reviewer samples take `--seed`; the default seed picks the
same thirty every time. The live pack is the best build so far, and a
round that changes rules materially ends with the full pack rebuilt.
Round eleven closed the last artifact classes the gate still counted, one
cause each: Parsoid's leaked protection markers, an unclosed ref tag, a
footnote template in the prose, a quoted or comma-separated script run
that left `(")` or `(,)`, an inline label rule that took a link's own
closing tag for the end of the piece and ate "Vizing's Theorem:", the
space a lost icon left inside a link's text, a table caption repeated as
the header of every row, rp page references, a formula whose middle the
dump lost, the residue of an align block, two quoted lines joined. Four
detectors were narrowed where every hit was legitimate: a colon after a
digit is a ratio or a title, `|-` is the turnstile's spelling, `{{ A, A }}`
is set notation, `[[1,3-...` is a chemical name. What stays flagged after
that is the dump's own and is listed in the report, not hidden. Round
twelve came from a cold read of the FULL pack's sample, which is stubs
with infoboxes where the essentials are long articles: hidden ISO-date
copies, an abbreviation's tooltip, "v t e" in a table header, a coordinate
pair glued to the lead, a name glued to its birth date, a spanning cell
said once per column, stacked header rows, a definition list that lost
its values (medal counts), a chembox sub-label glued to its value, and a
unit rule of ours that superscripted a longitude. Two more reads of
fresh full samples (round thirteen) closed the infobox shapes those
showed: dead link captions, a header taken for a group, a row whose
value is the title, a romanised aside that now keeps the word
"romanized" so a Latin string is not taken for the native spelling
("Russian, romanized: Semnadtsat'"), the title's own words reordered in
a lead aside. A rule of 36 equals signs stalled a full build on an
ambiguous quantifier; every rule is now timed on 400-character runs.
A fifth read (round fourteen) settled feet-and-inches beside the
unit-power rule, taxon authorities, commas in addresses, compass
points after a parenthesis, machine dates in cells and a density row
the dump nests under "Government".

What the census decided, in order: a pronunciation between slashes or
brackets goes whole and first; a symbol the serif lacks is spelled
(`symbols.py`, written from the census, most frequent first); a letter
becomes its compatibility form when that is drawable, else its base letter
alone, counted (letter plus combining mark would draw, EpdFont overlays
marks, but the reader composes every word to NFC before layout and then
looks up the precomposed letter the serif lacks: a box on the panel,
measured on the simulator); a letter of an orthography the serif lacks
becomes the plain letter it stands in for, inside a word only; a Greek
word or a native-script name goes with its label, the romanisation beside
it stays. Mario's call (2026-09-11): Greek is defensible, IPA, Cyrillic
and the other scripts are not ("if I can't even read them why would I
want them here"); so Greek and Cyrillic words are romanised in place
(`symbols.romanize`), pronunciations go whole, the rest goes with its
label, and nothing more goes into flash. A font on the card stays a
possible later card, measured on the device for page-turn cost first. The full pack does
not publish until the gate passes on the essentials built from the same
rules.

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
4. On-demand fetch: GET THE LATEST VERSION and the missing-article GET IT
   (cut from v1; stays here as the v2 slot).
5. **Twenty random articles, screenshotted on the panel and looked at one by
   one.** Mario, 2026-09-11: this is the gate nobody remembers, and it is
   where the readability insights come from. Not a sample of the good ones:
   random locators, whatever comes out, every screenshot opened and judged
   for the pitch, "all the knowledge in the universe in your e-reader, in
   your pocket, no internet". Fix what looks wrong, render again.
6. Full pack build and publish, release.
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

## After the critic (2026-09-11)

A cold review of this plan produced 27 findings (workspace
`wikipedia-research/plan-critique.md`). What changed, in the plan above and
in the format:

- **Three blockers fixed.** The install screen keeps the device awake and
  waits thirty minutes (the stock USB Drive screen deep-sleeps at ten
  minutes with the host mid-write). The address comes first and the QR
  second, with "Chrome or Edge on a computer" said up front, and the phone
  page only sends the link onward. And the build strips runs of letters the
  reader's serif cannot draw (10% of leads carry a native name in Chinese,
  Arabic, Cyrillic or Greek that would render as a row of boxes): a
  parenthetical in the lead that holds one is removed whole, elsewhere the
  run and its "Script:" label go, and the builder prints how many.
- **Cut from v1.** The leads-only tier (a second pack with a second index and
  a stub behaviour nobody asked for), and the on-demand refresh (see above).
- **The pack.** Shards are 256 MB, not 1 GB, so a browser's swap file and
  read-back after close stay bounded and a resume loses little. The title
  index is split per tier. The lead's first mention of the title is bold
  (the source has no inline styling; this one is recoverable and it is the
  Wikipedia convention people recognise). Ordered lists carry their
  numbers as text. "Simple table" means what the engine draws without
  stacking: at most four columns, at most 32 words and 512 bytes a cell;
  anything wider is listed row by row (`table_rows` in article_html.py).
  The infobox is a QUICK FACTS section of `<p><b>Key</b> value</p>` rows,
  listed in CONTENTS.
- **The reader.** `Section` gets an explicit-path constructor (html path,
  cache directory, section anchors), which is the only way the anchors that
  make a heading start a fresh page reach the parser, and it makes `Section`
  host-testable. A heading starts a fresh page only in articles over about
  24 KB; below that the median article would fan into near-empty pages. The
  per-page link cap rises from 32 to 96 and footnote capture is switched
  off for this path (every internal link was also being recorded as a
  footnote, 288 bytes each, for a popup this app does not have). The article
  cache lives under `/.crosspoint/wikipedia/<locator>-<revision>/`, so a
  replaced article can never hit an old layout. The footer shows the page
  number and section while the layout is still building and "of N" once it
  is complete. RANDOM draws from the essentials shard.
- **Measured, not asserted, in phase 0.** Write-plus-close of a 256 MB file
  through the device from a Mac and from Windows; a fifteen-minute USB
  session under load; sustained plain-HTTP download to the card with
  power-save off (the number that decides whether "get the essentials over
  Wi-Fi, no computer" can ever meet the twenty-minute rule; at the research's
  95 to 150 KB/s it is an hour, so it stays out until measured).
- **Rejected.** Hashing shards on the page after writing (reads 11.5 GB back
  through a 1 MB/s port); a `dl` infobox (the engine treats it as inline);
  "the same pack cut at different points" for the leads tier (it was a
  different pack).

## After the second critic: the three arrangements (2026-09-11)

The three home and footer arrangements were rendered side by side and a
cold reviewer went over the composites, the per-variant screens, the logs and
the code (session scratchpad `variants-critique.md`: fifteen findings, nine
nits). What changed, in every variant, before the choice was made:

- **The footer ran off the glass.** A 28-pixel constant under a line box
  taller than that put the small cut's descenders on row 799, and in the
  third arrangement through its own rule. The footer is now sized from the
  cut's line height and ends a gutter above the glass; the ui suite holds
  it there.
- **Justified prose without hyphenation.** Rivers a fifth of the measure
  wide under every long link word. Hyphenation is on for articles when the
  reader's alignment is justified (the setting stays the reader's for
  books).
- **Titles were elided everywhere.** The results, the trail, the contents
  rows and the continue card all cut with an ellipsis, against the fork's
  rule. Rows now grow to the wrapped title; the two-column recent grid of
  the third arrangement could not hold a title at any cut and is gone.
- **The contents marker sat under the first letter** (Insets are top,
  right, bottom, left; the extra went to the right edge). The row you are
  in is bold with a full-height bar in the margin, every row carries its
  page, and TOP heads the list.
- **The trail promised ten and silently dropped what did not fit**, and
  with it the count line. The count line is reserved first and the trail
  gets what is left; the article CONTINUE names is left out of it.
- **Nothing on the article said how to go back.** A chevron on the band's
  left walks the trail back: the previous article, or the main page when
  there is none. The edge swipe leaves the article for the main page in one
  move (Mario, after the install: "the back gesture and the back arrow do
  different things").
- **The keyboard up with nothing typed was a blank panel** in the two
  arrangements that raise it on a tap: the doors now stay above the keys,
  and the X puts the keyboard down.
- **The card line was Jersey lowercase**, the only lowercase Jersey in the
  fork. It, the parts line and the field's prompt are caps now.
- **Infobox values ran together** ("13 June 1645 (aged 60-61) Higo
  Province"): the age, computed against the snapshot, goes; Born and Died
  get a comma between date and place; one item after another gets one after
  its parenthesis.
- **The source itself leaves remnants**: "(German:; 6 January", "Fernandel
  ()", 303,000 quotations padded with spaces (the " beech "). The builder
  closes them (see the builder's `strip_undrawable`), and a lead
  parenthetical with a native script is now cut segment by segment so the
  dates survive.
- **Not changed, and why.** The pill on the article band stays in the small
  reader cut (above, "The article"). A short article's sections still flow
  (the 24 KB rule, above). The band's pill hugging the glass is the shared
  headerBand's, not this app's.

**The choice.** With those fixed in all three, a cold selector picked the
third arrangement, "reading first": its filled CONTINUE card is the only
element on any of the homes that says which action matters, and the action
it names is reading; the keyboard rising on a tap is the gesture every text
field teaches, where a keyboard always up is a third of the screen in a
third typeface; and the progress rule tells a child how far in they are
without a number. What was given up: the first arrangement's zero-tap
typing, the second's page area (recovered by moving the rule into the
gutter). The selector's own findings, all fixed: the rule had cost a line
of prose; the empty CONTINUE vanished; the X read as a typed letter; the
prose had rivers even hyphenated (ragged right now); the first page turn
met a third of a page of nothing (Quick facts flows); the contents capped
at twelve rows while a thirteenth fit. Not fixed: the link underline
through descenders (the reader's, upstream's), the chevron's hairline
weight, and the knocked-out serif on the black card, which the simulator
cannot judge and hardware has to.

**The cold user test (2026-09-11).** An agent used the app in the
simulator for half an hour, 19 runs and 90 screenshots (session scratchpad
`user-test.md`). What it changed: the surname entries above; GO no longer
opens the first match; the X closes the search; NOT FOUND and NOT YET name
the title (and the link tap is logged, which nothing was); KEEP TYPING FOR
MORE; the page on the CONTINUE card; swipes turn pages; the converter drops
the TeX the dataset writes beside every formula, names a lone Greek letter
("h nu"), and omits navboxes. What it did not change: page-turn taps can
land on links (a touch reader's nature; the side buttons never miss, and the
notice now says what was tapped), a hold reads as a tap (the fork's input
layer), duplicate section names in the source, and one screen lag seen once
in five runs and never again. Its delights are in the report; the one that
matters is "it reads like a book page, not a web page".

**The twenty-article gate, second run.** Six of twenty band titles were
elided with the h1 stripped from the page, and "Chișinău" had lost two
letters in the band: the two title findings above, both fixed, and the
gate is the reason they were found.
