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

## Identity

A session cannot learn its own id from Bash, so the SessionStart hook prints
it and every `board` write that belongs to a session takes `--session`. The
hooks see the real id on every call and check it against what the board says:
the orchestrator (`board orchestrator`), the integration claim (`board
integrator`), and the card a session bound (`board bind`).

## What the store is

The board is a Supabase project (`server/board/supabase/`: the schema and
row security as migrations, `config.toml` for the auth addresses; apply
with `supabase db push` or `psql -f` against the project, `supabase config
push` for the auth part). Every session, hook and page reaches it in one of
three ways:

- `board` (the CLI) with the service key from `<workspace>/.board/supabase.env`,
  which bypasses row security. That file is outside git and holds the
  project ref, the database password, the anon key and the service key.
- The site's `api/report.js` with the same service key from Vercel's
  environment, for strangers' reports.
- The inbox page (`site/inbox/`) as a signed-in user, by magic link. Row
  security admits only the emails in `allowed_users`.

The CLI mirrors claims, session bindings and bound cards into
`<workspace>/.board/` as JSON, and the hooks read only that mirror, so a
tool call never waits on the network. `BOARD_BACKEND=file` runs the CLI
against the mirror alone (what the tests do). `board sync` copies the file
store into Supabase once, ids kept, for the migration that already happened
on 2026-09-03.

## Tests

`host-tests/bugflow/run.sh` drives every branch of the guard with fixture
input, in both directions, against a throwaway workspace (`BOARD_ROOT`), and
the board end to end: ask, inbox, answer, import, claims. Both scripts honour
`BOARD_ROOT` for exactly that reason.
