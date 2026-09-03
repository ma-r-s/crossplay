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

A run that cannot finish files a card and stops. It never messages Mario.
