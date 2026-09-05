#!/usr/bin/env python3
"""The board: the only door into the workspace's shared state.

One card per piece of work. Sessions bind to a card, record blockers on it,
and move it; the orchestrator reads it and asks Mario through it; Mario's
inbox is the open blockers that need him. The hooks in scripts_local/hooks/
read a local mirror of the same facts to decide what a session may do.

Two stores behind one command line. The file store keeps JSON under
<workspace>/.board/ and is what the tests drive. The Supabase store is the
real board (server/board/supabase/), selected automatically when
<workspace>/.board/supabase.env holds SUPABASE_URL and
SUPABASE_SERVICE_ROLE_KEY; BOARD_BACKEND=file forces the file store. The
Supabase store mirrors claims, session bindings and bound cards into the
file store so the hooks never need the network.

    board init
    board orchestrator --name Main --session <id> [--app-id local_<id>]   who Mario's questions go through
    board integrator --session <id> [--release]       who may write firmware-next
    board dispatcher --name Dispatch --session <id>   the session Mario talks to; may message anyone
    board pulse [add <host> <GET|POST> <url> <alive> <app> | remove <host>]   what the board probes every 30 min
    board release                                     is the release watcher awake, and what is it owed
    board new "<title>" --from <app> [--kind bug|feature|task] [--body "..."] [--parent <id>] [--default "..."]
    board app <id> <app> [--default "..."]             move a card to another app
    board parent <id> --of <parent>                    put a card under another (subtasks)
    board bind <id> --session <sid> [--tree wt/x] [--branch app/x]
    board block <id> --session <sid> --need desk|design|info|mario --ask "..." --default "..."
    board unblock <id> [--n N]
    board ask <id> --ask "..." --default "..." [--steps "1. ...\n2. ..."]   orchestrator only; steps for a thing to do
    board answer <id> "<choice>" [--note "..."]
    board state <id> reported|triaged|working|review|merged|released|done|parked
    board owner <app> [--session <sid>] [--tree wt/x]  who owns an app (lookup with no flags)
    board route <id>                                   which session a card goes to
    board tick                                         the issue sweep, then the open board
    board show <id> | board list [--open] | board inbox | board import <file.md>
    board sync                                         copy the file store into Supabase, once

Session ids are the ones the SessionStart hook prints; nothing else identifies
a session from inside Bash, which is why every write that belongs to a session
takes --session explicitly.

A card on app `mario` is an inbox item by construction: filing one there, or
moving one there, opens a `mario` blocker asking the card's title, because the
inbox lists open `mario` blockers and Mario reads nothing else. --default says
what happens if he never answers; without one it says so honestly.
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
import urllib.error
import urllib.parse
import urllib.request

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

# The app whose cards are Mario's own decisions, and what an auto-opened
# blocker says happens when he never answers one. The wording is the honest
# one on purpose: a blocker with no stated default forces him to engage before
# he can safely ignore it, which is how an inbox turns into noise.
MARIO_APP = "mario"
MARIO_DEFAULT = "nothing happens until he answers"
# A decision already taken is not one to ask again. The rule and the backfill
# in 20260905000300 both skip these, and they have to agree: a board restored
# by INSERTing a dump would otherwise flood the inbox with every settled
# decision it ever held, which is the flood the backfill was written to avoid.
SETTLED = ("done", "released", "parked")


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


def read_env(path):
    out = {}
    try:
        for line in pathlib.Path(path).read_text().splitlines():
            if "=" in line and not line.lstrip().startswith("#"):
                k, _, v = line.partition("=")
                out[k.strip()] = v.strip()
    except OSError:
        pass
    return out


def hist(c, what):
    c.setdefault("history", []).append({"at": now(), "what": what})


# ----------------------------------------------------------------------------
# The file store: JSON under <workspace>/.board/. The hooks read exactly these
# files, so the Supabase store mirrors into it.


class FileStore:
    name = "file"

    def __init__(self, root):
        self.root = root
        self.dir = root / ".board"
        self.cards = self.dir / "cards"
        self.sessions = self.dir / "sessions"

    def init(self):
        self.cards.mkdir(parents=True, exist_ok=True)
        self.sessions.mkdir(parents=True, exist_ok=True)
        nid = self.dir / "next_id"
        if not nid.exists():
            nid.write_text("1\n")

    def lock(self):
        self.init()
        f = open(self.dir / ".lock", "w")
        fcntl.flock(f, fcntl.LOCK_EX)
        return f

    def _read(self, path):
        try:
            with open(path) as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def _write(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".tmp")
        with open(tmp, "w") as f:
            json.dump(data, f, indent=1, sort_keys=True)
        os.replace(tmp, path)

    def _next_id(self):
        p = self.dir / "next_id"
        n = int((p.read_text() or "1").strip() or 1)
        p.write_text(f"{n + 1}\n")
        return n

    # cards
    def create_card(self, fields):
        c = {
            "id": fields.get("id") or self._next_id(),
            "title": fields["title"],
            "from": fields.get("from", "general"),
            "kind": fields.get("kind", "task"),
            "body": fields.get("body", ""),
            "state": fields.get("state", "reported"),
            "created": fields.get("created") or now(),
            "updated": now(),
            "tree": fields.get("tree"),
            "branch": fields.get("branch"),
            "session": fields.get("session"),
            "blockers": fields.get("blockers", []),
            "answers": fields.get("answers", []),
            "history": fields.get("history", []),
            "github_issue": fields.get("github_issue"),
            "parent": fields.get("parent"),
        }
        self.save_card(c)
        return c

    def get_card(self, cid):
        c = self._read(self.cards / f"{int(cid)}.json")
        if c is None:
            sys.exit(f"board: no card #{cid}")
        return c

    def save_card(self, c):
        c["updated"] = now()
        self._write(self.cards / f"{c['id']}.json", c)

    def set_blocker_default(self, cid, n, default):
        c = self.get_card(cid)
        for b in c["blockers"]:
            if b["n"] == n:
                b["default"] = default
        self.save_card(c)

    def list_cards(self):
        out = []
        if self.cards.is_dir():
            for p in sorted(self.cards.glob("*.json"), key=lambda p: int(p.stem)):
                c = self._read(p)
                if c:
                    out.append(c)
        return out

    # owners, claims, sessions
    def owners(self):
        return self._read(self.dir / "owners.json") or {}

    def set_owner(self, app, session, tree):
        o = self.owners()
        cur = o.get(app, {})
        o[app] = {
            "session": session or cur.get("session"),
            "tree": tree or cur.get("tree"),
            "since": now(),
        }
        self._write(self.dir / "owners.json", o)
        return o[app]

    def claim(self, name):
        return self._read(self.dir / f"{name}.json") or {}

    def set_claim(self, name, session, display_name=None, app_session=None):
        d = {"session_id": session, "since": now()}
        if display_name:
            d["name"] = display_name
        if app_session:
            d["app_session"] = app_session
        self._write(self.dir / f"{name}.json", d)

    def del_claim(self, name):
        p = self.dir / f"{name}.json"
        if p.exists():
            p.unlink()

    def session(self, sid):
        return self._read(self.sessions / f"{sid}.json") or {"session_id": sid}

    def save_session(self, s):
        self._write(self.sessions / f"{s['session_id']}.json", s)


# ----------------------------------------------------------------------------
# The Supabase store: PostgREST with the service key. Same card shape out.


class SupaStore:
    name = "supabase"
    SELECT = "*,blockers(*),history(*)"

    def __init__(self, root, url, key):
        self.root = root
        self.url = url.rstrip("/")
        self.key = key
        self.mirror = FileStore(root)

    def init(self):
        self.mirror.init()

    def lock(self):
        return self.mirror.lock()

    def _req(self, method, path, body=None, prefer=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(
            f"{self.url}/rest/v1/{path}", data=data, method=method
        )
        req.add_header("apikey", self.key)
        req.add_header("Authorization", f"Bearer {self.key}")
        req.add_header("Content-Type", "application/json")
        req.add_header("Accept", "application/json")
        if prefer:
            req.add_header("Prefer", prefer)
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                raw = r.read()
                return json.loads(raw) if raw else None
        except urllib.error.HTTPError as e:
            sys.exit(
                f"board: supabase {method} {path}: {e.code} {e.read().decode()[:300]}"
            )
        except urllib.error.URLError as e:
            sys.exit(f"board: supabase unreachable: {e.reason}")

    @staticmethod
    def _to_card(row):
        blockers = []
        for b in sorted(row.get("blockers") or [], key=lambda b: b["n"]):
            ans = None
            if b.get("answered_at"):
                ans = {
                    "choice": b.get("answer_choice"),
                    "note": b.get("answer_note") or "",
                    "at": b["answered_at"],
                }
            blockers.append(
                {
                    "id": b["id"],
                    "n": b["n"],
                    "need": b["need"],
                    "ask": b["ask"],
                    "default": b["default"],
                    "open": b["open"],
                    "created": b["created_at"],
                    "by": b.get("by_session"),
                    "steps": b.get("steps"),
                    "answer": ans,
                }
            )
        history = [
            {"at": h["at"], "what": h["what"]}
            for h in sorted(row.get("history") or [], key=lambda h: h["at"])
        ]
        return {
            "id": row["id"],
            "title": row["title"],
            "from": row["app"],
            "kind": row["kind"],
            "body": row["body"],
            "state": row["state"],
            "created": row["created_at"],
            "updated": row["updated_at"],
            "tree": row.get("tree"),
            "branch": row.get("branch"),
            "session": row.get("session"),
            "blockers": blockers,
            "answers": [
                {
                    "blocker": b["n"],
                    "choice": b["answer"]["choice"],
                    "note": b["answer"]["note"],
                    "at": b["answer"]["at"],
                }
                for b in blockers
                if b["answer"]
            ],
            "history": history,
            "source": row.get("source"),
            "device": row.get("device"),
            "version": row.get("version"),
            "reporter_email": row.get("reporter_email"),
            "photo_path": row.get("photo_path"),
            "github_issue": row.get("github_issue"),
            "parent": row.get("parent"),
        }

    def create_card(self, fields):
        body = {
            "title": fields["title"],
            "app": fields.get("from", "general"),
            "kind": fields.get("kind", "task"),
            "body": fields.get("body", ""),
            "state": fields.get("state", "reported"),
            "source": fields.get("source", "session"),
            "tree": fields.get("tree"),
            "branch": fields.get("branch"),
            "session": fields.get("session"),
            "github_issue": fields.get("github_issue"),
            "parent": fields.get("parent"),
        }
        if fields.get("id"):
            body["id"] = fields["id"]
        if fields.get("created"):
            body["created_at"] = fields["created"]
        rows = self._req(
            "POST", "cards?select=" + self.SELECT, body, prefer="return=representation"
        )
        c = self._to_card(rows[0])
        for h in fields.get("history", []):
            self._req(
                "POST",
                "history",
                {"card_id": c["id"], "at": h["at"], "what": h["what"]},
                prefer="return=minimal",
            )
        for b in fields.get("blockers", []):
            # on_conflict + merge-duplicates, because on a card whose app is
            # `mario` the insert trigger has already written blocker n=1 by the
            # time this runs, and a plain POST is then a unique violation that
            # aborts `board sync` mid-run. The caller's blocker wins.
            self._req(
                "POST",
                "blockers?on_conflict=card_id,n",
                {
                    "card_id": c["id"],
                    "n": b["n"],
                    "need": b["need"],
                    "ask": b["ask"],
                    "default": b.get("default", ""),
                    "open": b.get("open", True),
                    "by_session": b.get("by"),
                    "steps": b.get("steps"),
                    "created_at": b.get("created") or now(),
                    "answer_choice": (b.get("answer") or {}).get("choice"),
                    "answer_note": (b.get("answer") or {}).get("note"),
                    "answered_at": (b.get("answer") or {}).get("at"),
                },
                prefer="resolution=merge-duplicates,return=minimal",
            )
        return self.get_card(c["id"])

    def get_card(self, cid):
        rows = self._req("GET", f"cards?id=eq.{int(cid)}&select={self.SELECT}")
        if not rows:
            sys.exit(f"board: no card #{cid}")
        return self._to_card(rows[0])

    def save_card(self, c):
        """Persist the mutable top-level fields; blockers and history have their own writers."""
        body = {
            "title": c["title"],
            "app": c["from"],
            "kind": c["kind"],
            "body": c["body"],
            "state": c["state"],
            "tree": c.get("tree"),
            "branch": c.get("branch"),
            "session": c.get("session"),
            "parent": c.get("parent"),
        }
        self._req("PATCH", f"cards?id=eq.{c['id']}", body, prefer="return=minimal")
        if c.get("session"):
            self.mirror.save_card(dict(c))

    def add_history(self, cid, what):
        self._req(
            "POST", "history", {"card_id": cid, "what": what}, prefer="return=minimal"
        )

    def add_blocker(self, cid, need, ask, default, by, steps=None):
        c = self.get_card(cid)
        n = 1 + max([b["n"] for b in c["blockers"]] + [0])
        self._req(
            "POST",
            "blockers",
            {
                "card_id": cid,
                "n": n,
                "need": need,
                "ask": ask,
                "default": default,
                "by_session": by,
                "steps": steps,
            },
            prefer="return=minimal",
        )
        return n

    def close_blocker(self, cid, n, answer=None):
        body = {"open": False}
        if answer:
            body.update(
                {
                    "answer_choice": answer["choice"],
                    "answer_note": answer.get("note", ""),
                    "answered_at": now(),
                }
            )
        self._req(
            "PATCH",
            f"blockers?card_id=eq.{cid}&n=eq.{n}",
            body,
            prefer="return=minimal",
        )

    def set_blocker_default(self, cid, n, default):
        self._req(
            "PATCH",
            f"blockers?card_id=eq.{cid}&n=eq.{n}",
            {"default": default},
            prefer="return=minimal",
        )

    def list_cards(self):
        rows = self._req("GET", f"cards?select={self.SELECT}&order=id.asc") or []
        return [self._to_card(r) for r in rows]

    def owners(self):
        rows = self._req("GET", "owners?select=*") or []
        return {
            r["app"]: {
                "session": r.get("session"),
                "tree": r.get("tree"),
                "since": r.get("since"),
            }
            for r in rows
        }

    def set_owner(self, app, session, tree):
        cur = self.owners().get(app, {})
        row = {
            "app": app,
            "session": session or cur.get("session"),
            "tree": tree or cur.get("tree"),
            "since": now(),
        }
        self._req(
            "POST", "owners", row, prefer="resolution=merge-duplicates,return=minimal"
        )
        self.mirror.set_owner(app, row["session"], row["tree"])
        return row

    def claim(self, name):
        rows = self._req("GET", f"claims?name=eq.{name}&select=*") or []
        if not rows:
            return {}
        r = rows[0]
        return {
            "session_id": r["session"],
            "since": r["since"],
            "name": r.get("display_name"),
            "app_session": r.get("app_session"),
        }

    def set_claim(self, name, session, display_name=None, app_session=None):
        self._req(
            "POST",
            "claims",
            {
                "name": name,
                "session": session,
                "display_name": display_name,
                "app_session": app_session,
                "since": now(),
            },
            prefer="resolution=merge-duplicates,return=minimal",
        )
        self.mirror.set_claim(name, session, display_name, app_session)

    def del_claim(self, name):
        self._req("DELETE", f"claims?name=eq.{name}", prefer="return=minimal")
        self.mirror.del_claim(name)

    def session(self, sid):
        rows = (
            self._req("GET", f"sessions?id=eq.{urllib.parse.quote(sid)}&select=*") or []
        )
        if not rows:
            return {"session_id": sid}
        r = rows[0]
        return {"session_id": r["id"], "cwd": r.get("cwd"), "card": r.get("card_id")}

    def save_session(self, s):
        self._req(
            "POST",
            "sessions",
            {
                "id": s["session_id"],
                "cwd": s.get("cwd"),
                "card_id": s.get("card"),
                "last_seen": now(),
            },
            prefer="resolution=merge-duplicates,return=minimal",
        )
        self.mirror.save_session(s)


def open_store(root):
    env = read_env(root / ".board" / "supabase.env")
    if (
        os.environ.get("BOARD_BACKEND", "").lower() != "file"
        and env.get("SUPABASE_URL")
        and env.get("SUPABASE_SERVICE_ROLE_KEY")
    ):
        return SupaStore(root, env["SUPABASE_URL"], env["SUPABASE_SERVICE_ROLE_KEY"])
    return FileStore(root)


# ----------------------------------------------------------------------------
# Commands. They speak in cards; the store decides where cards live.


def card_history(st, c, what):
    if isinstance(st, SupaStore):
        st.add_history(c["id"], what)
    else:
        hist(c, what)


def cmd_init(st, a):
    st.init()
    print(f"board: ready ({st.name})")


def _kept_app_id(st, name, given):
    """A re-registration without --app-id keeps the app id already on the claim.
    The hook-visible id changes when a session restarts; the desktop app's id
    does not, and dropping it silently cut every worker off from Main once."""
    if given:
        return norm_sid(given)
    cur = st.claim(name) or {}
    return norm_sid(cur.get("app_session")) or None


def cmd_pulse(st, a):
    """The hosts the board probes every 30 minutes (pulse_targets)."""
    if not hasattr(st, "_req"):
        print(
            "board: the pulse runs on the board; listing or changing its hosts needs the Supabase store (.board/supabase.env)"
        )
        return
    if a.action == "add":
        if not (a.host and a.method and a.url and a.alive and a.app):
            sys.exit("usage: board pulse add <host> <GET|POST> <url> <alive> <app>")
        st._req(
            "POST",
            "pulse_targets",
            {
                "host": a.host,
                "method": a.method.upper(),
                "url": a.url,
                "alive": a.alive,
                "app": a.app,
                "enabled": True,
            },
            prefer="resolution=merge-duplicates,return=minimal",
        )
        print(
            f"board: pulse probes {a.host} ({a.method.upper()} {a.url}, alive {a.alive}) for {a.app}"
        )
        return
    if a.action == "remove":
        if not a.host:
            sys.exit("usage: board pulse remove <host>")
        st._req("DELETE", f"pulse_targets?host=eq.{a.host}", prefer="return=minimal")
        print(f"board: pulse no longer probes {a.host}")
        return
    rows = st._req("GET", "pulse_targets?select=*&order=host") or []
    print(f"{'HOST':<10} {'METHOD':<6} {'ALIVE':<12} {'APP':<11} URL")
    for r in rows:
        flag = "" if r.get("enabled", True) else "  (disabled)"
        print(
            f"{r['host']:<10} {r['method']:<6} {r['alive']:<12} {r['app']:<11} {r['url']}{flag}"
        )


def cmd_release(st, a):
    """What the release watcher can see: whether it is armed, when it last got
    an answer out of GitHub, what it is still owed, and every fault it has
    already had its say about. The watcher opens its own cards; this is for the
    question those cards cannot answer, which is whether it is still awake."""
    if not hasattr(st, "_req"):
        print(
            "board: the release watcher runs on the board; reading it needs the Supabase store (.board/supabase.env)"
        )
        return
    rows = st._req("GET", "release_state?select=*") or []
    if not rows:
        print("board: the release watcher is not installed on this board")
        return
    st8 = rows[0]
    armed = (
        "armed"
        if st8.get("seeded")
        else "NOT ARMED (its next pass adjudicates the history)"
    )
    print(f"watcher   {armed}, last answer from GitHub {st8.get('last_ok_at')}")
    pend = st._req("GET", "release_pending?select=*&order=version") or []
    if pend:
        print(
            "owed      "
            + ", ".join(f"{r['version']} (tagged {r.get('at')})" for r in pend)
        )
    else:
        print("owed      nothing: every version the pipeline tagged is published")
    seen = st._req("GET", "release_seen?select=*&order=first_seen.desc&limit=12") or []
    if seen:
        print(f"{'SAID ITS SAY ABOUT':<28} WHEN")
        for r in seen:
            print(
                f"{r['key']:<28} {str(r.get('first_seen'))[:19]}  {(r.get('note') or '')[:60]}"
            )


def cmd_orchestrator(st, a):
    with st.lock():
        app = _kept_app_id(st, "orchestrator", a.app_id)
        st.set_claim("orchestrator", norm_sid(a.session), a.name, app)
    print(
        f"board: orchestrator is {a.name} ({norm_sid(a.session)}{', app ' + app if app else ''})"
    )


def cmd_dispatcher(st, a):
    with st.lock():
        app = _kept_app_id(st, "dispatcher", a.app_id)
        st.set_claim("dispatcher", norm_sid(a.session), a.name, app)
    print(
        f"board: dispatcher is {a.name} ({norm_sid(a.session)}{', app ' + app if app else ''})"
    )


def cmd_integrator(st, a):
    with st.lock():
        cur = st.claim("integrator")
        if a.release:
            if cur and norm_sid(cur.get("session_id")) != norm_sid(a.session):
                sys.exit(
                    "board: the integration claim belongs to another session; it releases it, not you"
                )
            st.del_claim("integrator")
            print("board: integration tree released")
            return
        if cur and norm_sid(cur.get("session_id")) != norm_sid(a.session):
            sys.exit(
                f"board: integration tree is held by {cur.get('session_id')} since {cur.get('since')}; wait or ask the orchestrator"
            )
        st.set_claim(
            "integrator",
            norm_sid(a.session),
            None,
            _kept_app_id(st, "integrator", a.app_id),
        )
    print(f"board: integration tree claimed by {norm_sid(a.session)}")


def cmd_new(st, a):
    with st.lock():
        c = st.create_card(
            {
                "title": a.title,
                "from": a.from_app.lower(),
                "kind": a.kind,
                "body": a.body or "",
                "parent": a.parent,
                "history": [{"at": now(), "what": "created"}],
            }
        )
        ensure_inbox(st, c["id"], a.default, "board", adopt=True)
        c = st.get_card(c["id"])
    # The card's own state, not whether this call did the filing: on the
    # Supabase store the trigger files inside the INSERT, and a marker keyed on
    # "did I file it" would go silent exactly where the SQL enforcer works.
    inbox = any(b["open"] and b["need"] == "mario" for b in c["blockers"])
    print(f"#{c['id']} {c['title']}" + ("  (in Mario's inbox)" if inbox else ""))


def cmd_bind(st, a):
    with st.lock():
        c = st.get_card(a.id)
        sid = norm_sid(a.session)
        c["session"] = sid
        if a.tree:
            c["tree"] = a.tree
        if a.branch:
            c["branch"] = a.branch
        if c["state"] in ("reported", "triaged"):
            c["state"] = "working"
        card_history(
            st, c, f"bound to session {sid}" + (f" in {a.tree}" if a.tree else "")
        )
        st.save_card(c)
        s = st.session(sid)
        s["card"] = c["id"]
        st.save_session(s)
    print(f"#{c['id']} bound to {sid}")


def _block(st, cid, need, ask, default, by, steps=None):
    c = st.get_card(cid)
    if isinstance(st, SupaStore):
        n = st.add_blocker(cid, need, ask, default, by, steps)
        st.add_history(cid, f"blocked ({need}): {ask}")
        if c.get("session"):
            st.mirror.save_card(st.get_card(cid))
    else:
        n = 1 + max([b["n"] for b in c["blockers"]] + [0])
        c["blockers"].append(
            {
                "n": n,
                "need": need,
                "ask": ask,
                "default": default,
                "open": True,
                "created": now(),
                "by": by,
                "steps": steps,
                "answer": None,
            }
        )
        hist(c, f"blocked ({need}): {ask}")
        st.save_card(c)
    return c, n


def ensure_inbox(st, cid, default=None, by="board", adopt=False):
    """A card on app `mario` is an item in Mario's inbox, by construction.

    The inbox is the open `mario` blockers and nothing else, so a card filed on
    the app that already means "only Mario decides this" was invisible to him
    until someone remembered to block on it. Twice nobody did (cards 75 and 84
    aged a day in `reported`), which is a dropped message, not a delay.

    Idempotent on the only thing that matters -- whether an open `mario`
    blocker is already there -- so re-running it, or moving a card to `mario`
    twice, never files a second one.
    """
    c = st.get_card(cid)
    if str(c.get("from", "")).lower() != MARIO_APP or c["state"] in SETTLED:
        return False
    want = (default or "").strip() or MARIO_DEFAULT
    already = [b for b in c["blockers"] if b["open"] and b["need"] == "mario"]
    if already:
        # On the Supabase store the trigger opens this blocker inside the
        # card's own INSERT or UPDATE, so the CLI arrives to find the work
        # done and its --default dropped on the floor. `adopt` is the caller
        # saying it knows none was open before it wrote, so the blocker it
        # found is that one and the words it was given belong on it.
        if adopt and (default or "").strip():
            st.set_blocker_default(cid, already[-1]["n"], want)
            return True
        return False
    _block(st, cid, "mario", c["title"], want, by)
    return True


def cmd_app(st, a):
    """Move a card to another app, and into Mario's inbox when the app is his."""
    app = a.app.lower()
    with st.lock():
        c = st.get_card(a.id)
        was = str(c.get("from", "")).lower()
        had = any(b["open"] and b["need"] == "mario" for b in c["blockers"])
        moved = was != app
        if moved:
            c["from"] = app
            card_history(st, c, f"moved to app {app}")
            st.save_card(c)
        # Only an actual move files, because only an actual move is something
        # the SQL trigger can see (`old.app is distinct from new.app`). A CLI
        # that also re-asked on a no-op would be a rule with two answers.
        took = (
            ensure_inbox(st, c["id"], a.default, "board", adopt=not had)
            if moved
            else False
        )
        c = st.get_card(c["id"])
    ob = [b for b in c["blockers"] if b["open"] and b["need"] == "mario"]
    print(f"#{c['id']} -> {app}" + ("  (in Mario's inbox)" if ob else ""))
    # Say when the words the filer typed went nowhere. A --default silently
    # dropped is the failure the default exists to prevent.
    if (a.default or "").strip() and not took:
        why = (
            f"it already had an open mario blocker (#{ob[0]['n']})"
            if ob and had
            else f"it is {c['state']} on app {app}, so nothing was filed"
        )
        print(f"board: --default not applied to #{c['id']}: {why}")
    # Moving a card off his desk does not withdraw what he was asked. Removing
    # an item from his inbox without an answer is the dropped message this rule
    # exists to prevent, so it is said out loud and left for a person.
    if was == MARIO_APP and app != MARIO_APP and ob:
        for b in ob:
            print(
                f"board: #{c['id']} is still in Mario's inbox on blocker #{b['n']}"
                f" ({b['ask']}); answer it or: board unblock {c['id']} --n {b['n']}"
            )


def cmd_block(st, a):
    with st.lock():
        c, n = _block(st, a.id, a.need, a.ask, a.default, norm_sid(a.session), a.steps)
    print(f"#{c['id']} blocked on {a.need}: {a.ask}")


def cmd_ask(st, a):
    with st.lock():
        c, n = _block(st, a.id, "mario", a.ask, a.default, "orchestrator", a.steps)
    print(f"#{c['id']} asked Mario: {a.ask}")


def cmd_unblock(st, a):
    with st.lock():
        c = st.get_card(a.id)
        for b in c["blockers"]:
            if b["open"] and (a.n is None or b["n"] == a.n):
                b["open"] = False
                if isinstance(st, SupaStore):
                    st.close_blocker(c["id"], b["n"])
                    st.add_history(c["id"], f"unblocked #{b['n']}")
                else:
                    hist(c, f"unblocked #{b['n']}")
        if not isinstance(st, SupaStore):
            st.save_card(c)
    print(f"#{c['id']} unblocked")


def cmd_answer(st, a):
    with st.lock():
        c = st.get_card(a.id)
        target = None
        if a.n is not None:
            for b in c["blockers"]:
                if b["open"] and b["n"] == a.n:
                    target = b
            if target is None:
                sys.exit(f"board: #{c['id']} has no open blocker #{a.n}")
        else:
            # More than one open `mario` blocker used to answer the LAST one
            # silently, so an answer typed against the first line of the inbox
            # landed on a different question. Rare before this rule; routine
            # once every card on app mario carries one of its own.
            asks = [b for b in c["blockers"] if b["open"] and b["need"] == "mario"]
            if len(asks) > 1:
                sys.exit(
                    f"board: #{c['id']} has {len(asks)} open blockers that need Mario; "
                    "say which with --n: "
                    + ", ".join(f"#{b['n']} {b['ask']}" for b in asks)
                )
            target = asks[0] if asks else None
        if target is None:
            for b in c["blockers"]:
                if b["open"]:
                    target = b
        if target is None:
            sys.exit(f"board: #{c['id']} has no open blocker to answer")
        answer = {"choice": a.choice, "note": a.note or "", "at": now()}
        if isinstance(st, SupaStore):
            st.close_blocker(c["id"], target["n"], answer)
            st.add_history(c["id"], f"answered #{target['n']}: {a.choice}")
        else:
            target["open"] = False
            target["answer"] = answer
            c["answers"].append(
                {
                    "blocker": target["n"],
                    "choice": a.choice,
                    "note": a.note or "",
                    "at": answer["at"],
                }
            )
            hist(c, f"answered #{target['n']}: {a.choice}")
            st.save_card(c)
    print(f"#{c['id']} answered: {a.choice}")


def cmd_state(st, a):
    with st.lock():
        c = st.get_card(a.id)
        c["state"] = a.state
        card_history(st, c, f"state {a.state}")
        st.save_card(c)
    print(f"#{c['id']} {a.state}")


def cmd_parent(st, a):
    """Put a card under another card (subtasks)."""
    with st.lock():
        c = st.get_card(a.id)
        if a.of is not None:
            st.get_card(a.of)
            if a.of == c["id"]:
                sys.exit("board: a card cannot be its own parent")
        c["parent"] = a.of
        card_history(st, c, f"under #{a.of}" if a.of is not None else "no parent")
        st.save_card(c)
    print(f"#{c['id']} -> parent {a.of}")


def cmd_owner(st, a):
    with st.lock():
        if a.session or a.tree:
            st.set_owner(
                a.app.lower(), norm_sid(a.session) if a.session else None, a.tree
            )
        o = st.owners().get(a.app.lower())
    if o:
        print(f"{a.app}: session {o.get('session')}  tree {o.get('tree')}")
    else:
        print(f"{a.app}: no owner")


def cmd_route(st, a):
    c = st.get_card(a.id)
    o = st.owners().get(str(c.get("from", "")).lower())
    if o and o.get("session"):
        print(
            f"#{c['id']} -> {c['from']} owner session {o['session']} (tree {o.get('tree')})"
        )
    else:
        print(f"#{c['id']} -> {c['from']} has no owner; start a worker")


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
        st_ = (
            "open"
            if b["open"]
            else f"closed: {b['answer']['choice'] if b.get('answer') else 'unblocked'}"
        )
        lines.append(
            f"  blocker {b['n']} [{b['need']}, {st_}] {b['ask']}  | if nothing: {b['default']}"
        )
        if b.get("steps"):
            lines.append(
                "    how: "
                + " / ".join(
                    l.strip() for l in str(b["steps"]).splitlines() if l.strip()
                )
            )
    for h in c.get("history", [])[-6:]:
        lines.append(f"  {h['at']}  {h['what']}")
    return "\n".join(lines)


def cmd_show(st, a):
    c = st.get_card(a.id)
    print(fmt_card(c, full=True))
    for k in st.list_cards():
        if k.get("parent") == c["id"]:
            print("    " + fmt_card(k))


def cmd_list(st, a):
    cards = st.list_cards()
    if a.open:
        cards = [c for c in cards if c["state"] not in ("done", "released", "parked")]
    if not cards:
        print("board: no cards")
        return
    by_id = {c["id"]: c for c in cards}
    children = {}
    for c in cards:
        if c.get("parent") in by_id:
            children.setdefault(c["parent"], []).append(c)
    for c in cards:
        if c.get("parent") in by_id:
            continue
        print(fmt_card(c))
        for k in children.get(c["id"], []):
            print("    " + fmt_card(k))


def cmd_inbox(st, a):
    n = 0
    for c in st.list_cards():
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
                if b.get("steps"):
                    for line in str(b["steps"]).splitlines():
                        if line.strip():
                            print(f"  How: {line.strip()}")
                print(f"  Answer: board answer {c['id']} '<choice>' --n {b['n']}")
                print()
    if not n:
        print("Nothing needs you.")


def cmd_import(st, a):
    text = pathlib.Path(a.file).read_text()
    sections = re.split(r"^## +", text, flags=re.M)[1:]
    made = 0
    with st.lock():
        for s in sections:
            head, _, body = s.partition("\n")
            head = head.strip()
            if not head:
                continue
            frm, title = (
                (head.split(":", 1) + [""])[:2] if ":" in head else ("general", head)
            )
            c = st.create_card(
                {
                    "title": title.strip() or head,
                    "from": frm.strip().lower(),
                    "kind": a.kind,
                    "body": body.strip(),
                    "source": "import",
                    "history": [
                        {
                            "at": now(),
                            "what": f"imported from {pathlib.Path(a.file).name}",
                        }
                    ],
                }
            )
            ensure_inbox(st, c["id"])
            made += 1
            print(f"#{c['id']} {c['title']}")
    print(f"board: imported {made} cards")


def guess_app(title, labels, owners):
    """The app an issue is about: an app:<name> label, else an owner's name in the title."""
    for l in labels:
        if l.lower().startswith("app:"):
            return l[4:].strip().lower()
    t = title.lower()
    for app in sorted(owners, key=len, reverse=True):
        if app and app in t:
            return app
    if "read" in t or "page turn" in t or "epub" in t:
        return "reader"
    return "unknown"


def cmd_issues(st, a):
    """Open GitHub issues become cards, once each; released cards close their issue."""
    if a.from_json:
        issues = json.loads(pathlib.Path(a.from_json).read_text())
    else:
        r = subprocess.run(
            [
                "gh",
                "issue",
                "list",
                "-R",
                a.repo,
                "--state",
                "open",
                "--limit",
                "200",
                "--json",
                "number,title,body,labels,url,author,createdAt",
            ],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if r.returncode != 0:
            sys.exit(f"board: gh issue list failed: {r.stderr.strip()[:200]}")
        issues = json.loads(r.stdout or "[]")
    cards = st.list_cards()
    known = {c.get("github_issue"): c for c in cards if c.get("github_issue")}
    owners = st.owners()
    made = 0
    with st.lock():
        for i in issues:
            labels = [
                l["name"] if isinstance(l, dict) else str(l)
                for l in (i.get("labels") or [])
            ]
            if i["number"] in known:
                continue
            kind = (
                "feature"
                if any(l.lower() in ("enhancement", "feature", "idea") for l in labels)
                else "bug"
            )
            author = i.get("author")
            author = author.get("login") if isinstance(author, dict) else author
            body = (i.get("body") or "").strip()
            c = st.create_card(
                {
                    "title": i["title"].strip()[:120],
                    "from": guess_app(i["title"], labels, owners),
                    "kind": kind,
                    "body": f"GitHub issue #{i['number']} by {author or 'someone'}: {i.get('url', '')}\n\n{body}",
                    "state": "reported",
                    "source": "github",
                    "github_issue": i["number"],
                    "history": [
                        {"at": now(), "what": f"from GitHub issue #{i['number']}"}
                    ],
                }
            )
            ensure_inbox(st, c["id"])
            made += 1
            print(f"#{c['id']} <- issue #{i['number']} {c['title']}")
    closed = 0
    if a.close_released:
        for c in cards:
            if c.get("github_issue") and c["state"] in ("released", "done"):
                msg = (
                    f"Shipped. This is card #{c['id']} on the board and went out in a release; "
                    "open a new issue if it comes back."
                )
                r = subprocess.run(
                    [
                        "gh",
                        "issue",
                        "close",
                        str(c["github_issue"]),
                        "-R",
                        a.repo,
                        "--comment",
                        msg,
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                if r.returncode == 0:
                    closed += 1
                    card_history(st, c, f"closed GitHub issue #{c['github_issue']}")
                    if not isinstance(st, SupaStore):
                        st.save_card(c)
    print(f"board: {made} new card(s) from issues, {closed} issue(s) closed")


def cmd_tick(st, a):
    """A tick's read in one command: sweep GitHub for new issues, then the open board.

    Never closes an issue. Closing stays a command typed on purpose.
    """
    ns = argparse.Namespace(repo=a.repo, from_json=a.from_json, close_released=False)
    cmd_issues(st, ns)
    print()
    cmd_list(st, argparse.Namespace(open=True))


def cmd_sync(st, a):
    """Copy the file store into Supabase, keeping ids. Run once, then the file store is only a mirror."""
    if not isinstance(st, SupaStore):
        sys.exit(
            "board: sync needs the Supabase store (is .board/supabase.env present?)"
        )
    files = FileStore(st.root)
    existing = {c["id"] for c in st.list_cards()}
    made = 0
    for c in files.list_cards():
        if c["id"] in existing:
            continue
        st.create_card(
            {
                "id": c["id"],
                "title": c["title"],
                "from": c["from"],
                "kind": c["kind"],
                "body": c["body"],
                "state": c["state"],
                "created": c.get("created"),
                "tree": c.get("tree"),
                "branch": c.get("branch"),
                "session": c.get("session"),
                "blockers": c.get("blockers", []),
                "history": c.get("history", []),
                "source": "import",
            }
        )
        made += 1
    for app, o in files.owners().items():
        st.set_owner(app, o.get("session"), o.get("tree"))
    for name in ("orchestrator", "integrator", "dispatcher"):
        cl = files.claim(name)
        if cl.get("session_id"):
            st.set_claim(name, cl["session_id"], cl.get("name"))
    print(f"board: synced {made} cards, {len(files.owners())} owners")


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="board",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("init").set_defaults(fn=cmd_init)
    app_help = "the desktop app's local_... id (get_session self), so messages addressed that way reach you"
    s = sub.add_parser("pulse")
    s.add_argument(
        "action", nargs="?", default="list", choices=["list", "add", "remove"]
    )
    s.add_argument("host", nargs="?")
    s.add_argument("method", nargs="?")
    s.add_argument("url", nargs="?")
    s.add_argument("alive", nargs="?")
    s.add_argument("app", nargs="?")
    s.set_defaults(fn=cmd_pulse)
    sub.add_parser("release").set_defaults(fn=cmd_release)
    s = sub.add_parser("orchestrator")
    s.add_argument("--name", required=True)
    s.add_argument("--session", required=True)
    s.add_argument("--app-id", help=app_help)
    s.set_defaults(fn=cmd_orchestrator)
    s = sub.add_parser("dispatcher")
    s.add_argument("--name", required=True)
    s.add_argument("--session", required=True)
    s.add_argument("--app-id", help=app_help)
    s.set_defaults(fn=cmd_dispatcher)
    s = sub.add_parser("integrator")
    s.add_argument("--session", required=True)
    s.add_argument("--app-id", help=app_help)
    s.add_argument("--release", action="store_true")
    s.set_defaults(fn=cmd_integrator)
    s = sub.add_parser("new")
    s.add_argument("title")
    s.add_argument("--parent", type=int)
    s.add_argument("--from", dest="from_app", required=True)
    s.add_argument("--kind", choices=["bug", "feature", "task"], default="task")
    s.add_argument("--body")
    s.add_argument(
        "--default",
        help="on app mario: what happens if he never answers (a card filed there opens a mario blocker by itself)",
    )
    s.set_defaults(fn=cmd_new)
    s = sub.add_parser("app", help="move a card to another app")
    s.add_argument("id", type=int)
    s.add_argument("app")
    s.add_argument(
        "--default",
        help="on app mario: what happens if he never answers",
    )
    s.set_defaults(fn=cmd_app)
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
    s.add_argument(
        "--steps", help="numbered lines, one per line, for a thing Mario must do"
    )
    s.set_defaults(fn=cmd_block)
    s = sub.add_parser("ask")
    s.add_argument("id", type=int)
    s.add_argument("--ask", required=True)
    s.add_argument("--default", required=True)
    s.add_argument(
        "--steps", help="numbered lines, one per line, for a thing Mario must do"
    )
    s.set_defaults(fn=cmd_ask)
    s = sub.add_parser("unblock")
    s.add_argument("id", type=int)
    s.add_argument("--n", type=int)
    s.set_defaults(fn=cmd_unblock)
    s = sub.add_parser("answer")
    s.add_argument("id", type=int)
    s.add_argument("choice")
    s.add_argument(
        "--n", type=int, help="which blocker; required when more than one needs Mario"
    )
    s.add_argument("--note")
    s.set_defaults(fn=cmd_answer)
    s = sub.add_parser("state")
    s.add_argument("id", type=int)
    s.add_argument("state", choices=STATES)
    s.set_defaults(fn=cmd_state)
    s = sub.add_parser("parent")
    s.add_argument("id", type=int)
    s.add_argument("--of", type=int)
    s.set_defaults(fn=cmd_parent)
    s = sub.add_parser("owner")
    s.add_argument("app")
    s.add_argument("--session")
    s.add_argument("--tree")
    s.set_defaults(fn=cmd_owner)
    s = sub.add_parser("route")
    s.add_argument("id", type=int)
    s.set_defaults(fn=cmd_route)
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
    sub.add_parser("sync").set_defaults(fn=cmd_sync)
    s = sub.add_parser("issues")
    s.add_argument("--repo", default="ma-r-s/crossplay")
    s.add_argument("--from-json")
    s.add_argument("--close-released", action="store_true")
    s.set_defaults(fn=cmd_issues)
    s = sub.add_parser("tick", help="the issue sweep, then the open board")
    s.add_argument("--repo", default="ma-r-s/crossplay")
    s.add_argument("--from-json")
    s.set_defaults(fn=cmd_tick)

    a = p.parse_args(argv)
    st = open_store(find_root())
    a.fn(st, a)


if __name__ == "__main__":
    main()
