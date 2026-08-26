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

## The fui layer honors it too -- and the games deliberately opt out

`FreeInkUIGfxRenderer.h::deviceContext()` fills `DeviceContext.safeArea`
from `getOrientedViewableTRBL()`, and `freeink::ui::Screen` seeds its
content rect from `frame.safeRect()` -- so a fui screen lays out clear of
the glass by default.

**Toybox-chromed apps (the games, the shelf, player, study, hacker news)
opt back out** via `toybox::absoluteChrome(screen)`, called first in each
chrome helper: the header band is paint and may bleed under the bezel (its
title ink sits well below the covered rows), and every layout in those apps
is tuned against the band at panel row 0. Shifting their chrome down by the
insets ate the gaps those layouts were tuned for -- boards touched the
divider rule, folder icons drifted off their rows, Toy Battle's helper text
crowded the rule -- while buying nothing, because no game ever drew content
in the covered rows. This was found on the device, not in the sim: the
first flip shipped shifted game chrome and Mario spotted all of it in one
look. What keeps the safe area: xkcd (its comic and menus reach the panel
edge), the readers, and every screen laid out from
`UITheme::getScreenSafeArea`.

What made the flip safe for the screens that DO honor it, in the order the
traps were found:

1. **The ~23 absolute-margin `setContentMargin` sites** (reader menus,
   settings lists, wifi, sliders) build margins in the full screen frame
   from `getScreenSafeArea()`. They now call `setContentMarginAbsolute()`,
   which insets from `screen()` rather than `safeRect()` -- same geometry as
   before the flip, insets applied exactly once. Never hand absolute margins
   to plain `setContentMargin()`; that applies the safe area twice.
2. **The divider rule** under header bands is `toybox::headerRule(screen)`
   (ToyboxScreen.h), derived from `screen.body().y` right after
   `screen.header(...)`. The old idiom (a fill at absolute
   `kHeaderHeight + 4`) would sit inside the shifted band and vanish.
3. **Decorations riding the header band** (the shelf's folder mark, toy
   battle's medal tally, murdle's face doors, connections' and murdle's
   header-door hit rects) position from the band's real top
   (`body().y - kHeaderHeight` right after header(), or `safeRect().y`),
   never from y=0.
4. **Deliberate full-bleed stays full-bleed**: band fills and rules span the
   panel width (paint may run under the bezel; content may not), the XKCD
   reader bar and its map stay on the true bottom edge, Connections'
   tap-anywhere hit rect covers the whole panel (the digitizer works over
   the covered strip), and the BEZEL ruler app draws edge-to-edge because
   measuring the bezel is its job.
5. **Solitaire's absolute landscape layout is untouched**: rotated into
   landscape, the insets put 1px on the logical top/bottom and the 10px
   strip inside its side margins, all absorbed. Its chrome shifts by 1px.

Verified by rendering every app's entry screen plus the shelf, both folders
and PLAYER in the simulator (21 screens) -- the ui host-tests cannot catch
any of this: they construct `DeviceContext` with `safeArea = {}`, which also
means they pin the same geometry before and after the flip by construction.

## Measuring a unit

Apps > BEZEL. Look straight on. For each edge, the smallest number whose tick
you can see is the hidden pixel count. The screen also prints the values the
firmware is currently configured with (`SET T.. R.. B.. L..`).
