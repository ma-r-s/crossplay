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

## The three rules

**1. A folder holds items, never folders.** There is nowhere in `shelf::Folder`
to put a `Folder`. The depth cap is structural, not a convention someone has to
remember, and on a panel that repaints in half a second a third tap is a real
cost.

**2. Back has two rules and no exceptions.** An app returns to its folder; a
folder returns to Home.

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
`tools_local/icons.txt` and run `./tools_local/gen_toybox_icons.sh`:

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

**A folder over `shelf::kMaxItemsPerFolder` does not compile either**, for the
same reason. The folder activity reads the registry into fixed arrays, and that
read used to clamp in silence: a seventeenth game simply did not appear, with no
log and nothing to grep for. Raising the number is not the answer to a folder
that outgrew it.

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

Its whole job is three facts -- which folder is open, which folder Home should
select, which row each folder should select -- and those are verified in the
simulator instead. Both were wrong at some point and both were caught by looking:
leaving a folder put the cursor on Browse Files, and returning from the third
game put it on the first. If you touch that bookkeeping, drive it:

```bash
./scripts/sim-shot.sh '2500:DOWN;2700:DOWN;2900:DOWN;3100:DOWN;4000:ENTER;5000:DOWN;5200:DOWN;5400:DOWN;6200:ENTER;10000:BACK;13000:QUIT' \
                      '12000:qa-artifacts/returned.bmp'
```

The cursor should come back on the row you opened.

## Two icon paths, opposite conventions

Worth knowing before you add an icon anywhere new, because it costs an
afternoon otherwise.

- `fui::bitmapFromIcon` + `DrawTarget::bitmap`, which everything in
  `apps_local` uses, takes **upright** bitmaps. The renderer maps logical
  coordinates onto the panel.
- `GfxRenderer::drawIcon`, which upstream's `drawButtonMenu` uses, rotates its
  input by -90. Every bitmap in `src/components/icons/` is therefore stored
  rotated the other way so it comes out upright.

So the two Home folder icons exist twice: upright in `ui/ToyboxIcons.h` for the
folder headers, and pre-rotated in `src/components/icons/shelfIcons.h` for the
theme. `tools_local/gen_toybox_icons.sh` writes both; never hand-edit either.

The joystick shipped lying on its side, then upside down, before this was
understood. The direction was settled by rotating a freshly generated `folder`
and comparing it against upstream's own stored `FolderIcon` -- not by
byte-equality, since the Lucide source has moved since theirs was generated, but
by looking at the shape.

## Adding a third folder

One row in `kFolders`, plus its icon in two places: a `UIIcon` value appended in
`BaseTheme.h` with a case in `LyraTheme.cpp` (Home draws it and accepts nothing
else), and a line in `tools_local/icons.txt` for the folder's own header. Those
are the only per-folder edits to upstream files this fork makes, which is
affordable at two folders and would not be at ten.

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
