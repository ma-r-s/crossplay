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
/wikipedia/titles.idx         title lookup: sorted, front-coded, 4 KB blocks + sampler
/wikipedia/blocks.dir         where every block lives: shard, offset, sizes
/wikipedia/shards/000.blk     the articles, as zstd frames of ~64 KB raw each
/wikipedia/shards/001.blk ... one file per shard, <= 1 GB, in importance order
/wikipedia/state.json         written by the DEVICE: recent articles, continue point
```

The site page and the device treat everything except `state.json` as
immutable; a pack is replaced whole (new manifest last), never edited. A
shard file that is missing or whose size differs from the manifest is "not
on the card yet", never an error: the page copies shards in order and the
app reads whatever is there.

## manifest.json

```json
{
  "format": 1,
  "pack": "en",
  "snapshot": "2026-05-13",
  "built": "2026-09-11T02:00:00Z",
  "articles": 7238251,
  "entries": 19217771,
  "blocks": 180000,
  "dict": { "file": "dict.zst", "bytes": 110000, "sha256": "..." },
  "titles": { "file": "titles.idx", "bytes": 290000000, "sha256": "..." },
  "blocksdir": { "file": "blocks.dir", "bytes": 2880000, "sha256": "..." },
  "shards": [
    {
      "file": "shards/000.blk",
      "bytes": 450000000,
      "sha256": "...",
      "firstBlock": 0,
      "blocks": 7000
    }
  ],
  "tiers": [
    {
      "name": "essentials",
      "shards": 1,
      "articles": 49938,
      "bytes": 452000000
    },
    { "name": "all", "shards": 12, "articles": 7238251, "bytes": 11500000000 }
  ]
}
```

`entries` counts title-index entries (articles plus redirects). `blocks` is
the block count in `blocks.dir`. A tier is a prefix of the shard list; its
`bytes` is the sum of everything the page must copy for it (dictionary,
index, directory and its shards), which is what the page's time estimate is
made from. Shard `firstBlock` and `blocks` say which directory records the
shard holds, so the app can answer "is this article on the card" from the
manifest and the directory without opening the shard.

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
Frame `i` starts at `offset` and is `csize` bytes. Decompressing it with the
dictionary yields exactly `usize` bytes, laid out as:

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
gets a block of its own, whatever its size (the longest seen so far is
247 KB). A block never straddles two shards; a shard closes when the next
block would push it past 1,073,741,824 bytes.

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

### The XHTML

Well-formed, and only this:

```
<html><body>
  <h1>Title</h1>
  <p>lead paragraph, with <a href="Other Title">links</a> ...</p>
  <div class="facts"><p><b>Key</b> Value</p> ...</div>      the infobox, if any
  <h2 id="s1">First section</h2>
  <p>...</p> <ul><li>...</li></ul> <ol><li>...</li></ol>
  <h3>subsection</h3> <h4>...</h4>
  <table><tr><th>..</th></tr><tr><td>..</td></tr></table>    only when simple
</body></html>
```

Elements: `html body h1 h2 h3 h4 p b i a ul ol li div table tr th td`.
Attributes: `id` on `h2` only, `class="facts"` on the infobox `div`, `href`
on `a`. An `href` is a display title (spaces, not underscores, no percent
encoding, no fragment); the app resolves it through `titles.idx`. Nothing
else: no `style`, no `span`, no images, no `dl` (the layout engine treats
`dl/dt/dd` as inline text). Character references are the five XML ones.
The infobox sits after the lead paragraphs and before the first `h2`, as
Wikipedia's own mobile view orders it. Tables appear only when they have at
most four columns and every cell is short plain text; anything else is
dropped and replaced by one line of italic text saying a table was omitted.

## titles.idx

```
magic         "WKTI"    4 bytes
version       u8        1
flags         u8        0
reserved      u16       0
entries       u32       total entries (articles + redirects)
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
both the Python module and `WikipediaFold.h`), and it is deliberately not
full Unicode normalisation, so both sides can be identical:

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
or two block reads.

### The sampler

```
count     u32       equals blocks
each:     u8 len, len bytes   the FOLDED title of the block's first entry
```

Loaded whole into PSRAM at open (about 1.4 MB for 19 million entries, under
20 KB for the essentials); a lookup binary-searches it and then reads exactly
one 4 KB block.

## What the device writes: state.json

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
