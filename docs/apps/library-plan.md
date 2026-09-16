# Library: fill the card with every book you will ever want

Status: research and design, written 2026-09-15 from Mario's dictated brief,
before any code. Card #517. The sections after the ledger are filled in as
the research lands; nothing below the ledger is decided until he has read it.

## What Mario asked for (the ledger)

Everything in the brief, numbered so nothing gets lost. Quoted where his
words carry the requirement; paraphrased where dictation ran on. Each item
says who decides it: **M** is his call, **me** is mine to figure out and
bring back.

1. **The premise.** Every reader has an SD card, mostly empty; cards are
   large and a book is under a megabyte. "Imagine an app that somehow has
   available every single book in existence."
2. **It is a website.** Not a device app first.
3. **Step one: pick the card.** "It asks you to insert the SD card and to
   select it."
4. **Step two: show the card.** "How big is the card, how much space has
   been used, and how much is left."
5. **Step three: choose how much to fill.** "How much of the card do you
   want to fill with books", a percentage from **30% to 95%**. Leave 30% for
   other things, or leave 5% "because they don't think they'll need the
   space". The mechanism is mine: "I'm not sure about the mechanism, but
   this is up to you to figure out." (me)
6. **Step four, maybe: questions.** "The user is asked some questions. I
   have no idea on what questions. That's up to you." The questions should
   let the site "take a wild guess on what will a person want to read". He
   is not sure the step should exist at all: "I don't even know if this is
   a good step, but it's up to you to figure out that too." (me)
7. **Step five: fill.** "The website will automatically fill the selected
   space from the card with all the books it can fit", chosen "in such a way
   that it's trying to maximize the probability of: if a person ever wants
   to read a book, the book is already on the device."
8. **Only e-reader-friendly books.** "No point in making books that are
   mostly images available, or books that are like papers or science or
   math that have no reason to be read in a tiny reader."
9. **Step six: the closing line.** "You now have 85% of books you will ever
   get recommended or want in your life." The number must be honest; how it
   is computed is mine. (me)
10. **The hard question, part one.** "Do we need to ask the user any
    questions?" (me, with evidence)
11. **The hard question, part two.** "How can we make this list generator?"
    His own idea: rank all books from most likely to be read to least, know
    each one's size, fill from the top until the space is full. (me)
12. **Research the method.** "What's the best method to achieve what I'm
    describing? I want you to research." (me)
13. **Keep the list.** "Keep track of everything I asked you for, so you
    don't forget any of it." This ledger is that.
14. **Ask.** "Ask any questions about anything that's unclear." The
    questions are at the end of this document, each with the default I take
    if he never answers.

15. **The premise is a thought experiment, and the question is the method.**
    Mario, 2026-09-15, after the first research pass drifted into which
    books can legally be shipped: "I didn't ask about legal sources, I asked
    about if I had all the books in existence." So: assume every book exists,
    with its size known, and answer how to rank, whether to ask, and what to
    say at the end. Where the books come from is an appendix, not the
    subject.

Things he did not say and I am treating as open until he does: what the
app is called; which languages; whether a device-side app is needed to make
thousands of books findable on the reader.

The research behind every number below is in the workspace's
`library-research/` (`ranking.md`, `browser.md`, `corpus.md`), each figure
with its source and date. "Measured" means computed on 2026-09-15 from
Project Gutenberg's full catalog (78,144 text records with their 30-day
download counts and EPUB sizes), used here as a sandbox to test the method
on real demand data, not as the pool.

## The answer in one paragraph

Rank every book by the probability that a reader will ever want it, divide
by its size, and fill the card from the top. That is a knapsack, and the
greedy fill is within one book of optimal because every book is tiny
against the budget. The signal for "want" is want-to-read shelvings and
readership, blended by rank, not sales. Ask one question, which languages
the person reads, because it is the only one that moves the probabilities
by more than a point or two; the books already on the card answer most of
it without a tap. Close with the share of demand the card now covers, never
with a personal "X% of the books you will ever want", because a head
inventory covers most aggregate demand and almost nobody completely.

## The method

### The objective, written down

Let W be the set of books a person will ever want to read, unknown, and S
the set on the card, with the sizes of S summing to at most the budget B.
The site wants to maximise the expected share of W that is in S. Knowing
nothing about the person, the probability that a book b is in W is its
population want-rate p_b, and the objective becomes: maximise the sum of
p_b over S subject to the sum of size_b over S being at most B.

That is a 0/1 knapsack. Sorting by p_b / size_b and packing until the
first book that does not fit is within one book of the optimum (the
fractional optimum is exactly that greedy prefix plus a fraction of the
next book, and the integer optimum can only be smaller). One book is
0.03% of a 4 GB budget, so no dynamic programming, no solver: sort and
fill. Mario's own description ("from the most likely to be read to the
least likely, and it knows how much each weighs") is this algorithm; the
one refinement is the division by size.

What the division changes, measured:

| Budget, images stripped | Fill by popularity | Fill by popularity per byte |
| --- | --- | --- |
| 4 GB | 60.7% of demand | 66.9% |
| 8 GB | 72.8% | 80.2% |
| 16 GB | 93.3% | 96.7% |

| Budget, images kept | Fill by popularity | Fill by popularity per byte |
| --- | --- | --- |
| 4 GB | 46.7% (1,653 books) | 58.0% (15,341 books) |
| 16 GB | 54.8% | 79.7% |
| 32 GB | 62.0% | 90.3% |

Popular books are longer, not shorter (Spearman +0.475 between downloads
and text-only size on the sandbox), so raw popularity order wastes bytes on
the heavy end of the head. Three rules follow. Ship the text-only edition
by default: an illustrated edition at the 90th percentile (3.2 MB) must be
sixteen times more wanted than a median novel (200 KB) to earn its place.
Recompute the densities for the edition actually shipped, because
stripping images shrinks titles by anything from nothing to fifty times
and reorders them. And count a work once: rank works, ship one edition per
work.

### The signal: what "want" is

"If a person ever wants to read a book" is awareness times appeal, and it
is measured most directly by want-to-read shelvings: the reader has heard
of it and reached for it. Readership (ratings counts, checkouts,
downloads) and awareness (page views, appearances on lists) are the
supporting signals. Sales are the weakest proxy: they measure what was
bought, often as a gift, once.

With every book available, the sources that exist today and what each is
worth:

| Signal | What it measures | Access |
| --- | --- | --- |
| Goodreads want-to-read and ratings counts (UCSD Book Graph, 2017: 2.36 M books, 228.6 M interactions with want-to-read flags) | Mass-market intent and readership | Academic-only licence; a shipping product cannot use it |
| Open Library reading log (12.8 M shelf events; `want_to_read_count` per work in the search API) | Explicit intent | Free monthly dump, 65 MB |
| Open Library ratings (1.05 M) | Readership | Free, 5 MB |
| Seattle Public Library checkouts (51.8 M rows since 2005, public domain; follows the same power law as sales, R^2 0.987) | Real reading, all formats | Free, one city |
| Wikipedia page views (`agent=user`, monthly since 2015) | Public curiosity, bot-filtered, multilingual | Free per title; needs a title map |
| OCLC Top 500 by library holdings | The canon | One CC-BY CSV; holdings in bulk are not public |
| Gutenberg 30-day downloads | Readership of one audience, every title | Free daily |
| Bestseller lists, prize lists, best-of aggregators | Awareness | Public, no counts, head only |

Combine them as a weighted sum of log-normalised ranks (the log kills each
source's crawler floor and its scale), weights by trust, averaged over
years rather than a month so that "will ever want" keeps the books people
wanted a decade ago. Each source's coverage is partial, so the aggregation
must treat a missing value as unknown, not as zero.

### How concentrated demand is

The sandbox's demand curve, share of all downloads captured by the top N
titles: top 100, 10.9%; top 1,000, 43.6%; top 10,000, 61.5%; top 50,000,
89.4%. The shape is the one every content cache sees: Zipf-like, with the
hit ratio growing like the logarithm of the cache size. A 16 GB card holds
about 60,000 text-only books, a 64 GB card about 250,000, so on this scale
the card is a very large cache.

The number that matters for the closing line comes from Goel and
colleagues' study of the long tail on Netflix and Yahoo Music (2010): the
3,000 most popular movies still leave 13% of consumption unmet, and only
11% of Netflix users are completely covered by a 3,000-title inventory
(63% are at least 90% covered); on Yahoo Music a 50,000-title inventory
fully covers 5% of users. Everyone is a bit eccentric. A head inventory
covers most of aggregate demand and almost no individual completely, and
books, with a longer tail than films, are not kinder.

### Do questions help

Three kinds of evidence, all pointing the same way.

Recommender systems: at list lengths of five to ten, personalised methods
beat "most popular" by two to three times in offline recall (Cremonesi and
colleagues 2010 and 2011; Ferrari Dacrema 2019), yet in the one user study
that asked, people rated the popular list as the most relevant. This card
is not a list of ten; it is a cache of tens of thousands.

Caching: the closest formulation to this product is proactive caching
under a storage budget. Preference-aware caching beats popularity caching
by 8% on average and up to 22% (SPARCQ, 2025), and only when tastes are
heterogeneous and clustered; when preference similarity is high the gain
"diminishes substantially".

The sandbox, measured with coarse profiles at a fixed budget, share of the
reader's own demand captured, global fill against a fill conditioned on
the answer:

| Reader | 4 GB, global / conditioned | 8 GB, global / conditioned |
| --- | --- | --- |
| English only (88% of all demand) | 69.4% / 70.9% | 82.8% / 85.2% |
| French only | 48.2% / 100% | 60.5% / 100% |
| German only | 58.5% / 100% | 68.9% / 100% |
| Spanish only | 52.6% / 100% | (all fits) |
| English, fiction only | 63.4% / 67.6% (at 2,000 titles) | 71.2% / 82.1% (at 10,000) |
| English, non-fiction only | 33.5% / 39.3% | 56.9% / 65.6% |

Language moves the answer by tens of points for anyone who does not read
English, and by about one point for someone who does, because the head of
demand is per language and a global list is English-dominated. Fiction
against non-fiction is worth four to eleven points, but only for a reader
who excludes the other half; for a mixed reader the global list is within
a point of optimal. Finer genres: no evidence they help a coverage
objective, and Goel's finding that individual tails are not a genre
phenomenon says they cannot help much.

So the page asks one question, which languages you read, as a multi-select
pre-filled from the browser's language and from the books already on the
card (the walk that measures used space also reads the EPUBs' language
and authors; the card is the questionnaire). An optional "mostly fiction /
mostly non-fiction / both" toggle, default both. Nothing about genres. For
the enthusiast, a later version can accept a Goodreads or StoryGraph
export: the want-to-read shelf goes on first, then the rest by the same
rule.

### What to say at the end

Three formulations, in order of honesty:

1. **Demand share, the headline.** "Your card holds the books behind 94%
   of what readers reach for." Sum of p_b over the card divided by the
   sum over every book, with the population named ("readers of ...").
   Computable, checkable, and true in aggregate.
2. **A named list, the proof line.** "Including 183 of the 500 novels
   most held by the world's libraries." Concrete, and immune to noise in
   the tail.
3. **The personal claim** ("85% of the books you will ever want"): not
   computable for one person, and by Goel's numbers false for most people
   at any head size. If Mario wants the personal phrasing, it needs a
   hedge: "if you read like everyone else, 94% of the books you will reach
   for are already here", and never a per-person number above the
   aggregate one.

### The e-reader filter

Item 8 is a filter on form, not subject: text that reads on a 480 by 800
one-bit panel. Out: image-led books (comics, art, photography, picture
books; detectable by the ratio of image bytes to text bytes and by
subject), reference works and tables (dictionaries, catalogs, timetables,
almanacs), periodicals and serials, sheet music, and any book whose text
depends on figures or formulas. A history of science reads fine; a
calculus textbook does not. The rule is written after a census of what
each candidate rule removes, never before (the corpus is counted first,
the rule second), and the closing-line denominator is the pool after the
filter.

## The site

### Picking the card and showing it (items 3 and 4)

The picker is the File System Access API, which exists in Chromium
browsers only (Chrome, Edge, Opera on the desktop, Chrome on Android since
132; Safari and Firefox have no directory picker, Brave ships it disabled).
The page says "Chrome or Edge on a computer" first, the way the Wikipedia
page does, and the phone page sends the link to a computer.

Two of the three numbers in item 4 the page can produce, and one it
cannot. **Used** it computes by walking the picked folder and summing file
sizes (the walk time for tens of thousands of files is unmeasured; the
prototype measures it and shows a count while it runs). **Total** no
browser API reports, on purpose (capacity leaks are a fingerprinting
vector), so it comes from one of two places: the device, whose install
screen writes it into `library/install.json` before handing the card over
(the card's size is a register read, free; the Wikipedia screen already
writes an `install.json` with `free: null` because counting free clusters
walks the FAT for seconds, but total costs nothing), or, on a card that
never went through that screen, the person, from a row of chips (16, 32,
64, 128, 256 GB; the X4 Pro ships with 16 GB and takes up to 256). **Free**
is the difference, labelled "estimated" when the total came from a chip.
The page also checks the folder is a reader's card (`/.crosspoint/`
exists) and says which drive to pick if it is not.

### The slider (item 5)

"Fill the card up to N%", 30 to 95, with the budget being N% of the total
minus what is used, shown as gigabytes and as a book count, and with the
time it will take once the first part has been measured. Books already on
the card count as used and are never touched. Running the page twice is
additive: it fills to the new mark from where the last run stopped, so
"more books" later is the same button.

### The copy (item 7)

The Wikipedia page's writer is the template and most of it is reused as
is: a manifest, a folder handle, streamed writes in 2 MB chunks with the
previous write awaited, a rolling rate, a projection that stops before the
next part if it crosses twenty minutes, resume by size (a file at its
manifest size is complete, because the browser only reveals a file after
its close succeeded), "the card is full" from the quota error, a marker
file so a second visit skips without re-reading, and a Playwright harness
that swaps the picker for the browser's private file system so every line
after the dialog runs in a test.

Two things are new. The pack arrives as tar shards of about 500 MB (under
the edge cache's per-file limit, some forty requests per 20 GB, sequential
and resumable by offset) and the page unpacks entries into individual
EPUB files as they stream, because the reader opens files. And the writes
run four to eight at a time, because the bottleneck for tens of thousands
of small files is not the card but the browser: Chrome scans every written
file (about a thousand files a minute when serialised, on Windows). The
pack is ordered by rank and cut into parts, so a partial copy is always
the most useful subset and "you can start reading after part 1" is true.

| Amount | Class 10 card (10 MB/s) | Typical card (50 MB/s) | 100 Mbit/s connection |
| --- | --- | --- | --- |
| 5 GB (about 20,000 books) | 8 min | 2 min | 7 min |
| 10 GB | 17 min | 3.5 min | 14 min |
| 14 GB | 23 min | 5 min | 19 min |

Plus the per-file scan, which the prototype measures. The twenty-minute
rule (the Wikipedia plan's) will cut the default fill at the head of the
list on slow cards and slow connections, and that is the right cut: the
head is where the demand is.

Names are sanitised to the FAT rules the firmware already uses
(`FsHelpers::sanitizePathComponentForFat32`), kept ASCII, and the books
are bucketed into folders, because FAT32 on Windows caps a directory at
65,535 slots and a long name uses several. The page writes
`.metadata_never_index` at the root so a Mac does not index the card while
the copy runs (the Wikipedia plan says it does; the code does not; both
pages should).

### Hosting

The pack lives where the Wikipedia pack lives: `packs.ma-r-s.com`, plain
files on the Orange Pi behind a Cloudflare Tunnel with the edge caching
each shard for a day, cross-origin and range requests already enabled and
verified. Mario chose it over R2 on 2026-09-11 because it is how every
service here runs and costs nothing. If the pi's uplink ever weighs under
the library's downloads, Cloudflare R2 is the move: zero egress, about
sixty cents a month for a hundred people pulling 20 GB each. Vercel is not
an option for the bytes at any plan (bulk file distribution is named as
ineligible in its terms), only for the page.

## On the device

The brief is a website, and the website's job ends with the files on the
card. But a card with 40,000 books and a reader with no search is a broken
product, and that is the reader we have. Facts from the code:

- The file browser reads a whole directory into RAM with no cap, four
  vectors per entry, sorts it all, and its own comment calls 500 files a
  large directory. It has no search. `/.crosspoint/` gets one cache
  directory per book on first open, unbounded and never evicted.
- The reader caches a book by the hash of its path: move or rename a
  book and its progress is gone. The library's layout must never change
  between versions.
- Names longer than the reader's line wrap or are cut by the browser, and
  the OPDS downloader already limits a name to 100 bytes with the author
  capped at 34; the pack uses the same budget.

So the layout is `/Library/<language>/<A>/<Author, Name>/<Title>.epub`,
with any bucket over about two hundred entries split further, and the
recommendation is a Library app on the device: a search field with
prefix matching over a title-and-author index the site writes beside the
books (the Wikipedia app's folded title index, built for 19 million
entries, does 40,000 in one card read), "surprise me", browse by author,
and a count line saying what is on the card. Without it the books are
there and unreachable. This is a product question and the last section
asks it.

## Questions for Mario, each with the default I take if he never answers

1. **Is this a design exercise, or does it get built?** If built, the
   only pool a public site can hand strangers today is the appendix's
   (public domain). The method above is pool-agnostic. Default: I build
   the pipeline and the page against the pool we can ship, so the method
   runs on real data and a stranger can use it, and the ranking swaps in
   whatever pool exists later.
2. **One question or none?** Default: one, languages, pre-filled from the
   browser and the card, plus the optional fiction toggle.
3. **The Library app on the device?** Default: yes, as the second pull
   request after the page, with the folder layout alone as the first
   version so the page can ship.
4. **The closing line: aggregate or personal?** Default: aggregate
   headline and a named-list proof line; the personal phrasing only with
   the hedge.
5. **The name.** Default: Library, tile LIBRARY, page `/library/`.
6. **The slider's meaning.** Default: "fill the card up to N% full",
   which is the reading where "leave 30% for other things" is true.

## What gets built first

1. `tools_local/library/`: the census (what each filter rule removes,
   counted), the ranking (signals, aggregation, density), the sharder and
   the manifest, and the publish script for the packs host. **Built
   2026-09-15 through the sharder** (`catalog.py`, `rank.py`,
   `olsignal.py`, `wikisignal.py`, `build_pack.py`; the all-books side is
   `universe.py`, `universe2.py`, `wikiviews.py`), measured in
   [`library-data.md`](library-data.md). Still to do here: the optimiser
   pass (the six steps every X4 EPUB tool converges on, from
   `epub-tools/`; Gutenberg's text-only EPUBs carry no fonts or images,
   so it is CSS and structure only), the publish script, and tests on the
   ranking's invariants: one edition per work, greedy within one book of
   the fractional bound, the language split.
2. `site/library/`: the page, built from the Wikipedia page's writer,
   with the walk, the chips, the slider and the language question added,
   and the same Playwright harness.
3. The device: folder layout only, then the Library app.

## Appendix: where the books would come from today

Per item 15 this is not the subject; it is recorded so whoever builds
knows the pool. A public site can legally hand strangers the public
domain and nothing else: Project Gutenberg's 78,144 texts (61,832
English; 19.2 GB as text-only EPUBs, 125 GB with images) and Standard
Ebooks' 1,530 hand-finished editions (about 1.6 GB, CC0). In the US that
is everything published in 1930 or earlier, one more year each January;
in the EU it is life plus seventy, a different set (Orwell is free there
and not here until 2041; Hemingway the other way round until 2032). Of
what people say they want, that pool covers about two thirds of a canon
list, 29 of the BBC Big Read's top 100, 13 of Goodreads' top 50, and
nothing from this decade. Gutenberg's servers send no cross-origin
headers and forbid bulk pulls, so the pack is built by rsync from a mirror
and rehosted, which they explicitly allow. The shadow libraries (Anna's
Archive, Library Genesis, Z-Library) are the only place "every book"
exists, and a public site redistributing from them is direct
infringement with the exposure in the news this year; not a source at any
size.
