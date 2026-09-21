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
that finds nothing spends a few kilobytes and no SD write and no repaint.

Host must be exactly one label below the apex: Cloudflare's Universal SSL on
the free plan covers ma-r-s.com and *.ma-r-s.com and nothing deeper, and the
reader's baked root bundle carries the chain that edge serves.
"""

import time

from fastapi import Cookie, FastAPI, Header, Request, Response
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, PlainTextResponse

from . import store
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
app.add_middleware(
    CORSMiddleware,
    allow_origins=[SITE_ORIGIN],
    allow_credentials=True,
    allow_methods=["GET", "POST", "PUT", "OPTIONS"],
    allow_headers=["content-type"],
    max_age=600,
)

# Copied in spirit from read-bridge's table. The claim limits are the ones that
# matter here: a six-digit code is a million, so the caps are what makes
# guessing hopeless rather than the size of the space.
CLAIM_IP = Window(10, 300)
CLAIM_GLOBAL = Window(120, 60)
PAIR_IP = Window(10, 300)
PULL_DEVICE = Window(30, 300)
POST_SENDER = Window(60, 300)


def client_ip(request: Request) -> str:
    # Behind Cloudflare and cloudflared, so the first hop is always local.
    fwd = request.headers.get("cf-connecting-ip") or request.headers.get("x-forwarded-for", "")
    return fwd.split(",")[0].strip() or (request.client.host if request.client else "?")


def refused(reason: str, code: int = 429) -> JSONResponse:
    # One sentence, ready to show. The reader must never invent its own wording
    # for a decision this service made (see BridgeHttp.h).
    return JSONResponse({"error": reason}, status_code=code)


@app.get("/healthz")
def healthz() -> PlainTextResponse:
    return PlainTextResponse("ok")


# ---------------------------------------------------------------- the reader


@app.post("/api/pair/start")
def pair_start(request: Request) -> JSONResponse:
    if not PAIR_IP.allow(client_ip(request)):
        return refused("Too many attempts from this address. Try again in a few minutes.")
    # The fridge and its device token are made HERE, not when somebody claims
    # the code. The browser is handed its cookie the moment it claims, and a
    # fridge that did not exist yet would make the page say it is not
    # connected until the reader next spoke -- which for a sleeping reader is
    # hours.
    import secrets

    fridge_id = store.new_fridge_id()
    device_token = secrets.token_urlsafe(32)
    fridge = store.Fridge(fridge_id)
    fridge.create(token_hash(device_token))
    store.index_token(device_token, fridge_id)
    return JSONResponse(PAIRINGS.start(fridge_id, device_token))


@app.post("/api/pair/join")
def pair_join(request: Request, authorization: str = Header(default="")) -> JSONResponse:
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
        return refused("Too many attempts from this address. Try again in a few minutes.")
    if len(fridge.load().get("senders", [])) >= store.MAX_SENDERS:
        # Said before a code is minted rather than after somebody types it.
        return refused(f"This reader already has {store.MAX_SENDERS} phones. Remove one first.", 409)
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
        {"name": s.get("name", "A phone"), "pairedAt": s.get("paired_at", 0), "id": s.get("token_hash", "")[:16]}
        for s in fridge.load().get("senders", [])
    ]
    return JSONResponse({"senders": out, "max": store.MAX_SENDERS})


@app.post("/api/senders/revoke")
async def revoke(request: Request, authorization: str = Header(default="")) -> JSONResponse:
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
            return JSONResponse({"ok": True, "remaining": len(fridge.load().get("senders", []))})
    return refused("That phone is not on this reader.", 404)


@app.get("/api/pair/poll")
def pair_poll(pollToken: str = "") -> JSONResponse:
    got = PAIRINGS.poll(pollToken)
    if got is None:
        return JSONResponse({"paired": False})
    return JSONResponse({"paired": True, "deviceToken": got["device_token"], "fridgeId": got["fridge_id"]})


@app.post("/api/pair/abandon")
def pair_abandon(pollToken: str = "") -> JSONResponse:
    PAIRINGS.abandon(pollToken)
    return JSONResponse({"ok": True})


@app.get("/api/pull")
def pull(
    request: Request,
    authorization: str = Header(default=""),
    if_none_match: str = Header(default="", alias="If-None-Match"),
) -> Response:
    """The only request a sleeping reader ever makes.

    304 means nothing changed: read the next-wake header and go back to sleep
    without touching the card or the panel. 200 carries the image.
    """
    token = authorization.removeprefix("Bearer ").strip()
    fridge = store.fridge_for_device(token) if token else None
    if fridge is None or not fridge.exists():
        return refused("This reader is not connected to anything.", 401)
    if not PULL_DEVICE.allow(fridge.id):
        return refused("Too many checks. Slow down.", 429)

    fridge.touch_checkin()
    state = fridge.load()
    interval = int(state.get("interval_s", store.DEFAULT_INTERVAL_S))
    image_id = state.get("image_id")

    headers = {
        # Seconds, not a wall-clock time: the reader arms a relative timer and
        # has no trustworthy clock of its own to convert one against.
        "X-Next-Wake": str(interval),
        "X-Server-Time": str(int(time.time())),
        "Cache-Control": "no-store",
    }
    if image_id is None:
        # Nothing has ever been sent. Not an error: the reader keeps whatever
        # is on the glass and asks again later.
        return Response(status_code=204, headers=headers)
    if if_none_match.strip('"') == image_id:
        return Response(status_code=304, headers=headers)
    payload = fridge.read_image()
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
    if not fridge.exists():
        return refused("That reader is gone.", 404)
    name = str(body.get("name", "") or "A phone")
    if not store.add_sender(fridge, got["sender_token"], name):
        # Refused, never silently rotated. Dropping the oldest to make room
        # would take a fridge away from whoever had it first and tell nobody,
        # and the person losing it is the one least able to notice.
        return refused(
            f"That reader already has {store.MAX_SENDERS} phones. Remove one on the reader first.", 409
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
    return JSONResponse(
        {
            "connected": True,
            "lastCheckin": s.get("last_checkin", 0),
            "intervalSeconds": s.get("interval_s", store.DEFAULT_INTERVAL_S),
            "nextExpected": fridge.next_expected(),
            "imageId": s.get("image_id"),
            "imageSetAt": s.get("image_set_at", 0),
            "senders": len(s.get("senders", [])),
        }
    )


@app.put("/api/image")
async def put_image(request: Request, live_sender: str = Cookie(default=None)) -> JSONResponse:
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
        return refused(f"That is not a reader picture ({len(payload)} bytes, expected {expected}).", 400)
    image_id = fridge.set_image(payload)
    return JSONResponse({"ok": True, "imageId": image_id, "nextExpected": fridge.next_expected()})


@app.put("/api/interval")
async def put_interval(request: Request, live_sender: str = Cookie(default=None)) -> JSONResponse:
    fridge = _sender_fridge(live_sender)
    if fridge is None or not fridge.exists():
        return refused("This browser is not connected to a reader.", 401)
    body = await request.json()
    try:
        seconds = int(body.get("seconds", 0))
    except (TypeError, ValueError):
        seconds = 0
    if not store.MIN_INTERVAL_S <= seconds <= store.MAX_INTERVAL_S:
        return refused("Pick an interval between fifteen minutes and a week.", 400)
    s = fridge.load()
    s["interval_s"] = seconds
    fridge.save(s)
    return JSONResponse({"ok": True, "nextExpected": fridge.next_expected()})


# No static mount and no page route: every path this service answers is under
# /api/ (plus /healthz). The reader's QR encodes the page's own address on the
# site -- wallpapersui::kLiveAddress, which is also the line the panel prints --
# so nothing walks through this host to reach it.
