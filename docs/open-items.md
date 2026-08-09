# Open items

Things known to be unfinished, as of 2026-08-07, the day v1.0.0 was published.

This is not a roadmap and not a wishlist. Everything here is either a promise
the project has not kept yet or a known way it can mislead someone. Anything
that turns out to be neither should be deleted from this file rather than left
to age.

Ordered by what would embarrass the project soonest, not by effort.

## No one has run this on a physical device

The whole thing is developed against a desktop simulator and a browser build of
the same sources. Both are the real firmware, but neither is a panel, and
nothing here has met an e-ink refresh, a real SD card, a battery, or the
frontlight.

The v1.0.0 release notes say this in the first paragraph under "Before you flash
this", and the site says it too. The item stays open until someone flashes an
X4 Pro and reports back.

**Done looks like:** one confirmed boot on real hardware, and either a green
report in the README or a list of what broke.

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
