# Suit artwork

The four suits are the Unicode card-suit glyphs (U+2660, U+2665, U+2666,
U+2663) from **Noto Sans Symbols 2**, Copyright 2022 The Noto Project Authors,
SIL Open Font License 1.1 (see `OFL.txt`). The glyph outlines were extracted
with fontTools and written out as SVG; no font binary ships on the device.

`*-solid.svg` is the glyph as drawn, used for the black suits. `*-outline.svg`
is the same path unfilled and stroked, which is the hollow form the red suits
use; deriving both from one path means they cannot drift apart.

## Why this set

Three candidates were rasterised at the exact sizes and threshold the panel
uses (18px corner index, 46px card centre) and compared side by side:

* **Lucide**, which the SDK already vendors. A UI icon set with one uniform
  rounded style, so at 18px its club and its spade are the same silhouette: a
  rounded mass on a short stem. A corner index has to carry that difference.
* **The traditional Wikimedia set** (F l a n k e r, public domain). Correct
  shapes, but its club has large lobes on a short stem and they begin to merge
  at index size.
* **Noto Sans Symbols 2**, chosen. The spade and club sit on a distinct flat
  pedestal, which gives them a shared deliberate base and is the feature that
  survives at 18px; the club's lobes are smaller with more air between them.

Regenerate with `tools_local/solitaire/gen_suit_icons.sh`, which runs the SDK's own
`freeink-sdk/libs/assets/Icons/tools/gen_icons.py` over these files. Extracting
the glyphs again needs fontTools and the font, neither of which is in the repo;
the SVGs here are the checked-in source of truth.
