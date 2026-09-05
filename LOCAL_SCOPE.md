# Fork Scope

This is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
Target devices: the **Xteink X4 Pro** (ESP32-S3, 480x800 touch panel,
frontlight) and, since 2026-08-25, the **Seeed reTerminal Sticky** (same S3,
same 800x480 panel and GT911 touch; three buttons, no frontlight). The design
floor stays the X4 Pro -- two side keys plus touch; see
[docs/buttons.md](docs/buttons.md).

**This file overrides [SCOPE.md](SCOPE.md) where the two disagree.** SCOPE.md is
upstream's document and is kept verbatim so it merges cleanly. It says
interactive apps and games are out of scope. For this fork they are the point.

## What this fork is for

Reading stays the primary purpose of the device, and the reading experience is
upstream's to own. We do not fork it, improve it, or diverge from it. Anything
that makes reading better belongs upstream, so send it there.

What we add is **games and small tools that make the device worth carrying
instead of a phone**. Twenty-one apps are here now: seventeen games, a
spaced-repetition trainer, a Hacker News reader, an xkcd viewer and a
catalog browser.

## The one rule that keeps this sustainable

Upstream moves fast and we want its work. Every change we make is measured by
how much upstream-owned code it touches.

- **Everything we add lives in new files**: `src/apps_local/`, `host-tests/`,
  `scripts_local/`, `tools_local/`, and our own `docs/`. New files never
  conflict.
- **A countable set of upstream files knows we exist**, and it is counted
  rather than remembered. Part of it is the fork's identity (README, LICENSE,
  templates, workflows); the code seams are in the table below, and none of
  them grows when we add an app. `sync.sh` keeps no copy of the list: it
  computes the conflict set from git, so it cannot go stale the way the
  hand-kept list did (it sat at seven entries while the truth grew past
  twenty). Do not write the total down here either, for the same reason.
- **Before editing any other upstream file, stop and ask whether it can be done
  in `src/apps_local/` instead.** If it genuinely cannot, keep the edit as small
  and as structurally stable as possible, and add it to the table below.

The whole fork is the diff from the newest upstream commit `xteink` already
contains:

```bash
git diff --stat $(git merge-base HEAD crosspoint/develop)..HEAD
```

Keep reading that number. It is written against `merge-base` rather than
against `base` on purpose: `base` is a local branch that has to be
fast-forwarded by hand, it is not pushed to `origin`, and when it lags the
diff quietly reports upstream's own progress as though it were ours.

### The upstream files we own

The code seams, each a deliberate, commented edit:

| File                                                                                                                                                                                                                                                                                      | Why                                                                                                                                                                                                                                                                                                                                                                                                                      | Size                                                                      |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------- |
| `src/activities/home/HomeActivity.{cpp,h}`                                                                                                                                                                                                                                                | The shelf seam: Games and Apps as rows on Home, plus the `upstreamMenuRows()` count                                                                                                                                                                                                                                                                                                                                      | 4 hooks + 1 method                                                        |
| `src/components/themes/BaseTheme.h`                                                                                                                                                                                                                                                       | Two values appended to the `UIIcon` palette: Games, Apps                                                                                                                                                                                                                                                                                                                                                                 | 1 block, appended                                                         |
| `src/components/themes/lyra/LyraTheme.cpp`                                                                                                                                                                                                                                                | Two cases mapping them to bitmaps                                                                                                                                                                                                                                                                                                                                                                                        | 4 lines                                                                   |
| `src/activities/ActivityManager.cpp`                                                                                                                                                                                                                                                      | `Frontlight.present()` guard on the light-panel gesture (the stack `#ifndef` seam retired: upstream adopted it in 1.6.0rc); plus `noteSurfaceBuilt()` around the render dispatch, so no screen can forget to stamp its play surface                                                                                                                                                                                      | 1 guard + 1 line                                                          |
| `src/MappedInputManager.{h,cpp}`                                                                                                                                                                                                                                                          | `swallowCurrentTouch()`: one-line wrapper over the SDK suppression latch, for apps that time their own holds (Minesweeper's flag)                                                                                                                                                                                                                                                                                        | 1 method                                                                  |
| `src/network/OtaUpdater.cpp`                                                                                                                                                                                                                                                              | Release URL repointed at `ma-r-s/crossplay`: pointing at upstream would flash a C3 build onto an S3                                                                                                                                                                                                                                                                                                                      | 1 URL + comment                                                           |
| `lib/hal/HalStorage.{h,cpp}`                                                                                                                                                                                                                                                              | `openFileForAppend()`: `openFileForWrite` carries `O_TRUNC`, so nothing could add to an existing file                                                                                                                                                                                                                                                                                                                    | 1 method                                                                  |
| `lib/GfxRenderer/GfxRenderer.cpp`                                                                                                                                                                                                                                                         | Thick lines thicken across their direction, not always downward: vertical paths drew 1px; plus `paintclock::notePainted()` at the five places pixels reach the panel                                                                                                                                                                                                                                                     | 1 fix + 5 one-liners                                                      |
| `lib/GfxRenderer/PaintClock.h`                                                                                                                                                                                                                                                            | New file. A count of completed paints, and the `RevealGate` latch built on it, so a tap is never routed against a screen the panel has not shown yet; see `src/apps_local/ui/ToyboxScreen.h`. Lives beside the renderer because the renderer is what knows the fact, and is freestanding so the toybox headers stay host-testable                                                                                        | new file                                                                  |
| `src/components/UiAppHost.{h,cpp}`                                                                                                                                                                                                                                                        | The `uiReady` handshake waits for the panel, not just for the rebuild: `resetUi()` arms a `RevealGate` and routing stays closed until one paint lands. Screen entry only, so ordinary repaints still route                                                                                                                                                                                                               | 3 lines + 1 member                                                        |
| `lib/GfxRenderer/RevealedInteractions.h`                                                                                                                                                                                                                                                  | New file. `RevealedInteractions<N>` (the table digest, lifted out of `ToyboxScreen.h` and made capacity-generic so the 17- and 48-slot components can use it) and `SurfaceGate` (the same rule for a board hit-tested against geometry, which never reaches `route()`)                                                                                                                                                   | new file                                                                  |
| `src/activities/Activity.h`                                                                                                                                                                                                                                                               | `surfaceMeaning()` / `surfaceRevealed()` / `noteSurfaceBuilt()` and a `SurfaceGate` member: the opt-in every geometry-hit-tested play surface inherits, so the games that hit-test a play surface do not each grow their own                                                                                                                                                                                             | 3 methods + 1 member                                                      |
| `src/components/OptionPopup.h`                                                                                                                                                                                                                                                            | Holds a `RevealedInteractions` rather than a raw `InteractionBuffer`, and asks the gate BEFORE routing so a suppressed tap cannot fall through to the dismiss-on-outside-tap branch                                                                                                                                                                                                                                      | 3 lines + 1 type                                                          |
| `src/activities/util/KeyboardEntryActivity.{h,cpp}`                                                                                                                                                                                                                                       | A `RevealGate` armed on screen entry, so a tap during the first paint belongs to the screen that pushed the keyboard rather than to a key. Entry only, deliberately: digesting this table would gate every layer change, and shift swaps the whole layout                                                                                                                                                                | 3 lines + 1 member                                                        |
| `lib/PngToBmpConverter/PngToBmpConverter.*`                                                                                                                                                                                                                                               | `...FitWithin()`: contain, not cover, for bounding downloaded images                                                                                                                                                                                                                                                                                                                                                     | 1 method                                                                  |
| `lib/KOReaderSync/KOReaderCredentialStore.cpp`                                                                                                                                                                                                                                            | A comment saying out loud that sync stays on upstream's server, and why                                                                                                                                                                                                                                                                                                                                                  | comment only                                                              |
| `src/components/UITheme.cpp`                                                                                                                                                                                                                                                              | `getScreenSafeArea()` starts from the bezel's viewable insets, so upstream screens stop drawing under the glass; see `docs/bezel-insets.md`                                                                                                                                                                                                                                                                              | 1 block                                                                   |
| `src/main.cpp`, `src/CrossPointSettings.h`, `src/SettingsList.h`, `src/activities/settings/OtaUpdateActivity.cpp`, `src/activities/settings/SdFirmwareUpdateActivity.cpp`, `src/network/HttpDownloader.cpp`, `src/apps_local/bridge/BridgeHttp.cpp`, `src/apps_local/study/StudySync.cpp` | The device report's hooks: `devreport::begin()` after the settings load, one settings field and its row, the install attempt/failure notes on both install screens (the SD one is the OTA one's twin), and `devreport::headersFor()`/`delivered()` around each request in the three transports that reach CrossPlay's own hosts; the module itself is `src/network/DeviceReport*.{h,cpp}`. See `docs/workflow/events.md` | 1 call + 1 field + 1 row + the notes on 2 screens + 2 calls per transport |
| `.gitignore`                                                                                                                                                                                                                                                                              | Ignore `qa-artifacts/` and the simulator's SD cards                                                                                                                                                                                                                                                                                                                                                                      | 3 lines, append-only                                                      |
| `platformio.ini`                                                                                                                                                                                                                                                                          | One `extra_configs` line pulling in `platformio.sim.ini`                                                                                                                                                                                                                                                                                                                                                                 | 1 line                                                                    |
| `AGENTS.md` (= `CLAUDE.md`)                                                                                                                                                                                                                                                               | The read-this-first banner pointing here, so agents find the fork rules                                                                                                                                                                                                                                                                                                                                                  | ~20 lines                                                                 |
| `SCOPE.md`                                                                                                                                                                                                                                                                                | One-line pointer here; it is the file that says "no games"                                                                                                                                                                                                                                                                                                                                                               | 2 lines                                                                   |

The rest are identity, not seams: `README.md`, `LICENSE`, `.github/`
templates, funding and workflows, `.skills/README.md`. They are this fork's
front matter and merge trivially or not at all. `GOVERNANCE.md` and
`ROADMAP.md` are not on that list because the fork does not carry them at all:
c8360519 deleted both rather than owning them, upstream's governance file
having routed harassment reports to a maintainer who never agreed to receive
them.

None of the seams grows when an app is added -- the two theme edits are per
_folder_, and there are two folders. Both are appends: values at the end of an
enum keep every number above them, and a case in a switch merges as an
addition. `HomeActivity.cpp`'s hooks likewise append after upstream's rows, so
their indices never shift.

Three deliberate near-misses, worth knowing so nobody "fixes" them:

- **`platformio.sim.ini`, not `platformio.local.ini`.** Upstream's `.gitignore`
  reserves `*.local*` for personal overrides. The simulator is not personal:
  every checkout needs it. Naming it differently keeps it tracked without
  touching an ignore rule.
- **Screenshots go to `qa-artifacts/`, which costs the one ignore line.** They
  could have gone to `$TMPDIR` for free. They did not, because a directory you
  will actually open beats a number in this table.
- **We never edit `src/activities/apps/`.** It does not exist upstream, and
  recreating CrossPoint's old Apps menu is how the previous base ended up with a
  junk drawer.

## CrossPlay's settings sort below CrossPoint's

A reader looking for a CrossPoint setting must never scroll past one of ours to
reach it. Ours go at the bottom of their category, under every upstream row.

Declaration order is not render order, and that is what made this invisible.
`SettingsList.h` declared the fork's two System rows last, with a comment saying
so, while `SettingsActivity::rebuildSettingsLists()` appended eight upstream
ACTION rows (WiFi, KOReader, OPDS, Clear cache, Check for updates, SD update,
Language, Keyboards) after building that list. Dev Mode rendered 6th of 14,
above WiFi and Language, for as long as those actions had existed. The comment
asserting a position the declaration does not decide was the tell.

`host-tests/settingsorder/` holds the line. It reads the append order out of the
activity rather than the declaration, and decides which rows are ours by asking
`crosspoint/develop` rather than naming them, so it still means something after
the third fork setting is added.

## The shelf

Home gains two rows, **Games** and **Apps**, as siblings. Each opens a folder.
That is the entire hierarchy, and it is capped at two levels by the type system
rather than by discipline.

Read [docs/shelf.md](docs/shelf.md) before adding anything. The short version:

- A folder holds items, never folders.
- Back has two rules: an app returns to its folder, a folder returns to Home.
- **No app names its own destination.** It calls `shelf::leave()`.

## Deliberate deviations from upstream's rules

All scoped to `src/apps_local/`.

| Upstream rule                                    | What we do in local apps                                  | Why                                                                                                                                                                                                                                                        |
| ------------------------------------------------ | --------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| All user-facing text uses `tr()`                 | Raw `const char*` titles and strings                      | Routing through `tr()` means editing `lib/I18n/translations/*.yaml` per app, which is per-app churn in an upstream file                                                                                                                                    |
| Icons are `UIIcon` enum variants added per app   | Shelf items carry a `freeink::Icon` generated from Lucide | An asset the shelf resolves gives us Lucide's 1735 icons instead of an enum that grows per app. The two _folder_ rows are the exception: upstream's Home menu accepts only a `UIIcon`, so Games and Apps are appended to that palette once and never again |
| Apps register via `ActivityManager::goTo<App>()` | A function-pointer factory in the shelf                   | Avoids editing `ActivityManager.{h,cpp}` per app                                                                                                                                                                                                           |
| All rendering through the `GUI`/UITheme macro    | Apps draw their own surface via FreeInkUI                 | A board or a grid is the app's own material; chrome still goes through Toybox, which is a FreeInkUI theme                                                                                                                                                  |

Everything else still applies: the resource protocol, `makeUniqueNoThrow`,
HAL-only access, no hardcoded screen dimensions, free in `onExit()` what you
allocate in `onEnter()`.

## Host tests live in `host-tests/`, never under `src/`

PlatformIO's build filter is `+<*>`, so **every file under `src/` is compiled
into the firmware, on every environment**. A test file with its own `main()`
placed under `src/` links into the firmware and replaces the real one.

So pure-logic modules stay in `src/apps_local/<app>/`, and their host tests live
in `host-tests/<app>/`. Keeping app logic freestanding (no Arduino, no renderer,
no heap) is what makes that split possible, and it is the only way to get real
coverage without a device.

```bash
./scripts_local/check.sh          # every suite, both builds
./scripts_local/check.sh --tests  # suites only, fast
```

**Every suite green is the only green; nothing here is a known failure.** A
suite that is allowed to stay red stops being read, and then so do the ones
beside it.

(Inside a worktree always call `./scripts_local/`; the workspace-root
`./scripts/` symlinks resolve back to the integration tree.)

Each suite builds into a directory keyed to **this checkout**, not just the
suite name. Two worktrees once shared one build dir, and a suite whose source
was not even present reported 52 green checks against the other tree's binary.

The suites are built by CI on Linux as well as here, so they have to compile
under GCC and not only Apple clang. What that costs, and what is still papered
over, is in [docs/open-items.md](docs/open-items.md).

## Branches

- **`base`** is a pure mirror of the upstream branch. Never commit here.
  Fast-forward only.
- **`xteink`** is the integration branch. `base` merges into it, never the
  reverse.

Building `base` gives a clean upstream binary for answering "is this bug mine
or theirs". It is only as good as its last fast-forward: `base` lives on this
machine and not on `origin`, so check it is current before trusting a diff
against it.

```bash
./scripts_local/sync.sh           # report what changed upstream, change nothing
./scripts_local/sync.sh --apply   # merge and verify in a trial worktree, then land
```

### The base branch died, and this fork inherited it

Until 2026-08-14 `base` tracked **`crosspoint/feat-touch-ui`**: the X4 Pro
beta branch, the only place X4 Pro and touch support lived. Upstream deleted
that branch without merging it. Its content is not in `develop`, not in
`master`, not anywhere upstream -- `xteink` is its only living continuation,
and our pushed copy survives as `origin/feat-touch-ui`. The touch layer this
fork's apps sit on is therefore ours to carry now, not a passthrough.

`base` now tracks **`crosspoint/develop`**, the durable trunk, re-founded at
`v1.5.0` (`e00f5958`): the last develop commit fully contained in `xteink`.
That keeps both invariants true -- `base` is always an ancestor of `xteink`,
and `sync.sh`'s behind-count is the truth.

Upstream then **reimplemented** X4 Pro + touch support on
`feat-x4-papermono-support` and landed it in `develop` as one squashed commit
(#2983), released as `1.6.0rc` on 2026-08-17. The foreseen sit-down merge
happened on 2026-08-25: 85 conflicted files, resolved by a simple rule --
upstream's side wins everywhere it reimplemented the inherited touch layer
(reader, settings, boot flow, gestures, dark mode), and the fork's authored
seams were re-applied on top (the table above). Two seams retired in the
process: upstream adopted the `CROSSPOINT_RENDER_TASK_STACK` `#ifndef` and
grew an SDK-level touch-suppression latch that replaced the fork's
swallow machinery (now a one-line wrapper kept for Minesweeper's flag hold).

After that merge `base` fast-forwards along `crosspoint/develop` as before;
the next syncs should be routine again until upstream's next big line.

## Why this base at all

An earlier incarnation of this fork sat on a different CrossPoint fork, picked
because it was the only app-capable one with a desktop simulator. That reason
expired: the simulator is now CrossPoint's own, and the X4 Pro environment
always was. Basing on CrossPoint directly costs nothing we were using and drops
a quarter of a million lines of reading features this fork does not touch.
