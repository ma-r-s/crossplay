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
import json
import html
import logging
import pathlib
import re
import secrets
import struct
import time

from fastapi import Depends, FastAPI, Request
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, RedirectResponse

from . import accounts, chrome, decks, engine, events, jobs, pairing, store, wire
from .ratelimit import Lockout, Window
from .journal import Journal

log = logging.getLogger("bridge.app")

# Mirrors StudySync::kMaxChosenDecks in the firmware.
MAX_CHOSEN_DECKS = 8
app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)

MAX_SYNC_BODY = 8 * 1024 * 1024  # revlog tails + cards.dat; a 5k-card deck is ~320KB
# Eight deck names. Anki's own deck-name limit is far under this; the cap is
# here to bound a hostile client, not to constrain a real one.
MAX_CHOOSE_BODY = 64 * 1024
MAX_DECK_NAME = 512


# ---------------------------------------------------------------- rate limits
# The shape here is read-bridge's, arrived at there and ported on 2026-09-05
# because this service had a weaker one and nobody could say why. See
# server/attacks.py for the run that found the difference.
LOGIN_IP = Window(5, 300)  # 5 attempts / 5 min / IP
# There is deliberately NO flat per-username Window beside LOGIN_LOCKOUT. There
# was, and it shadowed nothing here only because there was no lockout to
# shadow; a flat cap on ATTEMPTS also refuses the legitimate person who mistypes
# twice and then gets it right. Exponential backoff on FAILURES dominates it:
# an unlimited number of SUCCESSFUL sign-ins is harmless, because you already
# have the password.
LOGIN_LOCKOUT = Lockout()
# The ceiling with only ONE of it. Per-IP and per-username counters are both
# defeated by having many of each -- which is precisely what a credential
# stuffing run has -- and this one is not. Set well above any plausible real
# rate, so it is a backstop rather than a throttle: a person signs in once and
# then their reader syncs on a token forever after.
GLOBAL_LOGIN = Window(30, 60)
PAIR_IP = Window(10, 300)
# Guessing a pairing code. Eight characters from a 32-glyph alphabet is not
# guessable inside its five minutes; guessing it FOR FREE is the problem, and
# until 2026-09-05 the claim endpoint answered an unlimited number of wrong
# codes to anyone holding an account of their own. Per-IP and per-account,
# because either one alone is trivially sidestepped by varying the other.
CLAIM_IP = Window(20, 300)
CLAIM_USER = Window(20, 300)
# Device reports ride any response under 400, including /healthz, which takes
# no token at all. Each one costs a thread and a row on the board. These are
# generous for a real reader (it reports on the handful of requests a sync
# makes) and ruinous for a flood.
REPORT_IP = Window(30, 300)
GLOBAL_REPORT = Window(240, 60)
SYNC_USER = Window(6, 300)
GLOBAL_SYNC = Window(60, 60)

# A deck slug is what decks.slugify() produces and a build id is
# f"{int(time.time())}". Anything else was not made by this service, and both
# reach the filesystem, so they are matched against the shapes their own
# generators can emit rather than merely inspected for dots.
_SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
_BUILD_RE = re.compile(r"^[0-9]{1,20}$")


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


# ------------------------------------------------------------ device reports
@app.middleware("http")
async def device_reports(request: Request, call_next):
    """A device never makes a request just to report. Whatever it has to say
    (a crash, an update attempt) rides the X-CrossPlay-Report header of the
    request it was making anyway, on every endpoint, so it is read here and
    not in one handler. Posted after the answer, and only for an answer the
    device will count as delivered, so a request it retries does not post
    the same crash twice. events.Client.report never raises."""
    response = await call_next(request)
    # Capped, because this runs on endpoints that need no token: without a
    # limit an unauthenticated stranger writes to the board as fast as they
    # can open sockets, and each report costs a thread against pids_limit.
    # Dropping under flood is the right failure: a report is best-effort by
    # design and no sync waits on one.
    if response.status_code < 400:
        client = events.client_for(request)
        # The limiter is consulted only when there is actually something to
        # relay. Counting REQUESTS instead cost a real device its crash report
        # in the middle of an ordinary sync: every article download carries the
        # header, so a 21-article sync spent the whole budget on requests that
        # were reporting nothing. tests/test_api.py caught it.
        if client.crash is not None or client.ota is not None:
            if REPORT_IP.allow(client_ip(request)) and GLOBAL_REPORT.allow("all"):
                client.report(via="anki")
    return response


# -------------------------------------------------------------------- healthz
@app.get("/healthz")
async def healthz():
    # Static on purpose: no per-user state, no AnkiWeb calls (critic A6).
    return {"ok": True}


# ------------------------------------------------------------------ the pages
def page(title: str, body: str, *, step: int | None = None) -> HTMLResponse:
    """Every page this service serves. The look lives in chrome.py; this stays
    so the call sites read the same as they always did."""
    return chrome.page(title, body, step=step)


# The two typefaces the chrome asks for. An allowlist rather than a static
# mount: this process is on the public internet holding AnkiWeb credentials,
# and a directory served by name is a traversal bug waiting for a bad joiner.
_ASSETS = {"jersey25.woff2", "instrumentserif.woff2"}


@app.get("/assets/{name}")
async def asset(name: str):
    if name not in _ASSETS:
        return JSONResponse({"error": "not found"}, status_code=404)
    return FileResponse(
        pathlib.Path(__file__).parent / "static" / name,
        media_type="font/woff2",
        headers={"cache-control": "public, max-age=31536000, immutable"},
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
        "Sign in",
        "<h1>Your Anki deck, on paper</h1>"
        "<p class=lede>Review the cards you already have in Anki, on the"
        " reader in your hand.</p>" + chrome.service_flow() +
        "<form method=post action=/login>"
        "<label for=u>AnkiWeb email</label>"
        "<input id=u name=username autocomplete=username autofocus>"
        "<label for=p>AnkiWeb password</label>"
        "<input id=p name=password type=password autocomplete=current-password>"
        "<button>Sign in</button></form>"
        "<p class=small>The password is exchanged for a session key and never"
        " stored.</p>",
        step=1,
    )


@app.post("/login")
async def login(request: Request):
    form = await request.form()
    username = str(form.get("username", "")).strip()
    password = str(form.get("password", ""))
    ip = client_ip(request)
    key = username.lower()
    if not LOGIN_IP.allow(ip) or not GLOBAL_LOGIN.allow("all"):
        return page(
            "Slow down",
            chrome.mark(False) + "<h1>Slow down</h1>"
            "<p class=lede>Too many attempts. Wait a few minutes.</p>",
        )
    # Checked BEFORE the exchange, so a locked-out username costs AnkiWeb
    # nothing: the whole point is not to relay the attempt at all.
    waiting = LOGIN_LOCKOUT.locked_for(key)
    if waiting > 0:
        return page(
            "Slow down",
            chrome.mark(False) + "<h1>Slow down</h1>"
            f"<p class=lede>Too many failed sign-ins for that account. Try again in"
            f" {int(waiting // 60) + 1} minute(s).</p>",
        )
    try:
        st = await asyncio.to_thread(accounts.login, username, password)
    except ValueError as e:
        # A refusal is what an attacker is fishing for, so it is what the
        # backoff counts. A bridge fault below is not the user's doing and
        # does not penalise them.
        LOGIN_LOCKOUT.record_failure(key)
        return page(
            "Sign in failed",
            chrome.mark(False) + "<h1>Sign in failed</h1>"
            f"<p class=lede>{e}</p>"
            "<a class=btn href=\"/\">Try again</a>",
            step=1,
        )
    except Exception:
        # Whatever broke, the user gets a sentence and the log gets the
        # traceback -- never a bare Internal Server Error on the one page
        # where trust is earned.
        log.exception("login failed unexpectedly")
        return page(
            "Something broke",
            chrome.mark(False) + "<h1>Something broke</h1>"
            "<p class=lede>The bridge hit a problem on its side; nothing about"
            " your account was stored. Try again in a minute.</p>",
        )
    LOGIN_LOCKOUT.record_success(key)
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
            "Sign in first",
            "<h1>Sign in first</h1>"
            "<p class=lede>Your reader is asking to be paired to an account,"
            " and this browser is not signed in to one yet.</p>"
            + chrome.service_flow() +
            "<a class=btn href=\"/\">Sign in</a>"
            "<p class=small>Then press SYNC on the reader for a fresh code:"
            " they last five minutes.</p>",
            step=1,
        )
    return page(
        "Pair this reader",
        "<h1>Pair this reader</h1>"
        "<p class=lede>Type the code your reader is showing.</p>"
        + chrome.reader_with_code() + pair_form(s["csrf"]) +
        "<p class=small>Only do this for a device in your hands. Codes last"
        " five minutes.</p>"
        "<script>const c=location.hash.slice(1);if(c)document."
        "querySelector('[name=code]').value=c;</script>",
        step=2,
    )


def pair_form(csrf: str, action: str = "Pair this reader") -> str:
    """The code field, wherever someone needs it. It lives on /pair and on the
    empty /devices, because that page used to draw the box the code goes in and
    then not give anyone a box to type it in."""
    return (
        "<form method=post action=/api/pair/claim>"
        f"<input type=hidden name=csrf value='{csrf}'>"
        "<label for=code>Code on the reader</label>"
        "<input id=code class=code name=code autofocus autocomplete=off"
        " autocapitalize=characters maxlength=8 spellcheck=false>"
        f"<button>{action}</button></form>"
    )


@app.get("/devices")
async def devices_page(request: Request):
    s = session_of(request)
    if not s:
        return RedirectResponse("/")
    state = store.UserStore(s["uid"]).load_state()
    rows = ""
    for th, d in state["devices"].items():
        # One date format for both dates. They were %Y-%m-%d and %b %d %H:%M in
        # the same sentence, which read as two different kinds of fact.
        seen = (
            "last synced "
            + time.strftime("%Y-%m-%d %H:%M", time.localtime(d["last_seen"]))
            if d.get("last_seen")
            else "not synced yet"
        )
        rows += (
            f"<form method=post action=/devices/revoke><li>{chrome.READER}"
            f"<span class=reader-name><b>{d['name']}</b>"
            f"<small>paired {time.strftime('%Y-%m-%d', time.localtime(d['created']))}"
            f" &middot; {seen}</small></span>"
            f"<input type=hidden name=csrf value='{s['csrf']}'>"
            f"<input type=hidden name=token_hash value='{th}'>"
            "<button>Unpair</button></li></form>"
        )
    who = html.escape(str(s.get("username", "")))
    # The rail tells the truth about where this account actually is: an empty
    # list is not step three, it is someone still waiting to pair -- and that
    # page gets the pairing form itself, not a picture of one.
    return page(
        "Your readers" if rows else "Pair your reader",
        (
            chrome.mark(True) + "<h1>Your readers</h1>"
            f"<p class=lede>Signed in as {who}. These readers sync your"
            " collection.</p>"
            f"<ul class=readers>{rows}</ul>"
            if rows
            else "<h1>Almost there</h1>"
            "<p class=lede>No reader paired yet. Press SYNC in Study on the"
            " device and type the code it shows.</p>"
            + chrome.reader_with_code() + pair_form(s["csrf"])
        )
        + "<footer><p class=small>Sync acting up on every device? "
        "<a href='/?again=1'>Reconnect your AnkiWeb account</a>. AnkiWeb"
        " sometimes retires the session key CrossPlay syncs with, and signing"
        " in again mints a fresh one.</p></footer>",
        step=3 if rows else 2,
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
    # After the session and the CSRF token, so a wrong code costs an account
    # rather than an anonymous request, and before the guess is answered.
    if not CLAIM_IP.allow(client_ip(request)) or not CLAIM_USER.allow(s["uid"]):
        return page(
            "Slow down",
            chrome.mark(False) + "<h1>Slow down</h1>"
            "<p class=lede>Too many codes tried. Wait a few minutes, then ask"
            " the reader for a fresh one.</p>",
            step=2,
        )
    okay = pairing.PAIRINGS.claim(str(form.get("code", "")), s["uid"], s["username"])
    if not okay:
        return page(
            "Not found",
            chrome.mark(False) + "<h1>That code did not work</h1>"
            "<p class=lede>That code is unknown or expired. Codes last five"
            " minutes; press SYNC on the reader for a fresh one.</p>"
            "<a class=btn href=\"/pair\">Type another code</a>",
            step=2,
        )
    # Still step two: the pairing is not real until the human presses the
    # button on the device, and saying "done" here would be a lie the user
    # discovers standing at a reader that never paired.
    return page(
        "Almost done",
        "<h1>Now look at the reader</h1>"
        "<p class=lede>Now confirm on the reader: it shows who it is pairing to"
        " and asks for a button press. Nothing is stored until then.</p>"
        + chrome.confirm_on_reader() + chrome.waiting() +
        "<a class=btn href=\"/devices\">The reader says it is paired</a>"
        "<a class=\"btn quiet\" href=\"/pair\">Type another code</a>",
        step=2,
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
    # Every endpoint that takes a body needs a ceiling on it. This one had
    # none: eight deck names of any length each went straight into state.json,
    # so a 12MB post was a 12MB write to the card, repeatable, by anyone
    # holding a token for their own account.
    raw = await request.body()
    if len(raw) > MAX_CHOOSE_BODY:
        return JSONResponse({"error": "That deck list is too large."}, 413)
    try:
        body = json.loads(raw or b"{}")
    except ValueError:
        return JSONResponse({"error": "Malformed deck list."}, 400)
    # Malformed rather than merely unexpected: json.loads happily returns a
    # list, a string or a number, and .get() on any of them was a 500.
    if not isinstance(body, dict) or not isinstance(body.get("decks", []), list):
        return JSONResponse({"error": "Malformed deck list."}, 400)
    # Must equal StudySync::kMaxChosenDecks on the device. The picker enforces
    # the same number, so this truncation should never fire; it is the backstop
    # for a client that does not. Each name is capped at the longest an Anki
    # deck path can sensibly be, for the same reason.
    names = [str(n)[:MAX_DECK_NAME] for n in body.get("decks", [])][:MAX_CHOSEN_DECKS]
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
        # the sync itself. The converter refuses an empty deck, a deck renamed
        # away on the desktop side, and a deck whose every card is an empty
        # cloze; aborting the job here left the reader with an error, no
        # decks, and no route back to the picker, repeating identically
        # forever because the state below was never saved.
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

    job = jobs.JOBS.start(
        uid,
        work,
        service="anki",
        # The device's own id, board, version and health, read off its
        # headers. A reader that sends no id is counted under its token hash,
        # salted once more: the board can name none of them, and the raw
        # token never reaches this line at all.
        client=events.client_for(request, default_device=events.device_id(th)),
        # cards: what the reader posted, its whole hand across the chosen
        # decks; reviews: what this sync carried up into the collection.
        props=lambda s: {"cards": len(device_cards), "reviews": s["applied"]},
    )
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
    # slug and build are VALIDATED before they become a path, not merely
    # compared against afterwards. They used to go straight into base, and
    # the containment check below was then true by construction: dot segments
    # in them moved the base as well as the target, so ".." twice landed base
    # on <data>/users/ and every other account's state.json -- their encrypted
    # AnkiWeb hostKey and their device token hashes -- was one segment away.
    # The twin never had this because it strips its filename down to
    # alphanumerics, so the escape cannot be spelled there at all.
    if not _SLUG_RE.match(slug) or not _BUILD_RE.match(build):
        return JSONResponse({"error": "No such file."}, 404)
    base = (st.root / "decks" / slug / build).resolve()
    target = (base / path).resolve()
    # Still here, and now it means what it says: base is a directory this
    # service could itself have created, so containment is a real question.
    # is_relative_to, not str.startswith: the string form also accepts a
    # SIBLING whose name merely begins with base's ("decks/a/1" against
    # "decks/a/12"), which is a different bug wearing the same clothes.
    if not target.is_relative_to(base) or not target.is_file():
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
    # Says "events are off" once when the two variables are not both set.
    if events.enabled():
        log.info("events on: syncs and failures post to the board")
    log.info("bridge up; tools at %s", decks.TOOLS)
