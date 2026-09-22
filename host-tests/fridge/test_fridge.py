"""The Live service, driven through its own HTTP surface.

Pairing, sending, the shared history, picking an old one, deleting the picked
one, clock-time schedules, and the pending cadence: the window in which the
reader is still asleep on the schedule it last picked up.

  host-tests/fridge/run.sh
"""

import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "server" / "fridge-bridge"))

from fastapi.testclient import TestClient  # noqa: E402

from bridge import store  # noqa: E402
from bridge.app import SITE_ORIGIN, app  # noqa: E402

ok, bad = [], []


def check(name, cond, detail=""):
    (ok if cond else bad).append(f"{name}{(' -- ' + str(detail)) if detail else ''}")


def bmp(band_x=200, width=80):
    """A reader picture, byte-exact, built the way the page builds one."""
    W, H = 480, 800
    row = ((W * 2 + 31) >> 5) << 2
    off = 14 + 40 + 16
    b = bytearray(off + row * H)
    b[0:2] = b"BM"
    struct.pack_into("<I", b, 2, len(b))
    struct.pack_into("<I", b, 10, off)
    struct.pack_into("<I", b, 14, 40)
    struct.pack_into("<i", b, 18, W)
    struct.pack_into("<i", b, 22, H)
    struct.pack_into("<H", b, 26, 1)
    struct.pack_into("<H", b, 28, 2)
    struct.pack_into("<I", b, 34, row * H)
    struct.pack_into("<I", b, 46, 4)
    struct.pack_into("<I", b, 50, 4)
    for i, g in enumerate((0, 85, 170, 255)):
        o = 54 + i * 4
        b[o] = b[o + 1] = b[o + 2] = g
    for y in range(H):
        dst = off + y * row
        for x in range(W):
            lv = 0 if band_x <= x < band_x + width else 3
            b[dst + (x >> 2)] |= (lv & 3) << ((3 - (x & 3)) * 2)
    return bytes(b)


c = TestClient(app)

# --- pairing ---------------------------------------------------------------
started = c.post("/api/pair/start").json()
claimed = c.post("/api/claim", json={"code": started["code"], "name": "iPhone"})
check("a phone can claim the code", claimed.status_code == 200, claimed.text[:90])
polled = c.get("/api/pair/poll", params={"pollToken": started["pollToken"]}).json()
check("and the reader learns its token", polled.get("paired") is True, polled)
dev = {"Authorization": f"Bearer {polled['deviceToken']}"}

# --- nothing sent yet ------------------------------------------------------
h = c.get("/api/history").json()
check("the history starts empty", h == {"entries": [], "selected": None}, h)
pull = c.get("/api/pull", headers=dev)
check("a pull with nothing picked is 204", pull.status_code == 204, pull.status_code)
check("and still says when to wake", "X-Next-Wake" in pull.headers, dict(pull.headers))
check(
    "and what the cadence is, apart from the sleep",
    pull.headers.get("X-Cadence") == "86400",
    pull.headers.get("X-Cadence"),
)

# --- sending ---------------------------------------------------------------
first = c.post("/api/history", content=bmp(), headers={"X-Kind": "drawing"})
check("sending works", first.status_code == 200, first.text[:120])
e1 = first.json()["entry"]
check("the entry names the phone", e1["by"] == "iPhone", e1)
check("and it is picked", first.json()["selected"] == e1["id"])

second = c.post("/api/history", content=bmp(300, 60), headers={"X-Kind": "message"})
e2 = second.json()["entry"]
h = c.get("/api/history").json()
check("newest first", [e["id"] for e in h["entries"]] == [e2["id"], e1["id"]], h)
check("the newest is picked", h["selected"] == e2["id"])

thumb = c.get(f"/api/history/{e1['id']}/thumb")
check("a thumbnail is served", thumb.status_code == 200, thumb.status_code)
check("and it is a PNG", thumb.content[:8] == b"\x89PNG\r\n\x1a\n")
check("and it is small", len(thumb.content) < 4000, len(thumb.content))
check(
    "and it is cached forever, being content addressed",
    "immutable" in thumb.headers.get("cache-control", ""),
    thumb.headers.get("cache-control"),
)

# --- the reader takes the picked one --------------------------------------
pull = c.get("/api/pull", headers=dev)
check("the reader gets the picked picture", pull.status_code == 200, pull.status_code)
check("byte exact", len(pull.content) == store.IMAGE_BYTES_2BIT, len(pull.content))
etag = pull.headers["ETag"]
again = c.get("/api/pull", headers={**dev, "If-None-Match": etag})
check("and a second wake costs nothing", again.status_code == 304, again.status_code)

# --- picking an older one --------------------------------------------------
sel = c.post(f"/api/history/{e1['id']}/select")
check("an older one can be picked", sel.status_code == 200, sel.text[:90])
check(
    "and it is what the reader takes",
    c.get("/api/history").json()["selected"] == e1["id"],
)
check(
    "picking does not duplicate it",
    len(c.get("/api/history").json()["entries"]) == 2,
)
pull = c.get("/api/pull", headers={**dev, "If-None-Match": etag})
check(
    "and the reader is handed the older picture",
    pull.status_code == 200,
    pull.status_code,
)

# --- deleting the picked one ----------------------------------------------
gone = c.delete(f"/api/history/{e1['id']}")
check("deleting works", gone.status_code == 200, gone.text[:90])
check(
    "and the pick moves to the newest remaining",
    gone.json()["selected"] == e2["id"],
    gone.json(),
)
check(
    "deleting twice is refused", c.delete(f"/api/history/{e1['id']}").status_code == 404
)
check(
    "and its refusal is a sentence",
    "not on this reader" in c.delete(f"/api/history/{e1['id']}").json()["error"],
)

# The file behind an entry is shared, so deleting one copy keeps the other.
a = c.post("/api/history", content=bmp(), headers={"X-Kind": "drawing"}).json()["entry"]
b = c.post("/api/history", content=bmp(), headers={"X-Kind": "drawing"}).json()["entry"]
c.delete(f"/api/history/{a['id']}")
still = c.get(f"/api/history/{b['id']}/thumb")
check(
    "one send of a picture does not delete another's file",
    still.status_code == 200,
    still.status_code,
)

# --- the empty history -----------------------------------------------------
for e in c.get("/api/history").json()["entries"]:
    c.delete(f"/api/history/{e['id']}")
h = c.get("/api/history").json()
check("everything can be deleted", h["entries"] == [], h)
check("and then nothing is picked", h["selected"] is None, h)
pull = c.get("/api/pull", headers=dev)
check(
    "a reader with nothing picked keeps what is on its glass",
    pull.status_code == 204,
    pull.status_code,
)

# --- schedules -------------------------------------------------------------
st = c.get("/api/state").json()
check(
    "the default schedule is a repeat",
    st["schedule"]["mode"] == "every",
    st["schedule"],
)
check("nothing is pending yet", "pending" not in st, st.get("pending"))

put = c.put(
    "/api/schedule",
    json={
        "mode": "daily",
        "intervalSeconds": 86400,
        "dailyTime": "07:00",
        "tz": "America/Bogota",
    },
)
check("a clock time can be set", put.status_code == 200, put.text[:120])
check(
    "and it is named in the page's own words",
    put.json()["cadence"] == "07:00 daily",
    put.json(),
)
check("and it is pending", "pending" in put.json(), put.json())
check(
    "in one sentence, the same one the reader gets",
    put.json()["pending"] == c.get("/api/senders", headers=dev).json().get("pending"),
    (
        put.json().get("pending"),
        c.get("/api/senders", headers=dev).json().get("pending"),
    ),
)
check(
    "and the page says the same thing",
    c.get("/api/state").json().get("pending") == put.json()["pending"],
)
check(
    "the sentence names the cadence",
    put.json()["pending"] == "Changing to 07:00 daily after the next check.",
    put.json()["pending"],
)

# The reader is still asleep on the old cadence until it wakes.
pull = c.get("/api/pull", headers=dev)
wake = int(pull.headers["X-Next-Wake"])
check("a daily schedule hands out a part day", 900 <= wake <= 86400, wake)
check(
    "and announces the cadence, not the leftover",
    pull.headers["X-Cadence"] == "86400",
    pull.headers["X-Cadence"],
)
check(
    "nothing is pending once the reader has picked it up",
    "pending" not in c.get("/api/senders", headers=dev).json(),
    c.get("/api/senders", headers=dev).json(),
)
check(
    "and the page agrees",
    "pending" not in c.get("/api/state").json(),
)

# Setting the same schedule again is not a change anybody can see.
c.put(
    "/api/schedule",
    json={
        "mode": "daily",
        "intervalSeconds": 86400,
        "dailyTime": "07:00",
        "tz": "America/Bogota",
    },
)
check(
    "re-choosing the same schedule is not pending",
    "pending" not in c.get("/api/state").json(),
    c.get("/api/state").json().get("pending"),
)

# Live off means there is no next check to change after.
c.put("/api/schedule", json={"mode": "every", "intervalSeconds": 21600})
check("a change is pending again", "pending" in c.get("/api/state").json())
c.post("/api/off", headers=dev)
check(
    "Live off has nothing pending, because there is no next check",
    "pending" not in c.get("/api/senders", headers=dev).json(),
    c.get("/api/senders", headers=dev).json(),
)

# --- what a schedule may be ------------------------------------------------
check(
    "an interval outside the list is refused",
    c.put("/api/schedule", json={"mode": "every", "intervalSeconds": 3600}).status_code
    == 400,
)
check(
    "a timezone that does not exist is refused",
    c.put("/api/schedule", json={"mode": "daily", "tz": "Mars/Olympus"}).status_code
    == 400,
)
check(
    "a time that does not exist is refused",
    c.put("/api/schedule", json={"mode": "daily", "dailyTime": "29:99"}).status_code
    == 400,
)
check(
    "a mode that does not exist is refused",
    c.put("/api/schedule", json={"mode": "cron"}).status_code == 400,
)
check(
    "a wrong sized picture is refused",
    c.post("/api/history", content=b"x" * 100).status_code == 400,
)

# --- every sentence the reader can be handed -------------------------------
from bridge.app import PENDING_TEMPLATE  # noqa: E402

words = [
    store.cadence_words({"mode": "every", "interval_s": i})
    for i in store.ALLOWED_INTERVALS
]
words.append(store.cadence_words({"mode": "daily", "daily_time": "00:00"}))
longest = max((PENDING_TEMPLATE.format(w) for w in words), key=len)
check("the pending corpus is finite", len(words) == 7, words)
check("and its longest sentence is short", len(longest) <= 52, (len(longest), longest))

# --- the browser is allowed to make the calls the page actually makes -------
#
# A CUSTOM HEADER MAKES A REQUEST NON-SIMPLE, so the browser asks permission
# first, and a header the service does not name is refused. The browser then
# reports that refusal as a network failure, and the page says "could not reach
# the service" -- naming the wrong cause while the service is perfectly
# healthy. x-kind shipped with the history rail and was not added to
# allow_headers, so every send was blocked in production and the message sent
# everybody looking at the wrong thing.
#
# So the corpus is GENERATED from the page, not typed here: any header live.js
# sends cross-origin must survive a preflight. A new one cannot repeat this.
site_js = (
    pathlib.Path(__file__).resolve().parents[2] / "site" / "live" / "live.js"
).read_text()
sent_headers = sorted(
    {h.lower() for h in re.findall(r'"(x-[a-z0-9-]+)"\s*:', site_js)}
    | {"content-type"}
)
check("the page's cross-origin headers were found", len(sent_headers) >= 2, sent_headers)
for h in sent_headers:
    r = c.options(
        "/api/history",
        headers={
            "Origin": SITE_ORIGIN,
            "Access-Control-Request-Method": "POST",
            "Access-Control-Request-Headers": h,
        },
    )
    check(f"the browser may send {h}", r.status_code == 200, (h, r.status_code))

# --- the dev proxy's cookie, against the one the service really sets --------
#
# Three times tonight a cookie or header attribute was silently dropped by a
# browser and surfaced as an unrelated message: a missing allowed header read as
# "could not reach the service", and a Secure cookie over plain http read as the
# six digits being wrong. site/serve.py rewrites the forwarded Set-Cookie so a
# local page keeps its session, and that rewriting was trusted rather than
# asserted.
#
# THE COOKIE HERE IS THE SERVICE'S OWN, taken off the claim above rather than
# typed: a literal in a test goes on passing after the service changes what it
# sets, which is the shape of half the bugs in this file's history.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "site"))
import serve  # noqa: E402

# UNDER THE HEADER PRODUCTION SENDS. The service marks the cookie Secure from
# the scheme its request arrived on, and this in-process client speaks http, so
# a plain claim here would produce a cookie with no Secure on it and the case
# that broke Mario's phone would go unobserved. Cloudflare and cloudflared send
# x-forwarded-proto, so the claim below is the production one.
fresh = c.post("/api/pair/start").json()
claimed_https = c.post(
    "/api/claim",
    json={"code": fresh["code"], "name": "iPhone"},
    headers={"x-forwarded-proto": "https"},
)
raw = claimed_https.headers.get("set-cookie", "")
check("the claim really sets a cookie", "live_sender=" in raw, raw[:60])
check(
    "and marks it Secure behind https, which is what makes this necessary",
    "secure" in raw.lower(),
    raw,
)
check(
    "and does not mark it Secure over plain http, which is why it is derived",
    "secure" not in claimed.headers.get("set-cookie", "").lower(),
    claimed.headers.get("set-cookie", ""),
)
over_http = serve.dev_cookie(raw, https=False)
over_https = serve.dev_cookie(raw, https=True)
check(
    "the proxy drops Domain, which no host but ma-r-s.com may keep",
    "domain=" not in over_http.lower() and "domain=" not in over_https.lower(),
    over_http,
)
check(
    "and drops Secure over plain http, which is where the session was lost",
    "secure" not in over_http.lower(),
    over_http,
)
check(
    "and keeps Secure when the dev server is itself https",
    "secure" in over_https.lower(),
    over_https,
)
for attr in ("httponly", "samesite"):
    if attr in raw.lower():
        check(
            f"and leaves {attr} exactly as sent",
            attr in over_http.lower() and attr in over_https.lower(),
            over_http,
        )
check(
    "and keeps the value itself",
    over_http.startswith("live_sender="),
    over_http[:40],
)

print("PASS" if not bad else "FAIL")
for x in ok:
    print("  ok   " + x)
for x in bad:
    print("  FAIL " + x)
print(f"{len(ok)} ok, {len(bad)} failed")
sys.exit(1 if bad else 0)
