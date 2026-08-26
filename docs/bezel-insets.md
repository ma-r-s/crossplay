# Bezel insets: what is covered, who honors it, and what is still to do

The X4 Pro's front glass overlaps the panel's active area. The panel is a true
800x480; the enclosure hides its edge pixels, worst at the top because the
panel sits shifted toward its bottom flex cable. Upstream mapped the same
problem on the X4 (discussion #618: 5-11 hidden top rows across eleven units,
the default 9 chosen "mostly by eye"), then carried the values to the X4 Pro
unmeasured.

Measured on the physical unit on 2026-08-26 with the BEZEL ruler app
(`src/apps_local/bezel/`, Apps > BEZEL, 1px ticks on all four edges; the
smallest number visible on an edge is that edge's hidden pixel count):

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
  BEZEL app works there too; measure and override its profile.
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

## Who does NOT honor it yet (the stage-2 flip)

`freeink::ui::DeviceContext.safeArea` is never populated by the shared
adapter (`FreeInkUIGfxRenderer.h::deviceContext()`), so every toybox/fui
screen -- all the games, the shelf, hacker news, xkcd's menus -- still lays
out from the full panel: `freeink::ui::Screen` seeds `content_` from
`frame.safeRect()`, which today equals `screen()`.

Flipping it is ONE line in the adapter, but it moves every app's chrome at
once. Known traps, found while staging this:

1. **The 24 `setContentMargin` sites double-inset.** They build ABSOLUTE
   margins from `getScreenSafeArea()` (which now includes the insets) and
   apply them via `setContentMargin`, which insets from `frame.safeRect()`.
   Once `safeArea` is populated, that is the insets twice. Those sites must
   switch to margins relative to the safe rect in the same change.
2. **The divider-rule idiom goes stale.** ~20 screens draw a rule at absolute
   `y = toybox::kHeaderHeight + 4` right after `screen.header(...)`; with the
   header shifted down the rule lands inside the black band and vanishes.
   Derive it from `screen.body().y` instead, in the same change.
3. **Raw `device.screen()` uses need a paint-vs-content call.** ~35 sites
   (list in `git grep -n "device().screen()\|device.screen()" src/apps_local`).
   Full-bleed PAINT (band fills, backgrounds, deliberate bleed under the
   bezel, upstream PR #2138 style) should stay `screen()`; CONTENT bands move
   to `safeRect()`.
4. **Tight screens lose 10 vertical px.** Fixed-height stacks that just fit
   (Connections board + three buttons) can clip at the bottom; each app needs
   a render after the flip. The ui host-tests will NOT catch any of this:
   they construct their own `DeviceContext` with `safeArea = {}` and pin
   exact pixel positions, so they stay green through the flip and through
   regressions alike.
5. **`XkcdActivity::readerDevice()` becomes redundant** (it fills the same
   values) -- harmless, delete it in the flip.

The flip is one worktree's worth of work with a render of every app's main
screen next to the current one, the art-pass way. Until then, game chrome
draws its top ~10 rows under the glass, which is what it has always done.

## Measuring a unit

Apps > BEZEL. Look straight on. For each edge, the smallest number whose tick
you can see is the hidden pixel count. The screen also prints the values the
firmware is currently configured with (`SET T.. R.. B.. L..`).
