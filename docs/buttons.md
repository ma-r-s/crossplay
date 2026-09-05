# The buttons

What the physical buttons are for in our apps. The answer turned out to be
almost entirely a hardware question, and once the hardware is right the design
question mostly disappears.

---

## 1. The X4 Pro has two buttons

Not six. Our own board profile says so, and it is confirmed on hardware:

```cpp
// {back, confirm, left, right, up, down, power, powerActiveHigh}
{PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 0, 7, 3, false},
```

`freeink-sdk/.../BoardConfig.h`, `XTEINK_X4_PRO`

- **Two side keys.** Physically left is GPIO0, physically right is GPIO7. They
  are wired to logical **Up** and **Down**, and the comment beside them says
  what they are for: _"The two keys map to the reader's page pair (Up=prev /
  Down=next)."_ On the device they are moulded page-turn keys.
- **Power**, GPIO3.
- **No Back. No Confirm. No Left. No Right.** All four are `PIN_UNASSIGNED`,
  which `InputManager::begin` skips entirely (`if (pin >= 0)`), so they are
  never even configured as inputs. They cannot fire. They do not exist.

The X4 Pro dropped the front button row the older C3 X4 had. The
`frontButtonBack` / `frontButtonConfirm` settings still exist and still remap,
they just remap indices onto pins that are not connected to anything.

**So the entire button budget available to a game on this device is two keys
that the case labels as previous and next page.**

### The Sticky has three, and it changes nothing

The Seeed reTerminal Sticky (the second supported device) carries the same two
page keys (Up=GPIO5, Down=GPIO6) plus one more: a shared **Confirm/Power** key
on GPIO4 -- a click is Confirm, a hold is sleep
(`InputStyle::DigitalConfirmPowerHold` in its board profile). That key maps
onto the logical Confirm that every screen already accepts, so it works
everywhere without any app knowing it exists.

The design rule does not move: **design for the X4 Pro's two keys plus touch.**
An interaction that _requires_ the third key would be unreachable on the X4
Pro, so the Sticky's Confirm is a convenience alias for a tap, never the only
way to do something.

## 2. Back is a swipe, and it already works

Worth stating plainly, because the pin map above looks alarming and it is not.
`MappedInputManager::wasReleased` opens with:

```cpp
if (button == Button::Back && wasBackGesture()) return true;
```

and `wasBackGesture()` is a left-to-right swipe anchored near the left edge. So
every `mappedInput.wasReleased(Button::Back)` in `apps_local` resolves through
the gesture on this device. The physical half of that call has been dead since
the X4 Pro was targeted and nothing depended on it.

> **This section used to end "and every game is exitable", and that was not
> true.** Card #250, September 2026: Trivia could not be left by swiping. The
> mechanism above is correct and the conclusion drawn from it was not, because
> reading the button is not the same as reading it where a swipe can arrive.
> Trivia's only `Button::Back` was inside its download progress callback, a path
> that exists only while a multi-minute fetch has blocked the loop; its `loop()`
> returned early unless a tap had arrived, and a swipe is not a tap. **The
> button count in the table below counted that read**, which is why the number
> looked complete while one app had no exit at all.
>
> Two things follow, and both are worth carrying to the next app:
>
> 1. An app must read Back on its **per-frame input path** -- `loop()`,
>    `gameLoop()` or its `route*()` handlers -- and **above** any "nothing to do
>    unless a tap arrived" return. Below that line the read is on the frame path
>    in name only.
> 2. `host-tests/backgesture/` now enforces exactly that, and also refuses a
>    second Back hand-rolled out of a horizontal `wasSwipe()`. Counting reads per
>    file cannot see either failure; that suite looks at which function the read
>    is in and where in it.
>
> A related misreading the same card produced: `wasSwipe()` is consulted by only
> a few things in `apps_local`, which looks like most apps ignoring the back
> gesture. It is not. `wasSwipe()` is the four-direction **paging** swipe, every
> live comparison against it is `Up` or `Down`, and Back never goes through it.

## 3. What we actually use

Counted across `src/apps_local/`, excluding the shared modules that are not apps
(`link`, `player`, `sample`, `ui`, `bridge`). **That scope is not the shelf.**
GET BOOKS is on the shelf and its activity is upstream's
`src/activities/browser/OpdsBookBrowserActivity.cpp`, outside `apps_local`
entirely; it reads Back, Confirm and Left, so the shelf's real Confirm and
Left/Right figures are each one higher than the table below. The table counts
the directories this fork owns, which is the question this section asks.

`host-tests/docsclaims/` walks the same directories and fails when these numbers
drift. To see it by hand:

```bash
grep -rl "Button::Up\|Button::Down" src/apps_local/*/ | cut -d/ -f3 | sort -u
```

| Button       | Apps that read it | Exists on X4 Pro |
| ------------ | ----------------- | ---------------- |
| Back         | 24                | as a swipe       |
| Confirm      | 2                 | **no**           |
| Left / Right | 1                 | **no**           |
| Up / Down    | 11                | **yes**          |

Twelve of the twenty-four directories use Back and nothing else.

> **The finding below was true when it was written, in August 2026, and it is
> not true any more.** It said the two real keys were unused by every game we
> had built, and that the only apps touching them were the reader-shaped ones.
> Twelve apps read Up/Down today and eight of them are games: Checkers, Connect
> Four, Forehead, Picross, Sea Salt, Toy Battle, Wavelength and Yahtzee,
> alongside Hacker News, Instapaper, Wallpapers and xkcd. The rule in section 4
> is what survived and is still the thing to apply; the census that motivated it
> has been overtaken, which is the outcome it was arguing for.

That was the whole finding. Not "we should use the buttons more" as a matter of
taste -- the device shipped with two physical keys, they were page keys, and our
games ignored them.

### The first game that did not, and the exception that proved the rule

FOREHEAD (2026-08-28) is the first game here where the two keys are the primary
input rather than a page turn, and it earns that by satisfying section 4 exactly
rather than by making an exception to it. The player holds the panel against
their own forehead and **cannot see the screen**, so there is no "where" to
point at: GOT IT and PASS are answers, not positions. That is the one shape a
button is for, and it is why the game has no cursor either.

It also shows what the rule's second half costs when taken seriously. "Never
only a button" still holds and the two halves of the round screen are tappable
-- but not as halves, because the guesser's fingers curl over the long edges to
reach the keys and would answer their own card. The touch targets are bands
across the middle of each half instead. See [apps/forehead.md](apps/forehead.md).

Its landscape key mapping -- `Button::Up` is the BOTTOM key and `Button::Down`
the TOP one, once the panel is turned counter-clockwise -- was **confirmed on
hardware on 2026-08-30**. Reuse it rather than re-deriving it: it is the same
rotation for any app that turns the panel this way.

Worth knowing before writing another landscape app: `ScreenUp` / `ScreenDown`
look like exactly the right API for a rotated screen and are a trap here. In
`LandscapeCounterClockwise`, `mapScreenDirection()` resolves them to
`Button::Left` and `Button::Right`, which are `PIN_UNASSIGNED` on this board and
can never fire -- and the whole mapping is gated on
`SETTINGS.frontButtonFollowOrientation`, a reader preference most players have
never opened. Read `Button::Up` and `Button::Down` and do the rotation
arithmetic yourself.

---

## 4. The rule

**A button may only do something that has no "where".** Every action is one of:

- **Pointing** -- which cell, column, card, category. The answer is a position,
  the finger names it exactly, and a button can only express it by inventing a
  cursor. design-language.md removed cursors from Chess and Connections for
  cause and that stays removed.
- **Stepping** -- next page, previous page. No position at all. Drawing a
  control for these is drawing a place for something that does not live
  anywhere, which is why the shelf's page marks felt wrong as buttons.

On this hardware the rule is almost self-executing, because the only two buttons
are page keys. **Up and Down page. Nothing else is a button, because nothing
else is a button.**

**And paging by button is never the only route.** The page marks stay tappable,
and a swipe steps a page too -- it is the first thing every hand reaches for on
a touch panel showing a page indicator or a scrollbar. Three cold testers tried
it on the shelf before anything else, and a fourth tried it on Hacker News's
story list and reported the list broken when nothing moved. Not because touch
is better, but because the
moment a button is the only way to reach something we have two input models
again, and the invisible one wins arguments it should not.

**Paging is VERTICAL, everywhere.** Up for the next page, the way the page
moves under the finger; down for the previous one. One rule per app, never two,
and the same rule in an app's list as in its reader -- learn it once.

That used to read "the swipe follows the axis the content moves on", with the
shelf taking a horizontal swipe because its pages slide sideways, and it ended
with the line "Back is a left-EDGE swipe, so it never collides with either."
**That last sentence is false on the horizontal axis, and the shelf paid for
it.** Back is a left-to-RIGHT swipe anchored in the left 25% of the width
(`EDGE_SWIPE_SIDE_FRAC`, 120px of 480), which is the same visible gesture as a
horizontal page-BACK and differs from it only by where the finger started.
Nothing draws that boundary, so on the shelf a cold tester swiping back from
page two landed on Home, concluded that back meant exit, and went forward until
the pages came round -- onto a page they had not noticed, whose second row was
a different game.

So a horizontal paging axis cannot be symmetrical while Back owns left-to-right,
and a one-way axis is the asymmetry that was reported. The vertical axis
collides with nothing: it is orthogonal to Back, which is why the story list and
the reader never had this problem. The shelf moved onto it, and nothing in
`apps_local` pages sideways now.

Two vertical bands are consumed above the activity and are worth knowing before
using them for anything: a down-swipe starting in the top 14% is the light-panel
gesture, and an up-swipe starting in the bottom 14% is the Home gesture **on
boards with no home key**. The X4 Pro has a capacitive home key
(`BoardConfig` `hasHomeKey = true`), so its bottom edge is free and its Home
gesture is the key; the Sticky has no key and does spend that band.

---

## 5. The affordance is moulded into the case

This is where the iPhone comparison lands, and the answer is that we do not need
one.

The iPhone shows a transient indicator at the edge, aligned to the key, that
fades. E-ink cannot fade, and a refresh is a visible blink, so an appear/dismiss
indicator costs two full repaints to tell you about something you just did.

We do not need it because **the buttons are already labelled -- by the
hardware.** They are physical page-turn keys on a device whose primary app is a
reader that uses them to turn pages. A user who has read one book on this device
has already learned them. Making them page the shelf and the HOW TO PLAY pages
is _consistency with the thing they already do_, not a new capability that needs
teaching.

An on-screen tick would be permanent ink -- on every screen, forever -- to teach
something the case already teaches. That fails the ink-budget rule (black is
inversely proportional to how often it changes) for no gain.

**So: no affordance. Just make the page keys page.** If the buttons ever gain a
job the case does not imply, that decision comes back.

---

## 6. What this changes

1. **Up/Down page wherever there are pages**: the shelf folder, HOW TO PLAY in
   every game, and Hacker News's story list and reader. Behaviour only; nothing
   is drawn or removed. The shelf takes Left/Right too, which are
   `PIN_UNASSIGNED` on both target boards and exist only in the simulator and
   the browser emulator -- where all six keys are wired to the arrow keys, and
   where every arrow moves a cursor on Home. Two of the four doing nothing one
   level in is the only place a person meets these keys at all.
2. **design-language.md is wrong and gets corrected.** It says "Keep the
   physical buttons for Back and system functions", and on this device there is
   no physical Back button. The corrected rule is the one in section 4.
3. **The `mapLabels(...)` calls stay, and are documented as inert here.** All
   fifteen feed `drawButtonHints`, which returns immediately when
   `gpio.hasTouch()`. They are not dead code in the fork's other targets -- the
   C3 X4 and X3 have the front row and do draw hints -- but on the X4 Pro they
   have never drawn anything, and twelve of them pass `("Back", "", "", "")`,
   which would label a button that is not there.

Nothing here needs the device in hand. The pin map is committed, the gesture
path is testable in the simulator, and paging is verifiable with a screenshot.

## 7. A release belongs to whoever saw the press

Closed 2026-09-03. Read this before writing a `wasReleased` branch, and before
adding a `sawThePress` flag of your own -- there is one, in the framework, and
a second one per app is how a convention acquires nine patches and no fix.

**The bug it exists to stop.** A screen that finishes on the PRESS hands
control back while the button is still down. The RELEASE lands ~77ms later on
whatever is underneath, which never saw the press and reads it as its own
input. `WifiSelectionActivity` is `wasPressed` throughout, and it is the screen
apps put in front of themselves to get a network -- so on a device that has
never joined Wi-Fi, Hacker News could not be opened at all: backing out of the
picker shut the app, and the saved-articles shelf, the half that exists for
having no network, needed a network to reach.

Measured with a probe on every loop pass (168587fb): five clean passes go by
between the two edges, so it is genuinely one physical press producing two
logical events, not one latch read twice.

**The rule, and where it lives.** `ButtonReleaseGate` (`src/util/`) holds a
mask of buttons whose next release is not the current screen's to act on.
`ActivityManager` arms it at both points `currentActivity` changes, with the
buttons that are down right then; `MappedInputManager::readButton()` -- the one
place a physical index is read -- drops a release for an armed button.
Nothing an app writes has to know about it.

Four things worth knowing:

- **A screen that acts on the RELEASE arms nothing.** At the moment it hands
  back, the button is already up, so the mask comes out empty. Home, the shared
  list base and Settings are all release-based; the arm only ever fires for the
  press-exiting screens, which is nine files.
- **The arm cannot outlive one press.** It is cleared by a fresh press edge
  unconditionally, by the release it was waiting for, or by the button simply
  not being down. A gate that swallowed too much would leave Back dead, which
  reads as a frozen device and is worse than the double-fire.
- **Power is deliberately outside it.** Its release is consumed outside the
  activity stack (sleep, the frontlight double-click window), and nothing in
  `src/` finishes an activity on a Power press.
- **The swipe was never exposed, and still is not.** For a left-edge swipe,
  `wasPressed(Back)` and `wasReleased(Back)` are the same `wasBackGesture()`
  call, both true in one frame -- but the child and the parent read input in
  DIFFERENT frames (`ActivityManager::loop()` swaps the activity after the
  outgoing screen's `loop()` and before the incoming one's), and
  `touchReleasedEvent` is cleared by the `gpio.update()` in between. So the
  gesture reaches exactly one reader, and the gate leaves it alone: both
  spellings return before the gate is consulted.

**What the simulator can and cannot say about this.** It reproduces the BUTTON
case -- the five-pass measurement above was taken there -- but it does not
compile `lib/hal` and its latch clears in `beginFrame()` rather than
`update()`, so it cannot be used to argue about the touch one-shots or about
anything an `update()` does, and a green simulator run is not evidence about
the device's latches in either direction. The frame-by-frame checks are in
`host-tests/pickerseam/`.
