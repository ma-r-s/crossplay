"""The HTTP surface: a tiny human side and a smaller device side.

Human (browser, session cookie): / login page, /pair claim page, /devices
list with revoke. The session cookie is Fernet-sealed JSON (encrypt-then-
MAC, same master key discipline as the hostKeys), SameSite=Lax + HttpOnly,
and every state-changing form carries a CSRF token bound to the session --
the pairing-claim CSRF hole was critic finding A2.

Device (bearer token): pairing start/poll, deck list/choose, sync job
start/status, deck file download. The device never sees an error code, so
every refusal here is a sentence the firmware can show verbatim.

Rate limits are deliberately in-memory and crude: this process is pinned to
one worker, and the limits exist to blunt credential stuffing (A4) and
pairing-code scans (A3), not to be fair schedulers.
"""

import asyncio
import base64
import hashlib
import json
import logging
import secrets
import struct
import time

from fastapi import Depends, FastAPI, Request, Response
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, RedirectResponse

from . import accounts, decks, engine, jobs, pairing, store, wire
from .ratelimit import Window
from .journal import Journal

log = logging.getLogger("bridge.app")

# Mirrors StudySync::kMaxChosenDecks in the firmware.
MAX_CHOSEN_DECKS = 8
app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)

MAX_SYNC_BODY = 8 * 1024 * 1024  # revlog tails + cards.dat; a 5k-card deck is ~320KB


# ---------------------------------------------------------------- rate limits
LOGIN_IP = Window(5, 300)  # 5 attempts / 5 min / IP
LOGIN_USER = Window(3, 900)  # 3 attempts / 15 min / username
PAIR_IP = Window(10, 300)
SYNC_USER = Window(6, 300)
GLOBAL_SYNC = Window(60, 60)


def client_ip(request: Request) -> str:
    # cloudflared preserves the visitor in CF-Connecting-IP; the socket peer
    # is always the tunnel.
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
    return _unseal(request.cookies.get("bridge_session"))


def require_session(request: Request) -> dict:
    s = session_of(request)
    if not s:
        raise Unauthorized("sign in first")
    return s


class Unauthorized(Exception):
    pass


@app.exception_handler(Unauthorized)
async def unauthorized(_req, exc):
    return JSONResponse({"error": str(exc)}, status_code=401)


# ---------------------------------------------------------------- device auth
def require_device(request: Request) -> tuple[str, str]:
    """-> (uid, token_hash). Bearer token, hash-matched, revocation-checked."""
    auth = request.headers.get("authorization", "")
    if not auth.startswith("Bearer "):
        raise Unauthorized("This device is not paired.")
    th = pairing.token_hash(auth[7:].strip())
    uid = store.uid_for_token_hash(th)
    if uid is None:
        raise Unauthorized(
            "This device is not paired anymore. Pair it again from Study."
        )
    return uid, th


# -------------------------------------------------------------------- healthz
@app.get("/healthz")
async def healthz():
    # Static on purpose: no per-user state, no AnkiWeb calls (critic A6).
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
    # ?again=1 shows the sign-in form even with a live session: it is how an
    # account whose AnkiWeb key died gets a fresh one. Without it there was
    # no way to re-login at all -- the session cookie skipped the form and a
    # dead key looked like a broken bridge (found live, 2026-08-26).
    if s and request.query_params.get("again") != "1":
        return RedirectResponse("/devices")
    return page(
        "CrossPlay sync",
        "<h1>CrossPlay sync</h1>"
        "<p>Sign in with your AnkiWeb account. The password is exchanged for"
        " a session key and never stored.</p>"
        "<form method=post action=/login>"
        "<input name=username placeholder='AnkiWeb email' autocomplete=username>"
        "<input name=password type=password placeholder='AnkiWeb password'"
        " autocomplete=current-password>"
        "<button>Sign in</button></form>",
    )


@app.post("/login")
async def login(request: Request):
    form = await request.form()
    username = str(form.get("username", "")).strip()
    password = str(form.get("password", ""))
    ip = client_ip(request)
    if not LOGIN_IP.allow(ip) or not LOGIN_USER.allow(username.lower()):
        return page("Slow down", "<p>Too many attempts. Wait a few minutes.</p>")
    try:
        st = await asyncio.to_thread(accounts.login, username, password)
    except ValueError as e:
        return page("Sign in failed", f"<p>{e}</p><p><a href=/>Try again</a></p>")
    except Exception:
        # Whatever broke, the user gets a sentence and the log gets the
        # traceback -- never a bare Internal Server Error on the one page
        # where trust is earned.
        log.exception("login failed unexpectedly")
        return page(
            "Something broke",
            "<p>The bridge hit a problem on its side; nothing about your"
            " account was stored. Try again in a minute.</p>",
        )
    resp = RedirectResponse("/devices", status_code=303)
    resp.set_cookie(
        "bridge_session",
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
            else "<p>No reader paired yet. Press SYNC"
            " in Study on the device and scan the code it shows.</p>"
        )
        + "<p><small>Sync acting up on every device? <a href='/?again=1'>"
        "Reconnect your AnkiWeb account</a> -- AnkiWeb sometimes retires the"
        " bridge's session key, and signing in again mints a fresh one."
        "</small></p>",
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
            " press SYNC on the reader for a fresh one.</p>",
        )
    return page(
        "Almost done",
        "<p>Now confirm on the reader: it shows who it is pairing to and"
        " asks for a button press. Nothing is stored until then.</p>",
    )


@app.post("/api/pair/abandon")
async def pair_abandon(request: Request):
    """Best-effort hygiene from the device. Possession of the token IS the
    authorization: a pollToken kills its pending code, and a deviceToken
    revokes its own registration (the confirm screen was cancelled after
    poll() had already registered it -- without this, a ghost device row
    stays on /devices that nobody holds a token for)."""
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
        return JSONResponse(
            {"error": "That code expired. Start again for a fresh one."}, 410
        )
    if result["pending"]:
        return {"pending": True}
    th = pairing.token_hash(result["deviceToken"])
    store.register_device(result["uid"], th, "X4 Pro")
    return {
        "pending": False,
        "deviceToken": result["deviceToken"],
        "username": result["username"],
    }


# ------------------------------------------------------------ device: decks
def _user_bits(uid: str):
    st = store.UserStore(uid).ensure()
    state = st.load_state()
    return st, state


@app.get("/api/decks")
async def list_decks(dev=Depends(require_device)):
    uid, _ = dev
    st, state = _user_bits(uid)

    def read():
        if not st.collection_path.exists():
            return []
        from anki.collection import Collection

        col = Collection(str(st.collection_path))
        try:
            # Cards are counted against the deck they came FROM, matching the
            # converter: a card in a filtered deck (Custom Study) has did = the
            # filtered deck and odid = its home deck.
            rows = col.db.all(
                "select d.id, d.name, count(c.id) from decks d"
                " left join cards c on (case when c.odid = 0 then c.did else c.odid end) = d.id"
                " group by d.id"
            )
            # Filtered decks own no cards of their own -- they borrow them and
            # give them back -- so offering one as a choice builds a deck that
            # empties itself, after which every sync ends PART WAY until the
            # user re-picks. There is no dyn column any more (decks keep their
            # kind in a protobuf blob), so this is the API's question to answer.
            rows = [(n, c) for did, n, c in rows if not col.decks.is_filtered(did)]
        finally:
            col.close()
        # Count the subtree, not the deck's own cards. A shared deck arrives
        # as a parent whose cards all live in subdecks, so its own count is
        # zero -- and the reader hides a zero-card deck, because the converter
        # refuses a truly empty one. The converter itself matches subdecks
        # (anki_to_deck.collect_notes: d.name = ? or d.name like ? || x'1f%'),
        # so the parent builds correctly and only the count was lying.
        own = [(n, c) for n, c in rows]
        def subtree(name):
            return sum(c for n, c in own if n == name or n.startswith(name + "\x1f"))

        return [{"name": n.replace("\x1f", "::"), "cards": subtree(n)} for n, _ in own]

    async with store.LOCKS.for_user(uid):
        all_decks = await asyncio.to_thread(read)
    return {"decks": all_decks, "chosen": state["chosen_decks"]}


@app.post("/api/decks/choose")
async def choose_decks(request: Request, dev=Depends(require_device)):
    uid, _ = dev
    body = await request.json()
    # Must equal StudySync::kMaxChosenDecks on the device. The picker enforces
    # the same number, so this truncation should never fire; it is the backstop
    # for a client that does not.
    names = [str(n) for n in body.get("decks", [])][:MAX_CHOSEN_DECKS]
    st, state = _user_bits(uid)
    state["chosen_decks"] = names
    st.save_state(state)
    return {"chosen": names}


# ------------------------------------------------------------- device: sync
@app.post("/api/sync")
async def start_sync(request: Request, dev=Depends(require_device)):
    uid, th = dev
    if not SYNC_USER.allow(uid) or not GLOBAL_SYNC.allow("all"):
        return JSONResponse(
            {"error": "Syncing a lot right now; try again in a few minutes."}, 429
        )
    body = await request.body()
    if len(body) > MAX_SYNC_BODY:
        return JSONResponse({"error": "That sync payload is too large."}, 413)

    # Wire: [u32 header_len][JSON header][blobs in header order].
    # Header: {"decks": [{"slug", "revlogOffset", "revlogLen", "cardsLen"}]}
    if len(body) < 4:
        return JSONResponse({"error": "Malformed sync payload."}, 400)
    (hlen,) = struct.unpack("<I", body[:4])
    try:
        header = json.loads(body[4 : 4 + hlen])
        blobs = body[4 + hlen :]
    except (ValueError, IndexError):
        return JSONResponse({"error": "Malformed sync payload."}, 400)

    st, state = _user_bits(uid)
    if state["status"] == "new" or not state.get("hostkey_enc"):
        raise Unauthorized("This account is not connected to AnkiWeb yet.")
    journal = Journal(st.journal_path)
    acks = {}
    pos = 0
    device_cards = {}
    try:
        for d in header.get("decks", []):
            rl, cl = int(d["revlogLen"]), int(d["cardsLen"])
            revlog_blob = blobs[pos : pos + rl]
            cards_blob = blobs[pos + rl : pos + rl + cl]
            pos += rl + cl
            journal.ingest(wire.parse_revlog(revlog_blob))
            device_cards.update(wire.parse_cards(cards_blob))
            acks[d["slug"]] = int(d["revlogOffset"]) + rl
    finally:
        journal.close()

    # The ack is valid the moment the journal commit above returned; the
    # device may advance its offsets on this response even if the job fails.
    state["devices"][th]["last_seen"] = int(time.time())
    state["devices"][th].setdefault("ack_offsets", {}).update(acks)
    st.save_state(state)

    # Persist posted card states for the job (and any retry of it).
    devcards_dir = st.root / "devcards"
    devcards_dir.mkdir(exist_ok=True)
    (devcards_dir / "latest.json").write_text(json.dumps(device_cards))

    hostkey = accounts.hostkey_of(state)
    endpoint = state.get("endpoint") or accounts.ANKIWEB_ENDPOINT

    def work():
        journal = Journal(st.journal_path)
        try:
            cards = json.loads((devcards_dir / "latest.json").read_text())
            cards = {int(k): v for k, v in cards.items()}
            summary = engine.sync_cycle(st, journal, hostkey, endpoint, cards)
        finally:
            journal.close()
        fresh = st.load_state()
        manifests = []
        prints = fresh.setdefault("deck_fingerprints", {})
        # One deck that cannot be built must not cost the user the others, nor
        # the sync itself. The converter refuses an empty deck, a cloze-only
        # deck and a deck renamed away on the desktop side; aborting the job
        # here left the reader with an error, no decks, and no route back to
        # the picker, repeating identically forever because the state below
        # was never saved.
        failed = []
        for name in fresh["chosen_decks"]:
            try:
                content_now, schedule_now = decks.deck_fingerprints(st, name)
                stored = prints.get(name) or {}
                if isinstance(stored, str):  # pre-split single fingerprint
                    stored = {}
                existing = decks.latest_build(st, decks.slugify(name))
                if existing and stored.get("content") == content_now and stored.get("schedule") == schedule_now:
                    manifests.append(existing)
                    continue
                if existing and stored.get("content") == content_now:
                    manifests.append(decks.rebuild_cards_only(st, name, existing))
                else:
                    manifests.append(decks.build_deck(st, name))
                prints[name] = {"content": content_now, "schedule": schedule_now}
            except Exception:
                log.exception("deck build failed, skipping: %s", name)
                failed.append(name)
        fresh["status"] = "ok"
        fresh["last_sync"] = int(time.time())
        st.save_state(fresh)
        summary["manifests"] = manifests
        summary["failedDecks"] = failed
        return summary

    job = jobs.JOBS.start(uid, work)
    return {"job": job.id, "ackOffsets": acks}


@app.get("/api/sync/status")
async def sync_status(job: str, dev=Depends(require_device)):
    uid, _ = dev
    j = jobs.JOBS.get(job)
    if j is None or j.uid != uid:
        # Jobs live in memory; a deploy or reboot forgets them mid-flight.
        # The reviews are safe (acked into the journal before the job ran),
        # so the honest answer is a sentence, not a 404 the device cannot
        # explain to anyone.
        return {"status": "error",
                "message": "The bridge restarted mid-sync. Nothing was lost; press SYNC again."}
    out = {"status": j.status}
    if j.status == "done":
        out["summary"] = j.summary
    elif j.status in ("error", "frozen"):
        out["message"] = j.message
    return out


@app.get("/api/deck/{slug}/{build}/{path:path}")
async def deck_file(slug: str, build: str, path: str, dev=Depends(require_device)):
    uid, _ = dev
    st = store.UserStore(uid)
    base = (st.root / "decks" / slug / build).resolve()
    target = (base / path).resolve()
    if not str(target).startswith(str(base)) or not target.is_file():
        return JSONResponse({"error": "No such file."}, 404)
    return FileResponse(target)


@app.on_event("startup")
async def startup():
    import pathlib

    # tools_local/study rides in the image at /app/tools_local/study; in dev
    # it is the repo's copy. In the image this file is only three levels deep,
    # so the repo-relative candidate is computed lazily -- constructing it
    # eagerly raised IndexError before the container path was even tried,
    # which crash-looped the very first deploy.
    here = pathlib.Path(__file__).resolve()
    candidates = [pathlib.Path("/app/tools_local/study")]
    if len(here.parents) > 3:
        candidates.append(here.parents[3] / "tools_local" / "study")
    for candidate in candidates:
        if candidate.is_dir():
            decks.TOOLS = candidate
            break
    logging.basicConfig(level=logging.INFO)
    if decks.TOOLS is None:
        log.error("tools_local/study not found; deck builds will fail loudly")
    log.info("bridge up; tools at %s", decks.TOOLS)
