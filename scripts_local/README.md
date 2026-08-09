# Workspace scripts

The live copies. `../../scripts/*.sh` at the workspace root are symlinks to
these, so `./scripts/dev.sh` and `firmware-next/scripts_local/dev.sh` both work.

They live here because the workspace root is not a git repository and this
directory is; keeping them outside meant the whole development loop was one `rm`
from gone.

| Script           | What it does                                                                                                                                                                                                                                                                                                                                                                                                                       |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `wt.sh`          | One worktree per piece of work in flight. `new <name>` makes one (own branch, own build output, own SD card, own screenshots); `list` shows what exists and what state; `drop <name>` removes it, refusing while work is unmerged.                                                                                                                                                                                                 |
| `check.sh`       | Everything verifiable without a device: every host suite, then both builds. Run before every commit. `--tests` for suites only. `--committed` verifies HEAD in a throwaway worktree instead of your working tree. Reports each suite's own exit code, not just its last line. On the deploy branch it also fails when `site/emulator/` is older than the sources it was built from; `host-tests/checksh/` is that gate's own test. |
| `dev.sh`         | Mario's simulator. Watches the sources, rebuilds and restarts on change, reopens a closed window. Leave it running. Takes a worktree name (`dev.sh battleship`) to watch just that work; no argument watches the integration tree.                                                                                                                                                                                                 |
| `sim-shot.sh`    | Scripted headless run for agents: drives taps and keys, captures screenshots, prints the activity trace. Set `SIM_LOG_GREP` to widen it (`SIM_LOG_GREP=.` for everything); the default hides `LOG_INF`/`LOG_DBG`.                                                                                                                                                                                                                  |
| `mutate.sh`      | One mutation test, honestly. Distinguishes CAUGHT from SURVIVED from BUILD-FAIL from NO-MATCH, because the last two look exactly like the first two and were read as results three times in one session. Always restores the file.                                                                                                                                                                                                 |
| `sim-link.sh`    | Two simulators at once, for local multiplayer. No args gives two interactive windows; with args, headless with `-a`/`-b` screenshot suffixes.                                                                                                                                                                                                                                                                                      |
| `sim.sh`         | One-shot interactive launch. Prefer `dev.sh`.                                                                                                                                                                                                                                                                                                                                                                                      |
| `sync.sh`        | Reports how far behind CrossPoint we are, warns when upstream touched a file this fork also modifies, and checks whether the branch we are based on has been merged away. `--apply` merges and verifies in a throwaway worktree at the committed tip, then lands as a fast-forward. Works with a dirty tree unless upstream touched a file you have uncommitted work in.                                                           |
| `sim_catchup.py` | Not run by hand. A `pre:` build hook that patches the fetched simulator library where it lags this branch. Prints when a patch stops applying, which is how we learn CrossPoint has fixed it.                                                                                                                                                                                                                                      |

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

Each simulator instance gets its own SD card via `CROSSPOINT_SIM_SD`: each
tree's own `fs_agent/` for scripted runs, and `../fs_mario/` at the workspace
root for Mario's, which sits outside every tree so his saves and settings follow
him whichever one `dev.sh` is watching. All are gitignored, as is
`qa-artifacts/` where screenshots land.
