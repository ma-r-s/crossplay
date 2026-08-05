# Workspace scripts

The live copies. `../../scripts/*.sh` at the workspace root are symlinks to
these, so `./scripts/dev.sh` and `firmware-next/scripts_local/dev.sh` both work.

They live here because the workspace root is not a git repository and this
directory is; keeping them outside meant the whole development loop was one `rm`
from gone.

| Script          | What it does                                                                                                                                                                                                             |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `check.sh`      | Everything verifiable without a device: every host suite, then both builds. Run before every commit. `--tests` for suites only. Reports each suite's own exit code, not just its last line.                              |
| `dev.sh`        | Mario's simulator. Watches the sources, rebuilds and restarts on change, reopens a closed window. Leave it running.                                                                                                      |
| `sim-shot.sh`   | Scripted headless run for agents: drives taps and keys, captures screenshots, prints the activity trace. Set `SIM_LOG_GREP` to widen it (`SIM_LOG_GREP=.` for everything); the default hides `LOG_INF`/`LOG_DBG`.        |
| `sim-link.sh`   | Two simulators at once, for local multiplayer. No args gives two interactive windows; with args, headless with `-a`/`-b` screenshot suffixes.                                                                            |
| `sim.sh`        | One-shot interactive launch. Prefer `dev.sh`.                                                                                                                                                                            |
| `sync.sh`       | Reports how far behind CrossPoint we are, warns when upstream touched a file this fork also modifies, and checks whether the branch we are based on has been merged away. `--apply` syncs, then runs `check.sh`.         |
| `sim_catchup.py`| Not run by hand. A `pre:` build hook that patches the fetched simulator library where it lags this branch. Prints when a patch stops applying, which is how we learn CrossPoint has fixed it.                            |

Each simulator instance gets its own SD card via `CROSSPOINT_SIM_SD`
(`fs_mario/` and `fs_agent/`), so agent test runs cannot disturb a game in
progress. Both are gitignored, as is `qa-artifacts/` where screenshots land.
