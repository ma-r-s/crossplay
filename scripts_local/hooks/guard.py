#!/usr/bin/env python3
"""Claude Code hooks that make the workspace rules physical.

Three rules kept getting forgotten because they lived in prose: work never
happens in the integration tree, workers talk only to the orchestrator, and
a turn does not end by handing the decision back to Mario. This script is
wired from the workspace root's .claude/settings.json and runs as:

    guard.py pretool        PreToolUse: refuse the edit, command or message
    guard.py stop           Stop: refuse to end a turn on a hand-back
    guard.py session-start  SessionStart: print the contract and the session id

It reads the board (<workspace>/.board, written only by tools_local/board/
board.py) to know who the orchestrator is, who holds the integration tree,
and which card this session is bound to. It is INERT until
<workspace>/.board/enabled exists, so installing it changes nothing; arming it
is one `touch`, disarming is one `rm`.

Exit 2 with a reason on stderr is the only way a hook blocks; anything else
is a no-op. host-tests/bugflow/run.sh drives every branch below with fixture
input, including the ones that must NOT block.
"""

import json
import os
import pathlib
import re
import sys

HANDBACK = re.compile(
    r"(let me know|say the word|want me to|shall i\b|should i\b|do you want|"
    r"would you like|ready when you are|your call|next steps?:|what'?s (left|next)|"
    r"waiting (on|for) you|tell me (which|if|whether|what)|if you'd rather|"
    r"i'll wait|awaiting your)",
    re.IGNORECASE,
)

WRITE_IN_TREE = re.compile(
    r"(>{1,2}|\bsed\s+-i|\btee\b|\bcp\b|\bmv\b|\brm\b|\btouch\b|\bmkdir\b|"
    r"\bgit\s+(merge|commit|checkout|reset|rebase|cherry-pick|tag|push|pull|switch|stash|apply|am)\b|"
    r"\bpio\s+run|\bbuild\.py|\bprecompress\.py)"
)

RAW_PIO = re.compile(r"(^|[;&|(]\s*|&&\s*)pio\s+run\b")


def find_root():
    env = os.environ.get("BOARD_ROOT")
    if env:
        return pathlib.Path(env)
    here = pathlib.Path(__file__).resolve()
    for p in [here] + list(here.parents):
        if (p / "firmware-next").is_dir() and (p / "wt").is_dir():
            return p
    pd = os.environ.get("CLAUDE_PROJECT_DIR")
    if pd:
        return pathlib.Path(pd)
    return None


def read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def norm_sid(s):
    s = str(s or "")
    return s[6:] if s.startswith("local_") else s


class Board:
    def __init__(self, root):
        self.root = root
        self.dir = root / ".board"

    @property
    def enabled(self):
        return (self.dir / "enabled").exists()

    def orchestrator(self):
        return read_json(self.dir / "orchestrator.json") or {}

    def integrator(self):
        return read_json(self.dir / "integrator.json") or {}

    def session(self, sid):
        return read_json(self.dir / "sessions" / f"{norm_sid(sid)}.json") or {}

    def card(self, cid):
        if cid is None:
            return None
        return read_json(self.dir / "cards" / f"{int(cid)}.json")

    def is_orchestrator(self, sid):
        o = norm_sid(self.orchestrator().get("session_id"))
        return bool(o) and o == norm_sid(sid)

    def is_integrator(self, sid):
        i = norm_sid(self.integrator().get("session_id"))
        return bool(i) and i == norm_sid(sid)

    def note_session(self, sid, cwd):
        d = self.dir / "sessions"
        d.mkdir(parents=True, exist_ok=True)
        p = d / f"{norm_sid(sid)}.json"
        cur = read_json(p) or {}
        cur.setdefault("session_id", norm_sid(sid))
        cur["cwd"] = cwd
        try:
            with open(p, "w") as f:
                json.dump(cur, f, indent=1)
        except OSError:
            pass


def block(msg):
    sys.stderr.write(msg.rstrip() + "\n")
    sys.exit(2)


def under_integration_tree(root, path):
    if not path:
        return False
    try:
        p = pathlib.Path(path)
        if not p.is_absolute():
            p = pathlib.Path(os.getcwd()) / p
        p = p.resolve()
        tree = (root / "firmware-next").resolve()
        return p == tree or tree in p.parents
    except OSError:
        return False


def pretool(board, data):
    sid = data.get("session_id", "")
    tool = data.get("tool_name", "")
    inp = data.get("tool_input") or {}
    root = board.root

    if tool in ("Edit", "Write", "MultiEdit", "NotebookEdit"):
        path = inp.get("file_path") or inp.get("notebook_path") or ""
        if under_integration_tree(root, path) and not board.is_integrator(sid):
            block(
                "Refused: that file is in firmware-next, the integration tree. Work happens in "
                "wt/<name>/ (./scripts/wt.sh new <name>). Only the session holding the integration "
                "claim edits here: board integrator --session <id>."
            )
        return

    if tool == "Bash":
        cmd = inp.get("command") or ""
        if RAW_PIO.search(cmd) and "check.sh" not in cmd and "lib-sim.sh" not in cmd:
            block(
                "Refused: a raw `pio run` bypasses the workspace build lock and can corrupt another "
                "tree's build. Use ./scripts_local/check.sh (or dev.sh / sim-shot.sh) from your tree."
            )
        if (
            "firmware-next" in cmd
            and WRITE_IN_TREE.search(cmd)
            and not board.is_integrator(sid)
        ):
            block(
                "Refused: that command writes into firmware-next, the integration tree. Reading it "
                "is fine; changing it is the integrator's job. Work in wt/<name>/."
            )
        if re.search(r"board(\.py)?\s+ask\b", cmd) and not board.is_orchestrator(sid):
            block(
                "Refused: only the orchestrator asks Mario. Record what you need on your card: "
                "board block <card> --session <id> --need <desk|design|info|mario> --ask '...' --default '...'"
            )
        return

    if tool in ("SendMessage", "mcp__ccd_session_mgmt__send_message"):
        if board.is_orchestrator(sid):
            return
        orch = board.orchestrator()
        if not orch:
            return
        to = str(inp.get("to") or inp.get("session_id") or "")
        name = str(orch.get("name") or "")
        target = to.split(" [")[0].strip().lower()
        allowed = {name.lower(), "main"} if name else {"main"}
        if target in allowed or (
            norm_sid(to) and norm_sid(to) == norm_sid(orch.get("session_id"))
        ):
            return
        block(
            f"Refused: workers talk only to the orchestrator ({name or 'not named yet'}). A peer "
            "cannot resolve your blocker and cannot pass Mario's authority along. Message the "
            "orchestrator, or record the blocker on your card with `board block`."
        )


def last_assistant_text(transcript_path):
    text = ""
    try:
        with open(transcript_path) as f:
            for line in f:
                try:
                    obj = json.loads(line)
                except ValueError:
                    continue
                if obj.get("type") != "assistant":
                    continue
                content = (obj.get("message") or {}).get("content")
                if isinstance(content, str):
                    text = content
                elif isinstance(content, list):
                    parts = [
                        c.get("text", "")
                        for c in content
                        if isinstance(c, dict) and c.get("type") == "text"
                    ]
                    if parts:
                        text = "\n".join(parts)
    except OSError:
        return ""
    return text


def stop(board, data):
    if data.get("stop_hook_active"):
        return
    sid = data.get("session_id", "")
    if board.is_orchestrator(sid):
        return
    text = last_assistant_text(data.get("transcript_path", ""))
    m = HANDBACK.search(text)
    if not m:
        return
    sess = board.session(sid)
    cid = sess.get("card")
    card = board.card(cid)
    if card:
        for b in card.get("blockers", []):
            if b.get("open"):
                return
    where = f"card #{cid}" if card else "no card bound yet"
    bind = (
        ""
        if card
        else (
            "\nBind a card first: board list, then board bind <id> --session "
            f"{norm_sid(sid)} (or board new '<title>' --from <app> to create one)."
        )
    )
    block(
        f"This turn ends by handing back ('{m.group(0)}'), and a worker never hands back to Mario "
        f"({where}). Either take the next step now, or record why you cannot:\n"
        f"  board block <card> --session {norm_sid(sid)} --need <desk|design|info|mario> "
        "--ask '<one line>' --default '<what happens if nobody answers>'\n"
        "then end the turn with one line saying the card is blocked." + bind
    )


def session_start(board, data):
    sid = norm_sid(data.get("session_id", ""))
    board.note_session(sid, data.get("cwd", ""))
    orch = board.orchestrator()
    sess = board.session(sid)
    cid = sess.get("card")
    card = board.card(cid)
    lines = []
    lines.append(f"[bugflow] Your session id is {sid}.")
    if board.is_orchestrator(sid):
        lines.append(
            "[bugflow] You are the ORCHESTRATOR. Runbook: docs/workflow/orchestrator.md in the tree."
        )
    else:
        who = orch.get("name") or "not registered yet"
        lines.append(f"[bugflow] You are a WORKER. The orchestrator is: {who}.")
        if card:
            lines.append(
                f"[bugflow] Your card: #{card['id']} {card['title']} (state {card.get('state')}). Read it: board show {card['id']}"
            )
        else:
            lines.append(
                f"[bugflow] No card bound. Before any edit: board list, then board bind <id> --session {sid}."
            )
    contract = board.root / "firmware-next" / "docs" / "workflow" / "worker-contract.md"
    here = (
        pathlib.Path(__file__).resolve().parents[2]
        / "docs"
        / "workflow"
        / "worker-contract.md"
    )
    for p in (here, contract):
        try:
            with open(p) as f:
                lines.append(f.read().rstrip())
            break
        except OSError:
            continue
    sys.stdout.write("\n".join(lines) + "\n")


def main():
    if len(sys.argv) < 2:
        return
    mode = sys.argv[1]
    root = find_root()
    if root is None:
        return
    board = Board(root)
    if not board.enabled:
        return
    try:
        data = json.load(sys.stdin)
    except ValueError:
        return
    if mode == "pretool":
        pretool(board, data)
    elif mode == "stop":
        stop(board, data)
    elif mode == "session-start":
        session_start(board, data)


if __name__ == "__main__":
    main()
