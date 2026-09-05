# Toybox: a design language for a screen that holds still

The visual language for this fork's apps. Not for the reader: that is upstream's
craft and upstream's frozen theme surface. This governs `src/apps_local/` only.
The vocabulary lives in [`src/apps_local/ui/Toybox.h`](../src/apps_local/ui/Toybox.h).

For what the fork _is_ and how it talks about itself -- the layer above this
one, and the one that governs anything the public sees -- read
[`identity.md`](identity.md).

## Where it comes from

Playdate. We want its personality. But we cannot want it naively, because the
interesting difference between the two devices is not the screen depth, which is
1-bit on both. It is **time**.

|                     | Playdate     | Xteink X4 Pro                 |
| ------------------- | ------------ | ----------------------------- |
| Pixels              | 400 x 240    | 800 x 480                     |
| Density             | 173 ppi      | 220 ppi                       |
| Physical            | ~2.7 in      | ~4.3 in                       |
| Frame               | 30 to 50 fps | ~0.3 s partial, 1 to 2 s full |
| Image without power | gone         | held indefinitely             |

Two things fall out of that table.

**We are not the constrained one.** We have four times the pixels at higher
density on a larger screen. Playdate's chunkiness is a low-resolution
constraint, not a style. Copying it literally would mean inheriting a limitation
we do not have. Panic's own legibility benchmark is _"hold up your Playdate
against a printed book,"_ and their screen is described as having the appearance
of newsprint. They are already aiming at print; they just cannot fully spend it
at 400 pixels wide.

**Their charm is mostly not in the graphics.** Reading the design commentary,
what makes Playdate feel alive is tone (failure states that are funny rather
than punishing), constraint turned into character (the crank as "a little arm
waving hello"), and pacing (instant boot, no notifications, ten-minute
sessions, an OS that "stays out of your way"). None of that depends on frame
rate. All of it transfers to us intact, and most of it is writing and restraint
rather than rendering.

The only thing that genuinely does not transfer is motion.

## What we spend instead of motion

**Persistence.** E-ink holds its image at zero power. A board will sit on a desk
showing the same position for hours. Playdate screens are transient; ours are
furniture. Every screen has to be worth looking at while nothing is happening,
which is why dead space at the bottom of a layout is a real defect here and
merely untidy elsewhere.

**The refresh flash as punctuation.** The full-refresh blink is a physical
event. Hidden, it is a defect; spent deliberately, it is a page turn. Partial
refresh for a move, full refresh for a new game or a checkmate. The flash
becomes a designed beat rather than an apology.

**Composition instead of animation.** With no per-frame budget we can afford
things Playdate cannot: careful dither, considered spacing, real artwork.

## The one rule

> **The black you can afford is inversely proportional to how often it changes.**

A solid black header that never repaints costs nothing: e-ink holds it for free
and it cannot ghost, because ghosting is a residue of _change_. The same area of
black in a play surface that repaints every move ghosts badly and slows the
refresh.

Every e-ink design guide says "borders over fills, avoid large filled surfaces."
They are right about content and wrong about chrome. This rule is what lets
Toybox be loud exactly where loudness is free.

| Element                     | Repaints           | Treatment                           |
| --------------------------- | ------------------ | ----------------------------------- |
| Header bar                  | never              | solid black, knocked-out white type |
| Board squares               | every move         | dither, never solid                 |
| Selection frame             | every move         | 4px outline                         |
| Legal-move dot              | every move         | solid, but tiny                     |
| Status capsule              | every move         | outlined                            |
| Status capsule at game over | once, full refresh | solid, as the payoff                |

## How this relates to FreeInkUI

Toybox is a design language. FreeInkUI is a component runtime. They are not
alternatives, and the fork uses both: the components lay out, hit-test and route
input; Toybox says what they look like, through the tokens and style sets in
[`ToyboxTheme.h`](../src/apps_local/ui/ToyboxTheme.h).

So the rule for anything new is: if it is chrome, it is a FreeInkUI component
wearing Toybox, and nothing below is expressed by hand. If it is the app's own
surface (a board, a puzzle grid, a page of text) it takes a slot rect and draws
itself, and every rule below applies directly. The rules do not change with the
drawing mechanism; only who executes them does.

## Concrete rules

**Strokes.** Panic's minimum is 2px so strokes do not look wispy. At 220 ppi the
equivalent is thicker, and heavier outlines are also what stay legible against a
dithered ground. `kHairline` 1, `kRule` 3, `kFrame` 4. Hairlines read as timid;
use them for grid cells and secondary outlines, never for anything you want
noticed.

**Type is a pixel font, and the bit depth is the reason.** `GfxRenderer.cpp:451`:

```cpp
if (renderMode == GfxRenderer::BW && bmpVal < 3) {
  renderer.drawPixel(screenX, screenY, pixelState);
}
```

The stock fonts are built with `--2bit`, meaning antialiased, and that line paints
a pixel for **any** coverage above zero. Not a 50% threshold: any. Every
antialiased edge floods to solid black, so stems fatten, counters close, and the
type turns to mush. This is not a property of the face; it is a property of the
path. Any vector font rendered through it gets bloated.

So Toybox ships its own: **Jersey 25** (SIL OFL, Google Fonts), subset to ASCII
and converted in **1-bit** mode, where there is no coverage to flood and every
glyph pixel is deliberate. It is also condensed, which matters on a 480px-wide
screen. One weight only: a synthesised bold at these sizes is the same flooding
by another route, so weight comes from size and from inversion instead.
Registered from `ToyboxFonts.cpp` rather than `main.cpp`, so it costs no upstream
surface. 13KB of flash for both cuts.

**A game may have its own voice, but not its own chrome.** Connections adds
**Instrument Serif** for its tiles and its menu body, and that is enough to tell
the two games apart from across a room. What it does _not_ touch: the header bar
and the buttons stay in Jersey in every app, because those are the device
speaking, not the game. The rule generalises: the app's own surface and its menu
prose can carry a typeface; anything the system draws around it cannot.

Three cuts of a second face cost 424KB of flash, which is affordable exactly
once or twice, not per game. Check the budget before adding a face rather than
after.

**A reading cut, for a surface that is a page rather than a board.** The UI cut
is 20px, which is right for a row you glance at and wrong for a screenful you
actually read: at 20px a 480px panel holds about 28 characters a line, so an
article becomes forty page taps and half the headlines on a Hacker News front
page cannot finish. `toybox_14` is the same face one step down, and it roughly
halves both. An app binds it by passing `toybox::readingFaces()` to
`makeTarget`; the header band keeps the display cut, because the top bar is the
fork's chrome and a shared one is what makes two apps feel like one device.
2.2KB of flash. Regenerate the cuts with `tools_local/toybox/gen_toybox_fonts.sh`,
which fetches Jersey 25 rather than relying on a TTF nobody still has.

**There are not three fonts. There are three slots.** `FONT_SLOTS = 3` is
`int fonts[3]` on the render target: a working set, not a limit.
`GfxRenderer` holds an uncapped `std::map<int, EpdFontFamily>`, and rebinding a
slot mid-render is one assignment. This is why the Connections board can draw
its chrome in one face and its tiles in another: two passes, one rebind between
them. Treat the number 3 as "how many faces on screen at the same instant",
which has never once been a real constraint.

**Nothing is ever elided. No exceptions.** Mario's rule, and it outranks every
preference below it: whatever a box holds, the whole of it reaches the screen.
An ellipsis shows the player a different word and gives them no way to know it,
so it is not a fallback, it is a defect. Breaking a word across lines is the
acceptable price -- "NUDIB / RANCH" is ugly and honest; "NUDIBRA..." is neither.

This reverses half of an older rule here, which said to shrink rather than ever
break a word. Shrinking is still preferred where it is free, but it is no longer
allowed to be the reason a word gets cut short.

**Shrink to fit, then break; and measure both.** `EpdFont` is a bitmap format,
one pre-rasterised set per size, so there is no scaling to be had at draw time.
"Fit this text" therefore means "pick the largest cut it lays out in", walking
the available cuts down, where laying out may take more than one line. A break
goes at a space when one is in reach and through the word when it is not, cut
into even shares rather than filled greedily -- "MISCREAN / T" orphans a letter
and reads as a bug where "MISC / REANT" reads as a word that did not fit.

The measuring is the part that is easy to skip and fatal to skip. A fit test
that asks a different question from the layout is not a fit test: Connections
asked "is this narrow enough" while its splitter cut by CHARACTER COUNT, and the
two disagreed on 48 boards of the published archive, every one of them a tile
showing a phrase the player could not know was shortened. Break the text with
the same function that decides the cut, and check the emitted line, not the
input string.

**A grid of equals shrinks together.** The rule above sizes one text against one
box, and applied per box it breaks a grid: sixteen Connections tiles that mean
"compare these against each other" were each sized against their own word, so a
long one came out a quarter smaller than the fifteen beside it and read as the
odd one out. On a set of interchangeable candidates, size is not available as a
property of a member. Measure the whole set, pick the largest cut the WIDEST of
them lays out in, and set them all in it (`connectionsui::chooseTileCut`). It
costs a long word two lines rather than costing fifteen neighbours a size step:
1139 of the archive's 1143 boards hold the large cut, against 439 when the test
was "fits on one line". This does not apply where the boxes are not peers -- a
header and its subtitle are meant to differ.

**Two strings sharing a box are sized together.** A solved Connections row
carries a category and the four words that were in it, and each was set at a
fixed cut with no fallback -- so a long word list drew its second line past the
black fill it sits on, in white, on white paper. Sixteen percent of the archive's
boards had at least one. Where two blocks share a height, choose their cuts as a
pair, largest first, and accept the first pair whose stacked height the box
actually holds (`connectionsui::drawSolvedRow`).

**A name inside a sentence is a short name.** When the device name grew from two
words to three, chess's status capsule went from "CALM FINCH'S MOVE" to "SHAGGY
SLEEPY GOATEE'S MOVE" -- and what the component dropped was "MOVE", the one word
that said what the capsule was for. Battleship had the same shape in "%s SANK
YOUR %s". Neither was a truncation bug: both labels were built by concatenating
a value whose length had quietly doubled.

Shrinking the type was the wrong fix and so was shortening the word lists. The
right one was `player::shortName()` -- call them by their first word. It always
fits, it can never regress, it is warmer than the full name, and the full name is
still on the seat card where you met them. **When a value grows and a layout
breaks, ask whether that layout wanted the whole value.**

**A label in a box shorter than its own line box is not centred at all.** This
is the same defect one level up, and it is the one that keeps coming back.
`GfxRendererTarget::text` places centre-aligned text at
`rect.y + max(0, (rect.height - lineHeight) / 2)`, so it centres the font's
**line box** (`advanceY`) rather than its ink -- and it clamps. Jersey carries
heavy leading, so the moment the box is shorter than the line box that `max()`
pins the offset at zero and the text lands `ascender - inkHeight` below the top
with its foot out of the bottom.

| cut         | line box | ink  | needs a box of |
| ----------- | -------- | ---- | -------------- |
| `toybox_10` | 21px     | 13px | 21px           |
| `toybox_14` | 29px     | 18px | 29px           |
| `toybox_20` | 42px     | 25px | 42px           |
| `toybox_30` | 63px     | 38px | **63px**       |

The display cut therefore needs a 63px-tall rect to centre itself, and almost
nothing inside a game board is 63px tall. The error grows as the box gets
_smaller_, which is exactly why it reads as an intermittent font problem instead
of a rule: a 50px Sudoku cell put the 20px cut at 12 above and 13 below (fine)
and the 30px cut at 13 above and 0 below, crossing the border.

`toybox::inkCentred(box, cut)` returns a rect one line box tall, positioned so
the clamp is a no-op and the ink lands centred in `box`. Use it for any
centre-aligned label in a box shorter than its own line height. It assumes the
glyph sits on the baseline with nothing below it -- true of every capital and
digit in these cuts, not of lowercase with descenders. The constants live in
`ToyboxTokens.h` because screens are freestanding and cannot see a font;
`ToyboxFonts.cpp` re-derives them from the real font data at startup and logs an
error if a regenerated cut ever moves them.

The same clamp applies to a **left- or right-aligned** run: alignment is
horizontal, the clamp is vertical, and a left-aligned score in a short box is
exactly as low as a centred one. Audit by box height, never by alignment.

The rest of the cuts this fork ships, for the apps that do not speak Jersey and
for the reading face a prose app binds to its body slot:

| cut                | line box | ink  | used by                         |
| ------------------ | -------- | ---- | ------------------------------- |
| `instrument_10`    | 27px     | 15px | Connections menu, small slot    |
| `instrument_13`    | 35px     | 20px | Connections, tile and menu body |
| `instrument_24`    | 65px     | 36px | Connections, title slot         |
| `reading_serif_14` | 40px     | 21px | Hacker News, Xkcd, body slot    |

Two places `inkCentred` cannot reach, both worth knowing before hunting one:

- **A slot is not a cut.** An app that rebinds its slots per view -- Connections,
  and Insider's rules pages -- sets the same `kTileFont` in two different faces,
  so the right `CutMetrics` depends on which screen is drawing. Read the app's
  `Faces` before picking one.
- **A component owns its own rect.** `screen.button()` and the list rows lay
  their label out themselves, so a pill in the display cut is 3px low and the
  call site has no rect to wrap. Fixing that means fixing FreeInkUI.

**Centre on cap height, never on `getTextHeight()`.** It returns the font's
**ascender**, and `drawText()` takes `y` as the top of the ascender box. So
`(box - getTextHeight()) / 2` centres the _box_, not the letters, and capital ink
sits at the bottom of that box because the baseline is at `y + ascender`. Every
bar and capsule ends up with its text visibly hanging low. `toybox::drawCapsCentered()`
measures the real ink from the `H` glyph (`top` and `height`) and solves for the
`y` that puts that band in the middle. Use it for all chrome text.

**Give the content a margin.** Chess originally sized the board as
`screenWidth - 2 * kFrame`, which ran it to within 4px of the bezel and made the
whole screen feel cramped. Content insets by `kMargin`, and only full-bleed
chrome (the header bar) touches an edge.

**Artwork is licensed, not invented.** The piece set is "celtic" by Maurizio
Monge (MIT), chosen by rendering six candidate sets at the real 48px under the
real threshold and looking at them. An earlier hand-drawn set was legible and not
good; a set several people have refined beats one drawn from polygon coordinates
in an afternoon. The lesson generalises: draw our own only where no good licensed
option exists.

The avatar parts in `assets_local/avatar/` are the exception that proves it: no
icon set ships a face that comes apart into layers, so these are ours. What made
them cheap anyway was refusing to write a pipeline for them -- they are SVGs on
one shared 120x120 viewBox fed to the SDK's own `gen_icons.py`, so the whole
canvas rasterises at the requested size and the layers stay in register with no
per-part offsets to maintain. Forty-three parts at two sizes is 88KB of flash.

**Hair is a mass, so draw it as one.** This started as a beard fix and turned
out to govern the whole set. Drawn as an outline, a beard runs alongside the
jaw, and two parallel strokes a few pixels apart on a 1-bit panel merge into one
thick line: at 240px on the device the beard was simply not there. Three
attempts at moving and widening the outline failed identically, which is the
tell -- a symptom that survives every variant is not a variant problem.

The same defect then explained every weak hairstyle. An outlined cap traces the
skull, so its lower edge is a second line parallel to the first, and it reads as
a **headband** rather than as hair. The only outlined styles that ever worked
(spikes, tufts, a mohawk) worked because they _broke_ the silhouette instead of
tracing it. Every hairstyle is now solid, which does both and is the only
treatment that survives the 48px row size.

Two constraints fell out of drawing fourteen of them:

- **No hairline may come below y=40 on the 120px canvas.** That is the top of
  the brow zone. One fringe hung to y=60 and simply erased whichever eyes it was
  paired with -- the face was gone and only the hair was left. A layered part
  cannot be judged alone; it has to be rendered against the extremes of every
  other layer.
- **Solid black must not touch solid black across parts.** A goatee that reached
  the neck lines merged into them and read as a bib from the mouth to the chest.
  Adjacent masses need a gap, or they become one mass.

**A light shape must be knocked out before it is stroked.** Drawing only the
outline of a white piece leaves it hollow, so the surface underneath shows
through: harmless on a white square, muddy on a dithered one. Fill the silhouette
with the page colour first, then stroke the outline over it. That is what a white
piece physically is, and it is why the pieces looked wrong in every variant. A
symptom that survives every theme is not a theme problem.

**Silhouettes, not drawings.** Piece artwork is reduced to its outer form: flood
fill the background inward from the border and keep everything the fill cannot
reach. Celtic's engraved interior lines read as noise at 48px on a 1-bit panel and
fight the toy look. What survives is the shape, which is all this size can carry.

**Anything that draws outside its own cell needs its own pass.** The selection
frame is drawn outside the square (an inset frame eats the piece and makes the
square look smaller than its neighbours), so it overlaps its neighbours and must
be painted after every cell. Drawn inside the per-cell loop it was overdrawn by
whichever adjacent cell happened to render later, leaving a broken rectangle.

**Layouts are anchored, not centred.** A block floating with equal slack above
and below reads as unresolved. Chess anchors the board under the header and the
status to the bottom margin, so the composition reads as a page with a footer and
the remaining slack becomes one deliberate zone, which the captured-material
strips grow into.

**Dither is the only gray.** The renderer offers exactly two: `LightGray` (25%,
every other pixel on both axes) and `DarkGray` (50% checkerboard). That is thin.
The source comment on the 50% pattern still reads `TODO: maybe find a better
pattern?`. Extending this set is the highest-leverage change available to the
look of every app, and it is cheap.

**Colour reads from the drawing.** Black pieces are solid silhouettes, white
pieces reduce to an outline, because their fill is white and only their stroke
survives the threshold. No background chips. Chips were
the first attempt and they cluttered the board.

**Mark the cell, not the thing standing in it.** A capture hint was a dot in the
middle of the target square, which is exactly where the piece is: it disappeared
into a black silhouette and sat on a white one like a blemish. On 1-bit there is
no colour to escape to and no outline that survives at that size, so the answer
is not a better dot, it is a mark somewhere the artwork never reaches.
`cornerMarks()` puts four brackets in the corners, which no piece occupies at any
size, and it rhymes with the frame around the board. Empty squares keep the dot,
because there is nothing there to hide.

## Ornament, and the only kind we allow

E-ink rewards looking at a screen while nothing is happening, so a game's front
door has room for something that is not a control. The temptation is to fill it
with a mascot or a pattern. Both are wrong here, for the same reason: **a screen
that holds still turns anything static into wallpaper by the third day.** A
cartoon that never moves stops being seen faster than a control does.

> **Ornament has to be made of the app's own material, and it has to carry the
> app's own data.**

Connections' menu has a 4x4 grid in the middle. It is the shape of a Connections
board, which is the material. Each cell is one of your last sixteen days: solid
for a clean solve, dithered for a solve with mistakes, crossed for a loss, empty
for a day you never opened, heavier border on the last cell because that one is
today. That is the data. It is different every morning, it rewards coming back,
and it is the only thing on the screen that is yours.

The first version of that grid was a fixed decorative pattern and it looked
fine. Mario's objection was exact: if we are going to put something in the
middle, it should represent something. The cost of making it real was a struct
and one pass over a file that was already being read.

Test for anything decorative: **would a screenshot of it be identical on
everyone's device?** If yes, it is wallpaper. Give it data or take it out.

The avatar is the case where that test is passed by construction. A device's
name is three words -- hair, eyes, mouth, fourteen options apiece and so 2744
faces -- so the name _is_ a drawing
instruction, and the face on the PLAYER screen and in both link seats is a pure
function of a string that already exists. There is no avatar stored anywhere and
none on the wire; the other device rebuilds your face from the name its radio
already carried. Nothing can arrive stale, and a build with different word lists
draws the plain head everyone starts from rather than the wrong person.

Worth generalising: **when ornament has to carry data, look first for data the
system is already moving.** The Connections grid needed a struct and a pass over
a file. This needed neither, because the question "what should the ornament
show" and the question "what does this device already tell other devices" turned
out to have the same answer.

## The front door

Every game gets a menu, and a menu of three equal rows tells you nothing about
which game you are in or what state you left it in. The Connections menu is the
pattern for the rest:

| Band     | What it holds                            | Weight        |
| -------- | ---------------------------------------- | ------------- |
| Headline | today's date, display font, left set     | the loudest   |
| State    | `NOT STARTED` / `IN PROGRESS` / `SOLVED` | body          |
| Rule     | `kRule`, full width                      | the divider   |
| Record   | played, perfect, streak, one line        | small         |
| Ornament | bracketed panel, app material, app data  | the still bit |
| Doors    | the lesser actions, bottom-anchored rows | quietest      |

The headline is also the hit target for the main action, so the most common tap
is on the largest thing on the screen and needs no button at all. The lesser
doors sit at the bottom because that is where a thumb rests and because it keeps
them from competing with the headline.

The corner brackets around the ornament are the same shape the chess board
wears. That is deliberate: two games that share a bracket read as one device.

## Controls, not decoration

**Size a control to its widest label, once.** A capsule sized to its own text
grows and shrinks as the text changes, and reads as the thing vanishing and
coming back rather than as the same object saying something new. `pill()` takes
a `minWidth` for this; Chess passes the width of the longest status it can ever
show.

**Three things cannot share one centre line.** The shelf's player bar carries a
face, a name and a chevron. Handing the name to the button as its label was the
obvious move and it was wrong: the component centres a label across the whole
rect, so at the widest name the lists can roll the text ran straight through the
face on one side and the chevron on the other. A label that shares a bar with
anything needs a band of its own, bounded by what it must not touch -- and the
host test has to compare the drawn rects, not the text, because a fake font
metric is narrower than the real one and collides with nothing.

**A button is a region, not a screen.** "Tap anywhere to restart" meant a stray
tap threw away a finished game you were still reading. If something is an action,
give it a shape and make only that shape respond.

**Weights must differ enough to read as different.** The board border is 9px
against the selection frame's 4px. At equal weight they compete and neither wins.

**Reserve space for what will arrive.** The captured-material strips are empty at
the start of a game and fill as it goes. The layout allocates their space from
the first frame, because a board that reflows halfway through a game is worse
than one with a little air in it.

**Pointing is fingers. Stepping is buttons. There is no cursor.**

Chess grew a board cursor before touch was settled, and Connections inherited a
menu cursor from the same habit. Both were dead weight: an on-screen cursor that
only physical buttons move is a second, worse input model running alongside the
real one, and it comes with its own bugs. A calendar cursor that persisted
across months produced a black square that meant nothing and could not be
explained. Removing it removed the bug class, not the bug.

The shelf kept one until 2026-08 and it was worse than dead: the side keys moved
a selection that only **Confirm** could open, and `frontButtonConfirm` on the X4
Pro resolves to `PIN_UNASSIGNED`. You could move a cursor you had no way to act
on.

This rule used to end "keep the physical buttons for Back and system functions",
and that is not possible on this device. **The X4 Pro has two buttons**: the
side page keys, wired to logical Up and Down. Back, Confirm, Left and Right are
all unassigned in the board profile and are never configured as inputs. Back is
a left-edge swipe, which `wasReleased(Button::Back)` already folds in.

(The Sticky adds a third key, a shared Confirm/Power click. It maps onto the
logical Confirm every screen already accepts and changes nothing here: design
for the X4 Pro's two keys plus touch, and the third key comes along free. See
[buttons.md](buttons.md).)

So the rule is:

- **Pointing** -- which cell, column, card, box -- is touch, always. The answer
  is a position and the finger names it exactly; a button can only express it by
  inventing a cursor.
- **Stepping** -- next page, previous page -- is the two side keys. These have
  no position, so drawing a control for them is drawing a place for something
  that does not live anywhere.
- **Never only a button.** Every page key action stays reachable by touch, or
  the invisible input model wins arguments it should not.

And no on-screen hint for them: they are moulded page-turn keys on a device
whose reader turns pages with them. The affordance is in the case. See
[buttons.md](buttons.md).

**A control that cannot act dims. It does not disappear.** A button that
vanishes takes its space with it, so the layout jumps and you lose your place;
worse, you cannot tell whether the action is unavailable or whether you
misremembered the screen. A dimmed control still says what it would do.
`disabledStepperStyles()` is the treatment.

The trap, and it will catch you: **the dither has to be in the fill, not the
text.** `GfxRendererTarget::text()` decides ink with `style.color != Color::White`,
so every non-white text colour draws solid black. There is no grey type on this
device. Dim a control by dithering its ground.

**A destructive setting changes in place; leaving is what applies it.** Tapping
OPPONENT used to throw away the game and jump straight back to the board, so you
arrived somewhere else without having seen anything change and had to guess
whether it had worked. Now the row updates with the menu still open, you can see
the new value, and the capsule at the bottom relabels itself to **START NEW
GAME**: the confirmation is a label you were going to read anyway, not a dialog.
Flip the setting back and the label goes with it. This costs no extra screen and
no extra tap.

## What is still open

- Only two dither levels. A proper pattern library is the next real win.
- Ghosting behaviour is reasoned about, not measured. Everything here is
  simulator-verified, and the simulator has no ghosting model at all, so the one
  claim this design leans on hardest is the one that has never been tested. It is
  the first thing to check when the device arrives.
- No motion vocabulary at all yet, not even the cheap kind (a single partial
  refresh used as a beat).
- Tone and copy are barely touched. On the evidence, that is where most of
  Playdate's personality actually lives, and it is the cheapest thing we have
  not spent.
- Chess still has a board cursor, written before touch-only was settled. It
  contradicts the rule above and should go the way Connections' did.
- The front door is a pattern rather than a one-off: battleship's was built to
  it without argument (record line, rule, ornament, bottom-anchored doors), and
  the ornament rule survived contact with a game whose material is a grid --
  the menu draws the shots you fired in the game you just played, so it is
  different every time and identical on nobody else's device. The 2026-08-10
  art pass settled the remaining question: it does NOT hold for a game with no
  record, and the honest fix is the record, not the pattern. Checkers, Connect
  Four and Yahtzee had byte-identical empty menus because they saved nothing;
  each got a one-line .sav (knucklebones' shape) and the pattern then carried
  all five games of that pass, Minesweeper included, without adjustment.
