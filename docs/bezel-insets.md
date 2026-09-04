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
opt back out** via `toybox::absoluteChrome(screen)` followed by
`toybox::headerBand(screen, props)`: every layout in those apps is tuned
against the band's BOTTOM edge at `kHeaderHeight`, so that edge stays put
and nothing below moves -- but the band's visible TOP is the bezel's safe
top, and the title (plus any right label, and the decorations positioned
with `toybox::bandCenterY`) centres in the visible part rather than in
rows nobody can see. Mario's design, arrived at the hard way: the first
flip moved the whole chrome down by the insets, which ate the gaps the
layouts were tuned for (boards touched the divider rule, folder icons
drifted off their rows, Toy Battle's helper text crowded the rule) while
protecting nothing, because no game ever drew content in the covered rows.
Cut the hidden millimetre from the band and recentre; do not push the
screen down. Found on the device, not in the sim, both times. What keeps
the full safe-area treatment: xkcd (its comic and menus reach the panel
edge), the readers, and every screen laid out from
`UITheme::getScreenSafeArea`.

What made the flip safe for the screens that DO honor it, in the order the
traps were found:

1. **The ~23 absolute-margin sites** (reader menus, settings lists, wifi,
   sliders) build margins in the full screen frame from `getScreenSafeArea()`.
   Since the 2026-09-04 upstream sync they call upstream's
   `setContentMarginFromScreen()`. Never hand absolute margins to plain
   `setContentMargin()`; that applies the safe area twice.

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
   the covered strip). The BEZEL ruler app was the other exception, drawing
   edge-to-edge because measuring the bezel was its whole job; it has since
   been removed.
5. **Solitaire's absolute landscape layout is untouched**: rotated into
   landscape, the insets put 1px on the logical top/bottom and the 10px
   strip inside its side margins, all absorbed. Its chrome shifts by 1px.

Verified by rendering every app's entry screen plus the shelf, both folders
and PLAYER in the simulator (21 screens) -- the ui host-tests cannot catch
any of this: they construct `DeviceContext` with `safeArea = {}`, which also
means they pin the same geometry before and after the flip by construction.

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
