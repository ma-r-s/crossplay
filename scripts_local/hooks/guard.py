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

import datetime as dt
import hashlib
import json
import time
import os
import pathlib
import re
import subprocess
import sys

HANDBACK = re.compile(
    r"(let me know|say the word|want me to|shall i\b|should i\b|do you want|"
    r"would you like|ready when you are|your call|next steps?:|what'?s (left|next)|"
    r"waiting (on|for) you|tell me (which|if|whether|what)|if you'd rather|"
    r"i'll wait|awaiting your)",
    re.IGNORECASE,
)

# A verb counts only as a command word: not inside a flag (`grep -ln` is not
# `ln`) and not inside a quoted string (a grep PATTERN that says "sed -i" reads
# a file, it does not edit one). Both refused read-only commands on 2026-09-04.
WRITE_VERB = re.compile(
    r"((?<![\w-])sed\s+-i|(?<![\w-])(tee|cp|mv|rm|touch|mkdir|ln|truncate)(?![\w-])|"
    r"(?<![\w-])git\s+(merge|commit|checkout|reset|rebase|cherry-pick|tag(?!\s+(?:-l|-n|--list|--contains|--no-contains|--merged|--no-merged|--points-at)\b)|push|pull|switch|stash|apply|am|clean|restore)\b|"
    r"(?<![\w-])pio\s+run|\bbuild\.py|\bprecompress\.py)"
)
QUOTED = re.compile(r"'[^']*'|\"[^\"]*\"")
# A redirect whose TARGET is a file (2>&1 and >/dev/null are not writes into anything of ours).
REDIRECT = re.compile(r"(?<![0-9&<])>{1,2}\s*(?!&)(\S+)")
HEREDOC = re.compile(r"<<-?\s*['\"]?(\w+)['\"]?[^\n]*\n.*?\n\s*\1\s*(?=\n|$)", re.S)

RAW_PIO = re.compile(r"(^|[;&|(]\s*|&&\s*)pio\s+run\b")

# `tee out.txt`, `tee -a out.txt`: the other way output reaches a file.
TEE_TARGET = re.compile(r"(?<![\w-])tee(?:\s+-[\w-]+)*\s+(\S+)")


def scratch_root_of(path, workspace=None):
    """The scratchpad directory this path sits DIRECTLY in, or None.

    Only the flat top level is refused. `<scratchpad>/<ns>/gate.log` is the
    remedy and has to stay allowed, so this looks at the immediate parent and
    nothing higher. A `scratchpad/` inside the repository (there is none today)
    is somebody's source file and is none of this rule's business.
    """
    if not path:
        return None
    try:
        p = pathlib.Path(str(path))
    except (TypeError, ValueError):
        return None
    if p.parent.name != "scratchpad":
        return None
    if workspace is not None:
        # BOTH sides resolved. Comparing a resolved workspace against an
        # unresolved path silently fails under any symlinked component (/var ->
        # /private/var on macOS is the common one), and the exemption then does
        # not apply to the thing it exists for.
        try:
            ws = pathlib.Path(workspace).resolve()
            rp = pathlib.Path(str(p)).resolve()
            if ws == rp.parent or ws in rp.parents:
                return None
        except OSError:
            pass
    return p.parent


def scratch_ns(board, sid, cwd):
    """The name of this worker's own subdirectory inside the shared scratchpad.

    One card, one branch, one worktree is the workflow's own rule, so the tree
    name is a faithful per-worker key -- and it is the only thing that told the
    colliding runs apart on 2026-09-05, when `pgrep -f "check.sh --committed"`
    returned four pids across three worktrees and a session nearly killed two
    siblings' gates. Falls back to the bound card, then to the session id, so
    it always answers something.
    """
    try:
        parts = pathlib.Path(cwd or ".").parts
    except (TypeError, ValueError):
        parts = ()
    if "wt" in parts:
        i = len(parts) - 1 - list(reversed(parts)).index("wt")
        if i + 1 < len(parts):
            return parts[i + 1]
    card = (board.session(sid) or {}).get("card")
    if card is not None:
        return "card%s" % card
    return "s-" + (norm_sid(sid)[:8] or "unknown")


def scratch_targets(cmd, cwd):
    """Every path this command would WRITE that lands in a scratchpad directory.

    Redirects and `tee`, which is every shape the three incidents took. A `cd`
    into the scratchpad carries, because `cd <scratchpad> && cat > pr.md` is the
    same write spelled differently.

    A heredoc's BODY is data and is dropped, but its opening line is kept: the
    redirect in `python3 - <<'PY' > out.json` sits after the `<<` on that line,
    and dropping the whole construct (which is what writes_into_tree does) loses
    it. That is one of the two spellings the PR-body incident actually used.
    """
    body = HEREDOC.sub(lambda m: m.group(0).split("\n", 1)[0], cmd)

    # A `>` INSIDE a quoted string is text, not a redirect. `writes_into_tree`
    # learned this the expensive way (its comment names four read-only commands
    # refused on 2026-09-04) and answers it by deleting quoted strings outright
    # -- which here would also delete `> "<scratchpad>/gate.log"`, the very
    # thing being looked for. So each quoted string becomes a placeholder and is
    # put back only if it turns out to BE a target: `git log --grep="a > /x"`
    # then carries no redirect at all, while `> "/x"` still carries one.
    quoted = []

    def _stash(m):
        quoted.append(m.group(0)[1:-1])
        return " __Q%d__ " % (len(quoted) - 1)

    body = QUOTED.sub(_stash, body)

    def _unstash(text):
        m = re.fullmatch(r"__Q(\d+)__", text)
        return quoted[int(m.group(1))] if m else text

    here = cwd or ""
    out = []
    for seg in re.split(r"&&|\|\||;|\|", body):
        seg = seg.strip()
        if not seg:
            continue
        # A leading `(`, `{` or `pushd` is the same `cd`. Not exhaustive -- a
        # path held in a shell variable defeats this whole scan -- but these
        # three are what an agent actually types.
        m = re.match(r"[({]?\s*(?:cd|pushd)\s+(\S+)", seg)
        if m:
            here = _unstash(m.group(1)).strip("\"'")
            continue
        cands = [r.group(1) for r in REDIRECT.finditer(seg)]
        cands += [t.group(1) for t in TEE_TARGET.finditer(seg)]
        for target in cands:
            # A trailing `)`/`}`/`;` is the shell closing a group, not part of
            # the name. It only affects the path this refusal PRINTS, but the
            # refusal's whole value is that the remedy can be pasted.
            target = _unstash(target).strip("\"'").rstrip(")};")
            if not target or target.startswith("/dev/"):
                continue
            if not target.startswith("/") and here:
                target = here.rstrip("/") + "/" + target
            if "/scratchpad/" in target:
                out.append(target)
    return out


def scratch_refusal(board, sid, cwd, path, sroot):
    ns = scratch_ns(board, sid, cwd)
    name = pathlib.PurePath(str(path)).name
    return (
        "Refused: %s is at the top of the SHARED agent scratchpad.\n"
        "It is described as session-specific and it is not: several agents run under one "
        "session id, and every one of them independently reaches for gate.log, pr.md, "
        "out.txt, check.log. On 2026-09-05 that truncated one agent's gate log mid-build -- "
        "it read 'all green.' while its own gate was still compiling -- and put another "
        "session's text into the body of PR #117. The corruption is silent: a "
        "truncated-then-rewritten file reads as a legitimate result, never as damage.\n"
        "Write into your own subdirectory, which nothing else can choose:\n"
        "  mkdir -p %s/%s   then use %s/%s/%s\n"
        "And a gate's verdict is not a file at all: run check.sh, grep CHECKSH-VERDICT in "
        "its own captured output, or read the transcript path it prints on its first line."
        % (path, sroot, ns, sroot, ns, name)
    )



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

    @staticmethod
    def claim_ids(claim):
        """Both ids a claim may carry: the hook-visible session id and the desktop
        app's local_... id. A session is addressed by either, so both count."""
        return {norm_sid(claim.get("session_id")), norm_sid(claim.get("app_session"))} - {""}

    def is_orchestrator(self, sid):
        return norm_sid(sid) in self.claim_ids(self.orchestrator())

    def dispatcher(self):
        return read_json(self.dir / "dispatcher.json") or {}

    def is_dispatcher(self, sid):
        return norm_sid(sid) in self.claim_ids(self.dispatcher())

    def is_integrator(self, sid):
        return norm_sid(sid) in self.claim_ids(self.integrator())

    # Chosen against two clocks. The Bash tool caps one call at ten minutes, so
    # no session goes quiet that long from a single command; and a worker
    # waiting on a backgrounded gate makes no tool calls at all, which is why
    # a living gate on the tree counts as life below (tree_gate_pid) rather
    # than this number growing to cover it. Forty-five is well past the first
    # and does not need to cover the second.
    IDLE_MINUTES = 45

    @staticmethod
    def tree_gate_pid(tree_path):
        """The pid of a check.sh still verifying `tree_path`, or None.

        check.sh keeps ${TMPDIR:-/tmp}/xteink-check-<tag>.running with its pid
        while it runs (tag = the first eight hex of sha1 of the tree's real
        path, as check.sh computes it). A tree with a living gate is in use
        whatever its session is doing, and an edit landing in it would make
        that verdict meaningless while looking green.
        """
        try:
            real = str(pathlib.Path(tree_path).resolve())
        except OSError:
            return None
        tag = hashlib.sha1(real.encode()).hexdigest()[:8]
        lock = pathlib.Path(os.environ.get("TMPDIR") or "/tmp") / f"xteink-check-{tag}.running"
        try:
            pid = int((lock.read_text() or "0").split()[0])
        except (OSError, ValueError, IndexError):
            return None
        if pid <= 0:
            return None
        try:
            os.kill(pid, 0)
        except OSError:
            return None
        try:
            cmd = subprocess.run(["ps", "-o", "command=", "-p", str(pid)], capture_output=True, text=True).stdout
        except OSError:
            return None
        return pid if "check.sh" in cmd else None

    def session_seen(self, sid):
        """When the session last touched a tool, or None if the board never saw it.

        The hook touches the session file on every tool call, so its mtime is
        the last sign of life; SessionEnd (when wired) writes ended_at.
        """
        p = self.dir / "sessions" / f"{norm_sid(sid)}.json"
        try:
            st = p.stat()
        except OSError:
            return None
        cur = read_json(p) or {}
        if cur.get("ended_at"):
            return None
        return st.st_mtime

    def session_live(self, sid):
        seen = self.session_seen(sid)
        return seen is not None and (time.time() - seen) < self.IDLE_MINUTES * 60

    def touch_session(self, sid):
        p = self.dir / "sessions" / f"{norm_sid(sid)}.json"
        try:
            if p.exists():
                os.utime(p, None)
        except OSError:
            pass

    def end_session(self, sid):
        d = self.dir / "sessions"
        p = d / f"{norm_sid(sid)}.json"
        cur = read_json(p) or {"session_id": norm_sid(sid)}
        cur["ended_at"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
        try:
            d.mkdir(parents=True, exist_ok=True)
            with open(p, "w") as f:
                json.dump(cur, f, indent=1)
        except OSError:
            pass

    def tree_holders(self, tree):
        """Open cards bound to `tree` (as `wt/<name>`), as (session, card id).

        Two sessions once wrote into one worktree for an hour and ran two gates
        against it at the same time; the identical resulting SHAs read as
        "independently converged" and meant "there was only ever one tree"
        (2026-09-05). The cards know who holds a tree; the guard reads them.
        """
        out = []
        d = self.dir / "cards"
        if not d.is_dir():
            return out
        for p in d.glob("*.json"):
            c = read_json(p) or {}
            if str(c.get("tree") or "").rstrip("/") != tree:
                continue
            if c.get("state") in ("done", "released", "parked") or not c.get("session"):
                continue
            out.append((norm_sid(c.get("session")), c.get("id")))
        return out

    def foreign_tree(self, sid, path_or_cwd):
        """The `wt/<name>` under the workspace that `path_or_cwd` is inside, if a
        card binds it to a session other than `sid`; else None."""
        try:
            p = pathlib.Path(path_or_cwd)
            if not p.is_absolute():
                p = pathlib.Path(os.getcwd()) / p
            p = p.resolve()
            wt = (self.root / "wt").resolve()
            if wt not in p.parents:
                return None
            name = p.relative_to(wt).parts[0]
        except (OSError, ValueError, IndexError):
            return None
        tree = f"wt/{name}"
        holders = self.tree_holders(tree)
        if not holders:
            return None
        me = norm_sid(sid)
        if any(h == me for h, _ in holders) or self.is_orchestrator(sid):
            return None
        # A holder that has ended, or has not touched a tool for IDLE_MINUTES,
        # is gone: sessions end all the time without unbinding, and a tree
        # locked to a session that no longer exists is the claims stranded by
        # a dead session (card 44) again, once per worktree. The write is
        # allowed; binding the card afterwards records the takeover.
        live = [(h, c) for h, c in holders if self.session_live(h)]
        gate = self.tree_gate_pid(self.root / tree)
        if not live and gate is None:
            return None
        return (tree, live, gate)

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


def board_cmd(root):
    """The board CLI as an absolute command, from wherever it currently lives."""
    for rel in ("firmware-next/tools_local/board/board.py", "wt/bugflow/tools_local/board/board.py"):
        p = root / rel
        if p.exists():
            return f"python3 {p}"
    return "python3 <tree>/tools_local/board/board.py"


CURRENT = {"root": None, "sid": "", "tool": ""}


def note_refusal(msg):
    """One line per refusal in <workspace>/.board/refusals.log, and the same as a
    workflow event on the board when its address is at hand. A refusal is the
    hooks doing their job; how often they fire, and on what, is the number that
    says whether the rules are teaching or merely obstructing. Never raises."""
    root = CURRENT.get("root")
    if root is None:
        return
    first = msg.strip().splitlines()[0][:160] if msg.strip() else "refused"
    sid, tool = CURRENT.get("sid") or "?", CURRENT.get("tool") or "?"
    try:
        with open(root / ".board" / "refusals.log", "a") as f:
            f.write(f"{dt.datetime.now(dt.timezone.utc).isoformat()} {sid} {tool} {first}\n")
    except Exception:
        pass
    try:
        env = {}
        for line in (root / ".board" / "supabase.env").read_text().splitlines():
            if "=" in line and not line.startswith("#"):
                k, v = line.split("=", 1)
                env[k.strip()] = v.strip().strip("\"'")
        url, key = env.get("SUPABASE_URL"), env.get("SUPABASE_ANON_KEY")
        if url and key:
            import urllib.request
            body = json.dumps({"service": "workflow", "event": "refusal",
                               "props": {"session": sid, "tool": tool, "rule": first}}).encode()
            req = urllib.request.Request(url.rstrip("/") + "/rest/v1/events", data=body, method="POST",
                                         headers={"apikey": key, "Authorization": "Bearer " + key,
                                                  "Content-Type": "application/json", "Prefer": "return=minimal"})
            urllib.request.urlopen(req, timeout=2).read()
    except Exception:
        pass


def block(msg):
    note_refusal(msg)
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


def writes_into_tree(cmd):
    """Does this shell command change something under firmware-next?

    Segments are split on && || ; and |, a `cd` into the tree makes later
    segments count as inside it, heredoc bodies are data and are ignored, and
    `2>&1` or `>/dev/null` are not writes. Reading the tree is always fine.

    Quoted strings go before the split. Splitting first cut "$(git tag
    --contains x | tr ...)" at its pipe and left an unbalanced quote around
    a verb, which refused four read-only commands on 2026-09-04. A quoted
    string never carries a verb; it may carry the tree's path, which stays.
    """
    body = HEREDOC.sub(" ", cmd)
    body = QUOTED.sub(lambda m: " firmware-next " if "firmware-next" in m.group(0) else " ", body)
    in_tree = False
    for seg in re.split(r"&&|\|\||;|\|", body):
        seg = seg.strip()
        if not seg:
            continue
        m = re.match(r"cd\s+(\S+)", seg)
        if m:
            in_tree = "firmware-next" in m.group(1)
            continue
        names_tree = "firmware-next" in seg
        # Verbs are looked for outside quotes; the tree's name anywhere counts.
        if WRITE_VERB.search(seg) and (in_tree or names_tree):
            return True
        for r in REDIRECT.finditer(seg):
            target = r.group(1).strip("\"'")
            if target.startswith("/dev/"):
                continue
            if "firmware-next" in target or (in_tree and not target.startswith("/")):
                return True
    return False


def foreign_tree_refusal(board, sid, where):
    tree, holders, gate = where
    parts = [
        f"session {h} (card #{c}, active {int((time.time() - (board.session_seen(h) or time.time())) // 60)} min ago)"
        for h, c in holders
    ]
    if gate is not None:
        parts.append(f"a check.sh still verifying it (pid {gate}; ps -p {gate} -o etime,command)")
    who = ", ".join(parts) or "another session"
    return (
        f"Refused: {tree} is bound to {who}, and two sessions writing one tree verify nothing "
        "(the gates each ran against a tree the other was still changing). Work in your own tree: "
        f"./scripts/wt.sh new <name>, then {board_cmd(board.root)} bind <card> --session {norm_sid(sid)} --tree wt/<name>. "
        f"A holder idle for {board.IDLE_MINUTES} minutes counts as gone and this refusal lifts by itself; "
        f"to take a tree over now: {board_cmd(board.root)} bind <card> --session {norm_sid(sid)} --tree {tree} --take"
    )


def pretool(board, data):
    sid = data.get("session_id", "")
    tool = data.get("tool_name", "")
    inp = data.get("tool_input") or {}
    root = board.root
    board.touch_session(sid)

    if tool in ("Edit", "Write", "MultiEdit", "NotebookEdit"):
        path = inp.get("file_path") or inp.get("notebook_path") or ""
        sroot = scratch_root_of(path, root)
        if sroot is not None:
            block(scratch_refusal(board, sid, data.get("cwd"), path, sroot))
        where = board.foreign_tree(sid, path)
        if where:
            block(foreign_tree_refusal(board, sid, where))
        if under_integration_tree(root, path) and not board.is_integrator(sid):
            block(
                "Refused: that file is in firmware-next, the integration tree. Work happens in "
                "wt/<name>/ (./scripts/wt.sh new <name>). Only the session holding the integration "
                f"claim edits here: {board_cmd(root)} integrator --session {norm_sid(sid)}"
            )
        return

    if tool == "Bash":
        cmd = inp.get("command") or ""
        for target in scratch_targets(cmd, data.get("cwd") or ""):
            sroot = scratch_root_of(target, root)
            if sroot is not None:
                block(scratch_refusal(board, sid, data.get("cwd"), target, sroot))
        if RAW_PIO.search(cmd) and "check.sh" not in cmd and "lib-sim.sh" not in cmd:
            block(
                "Refused: a raw `pio run` bypasses the workspace build lock and can corrupt another "
                "tree's build. Use ./scripts_local/check.sh (or dev.sh / sim-shot.sh) from your tree."
            )
        # A write from inside another session's tree (the shell's cwd), or one
        # that names such a tree: the same rule as firmware-next, per tree.
        body = QUOTED.sub(" ", HEREDOC.sub(" ", cmd))
        if WRITE_VERB.search(body) or REDIRECT.search(body):
            candidates = [data.get("cwd") or ""]
            candidates += [m.group(0) for m in re.finditer(r"(?:/[^\s'\"]*)?wt/[A-Za-z0-9_.-]+/?", cmd)]
            for cand in candidates:
                if not cand:
                    continue
                cpath = cand if cand.startswith("/") else str(root / cand)
                where = board.foreign_tree(sid, cpath)
                if where:
                    block(foreign_tree_refusal(board, sid, where))
        if (
            writes_into_tree(cmd) and not board.is_integrator(sid)
        ):
            block(
                "Refused: that command writes into firmware-next, the integration tree. Reading it "
                "is fine; changing it is the integrator's job. Work in wt/<name>/, or if you are "
                f"integrating, claim the tree first: {board_cmd(root)} integrator --session {norm_sid(sid)}"
            )
        if re.search(r"board(\.py)?\s+ask\b", HEREDOC.sub(" ", cmd)) and not board.is_orchestrator(sid):
            block(
                "Refused: only the orchestrator asks Mario. Record what you need on your card: "
                f"{board_cmd(root)} block <card> --session {norm_sid(sid)} --need <desk|design|info|mario> --ask '...' --default '...'"
            )
        return

    if tool in ("SendMessage", "mcp__ccd_session_mgmt__send_message"):
        if board.is_orchestrator(sid) or board.is_dispatcher(sid):
            return
        orch = board.orchestrator()
        if not orch:
            return
        to = str(inp.get("to") or inp.get("session_id") or "")
        # A session's own subagents (Agent tool) are addressed by a bare agent
        # id, not a session name or a local_ id; they are this session, not a
        # peer, and the review cycle runs through them.
        if re.fullmatch(r"a[0-9a-f]{16}", to):
            return
        name = str(orch.get("name") or "")
        target = to.split(" [")[0].strip().lower()
        allowed = {name.lower(), "main"} if name else {"main"}
        if target in allowed or norm_sid(to) in Board.claim_ids(orch):
            return
        block(
            f"Refused: workers talk only to the orchestrator ({name or 'not named yet'}). A peer "
            "cannot resolve your blocker and cannot pass Mario's authority along. Message the "
            f"orchestrator, or record the blocker on your card: {board_cmd(board.root)} block <card> --session {norm_sid(sid)} --need <desk|design|info|mario> --ask '...' --default '...'"
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
    if board.is_orchestrator(sid) or board.is_dispatcher(sid):
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
    bc = board_cmd(board.root)
    bind = (
        ""
        if card
        else (
            f"\nBind a card first: {bc} list, then {bc} bind <id> --session "
            f"{norm_sid(sid)} (or {bc} new '<title>' --from <app> to create one)."
        )
    )
    block(
        f"This turn ends by handing back ('{m.group(0)}'), and a worker never hands back to Mario "
        f"({where}). Either take the next step now, or record why you cannot:\n"
        f"  {bc} block <card> --session {norm_sid(sid)} --need <desk|design|info|mario> "
        "--ask '<one line>' --default '<what happens if nobody answers>'\n"
        "then end the turn with one line saying the card is blocked." + bind
    )


def session_end(board, data):
    """SessionEnd: the session file says so, and every tree it held is free."""
    board.end_session(data.get("session_id", ""))


def session_start(board, data):
    sid = norm_sid(data.get("session_id", ""))
    board.note_session(sid, data.get("cwd", ""))
    orch = board.orchestrator()
    sess = board.session(sid)
    cid = sess.get("card")
    card = board.card(cid)
    bc = board_cmd(board.root)
    lines = []
    lines.append(f"[bugflow] Your session id is {sid}.")
    lines.append(
        "[bugflow] Your scratchpad is SHARED, not private: several agents run under one "
        f"session id. Write only inside <scratchpad>/{scratch_ns(board, sid, data.get('cwd', ''))}/ "
        "-- a write to the flat top level is refused, because every agent picks the same "
        "names and one of those collisions reached GitHub (card #314)."
    )
    if board.is_orchestrator(sid):
        lines.append(
            "[bugflow] You are the ORCHESTRATOR. Runbook: docs/workflow/orchestrator.md in the tree."
        )
    elif board.is_dispatcher(sid):
        lines.append("[bugflow] You are DISPATCH: Mario talks to you; you file cards and hand them to their owners. Runbook: docs/workflow/dispatch.md.")
    else:
        who = orch.get("name") or "not registered yet"
        lines.append(f"[bugflow] You are a WORKER. The orchestrator is: {who}.")
        if card:
            lines.append(
                f"[bugflow] Your card: #{card['id']} {card['title']} (state {card.get('state')}). Read it: {bc} show {card['id']}"
            )
        else:
            lines.append(
                f"[bugflow] No card bound. Before any edit: {bc} list, then {bc} bind <id> --session {sid}."
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
    CURRENT.update(root=root, sid=norm_sid(data.get("session_id")), tool=data.get("tool_name") or mode)
    if mode == "pretool":
        pretool(board, data)
    elif mode == "session-end":
        session_end(board, data)
    elif mode == "stop":
        stop(board, data)
    elif mode == "session-start":
        session_start(board, data)



def guarded_main():
    """Fail open, on purpose, and leave a trail.

    A hook that crashes on its own bug must not lock every session out of the
    repository, so anything unexpected here exits 0 (Claude Code treats that as
    "no opinion") after appending one line to <workspace>/.board/hook-errors.log,
    where the orchestrator can see that a check did not run. The only deliberate
    refusals are the exit-2 paths above, each of which names its remedy with the
    session id already filled in. A missing board, a missing switch file, or
    unreadable input are all "no opinion", never a block.
    """
    try:
        main()
    except SystemExit:
        raise
    except Exception as e:  # noqa: BLE001 - the point is to never lock anyone out
        try:
            root = find_root()
            if root is not None:
                with open(root / ".board" / "hook-errors.log", "a") as f:
                    f.write(f"{dt.datetime.now(dt.timezone.utc).isoformat()} {sys.argv[1:]} {type(e).__name__}: {e}\n")
        except Exception:
            pass
        sys.stderr.write(f"[bugflow] the guard could not run ({type(e).__name__}); letting this through and logging it\n")
        sys.exit(0)


if __name__ == "__main__":
    guarded_main()
