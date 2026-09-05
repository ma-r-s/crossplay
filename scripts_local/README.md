# Workspace scripts

The live copies. `../../scripts/*.sh` at the workspace root are symlinks to
these, so `./scripts/dev.sh` and `firmware-next/scripts_local/dev.sh` both work.

They live here because the workspace root is not a git repository and this
directory is; keeping them outside meant the whole development loop was one `rm`
from gone.

| Script                   | What it does                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `wt.sh`                  | One worktree per piece of work in flight. `new <name>` makes one (own branch, own build output, own SD card, own screenshots); `list` shows what exists and what state; `drop <name>` removes it, refusing while work is unmerged. Cuts from `origin/xteink`, never the local branch, so a tree made during a release train does not inherit that train's unpushed commits; `--from` overrides.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `check.sh`               | Everything verifiable without a device: every host suite, then both builds. Run before every commit. `--tests` for suites only. `--committed` verifies HEAD in a throwaway worktree instead of your working tree. Reports each suite's own exit code, not just its last line. **Read its verdict with `grep -o 'CHECKSH-VERDICT: [a-z-]*'`, never `tail -1` or `$?`**: it prints one token line (`green`, `host-green-device-skipped`, `withheld`, `failed`) that no appended wrapper line can defeat, and an absent token means the run never reached a verdict. Its first line is `transcript: <path>`, a `mktemp` name no other process can choose, so a backgrounded run is followed there rather than into an invented name in the shared scratchpad (cards #314, #317). On the deploy branch it also fails when `site/emulator/` is older than the sources it was built from; `host-tests/checksh/` is that gate's own test. Skips the device builds when nothing in the diff can reach a device image (`device-build-needed.sh`), and then says `HOST GREEN, DEVICE BUILDS SKIPPED` rather than `all green`; force them with `CHECK_FORCE_DEVICE_BUILDS=1`. **`--tests` reports that same verdict**, because its token is read by somebody who never saw the command line and `green` from a run that compiled nothing claims ground it never covered; the parenthetical says which of the two skips it was. Two device envs, never four: `--committed` swaps the dev pair (`x4pro`, `sticky`) for the release pair (`gh_release_x4pro`, `gh_release_sticky`) rather than adding to it, and builds both of them in ONE `pio run`, because PlatformIO wipes `.pio/build` once per invocation and a second invocation deletes the first one's output. Drops its own per-tree log dir on a green verdict and sweeps siblings no running gate could still own (`log-sweep.sh`), so the log dirs cannot pile up in `TMPDIR`. `--committed` carries that dir into the throwaway worktree (`CHECK_OUTER_LOGS`) instead of letting the trial run key one off its own per-pid path: without it every `--committed` run left a ~38MB dir no later run could reuse, and a failed run's logs now sit at the same predictable path for that tree every time. The cmake BUILD directory and log are keyed per RUN inside it (`cmake-build.$$`), because sharing them per tree meant one non-green run poisoned every later `--committed` run of that tree with a cache naming a deleted trial worktree -- printing `ctest FAILED (0s)` with no error lines, which reads as your own diff (card #320). |
| `whose-gate.sh`          | Which tree does each running build belong to? Every session runs an identically named `check.sh` from an identically named relative path, so `pgrep -f` returns pids nobody can attribute -- it once returned four across three worktrees and an agent nearly killed two siblings' gates. Resolves each pid's working directory instead, and follows a `--committed` run's throwaway worktree back to the tree that owns it. `--mine` lists only this tree's. |
| `log-sweep.sh`           | Sourced by `check.sh`; keeps its per-tree log directories (`$TMPDIR/xteink-check-*`) from accumulating. `--status` shows how many are there and how many a sweep would take; `--prune [base] [hours]` removes those whose whole subtree has been idle longer than the window (default 24h, far longer than any run), so a live gate's dir is never touched. `host-tests/logsweep` is its suite.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `device-build-needed.sh` | The one classification table: every path prefix carries two independent attributes, `builds` (can this change what a device build produces?) and `ships` (can a person receive something different?). Default mode answers `builds` for `check.sh`; `--ships` answers the other for `release-needed.sh` and `release_notes.py` and has three values (`no`; `yes`, a change in the thing a person uses; `quiet`, a change only in how the release is packaged, which cuts a release but earns a line only if the pull request wrote one); `--device-only` answers the mirror question, whether the host gate can see the change at all. The two columns exist because their risk profiles are opposite -- a wrong "build" costs runner minutes, a wrong "release" puts an update prompt on every device in the field -- and while one predicate answered both, `.gitignore` cut v1.12.21 and a fix to `crossplay-release.yml` was invisible to both. A path in no row is not guessed at: the build runs and says so, `--ships` exits 2 naming the path. `host-tests/gatepath` is its suite.                                                                                    |
| `release-needed.sh`      | Should a green merge into `xteink` become a release? Asks `device-build-needed.sh --ships` about the commits since the newest `v*` tag. Exit 0 release, 1 no, 2 refused (an unclassified path -- the autorelease job goes red on it). `quiet` releases like `yes`; the difference is a question about the notes, read in `release_notes.py`. Prints the path that decided, because the job log is the only place anybody can later ask why.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `release_notes.py`       | Writes the next release: bumps `[crossplay] version`, replaces the `### What is new in <version>` block in `docs/release-body.md` (what a tag publishes) and prepends the same block to `docs/release-notes.md` (the history). Only landings that pass `--ships` become lines; the rest are named in the job log, never on the page. `host-tests/autorelease` is its suite.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `dev.sh`                 | Mario's simulator. Watches the sources, rebuilds and restarts on change, reopens a closed window. Leave it running. Takes a worktree name (`dev.sh battleship`) to watch just that work; no argument watches the integration tree.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `sim-shot.sh`            | Scripted headless run for agents: drives taps and keys, captures screenshots, prints the activity trace. Set `SIM_LOG_GREP` to widen it (`SIM_LOG_GREP=.` for everything); the default hides `LOG_INF`/`LOG_DBG`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| `mutate.sh`              | One mutation test, honestly. Distinguishes CAUGHT from SURVIVED from BUILD-FAIL from NO-MATCH, because the last two look exactly like the first two and were read as results three times in one session. Always restores the file.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `sim-link.sh`            | Two simulators at once, for local multiplayer. No args gives two interactive windows; with args, headless with `-a`/`-b` screenshot suffixes.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `sim.sh`                 | One-shot interactive launch. Prefer `dev.sh`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `sync.sh`                | Reports how far behind CrossPoint's `develop` we are, computes (never lists) the files both sides changed to warn where the merge will conflict, and watches upstream's X4 Pro branch, whose landing is a sit-down merge (see LOCAL_SCOPE.md). `--apply` merges and verifies in a throwaway worktree at the committed tip, then lands as a fast-forward. Works with a dirty tree unless upstream touched a file you have uncommitted work in.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| `sim_catchup.py`         | Not run by hand. A `pre:` build hook that patches the fetched simulator library where it lags this branch. A patch whose anchor is gone now FAILS the build by name (`require_all_applied`): it is either landed upstream and should be deleted, or its anchor drifted and the build is about to break somewhere unrelated. `host-tests/simcatchup` is its suite.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| `stack-budget.sh`        | Proves every FreeRTOS task's deepest call path fits the stack it was given, from the `.su`/`.ci` files the firmware compiler emits. `--verbose` prints the deepest path. `stack_budget.py` is the implementation; CI's "Stack fits its task" step calls it directly.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| `shoot-board.sh`         | Regenerates the Toy Battle board shot the site card uses. Every site image was once captured by hand and its recipe lost; these two keep theirs.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `shoot-shell.sh`         | Photographs the shell end to end: menu, setup, map list, rules pages.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| `lib-sim.sh`             | Not run directly. Shared setup sourced by `dev.sh`, `sim.sh`, `sim-shot.sh` and `sim-link.sh`; derives every path from the tree it lives in.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| `sim_host_libs.py`       | Not run by hand. Links the host libraries the simulator's own headers assume.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |

## One tree per piece of work

Several apps get built at once, so several trees exist at once: `firmware-next/`
integrates, and each `wt/<name>/` is one effort's own worktree. Every path these
scripts use is derived from the tree they were invoked in -- build output, build
lock, build log, SD card, `qa-artifacts/` -- so two trees never collide.

Two consequences worth knowing:

- **Inside a `wt/` tree, use `./scripts_local/`, not the workspace-root
  `./scripts/`.** Those symlinks resolve back to `firmware-next` from anywhere,
  so the root copy would build and photograph the integration tree while you
  believed you were testing your own. It boots fine and every tap lands
  somewhere else, which is indistinguishable from the feature being broken. The
  scripts refuse rather than let that happen quietly.
- **The PlatformIO object cache is shared** at `../.pio-cache` via
  `PLATFORMIO_BUILD_CACHE_DIR`. It is 7.6GB and content-addressed, so a brand
  new worktree's first build is mostly cache hits instead of a cold compile.

### What "integration" actually means

`firmware-next/` is for the merge and what the merge forces, and nothing else:

- `git merge app/<name>` and resolving its conflicts;
- rebuilding `site/emulator/` from the merged tree, because both sides' binaries
  conflict and neither of them is right;
- the version bump and the release tag.

Everything else is work and wants its own tree, **including the things that feel
like plumbing**: a new script, a new `check.sh` stage, a build fix, a rewritten
doc. If you are AUTHORING rather than reconciling, you are in the wrong tree.

#### The release itself, in order

1. `check.sh --committed` green on the merged tree.
2. Rebuild `site/emulator/`.
3. Bump `version` under **`[crossplay]`** in `platformio.ini`. There are two
   `version` keys in that file; the other one is upstream's.
4. **Rewrite the `### What is new in <version>` section inside
   `.github/workflows/crossplay-release.yml`, and change the heading to the
   version you are about to tag.** The notes live in the workflow, not in a
   notes file, which is why v1.6.2 published v1.6.1's text word for word to
   announce a multiplayer fix it never mentioned. Say what is new; do not
   re-announce what the last release already told people.
5. Tag `v<version>` and push. The workflow's first step refuses a tag that does
   not match the version in `platformio.ini`.

`host-tests/release` enforces 3, 4 and 5 -- including that the notes are not
byte-identical to the previous tag's below the heading, because renaming the
heading and leaving the bullets is the same failure with one line of editing.
It stays quiet when no bump is pending, so a green run before you start is not
evidence the notes are written.

This needs saying because "it is only integration" is an easy thing to tell
yourself. On 2026-08-14 it produced eight direct commits on `xteink` -- a
screenshot recipe, a `check.sh` stage, two build fixes -- none of which were the
merge or forced by it.

**Two sessions in this tree at once is the normal case, not the exception.**
Before touching it:

```bash
git status --short --ignore-submodules=untracked  # dirty means someone is mid-merge
git log --first-parent origin/xteink..HEAD        # unpushed means the same
```

(`--ignore-submodules=untracked` because the icon tools drop a `__pycache__/`
inside `freeink-sdk`, which otherwise reads as permanent dirt.)

Either one means leave it alone. The cost is not hypothetical: that same day two
windows independently diagnosed the same simulator build failure, because both
were working in the shared tree and neither could see the other coming.

**Announce before you integrate.** The checks above catch a session that
already started; they cannot catch one about to. Before merging into `xteink`,
bumping the version, tagging, or rebuilding the emulator, announce the claim to
the other sessions (ListAgents, then SendMessage) and wait one round for
objections. One session owns a tag from bump to push; everyone else queues
behind the tag they can see on origin. Three near-collisions on 2026-08-25 (a
double integration, a queued site deploy, two simultaneous emulator rebuilds)
were each resolved by exactly this message, sent after the fact instead of
before.

## Desk devices, and Developer Mode

Identify a unit by MAC, never by port name -- `/dev/cu.usbmodem*` numbers track
port position and swap across sleep/wake, and on 2026-08-27 that cost one
session another's build. The cheapest probe opens nothing and resets nothing:

```bash
ioreg -r -c IOUSBHostDevice -l | awk '/USB Serial Number/{s=$NF} /IODialinDevice/{print s,$NF}'
```

Fall back to `tools_local/device/drive.py PING` (dev builds only, and it proves
the app is alive), and to `esptool read-mac` last -- that one reboots the device.
No `/dev/cu.usbmodem*` at all means asleep, not broken.

**After the first flash a device needs no cable.** Settings > System >
Developer Mode shows an address and six digits:

```bash
./scripts_local/wifi-flash.sh --pair 123456   # once per dev-mode session
./scripts_local/wifi-flash.sh                 # every flash after that
./scripts_local/wifi-flash.sh --disable       # close the device again
```

It is a runtime setting present in every build including releases, so flashing a
release does NOT turn it off -- the setting lives on the SD card. While it is on
the device will not deep-sleep and accepts firmware from anyone who pairs, so
close it with `--disable` before the device leaves your network.

A device in a multiplayer match is off Wi-Fi and cannot be flashed: the link
takes the radio outright, because ESP-NOW is pinned to channel 1 and an AP
association pins the radio to the router's. Leave the game and dev mode is back
in a few seconds. Until `app/linkradio` it did not yield at all, and multiplayer
died one move into every game. Full detail in `docs/developer-mode.md`.

Each simulator instance gets its own SD card via `CROSSPOINT_SIM_SD`: each
tree's own `fs_agent/` for scripted runs, and `../fs_mario/` at the workspace
root for Mario's, which sits outside every tree so his saves and settings follow
him whichever one `dev.sh` is watching. All are gitignored, as is
`qa-artifacts/` where screenshots land.

## `hooks/`: the rules that refuse instead of remind

`hooks/guard.py` is wired from the workspace root's `.claude/settings.json`
(outside git) and is inert until `<workspace>/.board/enabled` exists. It
refuses edits in the integration tree from anyone but the integration claim
holder, raw `pio run`, messages to any session but the orchestrator, and a
worker's turn that ends by handing a question back to Mario without a blocker
on its card. `docs/workflow/README.md` has the whole picture;
`host-tests/bugflow/` drives every branch of it.
