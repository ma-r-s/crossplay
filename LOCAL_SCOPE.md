# Fork Scope

This is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
Target device: **Xteink X4 Pro** (ESP32-S3, 480x800 touch panel, frontlight).

**This file overrides [SCOPE.md](SCOPE.md) where the two disagree.** SCOPE.md is
upstream's document and is kept verbatim so it merges cleanly. It says
interactive apps and games are out of scope. For this fork they are the point.

## What this fork is for

Reading stays the primary purpose of the device, and the reading experience is
upstream's to own. We do not fork it, improve it, or diverge from it. Anything
that makes reading better belongs upstream, so send it there.

What we add is **games and small tools that make the device worth carrying
instead of a phone**. Four games are here now; a spaced-repetition trainer and a
Hacker News reader are next.

## The one rule that keeps this sustainable

Upstream moves fast and we want its work. Every change we make is measured by
how much upstream-owned code it touches.

- **Everything we add lives in new files**: `src/apps_local/`, `host-tests/`,
  `scripts_local/`, `tools_local/`, and our own `docs/`. New files never
  conflict.
- **Seven upstream files know we exist**, most of them pointers or one-liners.
  The list is below, and none of them grows when we add an app.
- **Before editing any other upstream file, stop and ask whether it can be done
  in `src/apps_local/` instead.** If it genuinely cannot, keep the edit as small
  and as structurally stable as possible, and add it to the table below and to
  the `OWNED` list in `scripts_local/sync.sh`.

`git diff base..xteink` is the whole fork. Keep reading that number.

### The upstream files we own

| File                                   | Why                                                       | Size                 |
| -------------------------------------- | --------------------------------------------------------- | -------------------- |
| `src/activities/home/HomeActivity.cpp` | The shelf seam: Games and Apps as rows on Home            | 4 hooks, fixed       |
| `src/components/themes/BaseTheme.h`    | Two values appended to the `UIIcon` palette: Games, Apps  | 1 block, appended    |
| `src/components/themes/lyra/LyraTheme.cpp` | Two cases mapping them to bitmaps                     | 4 lines              |
| `.skills/SKILL.md` (= `CLAUDE.md`)     | Four-line pointer here, so agents find the fork rules     | 4 lines              |
| `.gitignore`                           | Ignore `qa-artifacts/` and the simulator's SD cards       | 3 lines, append-only |
| `platformio.ini`                       | One `extra_configs` line pulling in `platformio.sim.ini`  | 1 line               |
| `SCOPE.md`                             | One-line pointer here; it is the file that says "no games" | 2 lines              |

None of them grows when an app is added -- the two theme edits are per *folder*,
and there are two folders. Both are appends: values at the end of an enum keep
every number above them, and a case in a switch merges as an addition.
`HomeActivity.cpp`'s hooks likewise append after upstream's rows, so their
indices never shift.

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

| Upstream rule                                    | What we do in local apps                  | Why                                                                                                                     |
| ------------------------------------------------ | ----------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| All user-facing text uses `tr()`                 | Raw `const char*` titles and strings      | Routing through `tr()` means editing `lib/I18n/translations/*.yaml` per app, which is per-app churn in an upstream file |
| Icons are `UIIcon` enum variants added per app   | Shelf items carry a `freeink::Icon` generated from Lucide | An asset the shelf resolves gives us Lucide's 1735 icons instead of an enum that grows per app. The two *folder* rows are the exception: upstream's Home menu accepts only a `UIIcon`, so Games and Apps are appended to that palette once and never again |
| Apps register via `ActivityManager::goTo<App>()` | A function-pointer factory in the shelf   | Avoids editing `ActivityManager.{h,cpp}` per app                                                                        |
| All rendering through the `GUI`/UITheme macro    | Apps draw their own surface via FreeInkUI | A board or a grid is the app's own material; chrome still goes through Toybox, which is a FreeInkUI theme               |

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
./scripts/check.sh          # every suite, both builds
./scripts/check.sh --tests  # suites only, fast
```

Each suite builds into a directory keyed to **this checkout**, not just the
suite name. Two worktrees once shared one build dir, and a suite whose source
was not even present reported 52 green checks against the other tree's binary.

## Branches

- **`base`** is a pure mirror of the upstream branch. Never commit here.
  Fast-forward only.
- **`xteink`** is the integration branch. `base` merges into it, never the
  reverse.

Because `base` is a pure mirror, `git diff base..xteink` is exactly what this
fork owns, and building `base` gives a clean upstream binary for answering "is
this bug mine or theirs".

```bash
./scripts/sync.sh           # report what changed upstream, change nothing
./scripts/sync.sh --apply   # merge and verify in a trial worktree, then land
```

### The base is an unmerged upstream branch, on purpose

`base` tracks **`crosspoint/feat-touch-ui`**, not `develop`. X4 Pro support is
not in `develop`; `feat-touch-ui` is where it lives and where CrossPoint's public
X4 Pro betas are built from. It merges `develop` into itself every few days, so
it does not drift.

The one failure mode is that it could be squash-merged and deleted. `sync.sh`
checks for exactly that on every run and tells you to re-point `base` at
`develop`. If you see that message, do it there and here, and confirm `develop`
still builds `-e x4pro` before syncing.

## Why this base at all

The previous incarnation of this fork sat on CrossMux, chosen because it was the
only app-capable CrossPoint fork with a desktop simulator. That reason expired:
the simulator is now CrossPoint's own, and the X4 Pro environment always was.
[docs/crosspoint-migration.md](docs/crosspoint-migration.md) is the full record,
including what was measured and what was verified rather than assumed.
