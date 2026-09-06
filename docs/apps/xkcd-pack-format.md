# The xkcd pack

What `tools_local/xkcd/build_pack.py` writes to `/xkcd` on the card, what
`src/apps_local/xkcd/XkcdCore.h` reads back, and the measurements that decided
the shape of both.

The reasoning behind the two renditions is in
[xkcd-viewing-plan.md](xkcd-viewing-plan.md); this is the format.

## Why there is a pack at all

The archive is ~3300 comics. The device cannot build this itself: converting one
comic is a PNG decode plus a dither on a single core, and the whole archive
would be hours of it. So the bulk arrives as a pack you copy to the card, and
the device only ever converts the handful of comics published since.

Both paths produce **byte-identical output**, because both run the same code.
`tools_local/xkcd/convert.cpp` links `lib/GfxRenderer/BitmapHelpers.cpp` — the
firmware's own Atkinson ditherer — rather than reimplementing it. A Python port
would have been twenty lines and would have drifted the first time either side
was tuned, with the symptom being that comics from the card and comics from
wifi look subtly different on the same screen for no reason a user could name.

## Two renditions

Every comic has a **page** rendition. 15% also have a **closer** one, and
**those comics open in it**.

The page rendition is fitted so the comic's full width is on the panel, so the
page view has no horizontal axis at all and the only motion is down. 90% of the
archive is a single screen and never moves.

The closer rendition is for the comics that fit-to-width cannot render
legible — the big near-square ones like #3266, #256 and #1110, which rotation
cannot help. It has to be a _second stored image_: the panel is 1-bit, and
resampling art that is already 1-bit is mush, so the only place a second scale
can come from is the greyscale original, which lives on the host.

## Where the pack comes from

Nobody copies files to a card. The rolling `xkcd-pack` GitHub release (a
prerelease, so the OTA's `releases/latest` can never see it) hosts the three
files, and the app's first run offers to download them itself -- GET THE
COMICS, one confirm, a few minutes on WiFi. The files land as `.part` names
and are renamed only when all three are complete, so a torn download leaves
the card exactly as it was. After that, UPDATE fetches anything newer than
the pack on-device, so the hosted pack only needs replacing when the gap
grows large enough to be worth the refresh: build with
`tools_local/xkcd/build_pack.py`, upload with `gh release upload xkcd-pack
--clobber`, and never attach `read.bin` (it is a reader's personal state).

## Three files

```
/xkcd/index.dat    header + one fixed 40-byte record per comic, ascending by number
/xkcd/images.dat   the 1-bit bitmaps end to end; a comic's closer rendition
                   follows its own page rendition
/xkcd/text.dat     title then alt text, each NUL-terminated, per comic
```

One pack rather than 3300 files: a directory that size makes every open slow on
FAT, and the reader opens one on every list scroll.

### index.dat

Header, 16 bytes, little-endian throughout:

| Offset | Type   | Meaning                      |
| ------ | ------ | ---------------------------- |
| 0      | uint32 | magic, `"XKCD"` (0x44434B58) |
| 4      | uint16 | format version, currently 3  |
| 6      | uint16 | reserved, zero               |
| 8      | uint32 | record count                 |
| 12     | uint32 | highest comic number present |

Then `count` records of **40 bytes**:

| Offset | Type   | Meaning                                    |
| ------ | ------ | ------------------------------------------ |
| 0      | uint16 | comic number                               |
| 2      | uint16 | page width                                 |
| 4      | uint16 | page height                                |
| 6      | uint16 | page stride, bytes per row                 |
| 8      | uint16 | year                                       |
| 10     | uint8  | month                                      |
| 11     | uint8  | day                                        |
| 12     | uint32 | offset of the page image in `images.dat`   |
| 16     | uint32 | offset into `text.dat`                     |
| 20     | uint8  | flags; bit 0 = stored sideways             |
| 21     | uint8  | reserved, zero                             |
| 22     | uint16 | closer width, **zero when there is none**  |
| 24     | uint16 | closer height                              |
| 26     | uint16 | closer stride                              |
| 28     | uint32 | offset of the closer image in `images.dat` |
| 32..39 | —      | reserved padding, zero                     |

Records are **fixed width and sorted by comic number**, which is the whole
reason the strings live in a separate file: a lookup is then a binary search
over seeks, about twelve reads for the entire archive rather than a scan.

A closer width of zero is the sentinel for "no closer view", which works
because `valid()` already treats a zero width as "this slot is not filled in".

`Archive::open` rejects a pack whose header count exceeds what the file can
hold. That check matters more than it looks: a card pulled mid-write otherwise
reads as a complete archive whose last records are whatever bytes were there
before, which draws as corrupt artwork rather than as a missing comic.

Bump `kFormatVersion` for **any** layout change. A stale pack is rejected whole
rather than misread; the app then says the card has no archive and offers to
fetch one, which is what a v2 pack will do against this build.

### images.dat

Packed 1-bit rows, MSB first, **a set bit is ink**, each row padded to a whole
number of bytes. That is `toybox::blit1bpp`'s convention, so the reader blits
without translating anything. Note the inversion against BMP, where a set bit
is white; `convert.cpp` flips it deliberately at the one place it is produced.

Images are stored at the size the panel draws them at, so the device never
scales.

### text.dat

`title\0alt\0` per comic, at the record's `textOffset`. Both are folded to ASCII
by the builder, because the Toybox faces are subset to ASCII and a glyph the
font lacks draws as **nothing at all** — no box, no fallback, no log line.

That defect is not hypothetical and not confined to exotic characters: the
subset claims `U+0020-007E` but **`+` is missing from the small face**, which is
why the reader's bar says `OK` and not `OK+`. Same class as the curly
apostrophe that broke the Hacker News app.

There is no transcript. Roughly half the archive has one and they average 800
characters; storing them would be ~1.3MB for a full-text search nobody asked
for yet.

## The rule: never pan on two axes if one will do

Posture and zoom are chosen together to make that true, and it comes out true
for 97% of the archive.

`W` and `H` are the comic's width and height **at the scale its lettering
needs** — cap height, measured on the greyscale source by connected components,
settles that first. The device offers `SHORT = 480` and `LONG = 756`, and
turning it swaps which one runs across. Each of `W` and `H` therefore lands in
one of three bands, so there are **nine cases and no others**:

|                  | W &le; 480          | 480 &lt; W &le; 756  | W &gt; 756           |
| ---------------- | ------------------- | -------------------- | -------------------- |
| **H &le; 480**   | portrait, whole     | turned, whole        | **turned, across**   |
| **480 &lt; H &le; 756** | portrait, whole | portrait, across | portrait, across     |
| **H &gt; 756**   | portrait, down      | **turned, down**     | fewest taps, both    |

Read one out loud: bottom-left is a comic narrow enough for the short side but
too tall for the long one, so it goes portrait and pans down only. Top-right is
wider than anything, but its height fits the short side — so it turns, height on
the 480 side, length running along the 756 side, and pans across only.

Three rules produce that table:

1. **Fewest panning axes wins.** `portrait axes = (W > 480) + (H > 756)`,
   `turned axes = (W > 756) + (H > 480)`. Six of the nine cells end here.
2. **On a tie where the same axis pans either way, the axis that does *not* pan
   goes on the smallest side that contains it.** That leaves the longer side for
   the one that does, so it reads in fewer taps and wastes no space. Deciding
   this by counting taps instead ties far too often: 334 wide-and-short comics
   stayed portrait on a tap tie, which is #1518 zoomed 3x into five columns
   rather than turned and read in three.
3. **Anything still tied: fewest taps, then stay portrait.** Turning the device
   is a real cost and a tie means turning buys nothing.

**The scale may shrink by up to a sixth to buy an axis.** The target is 12px of
cap height on the panel and the floor is 10px; a comic that only just overflows
takes the smaller lettering and fits instead. That one allowance moves the
archive from 68% to **85% needing no panning at all**, and cuts two-axis comics
to 2.8%.

**Rotation** is what the table calls "turned": the comic is stored a quarter
turn, and the reader turns the *device*. The panel itself never rotates, so the
bar and the controls never move. `--rotate-cw` flips which way; that is a
rebuild, not a code change.

**A width that pans is snapped onto the column grid**, `COLUMN_STEP * N +
COLUMN_OVERLAP`, so every column reveals a full 432px and the last ends flush.

The snap has one invariant and it has been broken three times: **it never
increases the number of panning axes.** `N = 1` is a legal answer, and forcing
a minimum of two blew a comic that came out 481px wide up to 912, where it
started panning (2.8% -> 8.9%). Snapping also moves the *scale*, so it moves
the height, which pushed across-only comics past the viewport (8.9% -> 6.6%).
And rounding can put a fitting width one pixel over, after which the snap sent
pan-down-only comics to the next grid stop (6.6% -> 4.1%).

The first two were found by hand, weeks apart. The third was found by
`host-tests/xkcd/test_layout.py` the day it was written, which is the argument
for the suite existing: the rule now sits at the 2.8% the model predicted.

**Comics that pan also carry a whole-comic overview**, reached with OK, never
larger than one screen. Only 15% need one.

**A gap in the artwork is a threshold, not emptiness.** 20 of 30 sampled comics
are drawn inside a frame, so every interior row crosses two vertical strokes and
no row is ever empty. Asking for blank rows found one boundary in a thousand on
#1093 and the steps sliced straight through the table.

The budget is `max(width * 3%, 12)` ink pixels, and **the floor does more work
than the percentage**: structural ink is a count of vertical strokes and does
not scale with width.

**Storage.** 126MB before this change, **217MB** after: 3279 comics, of which
493 carry a second rendition. The closer renditions are the cost of being
readable at all; the card has 131GB free.

**Those numbers describe a LOCAL build, not the pack anyone has.** Measured
against the live `xkcd-pack` prerelease on 2026-09-05: the published assets are
`images.dat` 139,590,525 B, `index.dat` 131,176 B and `text.dat` 457,679 B,
**140,179,380 B** in total. The published index carries the same 3279 comics
(and its records imply an `images.dat` ending at exactly the published size, so
it is complete and self-consistent) but only **247** of them have a closer
rendition, against the 493 above. So the released pack was built under an
earlier rendition policy and nothing has republished it since 2026-08-25.

Nothing under `.github/workflows/` builds this pack, so that will stay true until
someone rebuilds and uploads by hand. Board #253 is the card for it; a size
quoted from this file is the local build's, and the number a device will actually
download is the one measured above. Say which you mean.

## Building one

```bash
tools_local/xkcd/build_pack.py --out fs_mario/xkcd
```

Resumable in both directions: the download cache and the pack are both rebuilt
from whatever is already there. `--limit`, or `--first`/`--last`, makes a small
pack for testing.

It ends by checking its own two guarantees against what it actually wrote — no
page rendition wider than the panel, no closer view whose second column reveals
less than half a screen — and says so rather than leaving them asserted in a
comment.

To look at what it produced, with the reader's own step positions drawn on:

```bash
tools_local/xkcd/inspect_pack.py --pack fs_mario/xkcd --num 1093 --out /tmp/x.png --show-gaps
```

Add `--closer` to render the second rendition instead. Red lines are where the
reader stops; what you are checking is that none of them cuts through a line of
lettering.


## How the reader walks a comic

Two rules, both of which came from Mario looking at the result rather than the
code.

**The panels are counted first, then the travel is divided evenly between
them.** A fixed half-screen stride leaves the last step as whatever is left
over, so at the end of a row of columns you would go all the way back to the
left and drop two pixels. `xkcd::rowsIn` works out how many panels the artwork
splits into, and `evenTargetY` spaces them equally, with panel 0 at the top and
the last exactly flush with the bottom. Gap snapping still nudges the interior
panels onto a gutter, but never the two ends and never past a fifth of a
screen, so the steps stay visibly equal.

A useful side effect: a position is now a *panel index* rather than a pixel
offset, and the offset is a pure function of it. Stepping forward and back is
therefore an exact inverse, where before each snap re-derived itself from
wherever the reader happened to be and the position drifted.

**Reading order is expressed in the comic's frame, not the stored image's.** A
sideways comic is stored a quarter turn clockwise, which relabels its axes: the
comic's left-to-right becomes the stored image's top-to-bottom, and its
top-to-bottom becomes the stored image's right-to-left. Walking the stored
image as though it were upright therefore starts in the wrong corner -- at what
the reader, holding the device turned, sees as the bottom of the strip. So a
sideways comic starts at the stored image's **right-hand** column and walks
leftwards. `xkcd::startOf` is the one place that knows this, so the starting
corner and the stepping cannot disagree.
