# Open items

Things known to be unfinished. Written 2026-08-07, the day v1.0.0 was published;
the first three items revised 2026-08-14 after the first tester session on real
hardware.

This is not a roadmap and not a wishlist. Everything here is either a promise
the project has not kept yet or a known way it can mislead someone. Anything
that turns out to be neither should be deleted from this file rather than left
to age.

Ordered by what would embarrass the project soonest, not by effort.

## Someone has now run this on a physical device, once

Updated 2026-08-14. A tester flashed v1.2.1 to an X4 Pro and played most of the
shelf. That closes the "nobody has ever booted it" version of this item; what
follows replaces it.

**What one session on real hardware found**, none of which any test caught:

- Murdle threw away most of its taps and its Back swipe, because it pumped the
  input manager a second time each frame and wiped the edges main.cpp had just
  latched. It was effectively unplayable.
- The Connections archive was dead from the 20th of any month onward: one
  interaction slot per date overflowed the 24-slot buffer.
- No covered card in Solitaire had ever shown its suit, in a game whose only
  rule is that runs alternate colour.

All three are fixed. The lesson is the ratio: three real defects in one sitting,
against a suite that runs 22,000+ assertions and was green throughout. **One
tester-hour is worth more than any number of host assertions for anything the
eye or the finger judges.**

Still true, and still the biggest gap: **Mario does not own the device**, so
every session after this one is back to the simulator, which fakes buttons the
hardware does not have and refreshes instantly where the panel takes hundreds of
milliseconds.

**Done looks like:** a second tester pass on a build that contains these fixes,
and a green report in the README naming the version that was actually flashed.

## Nothing knows what a refresh costs, so nothing can be tuned for feel

The tester's first sentence about Murdle was two complaints, not one: taps did
nothing (fixed), **and** it was hard to tell when a tap was acknowledged (not
fixed, and not fixable from here).

On this device a tap produces no feedback whatsoever until the result is drawn.
No app on the shelf arms `InteractionBuffer::setFlash`, the SDK's own
tap-acknowledgment, and nothing exposes a way to repaint the touched element
before the action lands.

The obvious fix is worse than the disease: acknowledging a tap with a full-screen
repaint costs a whole refresh, so the user waits once to be told they are
waiting, then again for the answer. Every real e-reader avoids this by repainting
a _region_ rather than the panel -- partial updates run roughly 200-600ms against
1500-3000ms for a full one, and the fast black-and-white modes (A2/DU) are
cheaper still and exactly good enough for a pressed button.

We have the primitive and cannot reach it: `FreeInkDisplay::displayWindow(x, y,
w, h)` exists, is marked EXPERIMENTAL, and `HalDisplay` exposes only three
whole-screen modes. There is even a `syncWriteBufferFromActive()` whose comment
describes "patching a few regions and re-displaying instead of fully
re-rendering".

**The blocker is a measurement, not a design.** Nobody knows what a FAST_REFRESH
costs on an X4 Pro; the SDK's own comment says refreshes are "~0.3-2 s" and that
is the whole of our knowledge. Every argument about whether tap feedback is worth
it rests on a number nobody has taken. Same shape as the Murdle generator, where
the fix was making the device report its own elapsed milliseconds instead of
estimating them.

**Done looks like:** two numbers off real hardware -- a full FAST_REFRESH, and a
`displayWindow` of a button-sized rect -- and then a decision. If a region update
is fast, wire it through the HAL and acknowledge taps the way every e-reader
does. If `displayWindow` is experimental because it does not work, say so here
and instead reduce how many taps a game needs.

## Flash is at 96.6% and every new screen costs some

6,331,054 bytes of 6,553,600 after Sudoku, which cost 25KB for a whole game
(a technique ladder is cheap: it is code and almost no data). About 222KB left.

Nothing is broken and no single change caused it, which is exactly why it is
worth writing down: the failure mode is a build that does not fit, arriving
without warning, on whichever app happens to be next.

**Done looks like:** a measurement of what actually occupies the binary (fonts
are the first suspect -- 80+ global `EpdFont` objects, though those are flash
constants by design), and either headroom recovered or a written ceiling on how
many more apps fit.

## The 1.6.0rc merge moved the reader's controls under a real user's hands

The foreseen sit-down merge (upstream deleted our base branch, reimplemented
X4 Pro support, released it as `1.6.0rc`) happened 2026-08-25; the story
lives in LOCAL_SCOPE.md's branch section now. What remains open is the human
side: upstream's control scheme won, so on the X4 Pro the reader menu is now
a middle tap (was: bottom-edge up-swipe), the top edge belongs to the
frontlight panel, and power-button behavior gained upstream's double-click
window. Our one tester learned the old controls. The release notes say so,
but nobody has confirmed the new scheme feels right on the physical device.

**Done looks like:** the tester (or Mario with a device) plays a session on
v1.3.0 and either blesses the upstream controls or names what regressed.

## Licensing

### KaiTi is on the browser demo's SD card without a redistribution licence

`tools_local/wasm/sdcard/` ships a rasterised KaiTi for the Study app's Chinese
deck. KaiTi is not cleanly licensed for redistribution, which makes this the one
asset in the repository that is shipped on a shrug.

Mario has parked replacing the font deliberately, so this is recorded rather
than being worked on. It is named in
[THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md), so the project is at least
not pretending otherwise.

**Done looks like:** an OFL or otherwise redistributable CJK face at the two
sizes `StudyFonts` asks for, or the Chinese deck dropped from the browser card.

### The device shows xkcd comics with no attribution

xkcd is [CC BY-NC 2.5](https://xkcd.com/license.html), and the BY half is not
optional: attribution has to travel with the work. The site and
THIRD-PARTY-NOTICES both credit Randall Munroe. The device does not: nothing in
`src/apps_local/xkcd/` puts his name or the licence on a screen, only
`xkcd.com` inside status messages and URLs.

**Done looks like:** the comic view or its info screen naming Randall Munroe and
the licence, on the device, where the comic is actually read.

## The browser demo

### No script rebuilds the emulator SD card

The card under `tools_local/wasm/sdcard/` is assembled by hand: `pack_subset.py`
cuts the xkcd slice, the canned HTTP bodies are curled one at a time, the font
is copied twice under two names. Reproducing it is a matter of remembering.

This is already flagged at the end of the "What the browser build fakes"
section in [site/README.md](../site/README.md); it is repeated here because a
note inside a paragraph is easy to lose.

**Done looks like:** one script that builds the card from nothing, so the demo
can be re-seeded after a change to `src/` rather than drifting.

## Accessibility on the site

Both came out of the second critic pass. The third item it raised, the emulator
canvas needing `role="img"` and a tab stop, is done and is not listed here.

### Control borders are below the contrast floor

`--edge` is `#a9a494` on `--paper` `#f5f2ea`, which measures **2.23:1**. WCAG
2.1 SC 1.4.11 asks for 3:1 on the visual boundary of a control. It is used as
the only boundary on real controls, not just on decorative rules, so this is a
genuine miss rather than a technicality.

**Done looks like:** a darker `--edge` for the controls that depend on it,
verified by measuring rather than by eye, in both colour schemes.

### The mobile shelf scroller cannot be reached by keyboard outside Chrome

Under 700px the shelf becomes a horizontal scroll-snap container. Chrome makes
scrollable regions focusable on its own; Firefox and Safari do not, so on those
browsers the shelf can be seen but not scrolled without a pointer. No element in
`site/index.html` carries `tabindex`.

**Done looks like:** the scroller focusable and arrow-key scrollable in Firefox
and Safari, checked in those browsers rather than in Chrome.

## Vertical centring, in the two places the app cannot reach it

Added 2026-08-25, after auditing every app in `src/apps_local/` for the clamp in
`GfxRendererTarget::text` and wrapping 47 call sites in `toybox::inkCentred`.
Both of these were found by that audit and left alone on purpose.

### A FreeInkUI component lays out its own label, so a pill is 3px low

`screen.button()` and the list rows measure and place their own text, so a call
site has no rect to wrap. A toybox pill is 58px and the display cut's line box
is 63, which puts every big button's label 3px below centre: Insider's DEAL and
its `+`/`-` steppers, Solitaire's UNDO and NEW, and every door drawn that way.

Three pixels is not what Mario complained about and the buttons read fine, but
it is the same defect and it is the only one left that the fork's own screens
cannot fix. The right fix is in the SDK's own `text()`, which is the one place
that knows both the rect and the font.

**Done looks like:** `FreeInkUIGfxRenderer.h` centring on ink rather than on the
line box when the rect is shorter than the line box, upstreamed rather than
patched locally, and the call-site wrappers retired as it lands.

### `inkCentred` has no answer for mixed-case prose

It centres the CAP band, so a run with descenders hangs them below the box. That
is fine for the caps and digits it was written for and wrong for a headline.
Xkcd's comic title is the live case: the reading cut's line box is 40px in a
28px band, which puts it 7.5px low with its foot outside -- worse than anything
that was fixed, and untouched because the correction would be wrong.

**Done looks like:** either a variant that takes the run's own descent into
account, or those bands grown to a full line box so no correction is needed.

## The test harness

### Two warning classes are suppressed suite-wide rather than at their sites

`host-tests/dungeon/run.sh` and `host-tests/ui/run.sh` pass `-Wno-comment` and
`-Wno-format-truncation` so the suites build under GCC. Both suppressions are
justified where they are written, but they are blanket: a genuinely truncating
`snprintf` added later in those files would now go unreported.

`-Wno-comment` exists only because `FreeInkUIIcon.h` in the pinned `freeink-sdk`
submodule ends a `//` comment with a backslash. That one is fixable at the
source rather than worked around here.

**Done looks like:** the comment fixed upstream in freeink-sdk and the flag
dropped; `-Wno-format-truncation` narrowed to the files that need it, or the
buffers given bounds GCC can see.
