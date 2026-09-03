#!/usr/bin/env python3
"""The board: the only door into the workspace's shared state.

One card per piece of work. Sessions bind to a card, record blockers on it,
and move it; the orchestrator reads it and asks Mario through it; Mario's
inbox is the open blockers that need him. The hooks in scripts_local/hooks/
read the same files to decide what a session may do.

v0 stores JSON files under <workspace>/.board/. The web board replaces that
directory behind this same command line; nothing that calls `board` changes.

    board init
    board orchestrator --name Main --session <id>     who Mario's questions go through
    board integrator --session <id> [--release]       who may write firmware-next
    board new "<title>" --from <app> [--kind bug|feature|task] [--body "..."]
    board bind <id> --session <sid> [--tree wt/x] [--branch app/x]
    board block <id> --session <sid> --need desk|design|info|mario --ask "..." --default "..."
    board unblock <id> [--n N]
    board ask <id> --ask "..." --default "..."        orchestrator only (the hook enforces it)
    board answer <id> "<choice>" [--note "..."]
    board state <id> reported|triaged|working|review|merged|released|done|parked
    board show <id> | board list [--open] | board inbox | board import <file.md>

Session ids are the ones the SessionStart hook prints; nothing else identifies
a session from inside Bash, which is why every write that belongs to a session
takes --session explicitly.
"""

import argparse
import datetime as dt
import fcntl
import json
import os
import pathlib
import re
import subprocess
import sys

STATES = [
    "reported",
    "triaged",
    "working",
    "review",
    "merged",
    "released",
    "done",
    "parked",
]
NEEDS = ["desk", "design", "info", "mario", "device", "other"]


def now():
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def find_root():
    env = os.environ.get("BOARD_ROOT")
    if env:
        return pathlib.Path(env)
    here = pathlib.Path(__file__).resolve()
    for p in [here] + list(here.parents):
        if (p / "firmware-next").is_dir() and (p / "wt").is_dir():
            return p
    sys.exit(
        "board: cannot find the workspace root (a directory holding firmware-next/ and wt/)"
    )


def norm_sid(s):
    s = str(s or "")
    return s[6:] if s.startswith("local_") else s


class Store:
    def __init__(self, root):
        self.dir = root / ".board"
        self.cards = self.dir / "cards"
        self.sessions = self.dir / "sessions"

    def init(self):
        self.cards.mkdir(parents=True, exist_ok=True)
        self.sessions.mkdir(parents=True, exist_ok=True)
        nid = self.dir / "next_id"
        if not nid.exists():
            nid.write_text("1\n")

    def _lock(self):
        self.init()
        f = open(self.dir / ".lock", "w")
        fcntl.flock(f, fcntl.LOCK_EX)
        return f

    def read(self, path):
        try:
            with open(path) as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def write(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".tmp")
        with open(tmp, "w") as f:
            json.dump(data, f, indent=1, sort_keys=True)
        os.replace(tmp, path)

    def next_id(self):
        p = self.dir / "next_id"
        n = int((p.read_text() or "1").strip() or 1)
        p.write_text(f"{n + 1}\n")
        return n

    def card_path(self, cid):
        return self.cards / f"{int(cid)}.json"

    def card(self, cid):
        c = self.read(self.card_path(cid))
        if c is None:
            sys.exit(f"board: no card #{cid}")
        return c

    def save_card(self, c):
        c["updated"] = now()
        self.write(self.card_path(c["id"]), c)

    def all_cards(self):
        out = []
        if self.cards.is_dir():
            for p in sorted(self.cards.glob("*.json"), key=lambda p: int(p.stem)):
                c = self.read(p)
                if c:
                    out.append(c)
        return out

    def session(self, sid):
        return self.read(self.sessions / f"{norm_sid(sid)}.json") or {
            "session_id": norm_sid(sid)
        }

    def save_session(self, s):
        self.write(self.sessions / f"{s['session_id']}.json", s)


def hist(c, what):
    c.setdefault("history", []).append({"at": now(), "what": what})


def cmd_init(st, a):
    st.init()
    print(f"board: ready at {st.dir}")


def cmd_orchestrator(st, a):
    with st._lock():
        st.write(
            st.dir / "orchestrator.json",
            {"name": a.name, "session_id": norm_sid(a.session), "since": now()},
        )
    print(f"board: orchestrator is {a.name} ({norm_sid(a.session)})")


def cmd_integrator(st, a):
    p = st.dir / "integrator.json"
    with st._lock():
        if a.release:
            cur = st.read(p) or {}
            if cur and norm_sid(cur.get("session_id")) != norm_sid(a.session):
                sys.exit(
                    "board: the integration claim belongs to another session; it releases it, not you"
                )
            if p.exists():
                p.unlink()
            print("board: integration tree released")
            return
        cur = st.read(p)
        if cur and norm_sid(cur.get("session_id")) != norm_sid(a.session):
            sys.exit(
                f"board: integration tree is held by {cur.get('session_id')} since {cur.get('since')}; wait or ask the orchestrator"
            )
        st.write(p, {"session_id": norm_sid(a.session), "since": now()})
    print(f"board: integration tree claimed by {norm_sid(a.session)}")


def cmd_new(st, a):
    with st._lock():
        c = {
            "id": st.next_id(),
            "title": a.title,
            "from": a.from_app,
            "kind": a.kind,
            "body": a.body or "",
            "state": "reported",
            "created": now(),
            "updated": now(),
            "tree": None,
            "branch": None,
            "session": None,
            "blockers": [],
            "answers": [],
            "history": [],
        }
        hist(c, "created")
        st.save_card(c)
    print(f"#{c['id']} {c['title']}")


def cmd_bind(st, a):
    with st._lock():
        c = st.card(a.id)
        sid = norm_sid(a.session)
        c["session"] = sid
        if a.tree:
            c["tree"] = a.tree
        if a.branch:
            c["branch"] = a.branch
        if c["state"] in ("reported", "triaged"):
            c["state"] = "working"
        hist(c, f"bound to session {sid}" + (f" in {a.tree}" if a.tree else ""))
        st.save_card(c)
        s = st.session(sid)
        s["card"] = c["id"]
        st.save_session(s)
    print(f"#{c['id']} bound to {sid}")


def cmd_block(st, a):
    with st._lock():
        c = st.card(a.id)
        n = 1 + max([b["n"] for b in c["blockers"]] + [0])
        b = {
            "n": n,
            "need": a.need,
            "ask": a.ask,
            "default": a.default,
            "open": True,
            "created": now(),
            "by": norm_sid(a.session) if a.session else "orchestrator",
            "answer": None,
        }
        c["blockers"].append(b)
        hist(c, f"blocked ({a.need}): {a.ask}")
        st.save_card(c)
    print(f"#{c['id']} blocked on {a.need}: {a.ask}")


def cmd_ask(st, a):
    a.need = "mario"
    a.session = None
    cmd_block(st, a)


def cmd_unblock(st, a):
    with st._lock():
        c = st.card(a.id)
        for b in c["blockers"]:
            if b["open"] and (a.n is None or b["n"] == a.n):
                b["open"] = False
                hist(c, f"unblocked #{b['n']}")
        st.save_card(c)
    print(f"#{c['id']} unblocked")


def cmd_answer(st, a):
    with st._lock():
        c = st.card(a.id)
        target = None
        for b in c["blockers"]:
            if b["open"] and b["need"] == "mario":
                target = b
        if target is None:
            for b in c["blockers"]:
                if b["open"]:
                    target = b
        if target is None:
            sys.exit(f"board: #{c['id']} has no open blocker to answer")
        target["open"] = False
        target["answer"] = {"choice": a.choice, "note": a.note or "", "at": now()}
        c["answers"].append(
            {
                "blocker": target["n"],
                "choice": a.choice,
                "note": a.note or "",
                "at": now(),
            }
        )
        hist(c, f"answered #{target['n']}: {a.choice}")
        st.save_card(c)
    print(f"#{c['id']} answered: {a.choice}")


def cmd_state(st, a):
    with st._lock():
        c = st.card(a.id)
        c["state"] = a.state
        hist(c, f"state {a.state}")
        st.save_card(c)
    print(f"#{c['id']} {a.state}")


def derived(c):
    """What git and GitHub say about this card's branch. Never stored."""
    out = {}
    tree, branch = c.get("tree"), c.get("branch")
    if not (tree and branch):
        return out
    root = find_root()
    tdir = root / tree if not pathlib.Path(tree).is_absolute() else pathlib.Path(tree)
    try:
        r = subprocess.run(
            ["git", "-C", str(tdir), "rev-list", "--count", f"origin/xteink..{branch}"],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if r.returncode == 0:
            out["ahead"] = int(r.stdout.strip() or 0)
        r = subprocess.run(
            ["git", "-C", str(tdir), "status", "--porcelain"],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if r.returncode == 0:
            out["dirty"] = len([l for l in r.stdout.splitlines() if l.strip()])
    except (OSError, subprocess.SubprocessError, ValueError):
        pass
    try:
        r = subprocess.run(
            [
                "gh",
                "pr",
                "list",
                "-R",
                "ma-r-s/crossplay",
                "--head",
                branch,
                "--state",
                "all",
                "--json",
                "number,state",
                "--limit",
                "1",
            ],
            capture_output=True,
            text=True,
            timeout=15,
        )
        if r.returncode == 0 and r.stdout.strip():
            prs = json.loads(r.stdout)
            if prs:
                out["pr"] = prs[0]
    except (OSError, subprocess.SubprocessError, ValueError):
        pass
    return out


def fmt_card(c, full=False):
    ob = [b for b in c["blockers"] if b["open"]]
    flag = f"  BLOCKED({','.join(b['need'] for b in ob)})" if ob else ""
    line = f"#{c['id']:<4} {c['state']:<9} {c['from']:<14} {c['title']}{flag}"
    if not full:
        return line
    lines = [
        line,
        f"  kind {c['kind']}  created {c['created']}  updated {c['updated']}",
    ]
    if c.get("tree") or c.get("branch") or c.get("session"):
        lines.append(
            f"  tree {c.get('tree')}  branch {c.get('branch')}  session {c.get('session')}"
        )
    d = derived(c)
    if d:
        lines.append("  derived " + json.dumps(d))
    if c.get("body"):
        lines.append("  " + c["body"].replace("\n", "\n  "))
    for b in c["blockers"]:
        st = (
            "open"
            if b["open"]
            else f"closed: {b['answer']['choice'] if b.get('answer') else 'unblocked'}"
        )
        lines.append(
            f"  blocker {b['n']} [{b['need']}, {st}] {b['ask']}  | if nothing: {b['default']}"
        )
    for h in c.get("history", [])[-6:]:
        lines.append(f"  {h['at']}  {h['what']}")
    return "\n".join(lines)


def cmd_show(st, a):
    print(fmt_card(st.card(a.id), full=True))


def cmd_list(st, a):
    cards = st.all_cards()
    if a.open:
        cards = [c for c in cards if c["state"] not in ("done", "released", "parked")]
    if not cards:
        print("board: no cards")
        return
    for c in cards:
        print(fmt_card(c))


def cmd_inbox(st, a):
    n = 0
    for c in st.all_cards():
        for b in c["blockers"]:
            if b["open"] and b["need"] == "mario":
                n += 1
                print(f"From {c['from']} · #{c['id']} {c['title']}")
                if c.get("body"):
                    since = c["body"].splitlines()[-1]
                    since = re.sub(r"^\s*since( then)?:\s*", "", since, flags=re.I)
                    print(f"  Since: {since}")
                print(f"  Need from you: {b['ask']}")
                print(f"  If you do nothing: {b['default']}")
                print(f"  Answer: board answer {c['id']} '<choice>'")
                print()
    if not n:
        print("Nothing needs you.")


def cmd_import(st, a):
    text = pathlib.Path(a.file).read_text()
    sections = re.split(r"^## +", text, flags=re.M)[1:]
    made = 0
    with st._lock():
        for s in sections:
            head, _, body = s.partition("\n")
            head = head.strip()
            if not head:
                continue
            frm, title = (
                (head.split(":", 1) + [""])[:2] if ":" in head else ("general", head)
            )
            c = {
                "id": st.next_id(),
                "title": title.strip() or head,
                "from": frm.strip().lower(),
                "kind": a.kind,
                "body": body.strip(),
                "state": "reported",
                "created": now(),
                "updated": now(),
                "tree": None,
                "branch": None,
                "session": None,
                "blockers": [],
                "answers": [],
                "history": [],
            }
            hist(c, f"imported from {pathlib.Path(a.file).name}")
            st.save_card(c)
            made += 1
            print(f"#{c['id']} {c['title']}")
    print(f"board: imported {made} cards")


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="board",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("init").set_defaults(fn=cmd_init)
    s = sub.add_parser("orchestrator")
    s.add_argument("--name", required=True)
    s.add_argument("--session", required=True)
    s.set_defaults(fn=cmd_orchestrator)
    s = sub.add_parser("integrator")
    s.add_argument("--session", required=True)
    s.add_argument("--release", action="store_true")
    s.set_defaults(fn=cmd_integrator)
    s = sub.add_parser("new")
    s.add_argument("title")
    s.add_argument("--from", dest="from_app", required=True)
    s.add_argument("--kind", choices=["bug", "feature", "task"], default="task")
    s.add_argument("--body")
    s.set_defaults(fn=cmd_new)
    s = sub.add_parser("bind")
    s.add_argument("id", type=int)
    s.add_argument("--session", required=True)
    s.add_argument("--tree")
    s.add_argument("--branch")
    s.set_defaults(fn=cmd_bind)
    s = sub.add_parser("block")
    s.add_argument("id", type=int)
    s.add_argument("--session", required=True)
    s.add_argument("--need", choices=NEEDS, required=True)
    s.add_argument("--ask", required=True)
    s.add_argument("--default", required=True)
    s.set_defaults(fn=cmd_block)
    s = sub.add_parser("ask")
    s.add_argument("id", type=int)
    s.add_argument("--ask", required=True)
    s.add_argument("--default", required=True)
    s.set_defaults(fn=cmd_ask)
    s = sub.add_parser("unblock")
    s.add_argument("id", type=int)
    s.add_argument("--n", type=int)
    s.set_defaults(fn=cmd_unblock)
    s = sub.add_parser("answer")
    s.add_argument("id", type=int)
    s.add_argument("choice")
    s.add_argument("--note")
    s.set_defaults(fn=cmd_answer)
    s = sub.add_parser("state")
    s.add_argument("id", type=int)
    s.add_argument("state", choices=STATES)
    s.set_defaults(fn=cmd_state)
    s = sub.add_parser("show")
    s.add_argument("id", type=int)
    s.set_defaults(fn=cmd_show)
    s = sub.add_parser("list")
    s.add_argument("--open", action="store_true")
    s.set_defaults(fn=cmd_list)
    sub.add_parser("inbox").set_defaults(fn=cmd_inbox)
    s = sub.add_parser("import")
    s.add_argument("file")
    s.add_argument("--kind", choices=["bug", "feature", "task"], default="task")
    s.set_defaults(fn=cmd_import)

    a = p.parse_args(argv)
    st = Store(find_root())
    a.fn(st, a)


if __name__ == "__main__":
    main()
