# The shelf

How to add a game or an app to this fork, and the rules that keep the
navigation from rotting. Read [LOCAL_SCOPE.md](../LOCAL_SCOPE.md) first for why
the fork exists at all.

## The shape

```
Home
  Browse Files / Recent Books / File Transfer / Settings    (upstream's)
  ---------------
  Games >    chess, battleship, connections, solitaire
             [ (face) SPIKY GRIM BEARD  > ]   the footer bar, opens PLAYER
  Apps  >    study, hacker news, ...
```

Two folders, siblings, appended after upstream's rows. **Uniformly two taps to
anything.**

## The footer bar, and PLAYER

The GAMES folder ends in a black bar carrying this device's face, its name, and
a chevron. Tapping it opens **PLAYER**, the one screen in the fork that is
neither a game nor a folder: the fork's System Settings, holding the one setting
a device has, which is who it is.

Two things about it are worth knowing.

**It is a door, not a control.** The bar used to reroll the name in place, which
meant the only way to look at your name was also the only way to lose it, and
there was no way to _choose_ one -- you pulled the lever until something
acceptable came out. Now the name comes apart into three words you can steer,
and each word is also a feature of the face. See
[`player/PlayerName.h`](../src/apps_local/player/PlayerName.h).

**It is not an `Item`, and `shelf::openPlayer()` exists for that.** PLAYER is
reached from a bar rather than a row, so it is in no folder -- but rule 3 still
applies, and `leave()` still has to have somewhere to send it. `openPlayer()`
records the current folder exactly the way `openItem()` does. That bookkeeping
is the whole function; without it, Back from PLAYER lands on Home.

Only a folder with `showsDeviceName` draws the bar (GAMES does, APPS does not),
because the name exists for playing against somebody in the room.

Games and Apps are siblings rather than Apps holding Games, and that was a
correction. Nesting made a game three taps and an app two, for a reason no user
could name, and it put a folder in the same list as loose leaf rows -- which is
the inconsistency that makes a menu feel arbitrary.

The naming works because they stand together. "Apps" on its own means
"everything that is not reading", which is a leftover rather than a category;
that is how the previous base's Apps menu ended up holding Reading Stats and the
WiFi transfer page next to Sokoban. "Apps" beside "Games" means the useful ones
beside the fun ones, and needs no explanation.

## Pages, not scrolling

A folder that does not fit is drawn a page at a time, with a row of numbered
marks above the footer and the page beside the folder's name in the header.
Tapping a mark goes straight to that page; a **vertical** swipe steps one page;
the two side keys step one page. All three go through
`ShelfFolderActivity::showPage`, so no route can move by a different amount than
another.

**The reason is touch, not taste.** The list component's 3px overflow track is
drawn but not tappable, so before paging existed every row past the ninth could
be reached only with the physical buttons, on a device whose games are
touch-only on purpose. Scrolling was not worse-looking; it was unreachable.

### The axis is vertical, because Back owns the horizontal one

Up is the next page, down is the previous one, the way the content moves under
a finger. A horizontal swipe pages nothing.

This list paged sideways first, and sideways cannot be symmetrical on this
device. Back is a left-to-right swipe anchored in the left 25% of the panel
(`EDGE_SWIPE_SIDE_FRAC`, 120px of 480), and it is the only way out of every app
here because the X4 Pro has no Back button. So on the horizontal axis
the backward page turn and the exit are **the same visible gesture separated by
an invisible line**: past 120px it paged, inside it you were on Home. A gesture
whose meaning flips on a boundary nothing draws cannot be made discoverable. It
can only be moved off that axis.

What that cost a cold agent, in order: they swiped back from page two, landed
on Home, concluded that back meant exit, and from then on tried to reach an
earlier page by going forward until it came round. Which it did, onto a page
they did not notice, whose second row was a different game.

So the horizontal axis carries Back and nothing else here, and the whole paired
step lives where it is symmetrical. Three things fall out of that:

- It is the axis `docs/buttons.md` already prescribes for content that
  continues downward, and the same way round as Hacker News's story list and
  the reader. Learn it once.
- It is the first thing a hand tries on a vertical list. The agent swiped
  vertically before anything else, got a byte-identical screen, and read the
  list as fixed in place.
- **A right-to-left swipe now does nothing**, and that is the price. It used to
  page forward, and a dead gesture is its own documented failure mode here. It
  is paid because keeping it would leave the forward step with a mirror image
  that exits, which is the asymmetry the agent reported in the first place.

Seven things follow, and each was got wrong once:

- **The page is derived from the selection, never held beside it.** Two facts
  that must agree are one fact stored once. A page member drifts the moment a
  button moves the cursor off it, and then the screen styles a row it is not
  showing. What crosses a reboot is that same single fact: the row. A page is
  written down as the row it starts at (`shelfui::rowForPage`), never as a page
  number -- rows per page is a property of the panel and the chrome, so a stored
  page number would mean a different set of games the first time a token moves,
  silently, while a stored row is the same game either way.
- **The screen is handed a slice, not the folder plus an offset.** The list
  component clamps `topIndex` to `count - visible` so its last screen is always
  full (`list.h:164`), which is right for scrolling and wrong for paging: page
  two of twelve showed items four to eleven, repeating half of page one. A page
  is a short list, so it is passed as one, and the component never learns pages
  exist. `ListItem::actionValue` still carries the absolute index, so a tap
  reports which game it is rather than which row.
- **The marks are an indicator, not a row of buttons.** A small centred cluster
  with air around it; targets a thumb wide and contiguous _within the cluster
  only_, so the screen edges do nothing. They stay tappable because
  `BaseTheme::drawButtonHints` returns early when `gpio.hasTouch()` and the X4
  Pro has a GT911: upstream teaches nothing about the physical buttons on a
  touch device, so a touch user cannot discover that Up and Down would page.
  Touch has to stay complete. The iOS home screen resolves the same tension the
  same way.

  **Each mark is its page's NUMBER, in a box of its own: outlined for a page you
  are not on, filled for the one you are.** That is the same language as the
  rows above it, which is what makes an indicator read as the control it also
  is -- two cold agents found the taps by accident and used them as their only
  reliable route, and a third never tried them and reported that the list could
  not be paged at all. A single hairline capsule around the whole cluster did
  that job first and cannot do this one: a filled cell inside a capsule of that
  radius pokes out through the curve at the two ends.

  They were 10px squares, and a fourth cold agent called them "the size of a
  full stop". At that size the only thing saying where you are is the difference
  between a filled square and an outlined one, which is smaller than the ink of
  one letter, at the bottom of an 800px panel, while the eyes are on the rows.

- **The page is said twice, and the second time in the header.** `GAMES 2/3`,
  right-aligned beside the folder mark. The bar answers the same question and
  answers it out of the fovea; the header is the first thing read on the screen.
  That matters here more than on any other list, because the folder resumes on
  the page it was left on, so **the row in position two is a different game on
  each visit** -- which makes "which page is this" the question that has to be
  answered before any tap is safe. A cold agent did not misread the bar. They
  never looked at it, tapped row two expecting TRIVIA, and got CHECKERS.

- **Nothing wraps.** Forward from the last page and back from the first do
  nothing at all, and nothing repaints either: a full-panel refresh that redraws
  the same eight rows is half a second of blink saying a step was taken when
  none was. `shelfui::pageStepClamped` is the shelf's step;
  `shelfui::pageStep` still wraps and Hacker News's story list still uses it.

  A wrap is only safe where the user can see where it put them. Every page draws
  its rows at the same eight screen positions, so a page arrived at by accident
  is indistinguishable from the page that was wanted until something opens --
  and walking forward off the last page is the one step nobody ever means. It
  was reached by an agent who wanted the PREVIOUS page, could not find a
  backward gesture, and pressed on hoping to come round.

  The old argument for the wrap was that a key which stops at the end reads as
  broken. That argument needed the indicator to be unreadable. The bar now says
  `3` of `1 2 3` and the header says `3/3`, so a step that does nothing is the
  screen telling the truth. The far page is still one tap on the bar, which is
  the same one tap the wrap cost, and is the whole reason the bar carries marks
  rather than arrows.

- **A folder reopens where it was left, which is the page you were ON.** Not the
  page holding the game you last launched. Those are the same thing until you
  browse and walk away, and browsing and walking away is most of what a shelf is
  for: paging to three and then leaving to read a book used to come back to page
  one. `resumeRow` in `shelf.cfg` is the stored fact, written by the two things
  that leave a folder standing somewhere -- opening an item (that item's row) and
  turning the page (the new page's first row) -- and read by `onEnter`.

  Written when the page turns rather than on the way out, because there is no way
  out to hook: Back destroys the activity and the idle timeout deep-sleeps
  wherever you happen to be, with wake a chip reset. Twenty bytes beside a
  full-panel repaint.

  **A folder that shrank under you opens on its LAST page**, never back at the
  top. A game removed by a firmware update leaves a card pointing past the end,
  and what was remembered was "near the end of this folder" -- the nearest
  surviving place to that is the end. `shelfui::resumeRowFor` is that rule, named
  rather than left to a clamp, because a clamp that cannot produce the last page
  is what made this area unpredictable before.

- **No row is ever marked.** A restored page says which page it is with the
  marks and the header, and with nothing else. An inverted row was tried as a landmark
  explaining why the list had not opened at the top, and it has been removed:
  navigation here is touch, the two side keys page, and `frontButtonConfirm` is
  an unassigned pin -- so a highlighted row is a cursor that nothing can move and
  nothing can open. That is the same reason the Instapaper reading list dropped
  its own inverted row.

  It did not read as a landmark either. `resumeRow` is written by opening an
  item, so a folder that fits on one page wore a permanent highlight on whichever
  app was used most: APPS came back marked INSTAPAPER every time, for a value
  that was correct and had nothing to explain, on a list that was already showing
  its first row at the top. A mark that is right and unreadable is furniture.

  The page-resume itself is untouched, because it was never the problem: the
  folder still reopens on the page it was left on. What went away is drawing that
  row.

Marks rather than prev/next arrows because arrows are up to `pageCount - 1` taps
to the far end and say nothing about where you are. A right chevron was the
obvious glyph for "next" and is exactly what could not be used: on this device a
right chevron already means "opens", and it is the only affordance the player
bar has.

## Choosing what a folder shows

Twenty games is a lot of games, and not everybody wants all of them. **Touching
the header band** turns the list into a chooser: every item the folder holds,
each with a box, hidden ones included. Tap a row to put it on the list or take
it off. `DONE`, or Back, closes the mode; the folder then draws, pages and
resumes over what is left, and the rest is simply not there.

```
GAMES                2/3  (joystick)      GAMES              2/3  [ DONE ]
  CHESS                      (crown)        [x] CHESS               (crown)
  SOLITAIRE                  (spade)        [ ] BATTLESHIP          (ship)
  ...                                       [x] SOLITAIRE           (spade)
  [ (face) SPIKY GRIM BEARD     > ]            TAP TO SHOW OR HIDE
            1  2  3                                    1  2  3
```

**It is a listing, not an uninstall.** Every item is still built into the
firmware and still works. A hidden game still resumes on wake if it was open
when the device slept, and still answers to `CROSSPLAY_AUTOSTART` -- a device
that woke up having forgotten the game on its own screen would be worse than a
long list.

Seven things this cost, each of which was wrong in a draft:

- **There is no button for the way in, and the folder's mark stays where it
  was.** The first version put a permanent `EDIT` chip in that corner and Mario
  turned it down: it is furniture that shouts on every visit for a thing done
  once, on a screen whose whole job is the list underneath it. The whole header
  band is the target instead -- the mark is what a finger aims at, and the strip
  around it answers, because a 32px glyph is under half a thumb and nothing else
  in that band could mean anything by a tap. The way OUT is still a drawn
  control (`DONE`, in the corner the mark occupies while browsing), because a
  mode whose exit is invisible is a trap; it exists only inside the mode.

  The page counter moved into the header component's own `rightLabel` with
  `rightReserve` for the mark. It was placed by hand at a fixed offset from the
  panel edge, which is right for exactly one of the two things that corner
  holds: `DONE` is wider than a 32px mark, so the hand-placed counter printed
  underneath it.

- **The chooser lists EVERYTHING, and the browsing list only what is shown.**
  That is the one thing that cannot be otherwise: a chooser over the shown items
  could not show you what you had hidden. It also means the chooser's rows ARE
  the registry, in registry order, so hiding something never moves the row under
  your finger. The list that shrinks is the other one, and it is not on screen
  while you are choosing.

- **The bottom band belongs to the FOLDER, not to the mode.** GAMES has one
  because it shows the device name; APPS has none. The chooser does not take a
  band of its own -- it puts its caption in the one the folder already has, and
  a folder without a bar gets no caption. That is what keeps the same page
  holding the same games in both modes, so entering the chooser adds boxes to
  the screen you were already looking at. A band the mode owned reflowed APPS:
  ten rows browsing against nine choosing, and the row under your finger is a
  different app before you have touched anything -- on a screen whose every page
  draws its rows at the same positions. The first draft had exactly that, and
  the test written for it compared `pagingFor` against itself and passed. The
  test now renders a full page both ways and demands every row.

- **Back closes the mode instead of leaving the folder**, and that is the only
  exception to the two Back rules below. It is named here rather than left to be
  discovered: on this device Back is also the only way out of an app, so a Back
  that walked out of the chooser would read as "undo what I was doing" on the
  one screen where it cannot be.

- **The box is drawn, not blitted.** A filled slab with a tick knocked out of
  it, or a hairline outline -- the same filled-versus-outlined pair the page
  marks below it already use for "this one of these". Lucide's `square-check` is
  a hairline tick inside a hairline box, and down a column of ten rows the two
  states read as the same grey smudge. Only the tick is an asset, because a
  hand-drawn tick is what goes wrong (solitaire's pips, minesweeper's flag,
  three attempts each).

- **The whole row toggles, not the box on it.** Two hit regions per row would be
  two of the screen's twenty-four for a control the finger is already on, and
  the row past the twenty-fourth stops answering in silence.

- **Hiding everything is allowed, and the empty folder is its own way back.**
  The whole empty body is a second hit region that opens the chooser, and it
  says `NOTHING HERE` above the sentence that says so. The band above it would
  have done, but it is 400px away at the top of an 800px panel and a person who
  has just found an empty folder is looking at the middle of it. A rule refusing
  the last game would be the device arguing with its owner.

### The file, and the two units

The set lives in `/.crosspoint/shelf-hidden.cfg`, beside `shelf.cfg` and
`player.cfg`, one item TITLE per line, only the hidden ones. So the ordinary
file is absent or empty and the ordinary parse is nothing at all.

**Titles, not row indices**, for the reason the wake-resume already learned:
indices move whenever a game is added, this card outlives firmware updates, and
an index would hide a DIFFERENT game after the next release with nobody able to
say why. A title in the file that matches no item today is KEPT on save, so a
build that does not know about a game is not the reason the build after it
forgets that the game was hidden. Everything else is dropped quietly -- blank
lines, whitespace, a stray carriage return, a duplicate, a line longer than any
title can be, anything past `MAX_HIDDEN` -- because there is nothing anybody
could do about any of it, and because the whole format fails OPEN: a file that
is missing, short, truncated or full of junk shows everything.

**A row is not an item, and the conversion is one named thing.** The card stores
the ITEM the folder was standing on; `shelf::resumeRowIn()` answers in SHOWN
rows and `shelf::rememberRowIn()` takes one, so the file holds one unit and the
folder counts in the other. The folder does hold items -- at the two points
where it must, opening one and hiding one -- and it gets them from
`ShelfFolderActivity::itemAtRow` and nowhere else. The arithmetic itself is in
`ShelfHidden.h` rather than beside the registry, because `Shelf.cpp` cannot be
built off a device and this is the part that must be tested: see
`host-tests/shelfhidden`. They are both small ints in the same range,
so a caller holding one while believing the other is a mistake nothing would
report -- and the symptom is this screen's oldest one: the header says page 2,
the rows sit where page 2's rows sit, and the tap opens page 1's game.

## The three rules

**1. A folder holds items, never folders.** There is nowhere in `shelf::Folder`
to put a `Folder`. The depth cap is structural, not a convention someone has to
remember, and on a panel that repaints in half a second a third tap is a real
cost.

**2. Back has two rules and one exception.** An app returns to its folder; a
folder returns to Home. The exception is the chooser above: while a folder is
choosing, Back closes the mode and stays put.

**3. No app names its own destination.** It calls `shelf::leave()`.

Rule 3 is the one with scar tissue. The previous base carried a
`setReturnHere()` / `takeReturnHere()` breadcrumb that had to be _redeemed by a
different activity on entry_, so a missed redemption stranded you in the wrong
menu. It existed only because upstream's own games each hardcoded `goToApps()`
and could not be edited. Every app here is ours, so none of them should ever
name a destination. When the four games moved onto CrossPoint, this rule made
each one a single-line change.

The current folder is module state in `Shelf.cpp` rather than something each app
carries. `replaceActivity` means exactly one activity exists at a time, so there
is exactly one current location: one fact, stored once. Threading a parent
through every factory would make six apps each keep their own copy of something
the device only has one of, and any app that forgot to store it could not leave.

## Adding a game or an app

**1. Copy the template.**

```bash
cp -r src/apps_local/sample src/apps_local/study
```

`sample` is sixty-six lines and is not registered, so it builds but never shows
up on the device. Do not start from a real app: the smallest is solitaire at
nineteen hundred lines.

Keep the app-name prefix on every file (`StudyActivity.h/.cpp`), matching
upstream convention: grep and crash logs stay unambiguous.

**2. Split it three ways.** This is the part that pays off later:

| Layer         | Example             | Knows about                               |
| ------------- | ------------------- | ----------------------------------------- |
| Rules / state | `ChessCore.h`       | nothing -- freestanding C++17             |
| Screens       | `ChessScreens.h`    | FreeInkUI and Toybox tokens, nothing else |
| Activity      | `ChessActivity.cpp` | the renderer, storage, input, the shelf   |

The first two are host-testable on macOS with no device. The third is the only
part that needs hardware, and it should be thin. See
[building-apps.md](building-apps.md) for the method in full.

**3. Register it.** One row in `src/apps_local/Shelf.cpp`:

```cpp
constexpr shelf::Item kApps[] = {
    {"STUDY", &icon_study_32, &StudyActivity::create},
};
```

That is the whole registration. No `ActivityManager` method, no `UIIcon` enum
variant, no i18n key, no `AppId` bit.

**Icons come from Lucide, not from `UIIcon`.** Add a line to
`tools_local/toybox/icons.txt` and run `./tools_local/toybox/gen_toybox_icons.sh`:

```
study = graduation-cap
```

That regenerates `src/apps_local/ui/ToyboxIcons.h`, which is committed because
generating needs librsvg and a checkout should build without it. Upstream's
palette is thirteen icons and growing it costs two upstream files per app;
Lucide ships 1735 and costs a line in a manifest.

A row with no icon does not compile -- `Shelf.cpp` static_asserts it, because a
blank icon gutter is silent otherwise. Pick for silhouette rather than
literalness: the label already says the name, so the icon's job is to be
distinct at a glance in a 62px row.

**A folder has no size limit.** It used to: the folder activity read the whole
registry into fixed arrays of sixteen and clamped in silence, so a seventeenth
game simply did not appear, with no log and nothing to grep for. The screen is
now handed one page at a time, so only a page is ever copied and the arrays are
sized by the tallest band this panel can draw rather than by how many games
Mario has promised people.

**4. Leave through the shelf.**

```cpp
if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
  shelf::leave(renderer, mappedInput);
  return;
}
```

**5. Write its tests before you believe it.** `host-tests/<app>/run.sh`, plus a
block in `host-tests/ui/test_ui.cpp` for the screens. Then break the code on
purpose and check the tests notice -- the shelf's own footer test passed against
a mutant that drew an invisible button, because asserting "the name is not
drawn" is not the same as asserting "the control is not there".

## What is not host-tested, and why

`ShelfScreen` is, like every screen here. `Shelf.cpp` is not: it exists to swap
activities, so it pulls in `ActivityManager` and cannot be built freestanding.

Its whole job is five facts -- which folder is open, which folder Home should
select, which page each folder should reopen on, which item was open when the
device went to sleep, and which items the folders are not showing -- and those
are verified in the simulator instead.

The arithmetic underneath the third one is not in that exception:
`shelfui::rowForPage` and `shelfui::resumeRowFor` are pure and live in the `ui`
suite, where the round trip (a page stored as a row comes back as the same page)
and the shrunken-folder rule are properties rather than examples.

Neither is the fifth one's file format: `ShelfHidden` is freestanding, and
`host-tests/shelfhidden` asks it what a file written by a later firmware does,
what a half-written one does, and what a card full of junk does -- the three
cases whose failures are all silent on the device, because each of them is a
game that appears or vanishes with nothing to say why. The chooser's own screen
is in the `ui` suite with the rest: that the corner chip routes, that a row
toggles instead of opening, that the page counter clears the chip, that both
modes fit the same games on the same page, and that an empty folder is tappable
everywhere.

The last three of those outlive a reboot, in `/.crosspoint/shelf.cfg` beside
`player.cfg`. They are plain `.bss` otherwise, and `main.cpp` deep-sleeps on the
idle timeout with wake being effectively a chip reset, so the shelf used to
forget which game you were playing every time you put the device down. Verified
by driving it twice: one run opens the third game and leaves, and a second run
from a cold boot must land Home on Games and the folder's cursor on that same
game. Both were wrong at some point and both were caught by looking:
leaving a folder put the cursor on Browse Files, and returning from the third
game put it on the first. If you touch that bookkeeping, drive it:

```bash
# Opened a game and came back: the row you opened, on its page.
./scripts_local/sim-shot.sh '2500:TAP:240,650;4500:TAP:285,687;7000:TAP:240,275;12000:BACK;15000:QUIT' \
                            '14000:qa-artifacts/returned.bmp'

# Browsed and walked away without opening anything: the page you were reading.
# This is the half that was wrong, and the half a launch-only test cannot see.
# The swipe is VERTICAL: a sideways one pages nothing here, so the horizontal
# version of this line passes by never leaving page one.
./scripts_local/sim-shot.sh '2500:TAP:240,650;4500:SWIPE:240,600,240,300;7000:BACK;9000:TAP:240,650;12000:QUIT' \
                            '11000:qa-artifacts/returned.bmp'
```

Both must come back on the page they left, and the first must come back on the
row it opened. Drive both: they were the same behaviour for as long as the stored
value was the launched item, so a run that only launches passes either way.

## Wake comes back into the app, not to Home

Deep sleep is a chip reset, so before it there is one write and after it one
read. `shelf::rememberForWake()` runs on the way into sleep and records the open
item's **title** on a second line of `shelf.cfg`; `shelf::resumeFromWake()` runs
from `setup()`'s routing block and reopens it, ahead of the reader's own resume.
Without it every sleep taken in a game woke up on Home, which is what made the
"Quick Resume" sleep screen a lie outside the reader: it leaves your game on the
panel with a small moon in the corner, and the wake then threw that game away.

Three things that look optional and are not:

- **The title, not the row index.** The card outlives firmware updates and row
  indices move whenever a game is added. An index would resume into a different
  game, silently. `findItemByTitle()` is the one place a title becomes a row,
  and a title that no longer exists falls back to Home.
- **The activity name, not `openFolderIndex`.** An item counts as open only when
  the activity it launched is the one on screen. The Home gesture leaves an app
  without passing through `leave()`, so `openFolderIndex` alone would claim a
  game you left ten minutes ago is still open and wake into it.
- **Only on a verified sleep wake.** The gate is `BootResume::SplashlessWake`,
  which needs both the one-shot `showBootScreen` flag the sleep wrote and a
  power-button wakeup reason. A cold boot must not resume off a stale card.

The simulator models the whole cycle -- a button press while asleep re-execs the
process reporting a power wake -- so drive it end to end rather than in halves:

```bash
CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE='7000:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE='4000:qa-artifacts/after-wake.bmp' \
./scripts/sim-shot.sh '2500:TAP:240,650;4500:TAP:240,472;7000:SLEEP;10000:POWER' ''
```

The trace must show the game entered again on the second process, with no Boot
and no Home between. Do not settle for checking the two halves separately: the
gate is the _combination_ of the flag and the wakeup reason, and setting each by
hand proves neither.

What this does **not** do is restore a half-played board. Reopening the app is
the shelf's job; remembering the position is the app's, through the same save
its `onExit` already writes. Chess, Solitaire and Sudoku come back mid-game;
Checkers, Connect Four, Knucklebones, Minesweeper and Yahtzee persist only their
history, so they come back on their own menu.

## Two icon paths, opposite conventions

Worth knowing before you add an icon anywhere new, because it costs an
afternoon otherwise.

- `fui::bitmapFromIcon` + `DrawTarget::bitmap`, which everything in
  `apps_local` uses, takes **upright** bitmaps. The renderer maps logical
  coordinates onto the panel.
- `GfxRenderer::drawIcon`, which upstream's `drawButtonMenu` uses, rotates its
  input by -90. Every bitmap in `src/components/icons/` is therefore stored
  rotated the other way so it comes out upright.

So the two Home folder icons exist twice: upright in `ui/ToyboxIcons.h`, which
is what `rotate_icons.py` reads, and pre-rotated in
`src/components/icons/shelfIcons.h`, which is what the theme draws.
`tools_local/toybox/gen_toybox_icons.sh` writes both; never hand-edit either.
The upright pair is the rotation's source rather than something a screen draws:
the folder header stopped carrying a mark when the chooser took that corner.

The joystick shipped lying on its side, then upside down, before this was
understood. The direction was settled by rotating a freshly generated `folder`
and comparing it against upstream's own stored `FolderIcon` -- not by
byte-equality, since the Lucide source has moved since theirs was generated, but
by looking at the shape.

## Adding a third folder

One row in `kFolders`, plus its icon in two places: a `UIIcon` value appended in
`BaseTheme.h` with a case in `LyraTheme.cpp` (Home draws it and accepts nothing
else), and a line in `tools_local/toybox/icons.txt` -- which is not for the
folder's own header, which carries no icon any more, but for the pre-rotated
copy `gen_toybox_icons.sh` writes into `src/components/icons/shelfIcons.h` for
Home's row. Those are the only per-folder edits to upstream files this fork
makes, which is affordable at two folders and would not be at ten.

Resist it until there is something that genuinely belongs in neither. Two
folders is a structure; four is a filing system, and a filing system is what you
build when you have not decided what the device is for.

## The Home seam

`src/activities/home/HomeActivity.cpp` is the only upstream file involved, with
four fixed-size touch points:

1. `getMenuItemCount()` adds `shelf::folderCount()`
2. the render loop appends folder titles and icons
3. the dispatch switch's `default:` case routes anything past upstream's rows
4. `onEnter()` restores the selection via `shelf::lastFolderOnHome()`

**Everything appends after upstream's rows**, so their indices never shift and
`indexToMenuItem()` / `menuItemToIndex()` stay untouched. That is what keeps the
hook fixed-size no matter how many folders exist.

**Row indices are not the menu's indices.** `getMenuItemCount()` counts the
recent-book cover tiles as well as the menu rows, but the dispatch has already
subtracted those to get its `menuIndex`. Deriving the shelf's offset from it
subtracts them twice, which is invisible on an empty card and breaks the moment
a book has been opened: Games falls out of range and Apps opens Games. Use
`upstreamMenuRows()`, which counts only what `indexToMenuItem()` walks -- and
remember the Continue Reading row that the RoundedRaff theme inserts at the top.

Point 4 exists because `goHome()` restores Home's selection by matching the
departing activity's _name_ against its own `HomeMenuItem` list, which cannot
know our rows exist. Without it you leave Games and the cursor is sitting on
Browse Files.

## Titles: Title Case outside, capitals inside

The registry stores `"Games"`. Home draws that, because Home is upstream's list
and has to look like it. `ShelfFolderActivity` shouts it into `GAMES` for its own
header, because Toybox chrome is capitals. Outside is their look, inside is ours,
and the line between them is one loop in one file.
