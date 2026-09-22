"""fridge.ma-r-s.com -- Live, the API and nothing else.

Somebody draws a note on their phone; a reader asleep on a fridge in another
country shows it in the morning. This service is the only thing between them.

NO PAGE LIVES HERE. What people open is crossplay.ma-r-s.com/live/, part of the
CrossPlay site and set in its stylesheet, its bar and its type. This host served
both for a while and the page was a design orphan: it shared nothing with the
site and drifted further every time the site changed.

TWO HOSTS, ONE REGISTRABLE DOMAIN, which is what makes the split safe rather
than merely tidy. The sender cookie is scoped to `.ma-r-s.com`, so it is
FIRST-party for both names and Safari's third-party cookie blocking -- which
would otherwise end this on an iPhone -- never applies to it. SameSite is
decided by site and not by origin, so Lax is still delivered on an XHR from one
subdomain to the other. See claim() for both, and CORS below for who may ask.

THE READER IS ASLEEP AND UNREACHABLE. Nothing here ever pushes, polls or opens
a connection to a device: deep sleep drops the radio and the USB, so a sleeping
reader cannot be asked anything at all. It wakes on its own schedule, makes one
request, and goes back down. Everything this service knows about a reader it
learned the last time the reader spoke.

ONE ROUND TRIP PER WAKE. /api/pull answers "is there anything new" and "when
should I wake next" together, and answers 304 when the answer is no. A wake
that finds nothing spends a few kilobytes and no SD write and no repaint. The
one fact only the reader has -- whether Live is still on -- rides that same
request as a header rather than a second call, so the count stays at one. The
only extra call a reader ever makes is POST /api/off, and that one happens
when somebody switches Live off, never on a wake.

Host must be exactly one label below the apex: Cloudflare's Universal SSL on
the free plan covers ma-r-s.com and *.ma-r-s.com and nothing deeper, and the
reader's baked root bundle carries the chain that edge serves.
"""

import pathlib
import time

from fastapi import Cookie, FastAPI, Header, Request, Response
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, PlainTextResponse

from . import events, store
from .pairing import Pairings, token_hash
from .ratelimit import Window

app = FastAPI(title="Live", docs_url=None, redoc_url=None, openapi_url=None)
PAIRINGS = Pairings()

# The one page allowed to call this service from a browser, and the domain the
# cookie is written for.
SITE_ORIGIN = "https://crossplay.ma-r-s.com"
COOKIE_DOMAIN = ".ma-r-s.com"

# EXACTLY ONE ORIGIN, WITH CREDENTIALS. A wildcard could not carry a cookie even
# if it were wanted -- the spec refuses "*" the moment credentials are included
# -- and a list of origins is a list of sites allowed to draw on somebody's
# reader. The reader is not a browser, sends no Origin and is unaffected by any
# of this; only the page is.
# THE SITE DEMANDS require-corp, so anything it loads from here must say it is
# willing to be loaded cross-origin. Without this header every history
# thumbnail is blocked by the browser and the rail draws broken-image icons --
# which looks like the pictures were lost rather than like a header is missing.
# CORS governs fetch(); this governs <img>. They are separate permissions and
# passing one does not pass the other.
@app.middleware("http")
async def allow_cross_origin_embedding(request, call_next):
    response = await call_next(request)
    response.headers["cross-origin-resource-policy"] = "cross-origin"
    return response


app.add_middleware(
    CORSMiddleware,
    allow_origins=[SITE_ORIGIN],
    allow_credentials=True,
    allow_methods=["GET", "POST", "PUT", "DELETE", "OPTIONS"],
    # EVERY header the page sends cross-origin. A custom header makes the request
    # non-simple, so the browser preflights it, and one missing here fails that
    # preflight with a 400 the browser reports as a network error -- which the
    # page shows as "could not reach the service", naming the wrong cause
    # entirely. x-kind arrived with the history rail and was never added here,
    # so every send was refused while the service was healthy.
    allow_headers=["content-type", "x-kind"],
    max_age=600,
)

# Copied in spirit from read-bridge's table. The claim limits are the ones that
# matter here: a six-digit code is a million, so the caps are what makes
# guessing hopeless rather than the size of the space.
CLAIM_IP = Window(10, 300)
CLAIM_GLOBAL = Window(120, 60)
PAIR_IP = Window(10, 300)
PULL_DEVICE = Window(30, 300)
# The only other thing a reader posts, and it writes state.json every time.
OFF_DEVICE = Window(10, 300)
POST_SENDER = Window(60, 300)
# Relaying a device's crash or install record, as the other two bridges do.
# These bound the RELAY, not the requests: see device_reports below.
REPORT_IP = Window(30, 300)
GLOBAL_REPORT = Window(240, 60)

# Live's word on the board's `events` table. Every Live event carries
# props.fridge, a pseudonymous id for the fridge, and `device` ONLY when the
# reader's own X-CrossPlay-Device header was on the request.
#
# Those are deliberately two different axes and are never added together. A
# fridge is a setup somebody started; a device is a reader that exists. Using
# the fridge id as `device` would have put every abandoned pairing into the
# fleet's device count, which is the exact inflation this instrumentation was
# added to expose.
SERVICE = "live"


def fridge_key(fridge_id: str) -> str:
    """The id Live counts a fridge by: salted, hashed, not reversible to the
    token or the directory name."""
    return events.device_id(fridge_id)


def client_ip(request: Request) -> str:
    # Behind Cloudflare and cloudflared, so the first hop is always local.
    fwd = request.headers.get("cf-connecting-ip") or request.headers.get(
        "x-forwarded-for", ""
    )
    return fwd.split(",")[0].strip() or (request.client.host if request.client else "?")


# WHAT THE READER SAYS WHEN THE SCHEDULE HAS MOVED AND IT HAS NOT NOTICED YET.
#
# A reader is asleep on the cadence it last picked up. Change the schedule from
# the website and there is a window -- up to one whole interval -- in which its
# real next wake and the schedule it is about to adopt disagree. This is the one
# sentence that names that, it is written HERE because the reader draws a
# decision this service made verbatim and never invents wording for one, and it
# is ABSENT rather than empty whenever nothing is pending, so the reader's line
# appears only when it is news.
#
# The website prints the same cadence words on its schedule chip, from
# store.cadence_words, so the two surfaces cannot tell one person two stories.
#
# host-tests/wallcaption expands this template over every phrase
# store.cadence_words can return and measures each one in the device's real
# font. That is why ALLOWED_INTERVALS is a finite tuple: a corpus the test
# cannot enumerate is a corpus nobody has measured.
PENDING_TEMPLATE = "Changing to {} after the next check."


def pending_sentence(fridge: store.Fridge) -> str | None:
    words = fridge.pending_cadence()
    return PENDING_TEMPLATE.format(words) if words else None


def refused(reason: str, code: int = 429) -> JSONResponse:
    # One sentence, ready to show. The reader must never invent its own wording
    # for a decision this service made (see BridgeHttp.h).
    return JSONResponse({"error": reason}, status_code=code)


# WHAT IS RUNNING, not merely that something is.
#
# "ok" answers the question docker asks and not the one a person asks after a
# deploy. `docker compose up -d --build` prints "Container Running" when it
# decided nothing needed recreating, which is indistinguishable from a rebuild,
# so the only way to tell a deployed fix from an undeployed one was to grep a
# source file inside the container. scripts/deploy.sh has always stamped the
# git short sha into BUILD; it just was not copied anywhere until now.
def _build() -> str:
    try:
        return pathlib.Path("/app/BUILD").read_text().strip() or "unknown"
    except OSError:
        return "unknown"


@app.middleware("http")
async def device_reports(request: Request, call_next):
    """Relay whatever crash or install record the reader is carrying.

    Byte-for-byte the shape study-bridge and read-bridge use, and the three
    details that matter are the same three:

    AFTER the response and only on < 400, so a request the device will retry
    does not post the same crash twice.

    The limiter is consulted ONLY when there is something to relay. Charging
    it per request instead once cost a real device its crash report in the
    middle of a sync.

    Until this existed a reader that used Live and nothing else could panic
    every day and never be heard: Live was the one service that read none of
    these headers, so its users' crashes reached nobody.
    """
    response = await call_next(request)
    if response.status_code < 400:
        client = events.client_for(request)
        if client.crash is not None or client.ota is not None:
            if REPORT_IP.allow(client_ip(request)) and GLOBAL_REPORT.allow("all"):
                client.report(via=SERVICE)
    return response


@app.on_event("startup")
def say_events() -> None:
    if events.enabled():
        log_line = "events on: pairings, check-ins and pictures post to the board"
    else:
        log_line = "events off: this service's numbers will not reach the board"
    print(f"[live] {log_line}", flush=True)
    # One pass over the records the old pairing flow left behind. Says the
    # number rather than doing it quietly: the count IS the finding, and a
    # deploy that silently deletes 67 directories should be readable in the
    # log afterwards.
    try:
        gone = store.sweep_orphans()
    except OSError as err:
        print(f"[live] could not sweep unclaimed fridges: {err}", flush=True)
        return
    if gone:
        print(f"[live] removed {gone} fridges nobody ever claimed", flush=True)


@app.get("/healthz")
def healthz() -> PlainTextResponse:
    return PlainTextResponse("ok " + _build())


# ---------------------------------------------------------------- the reader


@app.post("/api/pair/start")
def pair_start(request: Request) -> JSONResponse:
    if not PAIR_IP.allow(client_ip(request)):
        return refused(
            "Too many attempts from this address. Try again in a few minutes."
        )
    # The id and the token are MINTED here and the fridge is WRITTEN at the
    # claim (see claim()). It used to be written here, and that is why the
    # service held 67 fridges after nineteen hours of one person testing: the
    # reader mints one every time the Live screen is opened on an unpaired
    # device, again when a code expires on screen, and again on a 401. Nothing
    # ever deleted the ones nobody claimed, so "fridges" counted visits to a
    # setup screen.
    #
    # Nothing breaks by waiting. The reader does not receive its device token
    # from this call -- it gets it from /api/pair/poll, which only answers once
    # somebody has claimed -- so there is never a window where a token exists
    # for a fridge that does not. An unclaimed code now leaves nothing behind
    # at all: Pairings forgets it after CODE_TTL_S and that is the whole
    # record.
    import secrets

    fridge_id = store.new_fridge_id()
    device_token = secrets.token_urlsafe(32)
    started = PAIRINGS.start(fridge_id, device_token)
    # A code was shown. Proof of curiosity and nothing more, which is why it
    # is its own event rather than being counted as a fridge.
    events.post(
        SERVICE,
        "pair-start",
        device=events.client_for(request).device,
        props={"fridge": fridge_key(fridge_id), "joining": False},
    )
    return JSONResponse(started)


@app.post("/api/pair/join")
def pair_join(
    request: Request, authorization: str = Header(default="")
) -> JSONResponse:
    """A code that adds a phone to THIS fridge.

    Separate from /api/pair/start because that one makes a NEW fridge. Wiring
    "add somebody" to it would have handed the browser a different fridge and
    silently orphaned the first sender along with the picture on the glass.
    """
    token = authorization.removeprefix("Bearer ").strip()
    fridge = store.fridge_for_device(token) if token else None
    if fridge is None or not fridge.exists():
        return refused("This reader is not connected to anything.", 401)
    if not PAIR_IP.allow(client_ip(request)):
        return refused(
            "Too many attempts from this address. Try again in a few minutes."
        )
    if len(fridge.load().get("senders", [])) >= store.MAX_SENDERS:
        # Said before a code is minted rather than after somebody types it.
        return refused(
            f"This reader already has {store.MAX_SENDERS} phones. Remove one first.",
            409,
        )
    return JSONResponse(PAIRINGS.start(fridge.id, token, joining=True))


@app.get("/api/senders")
def senders(authorization: str = Header(default="")) -> JSONResponse:
    """Who can send, for the reader's own screen. Never the token, only its
    hash, which is what the revoke call names."""
    token = authorization.removeprefix("Bearer ").strip()
    fridge = store.fridge_for_device(token) if token else None
    if fridge is None or not fridge.exists():
        return refused("This reader is not connected to anything.", 401)
    out = [
        {
            "name": s.get("name", "A phone"),
            "pairedAt": s.get("paired_at", 0),
            "id": s.get("token_hash", "")[:16],
        }
        for s in fridge.load().get("senders", [])
    ]
    body: dict = {"senders": out, "max": store.MAX_SENDERS}
    # Absent, not null and not equal to the current cadence. The reader draws
    # the line only when the key is there.
    pending = pending_sentence(fridge)
    if pending:
        body["pending"] = pending
    return JSONResponse(body)


@app.post("/api/senders/revoke")
async def revoke(
    request: Request, authorization: str = Header(default="")
) -> JSONResponse:
    """The reader taking a phone's access away.

    Only the reader can do this, and that is the point: when the person who
    sends is in another country, the device is the one thing anybody can
    physically reach.
    """
    token = authorization.removeprefix("Bearer ").strip()
    fridge = store.fridge_for_device(token) if token else None
    if fridge is None or not fridge.exists():
        return refused("This reader is not connected to anything.", 401)
    body = await request.json()
    want = str(body.get("id", ""))
    for s in fridge.load().get("senders", []):
        h = s.get("token_hash", "")
        if h[:16] == want and store.revoke_sender(fridge, h):
            return JSONResponse(
                {"ok": True, "remaining": len(fridge.load().get("senders", []))}
            )
    return refused("That phone is not on this reader.", 404)


@app.post("/api/off")
def live_off(request: Request, authorization: str = Header(default="")) -> JSONResponse:
    """The reader saying Live was switched off on it, on its way out.

    Without this the only evidence is silence, and silence already means three
    other things: a flat battery, a router that moved, a reader somebody took
    to another house. None of them is knowable from here, so the page would
    have to guess -- and the one case somebody DID cause on purpose is the one
    it would get wrong.

    The reader sends this and does not wait for the answer. A call that fails
    costs nothing: the fridge simply goes quiet and the deadline passes, which
    is the flat-battery case and is handled.
    """
    token = authorization.removeprefix("Bearer ").strip()
    fridge = store.fridge_for_device(token) if token else None
    if fridge is None or not fridge.exists():
        return refused("This reader is not connected to anything.", 401)
    if not OFF_DEVICE.allow(fridge.id):
        return refused("Too many checks. Slow down.", 429)
    fridge.set_live(False)
    return JSONResponse({"ok": True})


@app.get("/api/pair/poll")
def pair_poll(pollToken: str = "") -> JSONResponse:
    got = PAIRINGS.poll(pollToken)
    if got is None:
        return JSONResponse({"paired": False})
    return JSONResponse(
        {
            "paired": True,
            "deviceToken": got["device_token"],
            "fridgeId": got["fridge_id"],
        }
    )


@app.post("/api/pair/abandon")
def pair_abandon(pollToken: str = "") -> JSONResponse:
    PAIRINGS.abandon(pollToken)
    return JSONResponse({"ok": True})


@app.get("/api/pull")
def pull(
    request: Request,
    authorization: str = Header(default=""),
    if_none_match: str = Header(default="", alias="If-None-Match"),
    x_live_on: str = Header(default="", alias="X-Live-On"),
) -> Response:
    """The only request a sleeping reader ever makes.

    304 means nothing changed: read the next-wake header and go back to sleep
    without touching the card or the panel. 200 carries the image.

    THE READER SAYS WHETHER LIVE IS STILL ON, in X-Live-On. That is a fact
    only the device has, and everything this service knows about a reader it
    learned the last time the reader spoke. When the NEXT check is does not
    need asking: it is the interval in this reply, stamped at the check-in and
    never recomputed. See store.next_expected.
    """
    token = authorization.removeprefix("Bearer ").strip()
    fridge = store.fridge_for_device(token) if token else None
    if fridge is None or not fridge.exists():
        return refused("This reader is not connected to anything.", 401)
    if not PULL_DEVICE.allow(fridge.id):
        return refused("Too many checks. Slow down.", 429)

    state = fridge.load()
    schedule = fridge.schedule()
    # A reader that is pulling is running Live; only an explicit 0 says
    # otherwise, so an old or absent header cannot switch a working fridge off.
    live_on = x_live_on.strip() != "0"

    # HOW LONG TO SLEEP, which is not the same number as how often it repeats.
    #
    # A repeating schedule hands out its interval. A daily one hands out however
    # many seconds are left until the next 07:00 in the timezone somebody chose,
    # which is a different figure every time and is the entire device-side cost
    # of clock-time schedules: none. The reader has no wall clock worth trusting
    # and never needs one.
    #
    # Clamped to the reader's own bounds (live::clampInterval applies the same
    # two), so a daily time eight minutes away becomes a fifteen-minute sleep
    # and overshoots by seven. That is inside the quarter of an hour both
    # surfaces promise, and it is why they promise a quarter of an hour.
    now = int(time.time())
    wake_in = max(
        store.MIN_INTERVAL_S,
        min(store.MAX_INTERVAL_S, store.next_after(schedule, now, now) - now),
    )
    # THE ALARM IS THE INTERVAL IN THIS REPLY, stamped once, here.
    #
    # It is tempting to have the reader report its own alarm -- it is the thing
    # holding the timer -- and a first version of this did exactly that, with a
    # header. The figure is composed before the reader has read the reply, and
    # a pull the reader got an answer to CLEARS ITS FAILURES and makes it adopt
    # the interval below, so the reported figure is never the alarm it goes on
    # to arm. One failed check was enough: the retry that succeeded reported
    # fifteen minutes, armed a day, and the website spent the next day saying
    # the check was due while the panel said "In a day".
    #
    # The one case a reader's own number could not be derived here -- a reader
    # in backoff -- is precisely the case whose pulls never arrive.
    #
    # What matters is that it is stamped ONCE, at the check-in, and never
    # recomputed: that is what stops a schedule change from moving a countdown
    # while the reader is still asleep on its old alarm. See next_expected.
    nth = fridge.touch_checkin(wake_in, live_on)
    entry = fridge.selected_entry()
    # THE NUMBER THAT MEANS SOMETHING. props.n is which check-in this was, so
    # the board can separate a reader that pulled once while somebody was
    # setting it up from one that has been waking on a fridge for a month.
    # Without it the only question answerable was "has it ever spoken", and
    # that counts an abandoned setup as a user.
    #
    # `device` comes from the reader's own header, which Live's /api/pull has
    # always carried (bridge::getToFile calls identify()) and this service
    # used to throw away. It is what joins a fridge to the rest of the fleet.
    events.post(
        SERVICE,
        "checkin",
        device=events.client_for(request).device,
        props={
            "fridge": fridge_key(fridge.id),
            "n": nth,
            "live_on": bool(live_on),
            "wake_in_s": int(wake_in),
        },
    )

    headers = {
        # Seconds, not a wall-clock time: the reader arms a relative timer and
        # has no trustworthy clock of its own to convert one against.
        "X-Next-Wake": str(wake_in),
        # AND WHAT TO SAY, which is a different question from what to sleep.
        #
        # The panel composes "Every N hours" from a figure, and under a daily
        # schedule the figure above is a part-day whenever the schedule changed
        # or a check was missed: set 07:00 at four in the morning and a reader
        # reading X-Next-Wake would announce "Every 3 hours" forever after.
        # live::scheduleNote reads THIS one, falling back to X-Next-Wake when it
        # is absent.
        "X-Cadence": str(store.cadence_seconds(schedule)),
        "X-Server-Time": str(now),
        "Cache-Control": "no-store",
    }
    if entry is None:
        # Nothing is picked: nothing has ever been sent, or the picked entry was
        # deleted and the history is empty. Not an error -- the reader keeps
        # whatever is on the glass and asks again later.
        return Response(status_code=204, headers=headers)
    image_id = entry["sha"]
    if if_none_match.strip('"') == image_id:
        return Response(status_code=304, headers=headers)
    payload = fridge.read_image(image_id)
    if payload is None:
        return Response(status_code=204, headers=headers)
    headers["ETag"] = f'"{image_id}"'
    return Response(content=payload, media_type="image/bmp", headers=headers)


# --------------------------------------------------------------- the browser


@app.post("/api/claim")
async def claim(request: Request) -> JSONResponse:
    ip = client_ip(request)
    if not CLAIM_IP.allow(ip) or not CLAIM_GLOBAL.allow("*"):
        return refused("Too many attempts. Try again in a few minutes.")
    body = await request.json()
    got = PAIRINGS.claim(str(body.get("code", "")))
    if got is None:
        return refused("That code did not work. Check the reader's screen.", 404)
    fridge = store.Fridge(got["fridge_id"])
    if got.get("joining"):
        # A join code adds a phone to a fridge that already exists; if it is
        # gone, there is nothing to join.
        if not fridge.exists():
            return refused("That reader is gone.", 404)
    elif not fridge.exists():
        # A setup code, claimed: THIS is where a fridge starts existing. Before
        # 2026-09-21 it was written when the code was shown, so every abandoned
        # setup screen left one behind for ever.
        fridge.create(token_hash(got["device_token"]))
        store.index_token(got["device_token"], fridge.id)
    name = str(body.get("name", "") or "A phone")
    if not store.add_sender(fridge, got["sender_token"], name):
        # Refused, never silently rotated. Dropping the oldest to make room
        # would take a fridge away from whoever had it first and tell nobody,
        # and the person losing it is the one least able to notice.
        return refused(
            f"That reader already has {store.MAX_SENDERS} phones. Remove one on the reader first.",
            409,
        )
    # Somebody at the other end typed the code. This is the first Live number
    # that involves two people, and it is still not a user: a pairing that
    # never checks in twice is an abandoned setup.
    events.post(
        SERVICE,
        "paired",
        props={
            "fridge": fridge_key(fridge.id),
            "joining": bool(got.get("joining")),
            "senders": len(fridge.load().get("senders", [])),
        },
    )
    resp = JSONResponse({"ok": True, "fridgeId": got["fridge_id"]})
    # Secure follows the scheme the request actually arrived on rather than
    # being hardcoded. In production that is always https (Cloudflare
    # terminates and cloudflared forwards the header), so this is Secure where
    # it matters. Hardcoding it broke the only environment where it is not:
    # a browser silently DROPS a Secure cookie from an http origin, so the
    # claim returned 200, the page believed it had connected, and every later
    # request arrived with no cookie at all. No error anywhere.
    proto = request.headers.get("x-forwarded-proto", request.url.scheme)
    # SCOPED TO THE WHOLE DOMAIN, because the page and this service are two
    # names under it. Without `domain` the cookie belongs to fridge.ma-r-s.com
    # alone; the page on crossplay.ma-r-s.com is then a third-party context to
    # it and Safari drops it outright, which means the claim returns 200, the
    # page believes it is connected, and every request after it arrives
    # anonymous with nothing anywhere saying why. With it the cookie is
    # first-party for both names.
    #
    # SameSite stays Lax and Lax is enough: SameSite is decided by the
    # REGISTRABLE DOMAIN, not the origin, so crossplay.ma-r-s.com calling
    # fridge.ma-r-s.com is same-site and the cookie rides the XHR. None would
    # buy nothing here and would offer the cookie to every other site on earth.
    # Measured in a browser against the deployed pair, not reasoned about.
    #
    # The domain is attached only under ma-r-s.com. A browser REFUSES a cookie
    # whose Domain does not cover the host that set it, so hardcoding it would
    # make every local run silently cookie-less -- the same failure `secure`
    # had, one attribute over.
    host = (request.headers.get("host") or "").split(":")[0]
    resp.set_cookie(
        "live_sender",
        got["sender_token"],
        max_age=400 * 86400,
        httponly=True,
        samesite="lax",
        secure=(proto == "https"),
        domain=COOKIE_DOMAIN if host.endswith("ma-r-s.com") else None,
    )
    return resp


def _sender_fridge(live_sender: str | None) -> store.Fridge | None:
    return store.fridge_for_sender(live_sender) if live_sender else None


@app.get("/api/state")
def state(live_sender: str = Cookie(default=None)) -> JSONResponse:
    fridge = _sender_fridge(live_sender)
    if fridge is None or not fridge.exists():
        return JSONResponse({"connected": False})
    s = fridge.load()
    schedule = fridge.schedule()
    body = {
        "connected": True,
        "lastCheckin": s.get("last_checkin", 0),
        # THE SCHEDULE SOMEBODY SET, and separately the cadence the reader is
        # actually asleep on. The page needs both: the chip says what was
        # chosen, and the countdown is measured against what is armed.
        "schedule": {
            "mode": schedule["mode"],
            "intervalSeconds": schedule["interval_s"],
            "dailyTime": schedule["daily_time"],
            "tz": schedule["tz"],
        },
        # 0 while Live is off on the reader: there is no next check, and a
        # figure there would be one the page then has to explain away.
        "nextExpected": fridge.next_expected(),
        # The reader's own report, not this service's opinion: what separates
        # "switched off on purpose" from "we have not heard from it", which
        # look identical from here and mean opposite things.
        "liveOn": bool(s.get("live_on", True)),
        "senders": len(s.get("senders", [])),
    }
    # THE SAME SENTENCE THE READER IS GIVEN, verbatim, so the panel and the page
    # cannot describe one reader two ways. Absent when nothing is pending.
    pending = pending_sentence(fridge)
    if pending:
        body["pending"] = pending
    return JSONResponse(body)


# ---------------------------------------------------------------- history
#
# EVERYTHING EVER SENT TO THIS READER, newest first, SHARED by every phone
# connected to it. It is the record of what the reader has shown rather than of
# what any one person sent, so any of them can send an old one out again or
# delete one, and every entry names who sent it.


def _entry_json(e: dict) -> dict:
    return {
        "id": e["id"],
        "kind": e.get("kind", "drawing"),
        "at": e.get("at", 0),
        "by": e.get("by", "A phone"),
        "thumb": f"/api/history/{e['id']}/thumb",
    }


@app.get("/api/history")
def history(live_sender: str = Cookie(default=None)) -> JSONResponse:
    fridge = _sender_fridge(live_sender)
    if fridge is None or not fridge.exists():
        return refused("This browser is not connected to a reader.", 401)
    s = fridge.load()
    return JSONResponse(
        {
            "entries": [_entry_json(e) for e in s.get("history", [])],
            "selected": s.get("selected"),
        }
    )


@app.get("/api/history/{entry_id}/thumb")
def history_thumb(
    entry_id: str, live_sender: str = Cookie(default=None)
) -> Response:
    fridge = _sender_fridge(live_sender)
    if fridge is None or not fridge.exists():
        return refused("This browser is not connected to a reader.", 401)
    entry = fridge.entry(entry_id)
    if entry is None:
        return refused("That is not on this reader.", 404)
    png = fridge.read_thumb(entry["sha"])
    if png is None:
        # The record survives its picture. A tile that says the picture is gone
        # is the truth; a broken image is a page that looks failed.
        return refused("That picture is gone.", 410)
    return Response(
        content=png,
        media_type="image/png",
        headers={
            # PRIVATE. This picture is behind a session: `public` invited any
            # shared cache between here and a phone to keep one person's
            # drawing and hand it to the next, and it let the browser reuse a
            # copy fetched before the page started sending credentials --
            # which then had no CORS header and was refused. Content-addressed
            # still, so it never changes under this entry.
            "Cache-Control": "private, max-age=31536000, immutable",
            # The answer differs by Origin, so anything caching it has to key
            # on that rather than serve the first copy it happened to store.
            "Vary": "Origin",
        },
    )


@app.post("/api/history")
async def send(request: Request, live_sender: str = Cookie(default=None)) -> JSONResponse:
    """Sending is what puts something in the history, and it is picked.

    The body is the reader picture itself and nothing else; the kind rides in a
    header and the sender's name is read off the fridge rather than taken from
    the caller, because a name a request could choose is a name a request could
    forge.
    """
    fridge = _sender_fridge(live_sender)
    if fridge is None or not fridge.exists():
        return refused("This browser is not connected to a reader.", 401)
    if not POST_SENDER.allow(fridge.id):
        return refused("Too many pictures at once. Try again shortly.")
    payload = await request.body()
    if len(payload) not in store.IMAGE_SIZES:
        # Bounded before anything is written. A wrong-sized file that still
        # parses is drawn half-rendered on the reader forever.
        expected = " or ".join(str(n) for n in store.IMAGE_SIZES)
        return refused(
            f"That is not a reader picture ({len(payload)} bytes, expected {expected}).",
            400,
        )
    kind = request.headers.get("x-kind", "drawing")
    entry = fridge.add_entry(payload, kind, store.sender_name(fridge, live_sender))
    # A phone sent a picture. No `device`: this request comes from a browser,
    # and attributing it to one would put a phone into the reader count. `kind`
    # rides along because a drawing and a photo are different things to send
    # and the difference is free to record here.
    events.post(
        SERVICE,
        "image",
        props={"fridge": fridge_key(fridge.id), "bytes": len(payload), "kind": kind},
    )
    return JSONResponse(
        {
            "ok": True,
            "entry": _entry_json(entry),
            "selected": entry["id"],
            "nextExpected": fridge.next_expected(),
        }
    )


@app.post("/api/history/{entry_id}/select")
def select(entry_id: str, live_sender: str = Cookie(default=None)) -> JSONResponse:
    """Point the reader at an older one. It costs the reader nothing until its
    next wake, and nothing at all if it is already showing that picture: the
    ETag is the content hash, so a re-select answers 304."""
    fridge = _sender_fridge(live_sender)
    if fridge is None or not fridge.exists():
        return refused("This browser is not connected to a reader.", 401)
    if not POST_SENDER.allow(fridge.id):
        return refused("Too many changes at once. Try again shortly.")
    if not fridge.select(entry_id):
        # Somebody else on this reader deleted it. The page refetches and says
        # so rather than reporting a failure nobody caused.
        return refused("That is not on this reader.", 404)
    return JSONResponse({"ok": True, "selected": entry_id})


@app.delete("/api/history/{entry_id}")
def delete_entry(entry_id: str, live_sender: str = Cookie(default=None)) -> JSONResponse:
    """Deleting is for EVERYBODY on this reader, which is why the page asks
    first. Deleting the picked entry moves the pick to the newest remaining
    one; the reply names it so the page can say what changed rather than
    silently re-pointing a device in another country."""
    fridge = _sender_fridge(live_sender)
    if fridge is None or not fridge.exists():
        return refused("This browser is not connected to a reader.", 401)
    if not POST_SENDER.allow(fridge.id):
        return refused("Too many changes at once. Try again shortly.")
    if not fridge.remove_entry(entry_id):
        return refused("That is not on this reader.", 404)
    return JSONResponse({"ok": True, "selected": fridge.load().get("selected")})


@app.put("/api/schedule")
async def put_schedule(
    request: Request, live_sender: str = Cookie(default=None)
) -> JSONResponse:
    """Two shapes: every so often, or once a day at a time in a named timezone.

    The reader hears about neither. It is told a number of seconds to sleep for
    on its next check and that is the whole of its involvement, which is why a
    daily alarm needed no firmware at all.
    """
    fridge = _sender_fridge(live_sender)
    if fridge is None or not fridge.exists():
        return refused("This browser is not connected to a reader.", 401)
    wanted = store.normalise_schedule(await request.json())
    if wanted is None:
        return refused("That is not a schedule this reader can keep.", 400)
    s = fridge.load()
    s["schedule"] = wanted
    fridge.save(s)
    body = {
        "ok": True,
        "nextExpected": fridge.next_expected(),
        "cadence": store.cadence_words(wanted),
    }
    # The reader is still asleep on the old one, and both surfaces say so in
    # the same sentence until it wakes up and picks this one up.
    pending = pending_sentence(fridge)
    if pending:
        body["pending"] = pending
    return JSONResponse(body)


# No static mount and no page route: every path this service answers is under
# /api/ (plus /healthz). The reader's QR encodes the page's own address on the
# site -- wallpapersui::kLiveAddress, which is also the line the panel prints --
# so nothing walks through this host to reach it.
