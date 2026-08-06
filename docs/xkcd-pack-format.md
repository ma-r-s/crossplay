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
| 20..31 | —      | reserved padding, zero   |

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

Images are stored at **native size and never scaled**. See below.

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

**The widest comic xkcd has ever published is 780px; p99 is 740.** The landscape
panel is 800. So at 1:1 in landscape **nothing is ever downscaled and nothing
ever needs to pan sideways.** 75% of the archive needs no panning at all, 21% is
two screens, 4% more.

That is why the reader is landscape and why there is one step function rather
than two. Portrait would mean fit-to-width, which shrinks 56% of the archive,
the worst to 0.62 — and rendering #1205 both ways through this fork's own
ditherer shows the 1:1 lettering clean and the 0.65 lettering broken. Hand
lettering is one or two pixels wide and there is no such thing as a
two-thirds-wide stroke on a 1-bit panel.

**A gap in the artwork is a threshold, not emptiness.** 20 of 30 sampled comics
are drawn inside a frame, so every interior row crosses two vertical strokes and
no row is ever empty. Asking for blank rows found one boundary in a thousand on
#1093 and the steps sliced straight through the table.

The budget is `max(width * 3%, 12)` ink pixels, and **the floor does more work
than the percentage**: structural ink is a count of vertical strokes and does
not scale with width, so a 340px framed table costs more per row than 3% of its
own width allows. Across every comic in a 160-comic pack tall enough to pan,
this lands 93% of steps on a gap.

**Storage.** ~28KB per comic at 1 bit, so roughly **93MB for the whole archive**.
Source PNGs would be ~180MB.

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
