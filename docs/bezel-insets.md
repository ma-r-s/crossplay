# Bezel insets: what is covered, who honors it, and what is still to do

The X4 Pro's front glass overlaps the panel's active area. The panel is a true
800x480; the enclosure hides its edge pixels, worst at the top because the
panel sits shifted toward its bottom flex cable. Upstream mapped the same
problem on the X4 (discussion #618: 5-11 hidden top rows across eleven units,
the default 9 chosen "mostly by eye"), then carried the values to the X4 Pro
unmeasured.

Measured on the physical unit on 2026-08-26 with the BEZEL ruler app, which
drew a numbered 1px tick ruler on each edge: looking straight on, the smallest
number still visible on an edge is that edge's hidden pixel count. The app was
a measuring instrument rather than something to ship, and it was removed from
the shelf once both edges of this table were filled in. See "Measuring another
unit" below for how to get it back.

| edge   | hidden px | firmware assumed |
| ------ | --------- | ---------------- |
| top    | 10        | 9                |
| right  | 1         | 7                |
| bottom | 0         | 3                |
| left   | 1         | 7                |

The old side value (7) was a scrollbar-aesthetics constant, not a measurement.
Per-unit variance is real; a unit that still clips gets re-measured with the
ruler, not padded blindly.

## The data

`BoardConfig::ViewableInsets`, per board profile, panel-native portrait frame
(`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`). Read through
exactly one accessor: `GfxRenderer::getOrientedViewableTRBL()`, which rotates
into the current orientation.

- X4 Pro: `{10, 1, 0, 1}` (measured, above).
- Sticky: **still the inherited default `{9, 3, 3, 3}` -- unmeasured.** The
  ruler works there too; restore it, measure, and override its profile.
- Simulator builds select the X4 profile (no `FREEINK_DEVICE_X4PRO` define in
  `platformio.sim.ini`), so sim renders use `{9, 3, 3, 3}`, not the X4 Pro
  values. Sim screenshots are therefore 1px off the device at the top and 2px
  at the sides; the mechanism is identical.

## Who honors it today

- The status bar (`BaseTheme.cpp`) and the readers (Epub/Xtc/Txt): direct
  `getOrientedViewableTRBL()` calls, always did.
- `UITheme::getScreenSafeArea()`: since app/bezel it starts from the viewable
  rect instead of `{0,0,w,h}`, so every activity that lays out from it
  (reader menus, settings lists, wifi, OPDS, dialogs) clears the glass. The
  ~24 `setContentMargin` call sites that convert this safe rect into absolute
  fui margins are correct BECAUSE fui's own `DeviceContext.safeArea` is still
  empty -- see the trap below.
- The XKCD reader: `XkcdActivity::readerDevice()` fills `safeArea` into its
  own device context; `xkcdui::readerViewport()` derives from `safeRect()`.
  Every reader-geometry site (drawing, panning, tap halves) goes through
  `readerDevice()` and nothing else, so they cannot disagree.
- The scroll indicators: `UIThemeTokens.h` derives `listScrollInset` from the
  side insets, so they moved from 7px to 1px off the edge on the X4 Pro.

## The fui layer honors it too -- and the games deliberately opt out

`FreeInkUIGfxRenderer.h::deviceContext()` fills `DeviceContext.safeArea`
from `getOrientedViewableTRBL()`, and `freeink::ui::Screen` seeds its
content rect from `frame.safeRect()` -- so a fui screen lays out clear of
the glass by default.

**Toybox-chromed apps (the games, the shelf, player, study, hacker news)
opt back out** inside `toybox::headerBand(screen, props)`, which calls
`absoluteChrome` itself so no screen can forget it: the band's TOP is the
panel's row 0 rather than the bezel's safe top, and the layouts below hang
off the chrome's bottom -- but the title (plus any right label, and the
decorations positioned with `toybox::bandCenterY` / `toybox::headerInkRect`)
centres between the bezel's safe top and the band's bottom edge, rather
than in rows nobody can see.
Mario's design, arrived at the hard way: the first flip moved the whole
chrome down by the insets, which ate the gaps the layouts were tuned for
(boards touched the divider rule, folder icons drifted off their rows, Toy
Battle's helper text crowded the rule) while protecting nothing, because no
game ever drew content in the covered rows. Cut the hidden millimetre from
the band and recentre; do not push the screen down. Found on the device,
not in the sim, both times. What keeps the full safe-area treatment: xkcd's
COMIC (`readerViewport()`, which reaches the panel edge), the readers, and
every screen laid out from `UITheme::getScreenSafeArea`.

**Not xkcd's menus, and not the Wallpapers grid, whatever this page said
until card 358.** Both apps took their sides and their bottom off the safe
rect, correctly, and then added `safe.y` to a body top derived from
`kHeaderHeight` as well -- so their content started ten pixels below every
other app's while protecting nothing, because the header band above it
already paints over the covered rows. Twenty apps agreeing was not the
evidence that settled it; `host-tests/ui`'s own
`testTheBandIsAbsoluteWithoutBeingAsked` was, by asserting
`screen.body().y == kHeaderHeight` on a BEZELLED frame. That makes
`kHeaderHeight`, `kChromeHeight` and now `toybox::kBodyTop` absolute panel
rows by construction, and any safe area added to one of them a double count.
`toybox::kBodyTop` (`ToyboxMetrics.h`) is the shared constant and carries the
whole argument. It is DERIVED, `bodyTopBelow() -> chromeBelow()`, not restated
beside the reservation: card 248 moved `headerBand()` from reserving the band
alone to reserving band + gap + rule, and a body top written as its own sum
would have kept the old row while every component-laid screen moved seven
pixels down, with nothing going red. `testTheHandRolledBodyTopMatchesTheReservedOne`
pins the two paths to each other; the shelf, hacker news, instapaper, xkcd and wallpapers all
name it now instead of each keeping a private copy.

The reason nothing caught it for as long as either app existed: `host-tests/ui`
builds every screen against `device()`, whose `safeArea` is empty, so twice
nothing is nothing. `testTheGlassNeverMovesABodyTop` renders each of those
screens on both frames and requires the same first body row, which is a rule a
screen written tomorrow cannot pass by accident.

**The band's PAINT, though, starts at the panel's physical top-left corner
and spans the full width -- ink centring is the only thing the insets move.**
That is the second half of the same rule and it was missing until
2026-09-03: `headerBand()` filled from the safe top, so the covered rows
stayed paper. A covered row is not an invisible row. It is invisible
HEAD-ON: the glass sits above the panel, so an eye below the device sees
past the bezel's edge and reads a white strip above every black header in
the fork. Mario reported it looking up at APPS and GAMES; it was on all 41
toybox band call sites across 27 files, and no gate could see it because the sim renders the same
strip and nobody had looked at the top ten rows.

Two consequences worth keeping straight when touching that helper:

- **Paint may bleed under the bezel, ink may not.** The band, the rule
  under it and Forehead's key bands are paint. The title is ink. A fix that
  fills to row 0 and then centres the title over the whole band buries the
  title's air in rows nobody can see; `host-tests/ui` pins the ink centring
  against exactly that.
- **`headerBand()` is now the ONLY way a toybox screen draws its band.**
  Eleven screens (the checkers, connect four, knucklebones, minesweeper and
  yahtzee boards and results, plus xkcd's `chrome()`) called
  `screen.header(props)` straight and were missed by the flip: their band
  came off the safe rect, inset on three sides, so it had a white strip
  above it AND a white column down each side, and its bottom edge sat 10px
  below their own menus'.

  **CLOSED by card 248.** `headerBand()` now calls `absoluteChrome()` itself,
  unconditionally, so no screen can lack it: the bottom-edge difference this
  paragraph described as unfixed is gone, and the eleven are absolute like
  everything else. It also reserves the rule it draws, so `screen.body().y`
  is the whole chrome rather than the band alone.

What made the flip safe for the screens that DO honor it, in the order the
traps were found:

1. **The ~23 absolute-margin sites** (reader menus, settings lists, wifi,
   sliders) build margins in the full screen frame from `getScreenSafeArea()`.
   Since the 2026-09-04 upstream sync they call upstream's
   `setContentMarginFromScreen()`. Never hand absolute margins to plain
   `setContentMargin()`; that applies the safe area twice.
   `host-tests/marginguard` reads the source for exactly that shape (a plain
   `setContentMargin(` whose arguments mention `safe.`, `getScreenSafeArea`
   or `safeRect`) and fails on it, because upstream's convention is the
   opposite and every sync imports screens written their way; the ui suite
   cannot see a doubled safe area of zero.

   `setContentMarginFromScreen()` does NOT apply it twice, which is why the
   fork could adopt it and delete its own `setContentMarginAbsolute()` from
   every screen upstream also has. The arithmetic, from `FreeInkApp.h`:

   ```
   marginBeyondSafeArea(m, s) = m > s ? m - s : 0
   setContentMarginFromScreen(m) -> screen inset by s + max(0, m - s) = max(m, s)
   setContentMarginAbsolute(m)   -> screen inset by m
   ```

   `max(m, s)` is idempotent: a margin that already folds the safe area in
   (every one of these sites does, they derive from `getScreenSafeArea()`)
   comes back unchanged. The two calls differ only where a margin is BELOW
   the safe area on some side.

   **That one difference is why `setContentMarginAbsolute()` still exists**,
   with exactly two callers left, both in `src/apps_local/`:

   - `toybox::absoluteChrome()` passes `Insets{}` -- all zeros, deliberately
     the full frame. `FromScreen({})` would give `max(0, safe)` = the safe
     rect, pushing every game's chrome down by the top inset. That is the
     regression described above, found on the device twice.
   - `XkcdScreens.cpp` passes a band that may sit on the true panel edge.
     On the X4 Pro `safe.bottom` is 0 so the two agree, but on a board using
     the inherited `{9, 3, 3, 3}` default `max(m, s)` would clamp a
     deliberate full-bleed bottom by 3px.

   Upstream has no way to express "content rect = the full frame": `Screen`
   seeds `content_` from `safeRect()`, and both upstream calls inset from
   `safeRect()`. So this is a capability the fork needs, not a style
   preference -- but it is now confined to fork-only screens, and no screen
   shared with upstream diverges.
2. **The divider rule** under header bands is drawn by `toybox::headerBand()`
   itself (ToyboxScreen.h), `kBandRuleGap` below the band it just painted.
   It used to be a separate opt-in `headerRule(screen)` call, and 12 of the
   fork's 41 band sites never made it -- the Yahtzee card among them, which is
   how Mario came to open a screen with no line under its header. `headerRule`
   survives as a no-op so a branch written before the change keeps compiling;
   the fork's own 25 calls are gone and `host-tests/chromeguard` refuses a new
   one. Solitaire's three hand-rolled fills at `kHeader + 4` are gone with
   them: they painted a SECOND rule on the same pixels as the first, which is
   invisible until the first one moves.

   The rule is deliberately NOT carved out of `kHeaderHeight`: shortening the
   band to make room was tried and fails on this very page's subject. The
   title cut's line box is about 64px, so a band shorter than that trips the
   vertical clamp in the text layout, the title stops being centred and pins
   to the top -- which behind the X4 Pro's glass, already eating the top ten
   rows, reads as a header sitting visibly low.

   So the band paints `kHeaderHeight` and RESERVES `kChromeHeight` (band + gap
   + rule): `headerBand()` takes the top with a `kBandRuleGap + kRule` trailing
   gap, and `screen.body().y` is therefore the first row a screen owns. Until
   card #248 it reserved the band alone, and the honest way of laying a screen
   out -- take the body rect and add a gutter -- still landed content five
   pixels under the rule. A dozen screens instead measured from the
   `kHeaderHeight` CONSTANT, which cannot see the rule at all; that is what
   Mario reported three times, and it is the reason two correct fixes to the
   header did not fix it. Where there is no `Screen` to ask (the geometry
   functions an Activity shares with its builder for hit-testing) the answer is
   `toybox::kChromeHeight`, or `toybox::chromeBelow(band)` for Solitaire's
   raised band.
3. **Decorations riding the header band** (the shelf's folder mark, toy
   battle's medal tally, murdle's face doors, connections' and murdle's
   header-door hit rects) ASK for the band rather than rebuilding it:
   `toybox::headerBandRect(screen)` for the whole painted band, and
   `toybox::headerInkRect(screen)` for the rows an eye can read, which is
   what a label drawn by hand wants. Four apps used to reconstruct it as
   `body().y - kHeaderHeight`, correct only while the chrome reserved the band
   and nothing else, and all four broke together the moment it stopped.
   Vertical centring on the band is `toybox::bandCenterY(screen, h)`, or
   `toybox::bandCenterY(renderer, h)` (ToyboxTheme.h) for an Activity drawing
   straight to the renderer -- `ChessActivity` open-coded that arithmetic and
   is the reason the renderer-side twin exists. Two more were missed and fixed
   on 2026-09-03 with the paint: trivia's hand-drawn right label boxed itself
   over the whole band, and chess's gear centred on
   `(kHeaderHeight - size) / 2`. Both centred partly in covered rows and rode
   about 5px above the title beside them.
4. **Deliberate full-bleed stays full-bleed**: band fills and rules span the
   panel width AND reach its top row (paint may run under the bezel; content
   may not -- and a band that stopped short of that row is the bug the
   section above describes), the XKCD
   reader bar and its map stay on the true bottom edge, Connections'
   tap-anywhere hit rect covers the whole panel (the digitizer works over
   the covered strip). The BEZEL ruler app was the other exception, drawing
   edge-to-edge because measuring the bezel was its whole job; it has since
   been removed.
5. **Solitaire's absolute landscape layout is untouched**: rotated into
   landscape, the insets put 1px on the logical top/bottom and the 10px
   strip inside its side margins, all absorbed. Its chrome shifts by 1px.

Verified by rendering every app's ENTRY screen plus the shelf, both folders
and PLAYER in the simulator (21 screens). Note what an entry screen is: a
menu, and every menu goes through `headerBand()`. That is why the eleven
board and result screens that did not were not among the 21 and kept a
three-sided inset band for a fortnight (card 248 has since made
`headerBand()` apply the absolute chrome itself, so no screen can miss it). A sweep scoped to "the screen each
app opens on" cannot see the screen you spend the whole game looking at.

The ui host-tests could not catch any of it either, and still cannot by
default: `device()` in `host-tests/ui/test_ui.cpp` constructs a
`DeviceContext` with `safeArea = {}`, which pins the same geometry with and
without the glass BY CONSTRUCTION. `bezelDevice()` beside it carries the
measured `{10, 1, 0, 1}`; a check about chrome and the bezel has to use it,
or it is asserting about a device that does not exist.

## Measuring another unit

The ruler is not on the shelf any more. It was a two-file app that existed to
fill in the table above, and a permanent row on APPS is a poor home for an
instrument used twice. It is not lost, though, and restoring it is a checkout
plus the one row it needs:

```bash
git log --all --diff-filter=D -1 --format=%H -- src/apps_local/bezel   # the commit that removed it
git checkout <that commit>^ -- src/apps_local/bezel
```

Then re-add its include and its `kApps` row in `src/apps_local/Shelf.cpp`, and
build. On the device: Apps > BEZEL, look straight on, and for each edge the
smallest number whose tick you can still see is that edge's hidden pixel count.
The screen also prints what the firmware is currently configured with
(`SET T.. R.. B.. L..`), so a measurement and the value it should replace are
on the panel together.

Take it back off the shelf when you are done. That is the whole reason this
section exists rather than the app.
