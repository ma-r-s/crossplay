"""The HTTP surface: a tiny human side and a smaller device side.

Human (browser, session cookie): / sign-in, /pair claim page, /devices list
with revoke. The session cookie is Fernet-sealed JSON, SameSite=Lax +
HttpOnly, and every state-changing form carries a CSRF token bound to the
session.

Device (bearer token): pairing start/poll/abandon, sync start/status, article
download. The device never sees an error code, so every refusal here is a
sentence the firmware shows verbatim.

Structural twin of server/study-bridge/bridge/app.py. The pages, the session
sealing, the rate-limit shape and the pairing endpoints are the same design;
the sync body and the article endpoint are this service's own.
"""

import asyncio
import json
import logging
import secrets
import time

from fastapi import Depends, FastAPI, Request
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, RedirectResponse

from . import accounts, engine, jobs, pairing, store
from .lockout import Lockout

log = logging.getLogger("bridge.app")

app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)

# The device posts its whole index plus its archive queue. 120 articles of
# {id, hash, progress, progressAt} is a few kilobytes; the cap is four orders
# of magnitude above that and exists only to bound a hostile client.
MAX_SYNC_BODY = 256 * 1024


class Window:
    def __init__(self, limit: int, per_s: int):
        self.limit, self.per_s = limit, per_s
        self.hits: dict[str, list[float]] = {}

    def allow(self, key: str) -> bool:
        now = time.time()
        hits = [t for t in self.hits.get(key, []) if t > now - self.per_s]
        if len(hits) >= self.limit:
            self.hits[key] = hits
            return False
        hits.append(now)
        self.hits[key] = hits
        return True


LOGIN_IP = Window(5, 300)
# There is deliberately NO flat per-username Window beside LOGIN_LOCKOUT. There
# was, and it shadowed the lockout completely: both key on the username, the
# flat one fired first, and its cruder message was the only one a locked
# account ever saw. Two limiters on one key, the weaker one winning, is worse
# than either alone -- it hides which mechanism is acting. Exponential backoff
# on FAILURES strictly dominates a flat cap on ATTEMPTS here, because an
# unlimited number of SUCCESSFUL sign-ins is harmless: you already have the
# password.
# A GLOBAL ceiling on sign-in, which /api/sync has had from the start and the
# endpoint that actually matters did not. Per-IP and per-username counters are
# both defeated by having many of each; this one is not, because there is only
# one of it.
#
# Set well above any plausible real rate -- a person signs in once and then
# their reader syncs on a token -- so it is a backstop rather than a throttle.
# It does mean a flood can deny sign-in to everyone while it lasts, which is
# the accepted trade: a bounded oracle that is briefly unavailable beats an
# unbounded one that is always up.
GLOBAL_LOGIN = Window(30, 60)
# And the per-username exponential backoff the design named as a precondition
# for opening registration. Counts failures, not attempts.
LOGIN_LOCKOUT = Lockout()
PAIR_IP = Window(10, 300)
SYNC_USER = Window(8, 300)
GLOBAL_SYNC = Window(60, 60)


def client_ip(request: Request) -> str:
    return request.headers.get("cf-connecting-ip") or (
        request.client.host if request.client else "?"
    )


# ------------------------------------------------------------------- sessions
def _seal(data: dict) -> str:
    return store.fernet().encrypt(json.dumps(data).encode()).decode()


def _unseal(cookie: str | None) -> dict | None:
    if not cookie:
        return None
    try:
        return json.loads(store.fernet().decrypt(cookie.encode(), ttl=86400))
    except Exception:
        return None


def session_of(request: Request) -> dict | None:
    return _unseal(request.cookies.get("read_session"))


class Unauthorized(Exception):
    pass


def require_session(request: Request) -> dict:
    s = session_of(request)
    if not s:
        raise Unauthorized("sign in first")
    return s


@app.exception_handler(Unauthorized)
async def unauthorized(_req, exc):
    return JSONResponse({"error": str(exc)}, status_code=401)


def require_device(request: Request) -> tuple[str, str]:
    """-> (uid, token_hash). Bearer token, hash-matched, revocation-checked."""
    auth = request.headers.get("authorization", "")
    if not auth.startswith("Bearer "):
        raise Unauthorized("This device is not paired.")
    th = pairing.token_hash(auth[7:].strip())
    uid = store.uid_for_token_hash(th)
    if uid is None:
        raise Unauthorized("This reader is not paired anymore. Pair it again from Instapaper.")
    return uid, th


# -------------------------------------------------------------------- healthz
@app.get("/healthz")
async def healthz():
    return {"ok": True}


# ------------------------------------------------------------------ the pages
def page(title: str, body: str) -> HTMLResponse:
    return HTMLResponse(
        "<!doctype html><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        f"<title>{title}</title>"
        "<style>body{font:16px/1.5 system-ui;max-width:26rem;margin:8vh auto;"
        "padding:0 1rem}input,button{font:inherit;padding:.5rem;width:100%;"
        "box-sizing:border-box;margin:.25rem 0}button{cursor:pointer}"
        "small{color:#666}</style>" + body
    )


@app.get("/")
async def home(request: Request):
    s = session_of(request)
    # ?again=1 shows the form even with a live session: it is how an account
    # whose token was revoked from Instapaper's side gets a fresh one.
    if s and request.query_params.get("again") != "1":
        return RedirectResponse("/devices")
    return page(
        "CrossPlay read later",
        "<h1>CrossPlay read later</h1>"
        "<p>Sign in with your Instapaper account. The password is exchanged"
        " for an access token and never stored.</p>"
        "<form method=post action=/login>"
        "<input name=username placeholder='Email address or username'"
        " autocomplete=username>"
        "<input name=password type=password placeholder='Password, if you have one'"
        " autocomplete=current-password>"
        "<button>Sign in</button></form>",
    )


@app.post("/login")
async def login(request: Request):
    form = await request.form()
    username = str(form.get("username", "")).strip()
    # Not stripped and not rejected when empty: Instapaper accounts may have
    # no password, and the docs say an empty one cannot be read as an error.
    password = str(form.get("password", ""))
    ip_addr = client_ip(request)
    key = username.lower()
    if not LOGIN_IP.allow(ip_addr) or not GLOBAL_LOGIN.allow("all"):
        return page("Slow down", "<p>Too many attempts. Wait a few minutes.</p>")
    # Checked before the exchange, so a locked-out username costs Instapaper
    # nothing: the whole point is not to relay the attempt at all.
    waiting = LOGIN_LOCKOUT.locked_for(key)
    if waiting > 0:
        return page(
            "Slow down",
            f"<p>Too many failed sign-ins for that account. Try again in"
            f" {int(waiting // 60) + 1} minute(s).</p>",
        )
    try:
        st = await asyncio.to_thread(accounts.login, username, password)
    except ValueError as e:
        # A refusal is what an attacker is fishing for, so it is what the
        # backoff counts. A bridge fault below is not the user's doing and does
        # not penalise them.
        LOGIN_LOCKOUT.record_failure(key)
        return page("Sign in failed", f"<p>{e}</p><p><a href=/>Try again</a></p>")
    except RuntimeError:
        log.exception("login blocked by configuration")
        return page(
            "Not configured",
            "<p>This bridge has no Instapaper application credentials yet, so"
            " it cannot sign anyone in. Nothing about your account was"
            " stored.</p>",
        )
    except Exception:
        log.exception("login failed unexpectedly")
        return page(
            "Something broke",
            "<p>The bridge hit a problem on its side; nothing about your"
            " account was stored. Try again in a minute.</p>",
        )
    LOGIN_LOCKOUT.record_success(key)
    resp = RedirectResponse("/devices", status_code=303)
    resp.set_cookie(
        "read_session",
        _seal({"uid": st.uid, "username": username, "csrf": secrets.token_urlsafe(16)}),
        httponly=True,
        samesite="lax",
        secure=True,
        max_age=86400,
    )
    return resp


@app.get("/pair")
async def pair_page(request: Request):
    s = session_of(request)
    if not s:
        return page(
            "Pair",
            "<p>Sign in first, then scan the code on your reader again.</p>"
            "<p><a href=/>Sign in</a></p>",
        )
    return page(
        "Pair this e-reader?",
        "<h1>Pair this e-reader?</h1>"
        "<p>Type the code your reader is showing. Only do this for a device"
        " in your hands.</p>"
        "<form method=post action=/api/pair/claim>"
        f"<input type=hidden name=csrf value='{s['csrf']}'>"
        "<input name=code placeholder='Code on the reader' autofocus"
        " autocomplete=off style='text-transform:uppercase'>"
        "<button>Pair this e-reader</button></form>"
        "<script>const c=location.hash.slice(1);if(c)document."
        "querySelector('[name=code]').value=c;</script>",
    )


@app.get("/devices")
async def devices_page(request: Request):
    s = session_of(request)
    if not s:
        return RedirectResponse("/")
    state = store.UserStore(s["uid"]).load_state()
    rows = ""
    for th, d in state["devices"].items():
        seen = (
            time.strftime("%b %d %H:%M", time.localtime(d["last_seen"]))
            if d.get("last_seen")
            else "never synced"
        )
        rows += (
            f"<form method=post action=/devices/revoke><li>{d['name']}"
            f" <small>paired {time.strftime('%Y-%m-%d', time.localtime(d['created']))}"
            f" &middot; last seen {seen}</small>"
            f"<input type=hidden name=csrf value='{s['csrf']}'>"
            f"<input type=hidden name=token_hash value='{th}'>"
            "<button style='width:auto'>Unpair</button></li></form>"
        )
    return page(
        "Your readers",
        "<h1>Your readers</h1>"
        + (
            f"<ul>{rows}</ul>"
            if rows
            else "<p>No reader paired yet. Open Instapaper on the device and"
            " scan the code it shows.</p>"
        )
        + "<p><small>Sync refusing on every device? <a href='/?again=1'>"
        "Reconnect your Instapaper account</a>.</small></p>"
        "<p><small>This bridge can read your articles and archive them. It"
        " never deletes anything: the delete endpoint is not wired up at"
        " all.</small></p>",
    )


@app.post("/devices/revoke")
async def revoke(request: Request):
    s = require_session(request)
    form = await request.form()
    if form.get("csrf") != s["csrf"]:
        raise Unauthorized("stale form; reload the page")
    store.revoke_device(s["uid"], str(form.get("token_hash", "")))
    return RedirectResponse("/devices", status_code=303)


# -------------------------------------------------------------------- pairing
@app.post("/api/pair/start")
async def pair_start(request: Request):
    if not PAIR_IP.allow(client_ip(request)):
        return JSONResponse({"error": "Too many tries; wait a few minutes."}, 429)
    return pairing.PAIRINGS.start()


@app.post("/api/pair/claim")
async def pair_claim(request: Request):
    s = require_session(request)
    form = await request.form()
    if form.get("csrf") != s["csrf"]:
        raise Unauthorized("stale form; reload the page")
    okay = pairing.PAIRINGS.claim(str(form.get("code", "")), s["uid"], s["username"])
    if not okay:
        return page(
            "Not found",
            "<p>That code is unknown or expired. Codes last five minutes;"
            " ask the reader for a fresh one.</p>",
        )
    return page(
        "Almost done",
        "<p>Now confirm on the reader: it shows who it is pairing to and asks"
        " for a button press. Nothing is stored until then.</p>",
    )


@app.post("/api/pair/abandon")
async def pair_abandon(request: Request):
    """Best-effort hygiene from the device; possession of the token IS the
    authorization. A pollToken kills its pending code, a deviceToken revokes
    the registration poll() created before the human confirmed."""
    if not PAIR_IP.allow(client_ip(request)):
        return JSONResponse({"error": "Too many tries; wait a few minutes."}, 429)
    try:
        body = await request.json()
    except Exception:
        body = {}
    poll = str(body.get("pollToken", "") or "")
    if poll:
        pairing.PAIRINGS.abandon(poll)
    token = str(body.get("deviceToken", "") or "")
    if token:
        th = pairing.token_hash(token)
        uid = store.uid_for_token_hash(th)
        if uid is not None:
            store.revoke_device(uid, th)
    return {"ok": True}


@app.get("/api/pair/poll")
async def pair_poll(pollToken: str):
    result = pairing.PAIRINGS.poll(pollToken)
    if result is None:
        return JSONResponse({"error": "That code expired. Ask for a fresh one."}, 410)
    if result["pending"]:
        return {"pending": True}
    th = pairing.token_hash(result["deviceToken"])
    store.register_device(result["uid"], th, "X4 Pro")
    return {
        "pending": False,
        "deviceToken": result["deviceToken"],
        "username": result["username"],
    }


# ---------------------------------------------------------------- device sync
@app.post("/api/sync")
async def start_sync(request: Request, dev=Depends(require_device)):
    uid, th = dev
    if not SYNC_USER.allow(uid) or not GLOBAL_SYNC.allow("all"):
        return JSONResponse(
            {"error": "Syncing a lot right now; try again in a few minutes."}, 429
        )
    raw = await request.body()
    if len(raw) > MAX_SYNC_BODY:
        return JSONResponse({"error": "That sync payload is too large."}, 413)
    try:
        body = json.loads(raw or b"{}")
    except ValueError:
        return JSONResponse({"error": "Malformed sync payload."}, 400)

    have = body.get("have") or []
    archive_ids = body.get("archive") or []
    if not isinstance(have, list) or not isinstance(archive_ids, list):
        return JSONResponse({"error": "Malformed sync payload."}, 400)

    st = store.UserStore(uid).ensure()
    state = st.load_state()
    if state["status"] == "new" or not state.get("token_enc"):
        raise Unauthorized("This account is not connected to Instapaper yet.")
    state["devices"][th]["last_seen"] = int(time.time())
    st.save_state(state)
    token, secret = accounts.credentials_of(state)

    def work():
        summary = engine.sync_cycle(st, token, secret, have, archive_ids)
        fresh = st.load_state()
        fresh["status"] = "ok"
        fresh["last_sync"] = int(time.time())
        st.save_state(fresh)
        return summary

    job = jobs.JOBS.start(uid, work)
    jobs.JOBS.gc()
    return {"job": job.id}


@app.get("/api/sync/status")
async def sync_status(job: str, dev=Depends(require_device)):
    uid, _ = dev
    j = jobs.JOBS.get(job)
    if j is None or j.uid != uid:
        # Jobs live in memory; a deploy or reboot forgets them mid-flight.
        # Nothing is lost -- archives are idempotent and progress rides the
        # next `have` -- so the honest answer is a sentence, not a 404.
        return {
            "status": "error",
            "message": "The bridge restarted mid-sync. Nothing was lost; sync again.",
        }
    out = {"status": j.status}
    if j.status == "done":
        out["summary"] = j.summary
    elif j.status == "error":
        out["message"] = j.message
    return out


@app.get("/api/article/{bookmark_id}/{bookmark_hash}")
async def article_file(bookmark_id: int, bookmark_hash: str, dev=Depends(require_device)):
    uid, _ = dev
    st = store.UserStore(uid)
    # article_path strips everything but alphanumerics out of the hash, so a
    # traversal cannot be spelled here; the resolve check is the second lock
    # on the same door.
    target = st.article_path(bookmark_id, bookmark_hash).resolve()
    base = (st.root / "articles").resolve()
    if not str(target).startswith(str(base)) or not target.is_file():
        return JSONResponse({"error": "That article is not on the bridge."}, 404)
    return FileResponse(target, media_type="text/plain; charset=utf-8")


@app.on_event("startup")
async def startup():
    logging.basicConfig(level=logging.INFO)
    try:
        import bridge.instapaper as ip

        ip.consumer()
        log.info("read-bridge up; Instapaper base %s", ip.BASE)
    except RuntimeError as e:
        # Loud, and not fatal: pairing and the pages still work, so an
        # operator can see the service is alive and know exactly what is
        # missing rather than watching a container crash-loop.
        log.error("%s -- sign-in and sync will refuse until this is set", e)
