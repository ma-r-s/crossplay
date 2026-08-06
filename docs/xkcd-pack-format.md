# The xkcd pack

What `tools_local/xkcd/build_pack.py` writes to `/xkcd` on the card, what
`src/apps_local/xkcd/XkcdCore.h` reads back, and the measurements that decided
the shape of both.

## Why there is a pack at all

The archive is ~3300 comics. The device cannot build this itself: converting one
comic is a PNG decode plus a dither on a 160MHz single core, and the whole
archive would be hours of it. So the bulk arrives as a pack you copy to the
card, and the device only ever converts the handful of comics published since.

Both paths produce **byte-identical output**, because both run the same code.
`tools_local/xkcd/convert.cpp` links `lib/GfxRenderer/BitmapHelpers.cpp` — the
firmware's own Atkinson ditherer — rather than reimplementing it. A Python port
would have been twenty lines and would have drifted the first time either side
was tuned, with the symptom being that comics from the card and comics from
wifi look subtly different on the same screen for no reason a user could name.

## Three files

```
/xkcd/index.dat    header + one fixed 32-byte record per comic, ascending by number
/xkcd/images.dat   the 1-bit bitmaps end to end
/xkcd/text.dat     title then alt text, each NUL-terminated, per comic
```

One pack rather than 3300 files: a directory that size makes every open slow on
FAT, and the reader opens one on every list scroll.

### index.dat

Header, 16 bytes, little-endian throughout:

| Offset | Type   | Meaning                      |
| ------ | ------ | ---------------------------- |
| 0      | uint32 | magic, `"XKCD"` (0x44434B58) |
| 4      | uint16 | format version, currently 1  |
| 6      | uint16 | reserved, zero               |
| 8      | uint32 | record count                 |
| 12     | uint32 | highest comic number present |

Then `count` records of **32 bytes**:

| Offset | Type   | Meaning                  |
| ------ | ------ | ------------------------ |
| 0      | uint16 | comic number             |
| 2      | uint16 | width in pixels          |
| 4      | uint16 | height in pixels         |
| 6      | uint16 | stride, bytes per row    |
| 8      | uint16 | year                     |
| 10     | uint8  | month                    |
| 11     | uint8  | day                      |
| 12     | uint32 | offset into `images.dat` |
| 16     | uint32 | offset into `text.dat`   |
| 20     | uint8  | flags; bit 0 = draw landscape |
| 21..31 | —      | reserved padding, zero   |

Records are **fixed width and sorted by comic number**, which is the whole
reason the strings live in a separate file: a lookup is then a binary search
over seeks, about twelve reads for the entire archive rather than a scan. The
twelve reserved bytes exist so a future field can be added without moving any
of the ones above it.

`Archive::open` rejects a pack whose header count exceeds what the file can
hold. That check matters more than it looks: a card pulled mid-write otherwise
reads as a complete archive whose last records are whatever bytes were there
before, which draws as corrupt artwork rather than as a missing comic.

Bump `kFormatVersion` for **any** layout change. A stale pack is rejected whole
rather than misread, for the same reason the EPUB caches carry versions.

### images.dat

Packed 1-bit rows, MSB first, **a set bit is ink**, each row padded to a whole
number of bytes. That is `toybox::blit1bpp`'s convention, so the reader blits
without translating anything. Note the inversion against BMP, where a set bit
is white; `convert.cpp` flips it deliberately at the one place it is produced.

Images are stored at the width the panel draws them at, so the device
never scales. See the measurements below.

### text.dat

`title\0alt\0` per comic, at the record's `textOffset`. Both are folded to ASCII
by the builder, because the Toybox faces are subset to ASCII and a glyph the
font lacks draws as **nothing at all** — no box, no fallback, no log line. This
is the same defect the Hacker News app hit with a real comment that opened on a
curly apostrophe.

There is no transcript. Roughly half the archive has one and they average 800
characters; storing them would be ~1.3MB for a full-text search nobody asked
for yet.

## What is deliberately not stored

**Panel boundaries.** An earlier version precomputed the rows a step should
land on and stored them in a fourth file. That needed a per-comic cap, a
truncation report, and a stored table the device's wifi conversion had to
reproduce identically forever — and a 15,000-row comic would have needed ~500
entries and silently truncated.

Since a step only ever consults a ±96px window around one target, the reader
reads those ~200 rows off the card when it steps: about 18KB, against a 300ms
screen refresh it is already paying. No file, no cap, no second implementation,
nothing that can go stale. `xkcd::gapWindowFor` names the rows and
`xkcd::scrollDown` consumes exactly them.

## The measurements

All from a sample of 1048 comics spread evenly across the archive.

**The archive is not one width, and that is the whole problem.** The widest
comic is 960px, but **44% of the archive is narrower than the 480 portrait
panel**, median 322px. xkcd letters at a roughly constant size in source
pixels, so how far a comic has been *shrunk* is the only thing deciding whether
it can be read. Three rules follow, applied in order by the builder:

**1. Orientation (`--rotate-aspect`, default 1.4).** Turn the panel only for
comics that are clearly wide. Portrait is the device's pose and rotating has to
be earned: a near-square comic gains almost nothing from landscape and still
has to be panned, so it stays upright. This puts 57% portrait, 43% landscape.

An earlier version chose orientation by "does it fit at 0.85", which sent a
1.01-aspect comic to landscape where it *still* needed panning -- the reader
turned the device and gained nothing. Aspect is the right test.

**2. Scale.** Fit the chosen panel's width, **up as well as down**. Enlarging
the greyscale source before the single dither is not the same as enlarging
1-bit art: at the median 1.5x the lettering comes out bigger and just as crisp.

**3. Zoom rather than shrink, within a budget (`--min-scale`, `--pane-budget`).**
If fitting would shrink past full size, keep the comic large and let it be read
in **columns** instead. This is the answer for comics that are big in both axes:
#3266 is 740x731 of fine detail, and fitting it to 480 is the shrink that makes
it unreadable, so it stays full size and reads as two columns.

But only while that stays under the pane budget. #1732 is 740x14957, and
zooming it would cost forty tiles, which is not reading -- so it takes the
shrink. 17% of the archive ends up with more than one column.

Whatever those rules decide, the stored image is finally clamped to the format
ceiling. #2067 is 960px wide and full size put it past 800, so it was rejected
outright and went **missing from the archive** with only a line in the build
log. A comic silently absent is worse than one shown slightly small.

**A gap in the artwork is a threshold, not emptiness.** 20 of 30 sampled comics
are drawn inside a frame, so every interior row crosses two vertical strokes and
no row is ever empty. Asking for blank rows found one boundary in a thousand on
#1093 and the steps sliced straight through the table.

The budget is `max(width * 3%, 12)` ink pixels, and **the floor does more work
than the percentage**: structural ink is a count of vertical strokes and does
not scale with width, so a 340px framed table costs more per row than 3% of its
own width allows. Across every comic in a 160-comic pack tall enough to pan,
this lands 93% of steps on a gap.

**Storage.** **132MB** for the whole archive. Keeping comics large is the
point of the rules above, and it is what the extra 44MB over a fit-to-480 pack
buys. Source PNGs would be ~180MB.

## Building one

```bash
tools_local/xkcd/build_pack.py --out fs_mario/xkcd
```

Resumable in both directions: the download cache and the pack are both rebuilt
from whatever is already there. `--limit` makes a small pack for testing.

To look at what it produced, with the reader's own step positions drawn on:

```bash
tools_local/xkcd/inspect_pack.py --pack fs_mario/xkcd --num 1093 --out /tmp/x.png --show-gaps
```

Red lines are where the reader stops. What you are checking is that none of
them cuts through a line of lettering.
