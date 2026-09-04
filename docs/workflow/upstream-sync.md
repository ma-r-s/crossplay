# Upstream sync: CrossPoint's develop, every day, without a person

CrossPoint (`crosspoint-reader/crosspoint-reader`, remote `crosspoint`) is
the base this fork tracks. `scripts_local/sync.sh` already reports how far
behind `develop` we are and merges in a throwaway worktree; what was missing
was a session to run it daily and resolve what it cannot. This is that
session's runbook. It runs as a cloud routine on a schedule, or by hand.

1. **Fetch and measure.** `git fetch crosspoint develop`; `git log --oneline
origin/xteink..crosspoint/develop | wc -l`. Zero: post nothing, stop.
2. **Branch.** `sync/upstream-<YYYYMMDD>` from `origin/xteink`. Never on
   `xteink` itself and never in `firmware-next`.
3. **Merge** `crosspoint/develop`. Conflicts are resolved by these rules, in
   order, and nothing else:
   - Files listed in `LOCAL_SCOPE.md` as fork-owned keep the fork's side.
   - `CLAUDE.md`, `SCOPE.md`, `README.md` and upstream's docs take upstream's
     side; fork-specific notes in them live in `LOCAL_SCOPE.md` or `docs/`.
   - `src/apps_local/`, `tools_local/`, `scripts_local/`, `host-tests/`,
     `site/`, `docs/workflow/`, `server/`: the fork's side, always.
   - Anything else: read both sides; if upstream fixed the same problem the
     fork already fixed differently, take upstream's and delete the fork's
     copy (see the `a-conflict-can-be-a-duplicate` memory). If they differ in
     intent, stop and file: `board new "upstream sync: <file> conflicts in
intent" --from tooling --body "<both sides, in prose>"`, leave the
     branch pushed, and end the run.
4. **Verify.** Host suites (`for s in host-tests/*/run.sh; do bash $s; done`)
   and the simulator build. A red suite that is red on `crosspoint/develop`
   too is upstream's and is noted in the pull request, not fixed here.
5. **Pull request** `sync/upstream-<date>` into `xteink`, titled `chore:
sync CrossPoint develop (<n> commits)`, body: what came in (their commit
   subjects), what was resolved and by which rule, what was not verified
   (device builds are CI's). CI gates it; the critic reviews it; it merges on
   green like any other pull request.
6. **The X4 Pro branch is not this.** Upstream's X4 Pro branch is a sit-down
   merge per `LOCAL_SCOPE.md` and stays manual.

7. **FreeInk first.** `freeink-sdk` is a submodule pinned to Mario's fork
   (`ma-r-s/freeink-sdk`), which carries the X4 Pro measured insets and
   safeArea, the frontlight RC_FAST fix and the SD free-space query; the
   upstream pull request that would have made it unnecessary was closed, so
   the fork is permanent. Before merging develop, the run merges the commit
   `crosspoint/develop` pins into the commit `origin/xteink` pins, on
   `sync/sdk-<YYYYMMDD>` in the SDK fork, pushes it, and resolves the
   submodule conflict in the crossplay merge to that commit. A conflict in
   intent inside the SDK stops the run the same way as one in crossplay.
   `.gitmodules` names a branch (`x4pro-measured-insets`) that is stale; the
   pin is what matters, and the fork's `main` is not the line.
8. **The board hears every run.** The cloud checkout has no board, so the
   old "file a card" step never ran and the first run's stop went unseen for
   a day. Now every run ends by posting one event through the public
   `/api/board-config` address: `upstream-sync`/`run`, `info` when a pull
   request opened or there was nothing to do, `error` with the branch and
   the reason when it stopped. The fingerprint `upstream-sync|stopped` is
   fixed, so a stop is one card and the next good run closes it.
9. **One stopped branch at a time.** While a `sync/upstream-*` branch from a
   stopped run exists on origin, the run posts the error again and does not
   start another; a person finishes that merge first (it is a firmware card).
10. **Shallow clones.** The cloud checkout is shallow; `git fetch --unshallow
    origin` first, or the merge reports unrelated histories.

A run that cannot finish posts the error event and stops. It never messages
Mario.
