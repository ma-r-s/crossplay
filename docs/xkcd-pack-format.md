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

## The rules the builder applies

All measured over the whole archive — 3278 comics, dimensions read from the
download cache, not sampled.

**The archive is not one width, and that is the whole problem.** Widths run
from 106 to 960px, median 526, with a quarter of the archive piled at 740 and
**44% narrower than the 480 panel**. The distribution is continuous from 200 to
760 with no empty stretch anywhere, which is why none of the rules below is
allowed to change _how the reader works_: wherever such a threshold went, real
comics would sit either side of it arbitrarily close.

**1. Which way round (`--rotate-gain`, default 1.30).** Compare what each
posture buys and turn the comic when turning it helps:

```
upright  = min(maxUpscale, 480 / w)                fit the width, pan down
sideways = min(maxUpscale, 480 / h, 756 / w)       fit both, never pan
```

46% of the archive is stored sideways. The reader turns the _device_; the panel
itself never rotates, so the bar and the controls never move. A first version
called `setOrientation` per comic and turned the whole UI around underneath the
reader, which was rightly rejected: it is the comic that is sideways, not the
app. Doing it in the pack is also cheaper and _removes_ device code.

Worth knowing: below an aspect of 1.575 (the panel's own ratio) this gain
_equals_ the aspect ratio, and above it the gain is always exactly 1.575. So
this is the old `--rotate-aspect` rule restated in terms of what it buys. It
differs only where the upscale cap binds — a 200x100 comic is already at 2.4x
upright, and the gain form correctly declines to turn it.

`--rotate-cw` flips which way they turn. That is a rebuild, not a code change.

**2. The page rendition.** Fit the panel width, **up as well as down**. A
sideways comic is fitted _whole_, both axes, because the point of turning it is
to see all of it.

Then one correction: **if fitting the width overflows the screen by less than
`--fit-height-slack` (8%), fit the height instead.** A pan control that moves
the comic three pixels is the same defect as a column that reveals one, and the
page view had it — #3179 is _enlarged_ 1.51x to 480x757 and then pans by a
single pixel. 96 comics were in that state; this removes all of them for at
most 8% of scale and a thin margin down the sides.

**3. The closer rendition**, for comics whose lettering the page view cannot
render readable. **These open in the closer view**, and Confirm pulls back to
the whole comic: showing a comic too small to read and making you ask for the
readable one is the wrong way round.

How far to zoom comes from **that comic's own lettering**, measured on the host
by connected components (a letter is one blob; the median letter-shaped blob
height is cap height). This is the measurement the whole feature turns on:

```
cap height in source px:  min 3   p10 10   median 12   p90 13   max 25
```

**A single zoom multiplier cannot serve both ends of that.** The first version
fixed the closer view at two columns because two columns was a tidy invariant,
which zoomed #3266 to 1.23x and left its lettering 5px tall on the panel --
still unreadable, which is exactly what it was reported as. A comic is now
zoomed until its lettering would be `--target-cap` (12px) on the panel and no
further, so #3266 goes to 2.98x and a big-lettered strip barely moves.

A comic whose lettering is already `--min-cap` (10px) tall on the page view is
readable as it is and gets no closer view at all. Across the archive the page
view already delivers a median of 13.7px, which is why this affects the bottom
quartile rather than everything.

The width is then snapped to the column grid:

```
kCloserWidth    = 2 * 480 - 48   = 912    two columns overlapping by 48px
--max-closer-scale               = 1.25   past this it is magnification, not detail
kMinCloserWidth = 480 + 756/2    = 720    column two must be worth the tap

closerScale = min(--max-closer-scale, kCloserWidth / w)
exists      = pageScale < --closer-floor  and  closerW >= kMinCloserWidth
```

The guarantee is in the dimensions, not in a check. That distinction is the
whole lesson of the previous version: it decided at _runtime_ whether a second
column was worth showing, and the answer it gave for #1606 — 481px wide — was
a whole extra column revealing **one pixel**. Measured across the archive, the
second column now reveals at least **432px**, well over half a screen, in every
one of the 246 comics that qualify.

The 48px overlap is so a word split at the column seam is readable on both
sides.

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
