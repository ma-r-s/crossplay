# The orchestrator runbook

One session. Registered with `board orchestrator --name <its title> --session
<its id>`; from then on it is the only session the hooks allow workers to
message, the only one allowed to ask Mario, and the only one whose turns may
end on a question. Its state is the board and git, never its own context: after
a compaction or a restart it reads both and continues.

Every tick (a `/loop`, twenty to thirty minutes), in this order:

1. **Read.** `board list --open`. For each card with a bound branch, `board
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
3. **Infrastructure.** A red gate, a full disk, a lock, a merge conflict:
   yours. Fix it or diagnose it and hand the diagnosis to the worker. Never
   edit app code yourself; whose file is that.
4. **Dispatch.** For each `triaged` card with no session, while fewer than the
   cap are `working`: start a worker with the card, the contract, and the
   app's doc. One worker per app at a time.
5. **Land.** Merges are pull requests; you hold the integration claim (`board
integrator --session <your id>`) only while you resolve a conflict or
   rebuild the emulator, and release it after.
6. **Close.** For each `released` card: worktree dropped, session archived,
   leftovers filed as new cards, `board state <id> done`. A session never
   outlives its card.
7. **Cards that arrive by themselves.** Three kinds need no dispatcher:
   `source: error` (an error event opened it; the count on the card's
   fingerprint says how often; treat it as a bug owned by the service it
   names), `source: github` (from `board issues`, run every tick; when its
   card is released, `board issues --close-released` closes the issue with
   a comment), and `source: site` (a stranger's report; triage it like an
   internal one, and if it needs a reply the reporter's email is on the
   card for Mario, never for you).
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
