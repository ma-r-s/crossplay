# The Wikipedia pack

What `tools_local/wikipedia/build_pack.py` writes under `/wikipedia/` on the
card, and what the app reads back. The design is argued in
[wikipedia-plan.md](wikipedia-plan.md); this is the format, version 1.
`tools_local/wikipedia/pack_format.py` is both the writer and a reference
reader, so the format is executable rather than only described, and
`src/apps_local/wikipedia/WikipediaCore.*` is the device reader; the host
test `host-tests/wikipedia/` reads a pack the Python wrote.

Every integer is little-endian. Every string is UTF-8, never NUL-terminated,
always length-prefixed.

## Files

```
/wikipedia/manifest.json      what this pack is, and every file's size and sha256
/wikipedia/dict.zst           the zstd dictionary every block was compressed with
/wikipedia/blocks.dir         where every block lives: shard, offset, sizes
/wikipedia/titles.0.idx       title lookup for tier 0 (the essentials) and its redirects
/wikipedia/titles.1.idx       title lookup for everything after tier 0
/wikipedia/shards/000.blk     the articles, as zstd frames of ~64 KB raw each
/wikipedia/shards/001.blk ... one file per shard, <= 256 MB, in importance order
/wikipedia/install.json       written by the DEVICE before it hands the card to USB
/wikipedia/state.json         written by the DEVICE: recent articles, continue point
```

The site page and the device treat everything except the two device-written
files as immutable; a pack is replaced whole (new manifest last), never
edited. A shard or index file that is missing, or whose size differs from
the manifest, is "not on the card yet", never an error: the page copies
files in the manifest's order and the app reads whatever is there.

Copy order, which is also the order in the manifest: `dict.zst`,
`blocks.dir`, `titles.0.idx`, the tier-0 shards, `titles.1.idx`, the rest of
the shards, `manifest.json` last. So after the first tier the essentials are
complete and readable with their own titles, and nothing in the first tier
is dead weight for it.

## manifest.json

```json
{
  "format": 1,
  "pack": "en",
  "snapshot": "2026-05-13",
  "built": "2026-09-11T02:00:00Z",
  "base": "https://packs.ma-r-s.com/wikipedia/en-2026-05-b3/",
  "articles": 7238251,
  "entries": 19217771,
  "blocks": 180000,
  "dict": { "file": "dict.zst", "bytes": 110000, "sha256": "..." },
  "blocksdir": { "file": "blocks.dir", "bytes": 2880000, "sha256": "..." },
  "titles": [
    {
      "file": "titles.0.idx",
      "tier": 0,
      "entries": 61000,
      "bytes": 1400000,
      "sha256": "..."
    },
    {
      "file": "titles.1.idx",
      "tier": 1,
      "entries": 19156771,
      "bytes": 288000000,
      "sha256": "..."
    }
  ],
  "shards": [
    {
      "file": "shards/000.blk",
      "tier": 0,
      "bytes": 250000000,
      "sha256": "...",
      "firstBlock": 0,
      "blocks": 3900
    }
  ],
  "tiers": [
    {
      "name": "essentials",
      "shards": 2,
      "articles": 49938,
      "bytes": 462000000
    },
    { "name": "all", "shards": 46, "articles": 7238251, "bytes": 11500000000 }
  ]
}
```

`base` is not written by the builder: the publisher adds it to the copy it
serves at the stable name, pointing at the directory that build's parts live
in (`server/packs/scripts/publish_pack.sh`). The page fetches parts from
there, with the first twelve hex of each part's `sha256` as a query, so a
copy in flight keeps its own build after the stable name moves to the next
one, and no cache between the page and the host can hand back a previous
build's part under the same name (which happened on 2026-09-11: `dict.zst`,
same name and size after a rebuild, "arrived damaged twice"). `built` is
what tells two builds of one snapshot apart: the page compares it when a
checksum disagrees, and the reader keys its article cache on it (a locator
names a different article in each build). The reader ignores `base`.

`entries` counts title-index entries over all index files (articles plus
redirects). `blocks` is the block count in `blocks.dir`. A tier is a prefix
of the shard list plus the index files up to and including its own; its
`bytes` is the sum of everything the page must copy for it, which is what
the page's time estimate is made from. Shard `firstBlock` and `blocks` say
which directory records the shard holds, so the app can answer "is this
article on the card" from the manifest and the directory without opening
the shard. The dictionary and the directory are always copied first and are
part of every tier's bytes.

## dict.zst

The raw zstd dictionary bytes as `zstd --train` writes them, 110,000 bytes
in the reference build. Every frame in every shard was compressed with it,
so the app loads it once (`ZSTD_createDDict_byReference` over a PSRAM copy)
and decodes every block with `ZSTD_decompress_usingDDict`.

## blocks.dir

```
magic     "WKBD"          4 bytes
version   u8              1
reserved  u8, u16         0
count     u32             number of blocks
records   count x 16 bytes
```

Record `i` describes block `i`:

```
shard     u16     index into manifest.shards
slots     u16     articles in the block
offset    u32     byte offset of the frame inside the shard file
csize     u32     compressed frame bytes
usize     u32     raw block bytes (what the frame decompresses to)
```

The whole directory is loaded into PSRAM at open (16 bytes x blocks: 2.9 MB
for the full pack, 112 KB for the essentials).

## A shard, and a block

A shard file is the concatenation of zstd frames; nothing else is in it.
Frame `i` starts at `offset` and is `csize` bytes. Every frame carries
zstd's content checksum (the command-line default), which the decoder
verifies, so a corrupt block is detected on decode and reported for that one
article; nothing on the page or the device ever hashes a shard after the
copy. Decompressing a frame with the dictionary yields exactly `usize`
bytes, laid out as:

```
slots     u16             articles in this block
reserved  u16             0
start     u32 x (slots+1) byte offset of each article from the block start,
                          plus a sentinel equal to usize, so article k spans
                          start[k] .. start[k+1]
articles  slots records, back to back
```

The builder fills a block with consecutive articles until the next one would
push the raw size past 65,536 bytes; an article larger than that on its own
gets a block of its own, whatever its size. So a block is "at least one
article and at most 64 KB, unless it is one article, in which case it is as
big as that article" (247 KB is the largest in the sample; real Wikipedia
has articles past a megabyte), and the device sizes its decode buffer per
block from `usize`, reading the compressed frame through a chunked internal
bounce buffer rather than one allocation. A block never straddles two
shards; a shard closes when the next block would push it past 268,435,456
bytes.

### An article

```
titleLen      u16
title         titleLen bytes      the display title, as Wikipedia shows it
sections      u16                 number of top-level headings
  each:       u8 len, len bytes   the heading text, in article order
xhtmlLen      u32
xhtml         xhtmlLen bytes
```

The heading list is what the CONTENTS overlay shows without laying the
article out. Heading `k` (1-based, in this list's order) is the element with
`id="s<k>"` in the XHTML, so the app jumps by anchor without storing ids.
When the article has an infobox, "Quick facts" is a heading like any other
(it is the first one), so CONTENTS lists it.

### The XHTML

Well-formed, and only this:

```
<html><body>
  <h1>Title</h1>
  <p><b>Title</b> is ... with <a href="Other Title">links</a> ...</p>
  <h2 id="s1">Quick facts</h2>                               only with an infobox
  <table><tr><th>Key</th><td>Value</td></tr> ...</table>
  <h2 id="s2">First section</h2>
  <p>...</p> <ul><li>...</li></ul>
  <p>1. first item</p> <p>2. second item</p>                  an ordered list
  <h3>subsection</h3> <h4>...</h4>
  <table><tr><th>..</th></tr><tr><td>..</td></tr></table>    only when simple
  <p><i>(a table was omitted)</i></p>                         otherwise
</body></html>
```

Elements: `html body h1 h2 h3 h4 p b i a ul li table tr th td`. Attributes:
`id` on `h2` only, `href` on `a`. An `href` is a display title (spaces, not
underscores, no percent encoding, no fragment) that the pack answers to: one
of its articles or one of its redirects. The builder turns every other link
into its own text once all the pack's titles are known, so no link in a pack
is dead by construction (a pack is a subset of Wikipedia; underlined, the
links it cannot keep read as promises, and every one ended in a NOT FOUND
notice on the panel). The app still resolves an `href` through the title
index, because a part of the pack may not have arrived yet (NOT YET). Nothing
else: no `style`, no `span`, no `div`, no images,
no `dl` and no `ol` (the layout engine treats `dl/dt/dd` as inline text and
numbers no lists, so ordered lists are paragraphs carrying their number).
Character references are the five XML ones.

Rules the builder applies, each one visible on the panel:

- **The subject is bold.** The first occurrence of the title (or, failing
  that, of its first word) in the first paragraph is wrapped in `<b>`. The
  source carries no inline styling; this one is recoverable and it is the
  convention readers recognise.
- **Quick facts** come after the lead paragraphs and before the first real
  section, as Wikipedia's own mobile view orders them: one
  two-column grid, one `<tr><th>Key</th><td>Value</td></tr>` per field, a
  value cut at 28 words with "..." (the engine's grid stacks past 32 words a
  cell; a justified `<p><b>Key</b> value</p>` pulled the two apart)
  (three ASCII periods; the ellipsis glyph is not in every cut).
- **Simple tables only.** Simple means what the engine draws without
  stacking: at most four columns, and every cell at most 32 words and 512
  bytes of plain text. Anything else becomes the one italic line.
- **Only letters the reader's serif can draw.** The builder reads the glyph
  intervals from `lib/EpdFont/builtinFonts/notoserif_14_regular.h` (Latin,
  Latin-1, Latin Extended-A, Vietnamese, Cyrillic, the common punctuation;
  no Greek, no CJK, no Arabic, no Devanagari). A run of code points outside
  them is removed: a parenthetical in the lead that contains one is removed
  whole ("Suk Suk (Chinese: 叔．叔; lit. 'uncle') is" becomes "Suk Suk is"),
  elsewhere the run and an immediately preceding "Script:" label go and
  doubled spaces and punctuation are tidied. The builder prints how many
  runs it removed. Without this, one article in ten opens with a row of
  boxes in its first line.
- **Sections the reader does not want** are dropped by name: References,
  External links, See also, Notes, Further reading, Bibliography, Sources,
  Notes and references, Footnotes, Citations, Gallery. Citation marks that
  leak into paragraph text are removed.
- **Links** are kept only when their target is an article: an `en.wikipedia.org/wiki/`
  URL that is not a `cite_note`, `Special:`, `File:`, `Category:`, `Help:`,
  `Wikipedia:` or `Template:` page. The link text is re-anchored to its first
  occurrence in the paragraph.

## titles.N.idx

One file per tier. `titles.0.idx` holds every article in tier 0 and every
redirect to one of them; `titles.1.idx` holds the rest. A lookup consults
every index file present on the card, in order; a prefix search merges
their results. The two files have the same layout:

```
magic         "WKTI"    4 bytes
version       u8        1
flags         u8        0
reserved      u16       0
entries       u32       entries in this file
blocks        u32       number of 4096-byte index blocks
blockBytes    u32       4096
samplerOffset u32       byte offset of the sampler from the file start
samplerBytes  u32
index blocks  blocks x blockBytes, starting at offset 32
sampler       at samplerOffset
```

### Sort key: the fold

Entries are sorted by their FOLDED title, bytewise on its UTF-8, ties broken
by the display title bytewise. The fold is defined by one table shared by
the builder and the device (`tools_local/wikipedia/fold_table.py` generates
both the Python module and `WikipediaFold.h`, and `fold_vectors.tsv` proves
they agree), and it is deliberately not full Unicode normalisation, so both
sides can be identical:

1. `_` becomes a space; runs of whitespace collapse to one space; leading
   and trailing spaces go.
2. ASCII letters lowercase.
3. Code points in Latin-1 Supplement, Latin Extended-A and B (U+00C0 to
   U+024F), Greek (U+0370 to U+03FF) and Cyrillic (U+0400 to U+04FF) map
   through the table: accents stripped to the base letter, then lowercased
   (so "É" and "é" both become "e", "ß" stays "ß", "Ω" becomes "ω").
4. Everything else passes through unchanged.

92% of titles are ASCII, so the fast path is a lowercase; the table covers
the scripts that occur in the other 8% often enough to matter.

### An index block

```
count     u16
entries   count records, back to back; the rest of the 4096 bytes is zero
```

Each entry, front-coded against the previous entry's display title (the
first entry of a block has `shared = 0`):

```
shared    u8      bytes shared with the previous entry's display title
suffixLen u8      remaining bytes of this display title
flags     u8      bit 0: this entry is a redirect
reserved  u8      0
locator   u32     block index << 12 | slot (redirects carry the TARGET's)
suffix    suffixLen bytes
```

The device reconstructs each display title while scanning, folds it, and
compares. A block is scanned in order, so a prefix query ("new y") is: find
the block, walk forward from the first entry at or after the key, collect
entries whose folded title starts with the query, stop at the first that
does not (continuing into the next block if needed). Eight results is one
or two block reads per index file. Two entries with the same display title
never occur (the builder dedupes); two entries with the same locator do
(an article and its redirects), and both are shown.

### The sampler

```
count     u32       equals blocks
each:     u8 len, len bytes   the FOLDED title of the block's first entry
```

Loaded whole into PSRAM at open (about 1.4 MB for 19 million entries, under
20 KB for the essentials); a lookup binary-searches it and then reads exactly
one 4 KB block.

## What the device writes

### install.json

Written right before the device hands the card to the USB host, so the page
can find the right drive, know what fits, and know what is already there:

```json
{
  "device": "X4 Pro",
  "firmware": "1.13.0",
  "free": null,
  "pack": "en",
  "snapshot": "2026-05-13",
  "shardsPresent": 3
}
```

`free` is null: counting free space walks the whole FAT (seven seconds on a
16 GB card, before the install screen could draw), and the page sizes the
copy from the manifest, so the device stopped asking. The field stays for a
device that can answer cheaply; the app never guesses.
`pack`, `snapshot` and `shardsPresent` are null when no manifest is on the
card.

### state.json

Owned by the app, not the pack, and not in the manifest:

```json
{
  "continue": { "locator": 123456, "page": 12, "title": "Photosynthesis" },
  "recent": [{ "locator": 123456, "title": "Photosynthesis" }, ...]
}
```

At most ten recent entries. A locator that no longer resolves (a different
pack was installed) is dropped silently.

## Reserved for later

`/wikipedia/overlay/`: single-article frames fetched over Wi-Fi, shadowing
the base pack, with their own index. Format version 2 will define it;
version 1 readers ignore the directory.
