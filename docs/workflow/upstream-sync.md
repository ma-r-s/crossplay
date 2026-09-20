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
   - Upstream's docs take upstream's side; fork-specific notes about them
     live in `LOCAL_SCOPE.md` or `docs/`. `README.md`, `AGENTS.md` (=
     `CLAUDE.md`) and `SCOPE.md` are NOT in that group: `LOCAL_SCOPE.md`
     lists all three as fork-owned, so the rule above already keeps our
     side. Taking upstream's would replace this fork's front page with
     CrossPoint's and drop the read-this-first banner from the agent
     guide.
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

   **Before that, look for the seam the merge took without saying so.** A
   conflict marker is the easy case; the dangerous one is upstream refactoring
   the lines a fork seam hangs on, which deletes the seam cleanly and reports
   nothing. On 2026-09-19 that removed `finishWifiSessionWithoutRestart()` and
   all three of its call sites, the live WiFi-teardown path on every touch
   device this fork targets. The check is mechanical and takes a minute: every
   line the fork ever added to an upstream-owned file, tested for presence in
   the merged tree.

       git diff --name-only <base> HEAD | while read -r f; do
         git cat-file -e "<base>:$f" 2>/dev/null || continue   # fork-only file
         git diff -U0 "<base>" HEAD -- "$f" | grep '^+[^+]' | cut -c2- \
         | while IFS= read -r l; do
             [ ${#l} -gt 6 ] && ! grep -qF "$l" "$f" 2>/dev/null && echo "$f: $l"
           done
       done

   Every hit is either a resolution you made on purpose or a seam the merge
   ate. Read all of them; there is no safe skim. The seam table in
   `LOCAL_SCOPE.md` is the second pass, not the first: this one found a seam
   the table did not list.

   **`check.sh --committed` refuses a sync branch by construction.** Its undo
   guard intersects what the branch removes with what trunk added in the sixty
   commits before the merge base, and the PREVIOUS sync merge is inside that
   window -- so upstream deleting what upstream added reads as a revert. The
   2026-09-19 run was flagged 112 lines: 74 from the previous sync, 38 of
   generic boilerplate matched as bare text. `CHECK_ALLOW_UNDO=1` is the way
   past it, and the pull request says so and says what the flagged lines were.
   Card #534 is making the guard skip a merge whose second parent is
   `crosspoint/develop`.
5. **Pull request** `sync/upstream-<date>` into `xteink`, titled `chore:
sync CrossPoint develop (<n> commits)`, body: what came in (their commit
   subjects), what was resolved and by which rule, what was not verified
   (device builds are CI's). The run's `info` event carries the pull
   request's URL, title and summary, and the board opens a task card in
   `review` from it (`20260904001300_sync_pr_card.sql`); that card is where
   the orchestrator's critic finds the pull request. CI gates it; the critic
   reviews it; it merges on green like any other pull request.
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
   `.gitmodules` named `x4pro-measured-insets` until 2026-09-04, by which
   point that branch had been DELETED on the fork -- so a
   `fetch --recurse-submodules` failed with `upload-pack: not our ref`. It now
   names `fix/sd-free-space`, which is where the pinned commit actually lives.
   The pin is still what matters, and the fork's `main` is still not the line:
   `main` sits 40 commits behind the pin. Whoever renames that branch to
   something durable should fast-forward `main` to the pin in the same move,
   so the fork's default branch stops lying about where its work is.
8. **The board hears every run.** The cloud checkout has no board, so the
   old "file a card" step never ran and the first run's stop went unseen for
   a day. Now every run ends by posting one event through the public
   `/api/board-config` address: `upstream-sync`/`run`, `info` when a pull
   request opened or there was nothing to do, `error` with the branch and
   the reason when it stopped. The fingerprint `upstream-sync|stopped` is
   fixed, so a stop is one card and the next good run closes it. An `info`
   run whose `result` is a pull request URL also carries the pull request's
   `title` and `summary`, and the board opens one task card in `review`
   from it, once per pull request. Without that, PR #43 sat on GitHub with
   its URL in an event nobody reads.
9. **One stopped branch at a time.** While a `sync/upstream-*` branch from a
   stopped run exists on origin, the run posts the error again and does not
   start another; a person finishes that merge first (it is a firmware card).
   A `sync/upstream-*` branch that is already merged into `xteink` is a
   leftover, not a stop: the run deletes it and continues. GitHub deletes
   merged branches by itself since 2026-09-04 (`delete_branch_on_merge`, the
   repository setting), so that is the fallback; PR #43's branch outlived its
   merge by an hour before the setting existed.
10. **Shallow clones.** The cloud checkout is shallow; `git fetch --unshallow
    origin` first, or the merge reports unrelated histories.

A run that cannot finish posts the error event and stops. It never messages
Mario.
