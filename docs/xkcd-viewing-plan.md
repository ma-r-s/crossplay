# Reworking how xkcd comics are shown

The reader's handling of large comics is broken. This is the diagnosis, the
measurements behind it, and what replaces it.

## What is actually wrong

Three defects, and the first is the one you feel.

### 1. A whole extra column for one pixel

`build_pack.py` ships with `--min-scale 1.0`. Read the rule literally: **any**
upright comic wider than the 480 panel fails `fit < min_scale` and is kept at
full size to be read in columns. Not comics that shrink badly. All of them.

Replayed over the archive's real dimensions:

|                          | comics  | share     |
| ------------------------ | ------- | --------- |
| stored sideways          | 1414    | 43.1%     |
| upright, one column      | 1304    | 39.8%     |
| **upright, two columns** | **560** | **17.1%** |

Of those 560, here is how much new artwork the second column reveals:

| new art in the last column | comics |
| -------------------------- | ------ |
| under 48px                 | 132    |
| 48-96px                    | 94     |
| 96-192px                   | 157    |
| 192-336px                  | 177    |

**#1606 is 481px wide and gets a second column for one pixel.** #2395 and
#2570, likewise one pixel. 226 comics (7% of the archive) pay a whole extra
column for under 96px. That is the "I barely move a little bit" symptom, and
it is not a tuning problem: the rule as written has no notion of whether the
extra column is worth anything.

### 2. Columns scramble reading order

`stepForward` walks to the bottom of a column and then returns to the _top_ of
the next one. For a comic laid out as panels that is: panel 1, panel 4, panel
7, then back up for panel 2, panel 5, panel 8. The comic is read in the wrong
order, and on e-ink there is no animation to show that the view moved sideways
rather than jumping somewhere arbitrary. That is the "randomly zooming in to
random parts" symptom. Every reader that solves this well
([Kindle Panel View](https://kdp.amazon.com/en_US/help/topic/G9GSTY4LTRT39D4Z),
ComiXology Guided View) moves in **reading order**, and says so on screen.

### 3. No threshold can be placed safely

The obvious fix is to move the threshold rather than remove it. The archive
says no. Source widths, 40px bins:

```
  200- 239   109 #######
  240- 279   235 ################
  280- 319   332 #######################
  ...        ... continuous, every bin populated ...
  680- 719   159 ###########
  720- 759   833 ############################################################
```

There is **no empty stretch between 200 and 760px**. Wherever a width-based
threshold goes, real comics sit either side of it, arbitrarily close, and the
one on the far side behaves completely differently. The cliff is not a bad
constant. It is inherent to deciding this automatically.

## The principle

> A rule that changes _how the reader works_ may not be decided by a
> measurement of the artwork. The default must be the same kind of thing for
> every comic; anything more is the reader's own choice.

This is Mario's second instinct ("show the entire thing and then a button to
appear to enable paneling") and it is the only option the width distribution
leaves open.

## What replaces it

### The page view: every comic, no exceptions

Every comic is stored fitted so **its full width is on the screen**. There is
never a horizontal axis. Motion is down, half a screen at a time, snapped to a
gap in the artwork exactly as it is today.

- **Upright**: fitted to 480 wide, pans down.
- **Sideways**: fitted _whole_ into 480x756, so it never pans at all.

Scale is a continuous function of source width, so #1606 at 481px and a comic
at 479px differ by 0.4% in scale and in nothing else. The cliff is gone
because the branch is gone.

What that costs, over the whole archive:

| taps to see the whole comic | comics | share |
| --------------------------- | ------ | ----- |
| 1 (no panning)              | 2950   | 90.0% |
| 2                           | 262    | 8.0%  |
| 3                           | 38     | 1.2%  |
| 4 or more                   | 28     | 0.8%  |

Page scale: median 1.18 (half the archive is _enlarged_), p10 0.89, worst 0.62.

### Rotation stays, but earns its place

Not an aspect-ratio threshold. Compare what each posture actually buys:

```
upright  = min(maxUpscale, 480 / w)                 fit the width, pan down
sideways = min(maxUpscale, 480 / h, 756 / w)        fit both, never pan
```

and turn the comic when `sideways >= upright * kRotateGain`. This is the right
shape of rule because it rotates exactly when rotating helps: a 694x272 strip
gains 1.57x and turns; a 740x699 near-square gains 1.06x and stays upright,
which is the "close to square but slightly horizontal should still be
vertical" case.

It is still a threshold, but a harmless one: at the boundary both postures give
nearly the same scale and **the interaction model is identical either side**.
Only the posture changes, never how the reader works.

Worth writing down, because it stops this being sold as a bigger change than it
is: below an aspect of 1.575 (the panel's own ratio) the gain _equals_ the
aspect ratio, and above it the gain is always exactly 1.575. So this is the
existing `--rotate-aspect` rule restated in terms of what it buys. It differs
only where the upscale cap binds -- a small 200x100 comic is already at 2.4x
upright and the gain form correctly declines to turn it, where an aspect rule
would have turned it for nothing.

`kRotateGain = 1.30`, chosen off the sweep below. The curve is flat: dropping
to 1.15 sends 53% of the archive sideways to rescue 45 more comics, and raising
to 1.5 costs the worst 5% of page scales (0.74 -> 0.65).

### The closer view, and which one a comic opens in

Rendered through the shipping path (LANCZOS then the firmware's own Atkinson),
#3266 "Holes" at its 0.649 page scale is structurally clear and **its lettering
is gone**; at native it reads. So for these a closer view is not a nicety, it is
the only way to read them at all.

Those comics get a **second rendition in the pack** at native size. It has to
be a second stored image: the panel is 1-bit, and enlarging 1-bit art on the
device is mush. This is what the format change buys.

With rotation carrying most of the load, **only 134 comics (4.1%) need one**,
for 20MB -- and they are precisely the class that prompted this: the big
near-square ones like #3266, #256 and #1110, which rotation cannot help and
fit-to-width cannot render legible.

The sweep behind the rotation constant:

| kRotateGain | sideways | need a closer view | worst 5% page scale |
| ----------- | -------- | ------------------ | ------------------- |
| 1.15        | 53%      | 216 (7%)           | 0.76                |
| **1.30**    | **46%**  | **261 (8%)**       | **0.74**            |
| 1.40        | 43%      | 307 (9%)           | 0.69                |
| 1.50        | 40%      | 343 (10%)          | 0.65                |

#### The closer view cannot slive either, by construction

The page view is safe because it has no horizontal axis at all. The closer view
does have one, so the guarantee has to be built into its dimensions rather than
checked after the fact -- the same lesson as `kSnapToleranceNum`, where the
constants are the mechanism and the runtime guard was dead code.

The closer view is **always exactly two columns**:

```
kCloserWidth    = 2 * 480 - 48   = 912   two columns overlapping by 48px
kMaxCloserScale = 1.25                   past this it is magnification, not detail
kMinCloserWidth = 480 + 756/2    = 720   column two must be worth the tap

closerScale = min(kMaxCloserScale, kCloserWidth / w)
exists      = closerW >= kMinCloserWidth  and  closerScale >= pageScale * kZoomGain
```

A comic that cannot satisfy all of that has no closer view, which is the honest
answer: there is nothing worth showing. Measured over the archive, **the second
column reveals at least 378px of new artwork -- exactly half a screen -- in
every one of the 134 comics that qualify.** A 481px comic is nowhere near
qualifying, and neither is a 520px one.

The 48px overlap is there so a word split at the column seam is readable on
both sides.

Traversal is **reading order**: left, right, then down. Vertical motion is the
existing gap-snapped half-screen step, so a step still lands under a gutter
rather than through a line of lettering. Costs:

| closer view costs | comics |
| ----------------- | ------ |
| 2 tiles           | 20     |
| 4 tiles           | 67     |
| 6 tiles           | 23     |
| 8 or more         | 24     |

Storage across the whole change: 126.2MB today, **134.9MB** after. The card has
131GB free.

### The guide

The permille rail goes. In its place, at the right end of the bar, a **map**:
the comic's own outline at its own aspect ratio, with the part you are looking
at drawn filled inside it. About 44x18px of an existing bar, no new furniture,
and it answers the question the rail could not -- not "how far through" but
"where am I, and how much is there". Same ornament rule as the menu mosaic:
made of the app's own material, carrying the app's own data.

The map is also the control. Tapping it enters or leaves the closer view, and
it only appears when there is something to see: a comic that fits entirely and
has no closer view shows no map, because there is nothing to say.

It degrades in the right direction. A 480x9707 comic cannot be drawn at its own
aspect in a bar, so the outline is clamped to a minimum width and what is left
is a vertical bar with a block in it -- which is exactly the affordance a comic
that long wants anyway.

### Controls, in full

|                      |                               |
| -------------------- | ----------------------------- |
| artwork, top half    | back one view                 |
| artwork, bottom half | forward one view              |
| Confirm button       | whole comic <-> closer view   |
| the bar              | the alt text                  |
| side buttons         | previous / next comic         |

One gesture moves you through a comic and it means the same thing in both
views; only the definition of "a view" changes.

## Format

`kFormatVersion` 2 -> 3, record 32 -> 40 bytes, and the whole pack is rebuilt.

| off    | type | field                                            |
| ------ | ---- | ------------------------------------------------ |
| 0      | u16  | comic number                                     |
| 2      | u16  | page width                                       |
| 4      | u16  | page height                                      |
| 6      | u16  | page stride                                      |
| 8      | u16  | year                                             |
| 10     | u8   | month                                            |
| 11     | u8   | day                                              |
| 12     | u32  | page image offset                                |
| 16     | u32  | text offset                                      |
| 20     | u8   | flags; bit 0 = stored sideways                   |
| 21     | u8   | reserved                                         |
| 22     | u16  | closer width, **0 when there is no closer view** |
| 24     | u16  | closer height                                    |
| 26     | u16  | closer stride                                    |
| 28     | u32  | closer image offset                              |
| 32..39 |      | reserved                                         |

Storage goes from 126MB to about 151MB. The card has 131GB free.

## What this does not fix

The wifi update path converts through `PngToBmpConverter`, which scales to fit
a target but **cannot rotate**. A comic fetched over wifi that should be
sideways will arrive upright. The page and closer renditions can both be
produced (two passes at different target widths); the rotation cannot, without
a 1-bit transpose the firmware does not have today. That path has never met a
real network either, so it is noted rather than claimed.


## Amendment, same day: which view a comic opens in

Shipped with the whole-comic view as the default and the closer view opt-in.
Mario's reaction to the first screenshot was immediate and correct: "it's still
really zoomed out, not good enough to read."

He was right, and the mistake is worth keeping. The argument above -- that no
automatic rule may decide how the reader works -- is sound, but it was applied
one step too far. It says the *interaction model* must not be chosen by
measuring the artwork. It does not say the *starting position* must not be:

- both views use exactly the same controls, so nothing has to be relearned;
- one button press undoes the choice, immediately and visibly.

A cliff you can step back from in one press is not the cliff that made the
first version unusable. So a comic whose page view is shrunk past
`--closer-floor` (0.80) now **opens in its closer view**, and Confirm pulls
back to the whole comic. 246 comics (8%) do that. Showing someone a comic too
small to read and asking them to request the readable one is the wrong way
round, whatever the principle behind it.
