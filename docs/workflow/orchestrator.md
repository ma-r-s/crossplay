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
     yourself.
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
7. **Mario.** He reads `board inbox` and nothing else. If nothing is there,
   he hears nothing from you.
