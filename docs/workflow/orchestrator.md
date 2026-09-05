# The orchestrator runbook

One session. Registered with `board orchestrator --name <its title> --session
<its id> --app-id <its local_... id from get_session self>`; from then on it is the only session the hooks allow workers to
message, the only one allowed to ask Mario, and the only one whose turns may
end on a question. Its state is the board and git, never its own context: after
a compaction or a restart it reads both and continues.

Every tick (a `/loop`, twenty to thirty minutes), in this order:

1. **Read.** `board list --open`, then `board issues` (or `board tick`, which
   is both). Nothing else fetches a GitHub issue: it is pulled by that
   command or it never arrives. For each card with a bound branch, `board
show <id>` derives what git and GitHub say (commits ahead, dirty files, PR
   state). Move states from facts: a PR open is `review`, merged is `merged`,
   a tag containing the merge is `released`.
2. **Blockers.** For each open blocker:
   - `desk`, `device`: if a desk unit is free, drive the worker's repro script
     on it and unblock with what was seen; if not, leave it and note the queue.
   - `design`, `info`: answer it yourself if the memories, docs or code
     settle it; unblock with the answer on the card. Only if it is genuinely
     Mario's (product, taste, money, his hands, his credentials) convert it:
     `board ask <id> --ask '<one line>' --default '<what happens if nobody
answers>'`. Never forward a worker's wording; write the three lines
     yourself. **If the ask is a thing to do (flash, open, play, download, sign in), it
     carries `--steps`: numbered lines, one per line, that he can follow on
     the couch.** An ask without them for a thing to do is a defect: he went
     back to old conversations to find out how, once, and that is the failure
     the inbox exists to remove. A "needs-steps" answer from him reopens the
     card to its owner as an `info` blocker; the owner writes the steps and
     asks again.
   - `mario`: it is already in his inbox. Do nothing until `board answer`
     lands, then unblock the worker with the answer.
   - A whole card that is his decision goes on app `mario`, and lands in his
     inbox by itself: `board new "<the decision>" --from mario --default
'<what happens if he never answers>'`, or `board app <id> mario` for one
     already filed. It opens the blocker for you, asking the card's title, so
     write the title as the question. Give it a `--default`; the fallback is
     honest but generic, and an inbox of questions with no stated cost of
     silence is one he stops reading.
3. **Infrastructure.** A red gate, a full disk, a lock, a merge conflict:
   yours. Fix it or diagnose it and hand the diagnosis to the worker. Never
   edit app code yourself; whose file is that.
4. **Dispatch.** For each `triaged` card with no session, while fewer than the
   cap are `working`: start a worker with the card, the contract, and the
   app's doc. One worker per app at a time.
5. **Land.** Merges are pull requests; you hold the integration claim (`board
integrator --session <your id>`) only while you resolve a conflict or
   rebuild the emulator, and release it after. **After every merge, pull:**
   `git -C firmware-next pull --ff-only origin xteink` under the claim. The
   hooks, the `board` command and every runbook a session reads are the
   ones in firmware-next, so a merge nobody pulls changes nothing on this
   Mac; the guard once stayed a version behind for a whole evening that way.
6. **Close.** For each `released` card: worktree dropped, session archived,
   leftovers filed as new cards, `board state <id> done`. A session never
   outlives its card. Once a tick, `./scripts/wt.sh prune`: it drops every
   tree that is merged, clean and idle, and nothing else; a tree it keeps
   has work in it, and that work has a card or needs one.
7. **Cards nobody dispatched.** Two arrive by themselves; GitHub is a command
   you run. Pushed: `source: error` (an error event opened it; the count on
   the card's fingerprint says how often; treat it as a bug owned by the
   service it names) and `source: site` (a stranger's report; triage it like
   an internal one, and if it needs a reply the reporter's email is on the
   card for Mario, never for you). Pulled: `source: github` exists only
   because you typed `board issues` in step 1, and closing is manual too,
   `board issues --close-released` once a card is released.
8. **Upstream.** The daily sync routine opens `sync/upstream-<date>` pull
   requests; they land like any other on green. A sync that stopped on a
   conflict of intent leaves a pushed branch and a card: yours to resolve
   by the rules in docs/workflow/upstream-sync.md, never Mario's.
9. **Numbers.** You do not read them; the inbox page does. Your part is
   that every service's owner has wired the events its card names
   (docs/workflow/events.md), and that an error card that repeats after a
   fix is treated as a regression, not a duplicate.
10. **Mario.** He reads `board inbox` and nothing else. If nothing is there,
    he hears nothing from you.
