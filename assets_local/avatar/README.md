# Avatar parts

The layers a face is drawn from. Ours, not vendored: no icon set ships a face
that comes apart into hair, eyes and mouth, and this fork's rule is to draw its
own only where no good licensed option exists (see
[design-language.md](../../docs/design-language.md)).

Generated into `src/apps_local/player/PlayerAvatarArt.h` by
[`tools_local/gen_avatar.sh`](../../tools_local/gen_avatar.sh), which is the
SDK's own `gen_icons.py` pointed at this directory instead of Lucide's.

## The rules these files follow

**One shared `viewBox="0 0 120 120"`, and each part drawn where it belongs on
it.** This is the whole trick. The generator rasterises the entire canvas at the
requested size, so four parts stacked on one rect stay in register with no
offsets to store and nothing to keep in step. Do not crop a part to its ink.

**Head geometry is fixed**, and every feature is positioned against it:

| Landmark  | Where                                 |
| --------- | ------------------------------------- |
| Head      | ellipse `cx=60 cy=46 rx=34 ry=37`     |
| Crown     | `y = 9`                               |
| Brow line | `y ≈ 32`                              |
| Eye line  | `y ≈ 46`, centres at `x = 45 / 75`    |
| Mouth     | `y ≈ 66`                              |
| Chin      | `y = 83`                              |
| Neck      | `x = 49..71`, `y = 80..93`            |
| Shoulders | apex `y = 93`, running off both edges |

**`stroke-width="5"`, round caps and joins.** Thinner breaks up at 48px, where
the rasteriser thresholds at 110 luminance.

**All hair is filled, not stroked -- scalp hair and facial hair alike.** An
outline runs parallel to the thing it sits on, and two strokes a few pixels
apart merge into one line on a 1-bit panel: outlined beards vanished into the
jaw, and outlined caps read as headbands rather than hair. The only outlined
styles that ever worked (SPIKY, PUNK, TUFTY) work because they break the
silhouette instead of tracing it.

**No hairline below y=40.** That is the top of the brow zone. A fringe that hung
to y=60 erased whichever eyes it was paired with. Always render a new part
against the extremes of the other two layers, never on its own.

**Solid black must not touch solid black across parts.** A goatee reaching the
neck lines merged with them and read as a bib. Adjacent masses need a gap.

**`hair_BALD.svg` is deliberately empty.** It is also what an unrecognised word
draws, which is the right coincidence: a name from a build with different word
lists comes out as a plain head.

## Adding a part

The alias in [`tools_local/avatar.txt`](../../tools_local/avatar.txt) is
SHOUTED, because `PlayerAvatar.cpp` spells the word and both bitmap names from
that one token. Add the SVG, add the manifest line, add the word to the matching
list in `PlayerName.cpp`, run the generator, and run `./scripts/check.sh` -- a
static_assert catches a word without a drawing, and `host-tests/ui` catches the
two lists falling out of order.
