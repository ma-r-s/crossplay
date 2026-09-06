# Workflow machinery

The rules sessions kept forgetting, moved out of prose and into things that
refuse. Three pieces:

| Piece                                                 | What it is                                                                                                                                                                                                                                                                                                                           |
| ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `scripts_local/hooks/guard.py`                        | Claude Code hooks: `pretool` refuses edits in the integration tree, raw `pio run`, messages to anyone but the orchestrator, and `board ask` from a worker; `stop` refuses a turn that ends by handing back to Mario without a blocker on the card; `session-start` prints the session id, the role, the bound card and the contract. |
| `tools_local/board/board.py`                          | The board. Cards, blockers, Mario's inbox, the orchestrator and integrator claims. The only writer of `<workspace>/.board/`, which the hooks read. `board --help` lists every command.                                                                                                                                               |
| `docs/workflow/worker-contract.md`, `orchestrator.md` | What a worker and the orchestrator do, in the words the SessionStart hook prints.                                                                                                                                                                                                                                                    |

## How it is wired

Sessions in this workspace start with the workspace root as their project
directory (they `cd` into trees per command), so the hooks are configured in
**`<workspace>/.claude/settings.json`**, which is outside git because the
workspace root is not a repository. That file runs `guard.py` from
`firmware-next/scripts_local/hooks/` and falls back to `wt/bugflow/` while the
branch is unmerged; once merged, the first path wins and the fallback is dead.

The hooks are **inert until `<workspace>/.board/enabled` exists**. Installing
them changes nothing; `touch .board/enabled` arms every session from its next
start, `rm .board/enabled` disarms. A running session picks the hooks up when
it restarts, not before.

## The scratchpad is shared

The agent scratchpad is described as session-specific and is not: several
agents run under one session id, so they all get the same directory, and each
of them independently reaches for `gate.log`, `pr.md`, `out.txt`, `check.log`.
Collision is therefore the default rather than bad luck, and it is silent --
a truncated-then-rewritten log reads as a legitimate result. Three runs were
corrupted on 2026-09-05 and one reached GitHub: an agent wrote its pull request
body to `scratchpad/pr.md`, another session overwrote that exact path, and the
first pushed the second's text into PR #117.

A convention cannot fix this, because the failure mode IS everybody
independently choosing the same obvious name. So the guard **refuses a write to
the flat top level** and names the subdirectory to use instead:
`<scratchpad>/<your worktree name>/`. The worktree is the key because one card,
one branch, one worktree is this workflow's own rule -- and because a working
directory was the only thing that distinguished four concurrently running gates
the night `pgrep -f "check.sh --committed"` nearly got two siblings killed.
`scripts_local/whose-gate.sh` answers that question directly now.

The other half of the same lesson is in the worker contract: a run's verdict is
its own captured output, never a file another process can write. `check.sh`
prints a `transcript:` path named with `mktemp` for exactly this, and a
`CHECKSH-VERDICT:` token to grep for instead of a line to count to.

## Identity

A session cannot learn its own id from Bash, so the SessionStart hook prints
it and every `board` write that belongs to a session takes `--session`. The
hooks see the real id on every call and check it against what the board says:
the orchestrator (`board orchestrator`), the integration claim (`board
integrator`), and the card a session bound (`board bind`).

## What the store is

The board is a Supabase project (`server/board/supabase/`: the schema and
row security as migrations, `config.toml` for the auth addresses;
`server/board/migrate.sh` applies the files the board has not seen, in name
order, one transaction each, records each in `board_migrations` and reloads
PostgREST; `--list` says what is pending; `supabase config push` for the
auth part). Until 2026-09-04 every file was applied by hand and nothing
tracked it, so never `supabase db push`: it would replay all of them from
the first. Version prefixes stay unique (two files once shared one; the
script refuses that). Every session, hook and page reaches it in one of
three ways:

- `board` (the CLI) with the service key from `<workspace>/.board/supabase.env`,
  which bypasses row security. That file is outside git and holds the
  project ref, the database password, the anon key and the service key.
- The site's `api/report.js` with the same service key from Vercel's
  environment, for strangers' reports.
- The inbox page (`site/inbox/`) through `site/api/inbox.js`, which checks a
  passphrase against `INBOX_PASSPHRASE_HASH` and only then touches the board
  with the service key. The page never holds a board key.

The CLI mirrors claims, session bindings and bound cards into
`<workspace>/.board/` as JSON, and the hooks read only that mirror, so a
tool call never waits on the network. `BOARD_BACKEND=file` runs the CLI
against the mirror alone (what the tests do). `board sync` copies the file
store into Supabase once, ids kept, for the migration that already happened
on 2026-09-03.

## A card addressed to Mario is an inbox item

The inbox is the open `mario` blockers and nothing else, and a card is not a
blocker. So a card filed on app `mario` -- which by convention already means
"only Mario can decide this" -- reached him only if somebody also remembered
to block on it, and twice nobody did: cards 75 and 84 were his decisions and
aged a day in `reported` while his inbox said nothing needs you. That is a
dropped message, not a delay.

Since card #209 it is not something to remember. Filing a card on app `mario`
(`board new "..." --from mario`) or moving one there (`board app <id> mario`)
opens a `mario` blocker asking the card's title, and one only: a card that
already has an open one never gets a second, however many times it is moved.
`--default` says what happens if he never answers; without one the blocker
says `nothing happens until he answers`, which is honest and lets him ignore
it safely. A card already `done`, `released` or `parked` is left alone: a
decision taken is not one to ask again.

Two enforcers, because the CLI is not the only writer -- the site's report
function, the inbox page and a hand-typed `UPDATE` all reach `cards` directly.
**Only the CLI half is live.** `20260905000300_mario_inbox.sql` adds the
triggers and backfills the cards dropped before the rule existed, and it is
written but **not yet applied**; until `server/board/migrate.sh` has run it,
a card that reaches `cards` by any route other than `board` gets no blocker.
`server/board/migrate.sh --list` says whether it is still pending.

## Who reported a card

`source` says by what MECHANISM a card arrived -- the CLI, the site's form, a
GitHub issue, the error trigger. It never said whose observation it was, so a
bug Mario hit on his own device and a bug an audit found by reading code were
both `source: session`. He asked "what have I reported?" and the answer had to
be reconstructed from conversation, which does not scale and cannot be trusted.

Every card carries a `reporter`:

| value | who |
| --- | --- |
| `mario` | he hit it, asked for it, or ruled on it, and said so |
| `user` | a person who is not Mario: the public report form, a GitHub issue |
| `session` | our own side found it: an audit, a gate, a cold review, a probe, the error trigger |
| `unknown` | nothing establishes either way |

```bash
board new "<title>" --from <app> --reporter mario|user|session
board list --from-mario           # the question he asks
board list --reporter unknown     # what nobody stamped
```

**The default is `unknown`, deliberately not `session`.** A path that forgets
to stamp has to be visible rather than quietly credit one of our own sessions,
because the only value this column has is that the list can be trusted. Two
mechanisms answer for themselves and are derived rather than remembered: an
error-trigger or upstream-sync card is `session`, a GitHub-issue card is
`user`. The site's form is public, so it stamps `mario` only when the address
given is the owner's and `user` otherwise.

The cards that predate the column were recovered from their own text and from
verbatim matches against the session transcripts:
[what-mario-reported.md](what-mario-reported.md) has the counts, the list he
asked for, the evidence behind every `mario` row, and the ones that could not
be established.

**Read all three channels if you ever redo this.** Anything Mario types WHILE a
session is mid-turn is never stored as a transcript entry of type `user`: it
arrives as an attachment of type `queued_command` whose origin kind is `human`.
A filter on `type: "user"` alone misses it, and it missed 221 of his messages
the first time, 74 of them inside the window the board covers. His typed inbox
answers are a third channel again, in the `blockers` table. The same trap
applies to anything else that reads these transcripts for what he said.

## When the guard itself breaks

It fails open, on purpose. Anything unexpected inside `guard.py` exits 0
(Claude Code reads that as "no opinion") after appending one line to
`<workspace>/.board/hook-errors.log`, so a check that did not run looks
different from one that passed and the orchestrator can see it. A missing
board, a missing switch file or unreadable input are also "no opinion". The
only deliberate refusals are the exit-2 paths, and each ends with the exact
command to run, the session id already filled in. A gate that locked every
session out on its own bug would be worse than no gate.

## Tests

`host-tests/bugflow/run.sh` drives every branch of the guard with fixture
input, in both directions, against a throwaway workspace (`BOARD_ROOT`), and
the board end to end: ask, inbox, answer, import, claims. Both scripts honour
`BOARD_ROOT` for exactly that reason.
