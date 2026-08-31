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

No longer true as of 2026-08-25: **both devices are on the desk** -- an X4 Pro
(since 2026-08-25, releases are flashed there before they ship) and a Seeed
reTerminal Sticky (arrived the same day, port in progress on `app/sticky`).
The simulator still fakes buttons the hardware does not have and refreshes
instantly where the panel takes hundreds of milliseconds, so the desk devices
remain the only honest judge of feel.

**Done looks like:** a second tester pass on a build that contains these fixes,
and a green report in the README naming the version that was actually flashed.

## The Sticky port is new and mostly unproven in the field

Added 2026-08-25, the day the device arrived and the port was made; SD half
verified 2026-08-26 on the desk unit's own card (formatted over the serial
bridge, chess save survived a reboot, the player identity stopped renaming
itself). The virgin-card save bug that session found -- nothing created
`/.crosspoint` before the plain HalFile writers needed it -- is fixed at mount
in main.cpp and was never Sticky-specific. What is still only as strong as
one desk session:

- **PLAY NEARBY between two Stickys is untested.** One Sticky exists here and
  two-device play needs two. Sticky-to-X4-Pro IS verified (2026-08-26, both
  desk devices on v1.4.0 dev builds): discovery, pairing, and a chess opening
  exchanged in both directions, driven over both serial bridges.
- **Flash headroom is thinner than the X4 Pro's**: the sticky app image
  carries the mic/buzzer/sensor SDK drivers the x4pro build never compiles,
  so it sits closer to the 6.25MB slot ceiling. Watch it at release time.

The panel mount question is closed: Mario confirmed the glass upright on
2026-08-26, so the profile's NO_FLIP is correct as shipped -- exactly the
"confirmed on a unit" its comment asked for.

**Done looks like:** a release that names the Sticky, an OTA install onto the
desk Sticky from that release, and a stranger's issue report -- either way.

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

## The image no longer fits the OLD partition table, so early installs cannot OTA

Measured 2026-08-31 on current `xteink`: `gh_release_x4pro` is **6,560,918
bytes**. Two ceilings matter and they are not the same number:

| slot             | bytes     | since                    | this image               |
| ---------------- | --------- | ------------------------ | ------------------------ |
| new (`0x7F0000`) | 8,323,072 | `app/flashroom`, v1.5.3+ | 78.8% used, 1.68MB spare |
| old (`0x640000`) | 6,553,600 | before that              | **7,318 bytes OVER**     |

So on flash headroom the fork is comfortable, and the old "96.6% and every new
screen costs some" framing this section used to carry was measuring against a
ceiling that no longer applies to new installs.

**What it does mean:** the partition table is written by a USB flash and NEVER
by an OTA, so a device installed before v1.5.3 keeps 6.25MB slots for life. Any
such device cannot receive this build over the air -- the write has nowhere to
go. It crossed by 0.1%, which is the awkward part: a small trim would restore
it, and every future app pushes it further away.

**It already fails legibly, which was checked rather than assumed.**
`OtaUpdater.cpp:164` compares the release's stated size against the partition
BEFORE downloading and returns `TOO_LARGE_ERROR`, so the device does not erase
its spare slot and grind through 6MB first;
`OtaUpdateActivity.cpp:196` shows `STR_FIRMWARE_TOO_LARGE`, "Firmware too large
for partition". Someone built this for exactly this day.

The gap left is one sentence of wording: that message says what went wrong and
not what to do about it, and the remedy (reflash once over USB, which rewrites
the table and moves the device to 7.94MB slots permanently) currently exists
only in a log line the user never sees.

**Undecided, and a product call rather than a code one:** trim back under
6,553,600 so every existing device stays updatable over the air, or accept the
split and point affected users at a one-time USB reflash.

**Done looks like:** the decision made and written here; and, either way, that
string naming the remedy.

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

## The Sticky's bezel insets are still the inherited guess

Added 2026-08-26, revised the same day: the X4 Pro side of this item is done
(measured insets, safe-area enforcement through both the UITheme and fui
layers; see [bezel-insets.md](bezel-insets.md)). What remains is the Sticky:
its profile still carries the default {9,3,3,3} nobody measured, so its
screens are laid out around a guess. The BEZEL ruler measures it in two
minutes at a desk, once it is restored: it was removed from the shelf after
the X4 Pro was measured, and [bezel-insets.md](bezel-insets.md) carries the
two commands that bring it back.

**Done looks like:** a measured `ViewableInsets` override in the STICKY
profile in BoardConfig.h, read off the restored BEZEL ruler on the physical
unit.

## The freeink-sdk pin fetches from the ma-r-s fork

Added 2026-08-26. Three sdk commits (measured X4 Pro insets, safeArea fill,
header-at-rect) live on github.com/ma-r-s/freeink-sdk so releases can build
without waiting on upstream; Free-Ink/freeink-sdk#59 carries them upstream.

**Done looks like:** the PR merged, `.gitmodules` pointing back at
Free-Ink/freeink-sdk, and the submodule pinned to the upstream hash.

## Study still carries its own copy of the bridge transport

Added 2026-08-30, with the Instapaper app.

`src/apps_local/bridge/BridgeHttp.{h,cpp}` is the shared version: verified TLS
with an SD-override root bundle, the heap floor a wolfSSL handshake needs, the
dev-build diagnosis probe, and the two request shapes. The Instapaper app uses
it. Study does not -- its copy still lives inside `StudySync.cpp`, because
`app/studyradio` is a long-lived branch sitting on that exact file and
refactoring under it would turn a merge into an archaeology session.

The certificate bundle is NOT duplicated (BridgeHttp includes Study's), so the
thing whose duplication would actually rot is already single. What is doubled
is the plumbing around it, and the risk is the ordinary one: a fix landing on
one path and not its twin.

Move Study onto BridgeHttp the week `app/studyradio` merges. It is a delete and
five call sites.
