# Moving this fork from CrossMux to CrossPoint

**Done, 2026-08-04.** This tree is the CrossPoint-based fork. The document is
kept as the record of why, what was measured, and what was verified rather than
assumed -- because the next person to ask "why not just track develop?" deserves
the answer without re-deriving it.

This fork previously sat on [`0x1abin/crossmux`](https://github.com/0x1abin/crossmux),
itself a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
It now sits directly on CrossPoint, on the `crosspoint/feat-touch-ui` branch.

For how to work in the result, read [../LOCAL_SCOPE.md](../LOCAL_SCOPE.md) and
[shelf.md](shelf.md). What follows is history and rationale.

## Why, and why the original reason no longer holds

CrossMux was chosen (2026-08-02) for one reason: it had a desktop simulator, and
no other app-capable CrossPoint fork was both current and on the FreeInk SDK.
See the note in [shelf.md](shelf.md) for the seam that decision bought.

That reason is gone. The simulator is now
**`github.com/crosspoint-reader/crosspoint-simulator`** -- CrossPoint's own org.
It is a PlatformIO `lib_deps` line plus an `[env:simulator]` block, not a fork
feature, and it ships sample configs including an `[env:simulator_x4_pro]`. Our
`platformio.ini` still pins the older `0x1abin/crosspoint-simulator` copy at a
frozen commit.

The X4 Pro hardware target was never CrossMux's either. Our `[env:x4pro]` in
`platformio.local.ini` says so in its own comment: "ported from CrossPoint's
`[env:x4pro]`".

What CrossMux actually contributes on top of CrossPoint is +276,000 lines across
597 files: WeRead, the Chinese-first stack, AirPage, Buddy, Pixel Switch, Ugly
Avatar, six games, its own docs set and CI. Almost all of it is either unwanted
or already retired.

## The target branch: `crosspoint/feat-touch-ui`

Not `develop`. X4 Pro support lives on `feat-touch-ui`, and that branch is not
speculative -- **it is the branch CrossPoint ships the X4 Pro betas from.** The
public "X4 Pro Beta 10" changelog (2026-08-02) maps onto its commits:

| Changelog line                                 | Commit                                                     |
| ---------------------------------------------- | ---------------------------------------------------------- |
| Fixed GPIO pin assignment for Recovery Mode    | `77fba747` Fix X4 Pro recovery mode false-trigger on GPIO0 |
| Support for additional X4 Pro display variants | `34ca43d5` Probe display controller on S3 X4 Pro boards    |
| Text settings not saving on Home/sleep         | `13a077df` Save text settings immediately on each change   |
| (the base)                                     | `4f6d9cae` Add support for Xteink X4 Pro device            |

It started 2025-12-03, took 159 commits in the four weeks to 2026-08-04, and
merges `origin/develop` back into itself every few days (5 times since July 1),
so it does not drift. Treat it as CrossPoint's X4 Pro release line, not as a
feature branch waiting to land.

## Verified: it builds and boots

Done on 2026-08-04 in a throwaway worktree, with no CrossMux anywhere in the
tree. Plain `crosspoint/feat-touch-ui` + the official simulator compiles, links,
and reaches `Entering activity: Home` at 480x800.

Getting there needed six pieces, all of them small, all of them saved in
[`sim-stubs/`](../sim-stubs/) and `scripts_local/sim_catchup.py`:

- Four stub headers the simulator does not ship (`XteinkDetect.h`,
  `driver/gpio.h`, `soc/soc_caps.h`, `nvs.h`), 152 lines total. The big one is
  derived mechanically from the SDK's own header, whose comment already says the
  functions compile to no-ops without a device profile.
- Two additions to the simulator library itself: `BoardConfig::BoardProfile`
  needs its `input` member (matching the SDK's `InputPins`), and `Arduino.h`
  needs `digitalRead`.

The gap exists because the simulator tracks `develop`, which is slightly behind
`feat-touch-ui`. It is not an architectural incompatibility. Plain
`crosspoint/develop` builds in the simulator with **zero** patches.

**Open question for Mario:** whether to send the two simulator-library fixes
upstream as a PR. Outward-facing, so it needs his say-so. If sent and accepted,
the local shim disappears.

## Measured cost: 130 of 135 files move untouched

`git diff upstream/main...HEAD` at the time of writing: 88 commits, 135 files,
+27,486 / **-18**.

- **130 files added** -- all ours, all under `src/apps_local/`, `host-tests/`,
  `docs/`, `scripts_local/`, `tools_local/`.
- **5 files modified**: `.gitignore`, `SCOPE.md`, `.skills/SKILL.md` (one-line
  pointers), plus `AppsMenuActivity.cpp/.h` (+158 lines).
- **0 files deleted.**

The API surface our apps use from upstream is seven headers plus `Storage`,
`GUI` and `SETTINGS`:

| Upstream dependency             | On `feat-touch-ui`                                                                                       |
| ------------------------------- | -------------------------------------------------------------------------------------------------------- |
| `activities/Activity.h`         | byte-identical                                                                                           |
| `components/UITheme.h`          | byte-identical                                                                                           |
| `MappedInputManager`            | every method and button we call exists (`wasScreenTapped`, `mapLabels`, Back/Confirm/Up/Down/Left/Right) |
| `components/themes/BaseTheme.h` | 13 icons, no `Apps`, no `Gomoku`. We use 3; two need swapping                                            |
| `activities/ActivityManager.h`  | 22-line delta, and **no `goToApps()`**                                                                   |
| freeink-sdk submodule           | we are 48 commits **behind** with zero divergence -- pure catch-up                                       |

`AppsMenuActivity` does not exist on CrossPoint at all, so the one upstream file
we patch does not come with us. That is a gain, not a loss -- see the next
section.

## The new navigation shape

CrossPoint's Home is `Browse Files / Recent Books / File Transfer / Settings`.
We append two folder rows as **siblings**:

```
Home
  Browse Files / Recent Books / File Transfer / Settings
  ---------------
  Games >    chess, battleship, connections, solitaire
  Apps  >    study (FSRS), hacker news, ...
```

**Uniformly two taps to anything.** Earlier drafts of this plan nested Games
inside Apps, which made a game three taps and an app two for no reason a user
could name, and put a folder in the same list as loose leaf rows -- which is the
inconsistency that makes a menu feel arbitrary. Siblings fix both.

It also fixes the naming. "Apps" alone means "everything that is not reading",
which is a leftover rather than a category; that is how CrossMux's Apps menu
ended up holding Reading Stats and the WiFi transfer page next to Sokoban. "Apps"
standing next to "Games" means the useful ones next to the fun ones, which needs
no explanation.

### The rules

- **A folder holds apps, never another folder.** Depth is capped at two by
  construction rather than by discipline.
- **Back has two rules and no exceptions**: an app returns to its folder, a
  folder returns to Home.
- **An app is told its parent when it is launched.** It never hardcodes a
  destination.

That last rule is the whole reason this section is written before any code. The
`setReturnHere()` / `takeReturnHere()` breadcrumb in `games/Games.h` exists only
because upstream's six games each hardcoded `goToApps()` and there is no activity
back-stack to consult. Every app in the new tree is ours, so the breadcrumb can
be deleted outright -- but only if the shape is settled before four more games
are written against the old assumption.

### Files

```
LocalApps.cpp     kFolders[] = { {"GAMES", games::folder()}, {"APPS", apps::folder()} }
games/Games.cpp   chess, battleship, connections, solitaire
apps/Apps.cpp     study, hackerNews
```

One entry type shared by both folders, one launcher, one fixed-size hook on Home
that does not grow when a third folder is added.

### What the migration deletes

- `LocalApps.h/.cpp` in its current form. It exists to append our registry to
  _upstream's_ Apps menu; there is nothing to append to. The name survives, the
  job changes to "the list of fork-owned Home rows".
- `localapps::isRetired()` and `kRetiredAppIds`. CrossPoint's Home has no games
  to hide. (Retiring upstream's six was still right for the tree we are on.)
- `games::setReturnHere()` / `takeReturnHere()`.
- `AppId` bits, the hidden-apps mask, `games::ownsAppId`.

## How FreeInk is used, and how we use it

Worth knowing so the abstractions stay honest.

**Hardware: system-wide and total.** CrossPoint symlinks 13 SDK libs --
`BoardConfig`, `InputManager`, `FreeInkDisplay`, `BatteryMonitor`,
`PowerManager`, `FrontlightManager`, `SDCardManager`, `Rtc`, `Imu`,
`XteinkDetect`, `SecureNet`, `Icons`, `FreeInkUI`. Panel, touch, buttons,
battery, frontlight, SD, RTC, IMU are all SDK. That is why the whole X4 Pro
target is one flag: the board profile lives in the SDK.

**UI: adopted, mid-migration, and at a different altitude from ours.** 37 of
CrossPoint's 85 source files touch FreeInkUI, but they use it as an _interaction
runtime_ -- `FreeInkApp<N,M>` owns the hit-test buffer and dispatches actions to
handlers -- while still drawing chrome with the older `GUI.drawHeader`. Their top
symbols are `ActionEvent` (44), `InputSnapshot`, `ListProps`.

We use the layer underneath `FreeInkApp`: `freeink::ui::Screen` / `Frame`, via
Toybox. Our top symbols are `makeRect` (96), `Paint` (76), `Color` (71). This is
deliberate and it is what makes the screens host-testable -- the builders compile
on macOS against a fake draw target with no renderer and no Activity. Keep it.

**In the box, unused by us, worth remembering:**

- **There is a keyboard component** (`KeyboardLayout`, `KeyboardKey`, QWERTY).
  CrossPoint uses it in `KeyboardEntryActivity`. Rolling player names instead of
  typing them is still right -- a name is recognised, not written -- but if an
  app needs real text entry it is free.
- **`NearbyTransfer` is an ESP-NOW library in the SDK**: Discover / Advertise /
  Offer / Accept / Data / Ack, CRC32, chunking. Our `link/` layer is 2,249 lines
  and overlaps it by roughly the 100 lines of `EspNowTransport`; the rest
  (pairing, coin toss, turn alternation, the note channel) has no SDK equivalent,
  because `ReliableTransferSession` is a one-shot _file_ transfer state machine,
  not a continuing conversation. Not worth undoing. **But ESP-NOW allows exactly
  one global receive callback** -- if CrossPoint ever adopts NearbyTransfer for
  device-to-device transfer, it collides with `LinkRadio`. This belongs as a
  comment in `LinkRadio.cpp`.
- **`FreeInkBook` is unused by CrossPoint** -- they kept their own EPUB engine in
  `lib/Epub`. The SDK is not the whole story.
- We do not use `FreeInkApp`'s refresh-hint policy (`invalidateTransition`,
  `setTransitionFullEvery`). `displayBuffer()` already defaults to
  `FAST_REFRESH`, so nothing flashes, but we have no ghosting policy. Minor;
  revisit after the migration.

## Planned apps beyond games

Both are Mario's, both are non-games, and both are the reason the Apps folder
exists.

### FSRS (spaced repetition, for practising Chinese)

Precedent already exists in this tree. The Connections store is `.dat` (bulk) +
`.idx` (index) + `.res` (per-item results), each written to `.tmp` and renamed --
structurally what a card store needs: cards, an index, review state, crash-safe
writes.

**The real risk is the clock.** Nothing we have built cares what day it is; a
review schedule is nothing but aging. The SDK has `Rtc` and CrossPoint syncs
SNTP, so reading the date is easy. The hard part is a _wrong_ clock: after a flat
battery, every card comes due at once or none does, and FSRS's difficulty
estimates absorb the damage permanently. Design that before writing code.

**Chinese glyphs, and the migration helps.** CrossPoint embeds no CJK fonts;
CrossMux embeds `notosans_cjk_10` through `18` behind `ENABLE_CHINESE_VERSION`.
But CrossPoint has the full SD-card font stack (`SdCardFontRegistry`,
`FontInstaller`, `FontDownloadActivity`) and CJK rendering in `EpdFont`, so the
font comes off the SD card instead of the binary. At 90.7% flash on CrossMux,
that is the better arrangement.

### Hacker News reader

The networking is already solved here. Connections streams a **1.3MB** JSON
archive through `HttpDownloader::fetchUrl` with a push parser that buffers
nothing beyond the single item being assembled -- which is how a 380KB device
imports a file three times its RAM. See `connections/ConnectionsImport.h`. HN's
API is smaller and simpler.

**Open question, and it is a product one:** whether the reader is "top stories +
the linked article" or "top stories + the discussion". Their EPUB engine lays out
flowing paragraphs; a comment thread is nested and collapsible. Those are
different products and the answer decides whether we reuse their text layout,
draw our own thread view, or both.

## Order of work, when Battleship is done

1. Fresh clone or worktree of `crosspoint/feat-touch-ui`; add the simulator env
   and the shim from `tools_local/crosspoint-sim-stubs/`.
2. Bump the freeink-sdk submodule forward 48 commits (no divergence to resolve).
3. Build the folder registry and the Home hook **first**, before any app moves,
   so nothing is written against the old navigation.
4. Copy `src/apps_local/`, `host-tests/`, `docs/`, `scripts_local/`,
   `tools_local/` across. Swap the two missing icon names.
5. Point `scripts_local/` at the new env names.
6. Games, then the two new apps.

## Making this the only tree

The migration was done in a git worktree so the CrossMux tree and its running
simulators were never disturbed. Both share one `.git`, which is how files came
across with `git checkout link -- <paths>`.

When the new tree has run long enough to trust:

```bash
cd /Users/mario/Projects/Personal/Code/Xteink
git -C firmware worktree move ../firmware-next ../firmware-crosspoint
# then swap the names, and repoint scripts/*.sh at the survivor
```

Nothing depends on the directory name except the `scripts/*.sh` symlinks at the
workspace root, which currently point at `firmware-next/scripts_local/`.

Two loose ends that are Mario's, not code decisions:

- **Which remote does `xteink` push to?** `origin` is still
  `ma-r-s/crossmux`, which is the wrong lineage for a CrossPoint-based branch
  even though pushing there would work. A fork of `crosspoint-reader` would be
  the honest home. Nothing is pushed yet.
- **The four game icons.** All of them draw `UIIcon::Book`, because CrossPoint's
  palette has thirteen entries and none is a chess piece or a ship.

## Answered since

- **Send the simulator fixes upstream?** No. Watch how CrossPoint solves the same
  problem and compare; a merged PR would make their answer ours.
- **Audit CrossMux's reading work before dropping it?** No. It is overwhelmingly
  WeRead and the Chinese-first stack, which is the part of CrossMux that was
  unwanted in the first place.
- **Does the order of work above still read true?** Yes, and it was followed:
  simulator, then the shelf before any app, then chess, then the rest, then the
  docs and scripts.
