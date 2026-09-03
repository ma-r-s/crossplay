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

v0 is JSON under `<workspace>/.board/`: `cards/<id>.json`,
`sessions/<id>.json`, `orchestrator.json`, `integrator.json`. The web board
replaces that directory behind the same command line. Nothing that calls
`board` or reads the hooks' refusals changes when it does.

## Tests

`host-tests/bugflow/run.sh` drives every branch of the guard with fixture
input, in both directions, against a throwaway workspace (`BOARD_ROOT`), and
the board end to end: ask, inbox, answer, import, claims. Both scripts honour
`BOARD_ROOT` for exactly that reason.
