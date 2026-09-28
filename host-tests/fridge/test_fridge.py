"""The Live service, driven through its own HTTP surface.

Pairing, sending, the shared history, picking an old one, deleting the picked
one, clock-time schedules, and the pending cadence: the window in which the
reader is still asleep on the schedule it last picked up.

  host-tests/fridge/run.sh
"""

import json
import pathlib
import re
import struct
import sys
import time

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

# --- the reader's battery ----------------------------------------------------
#
# It rides the pull as X-Battery. The failure that matters is not a crash: it is
# an absent or garbled reading turning into a CONFIDENT number -- a 0% that sends
# somebody across town with a charger for a full battery, or a stale figure
# replaced by nothing. So absent stays absent, and garbage leaves the last real
# reading standing.
st = c.get("/api/state").json()
check("no battery until the reader has reported one", "battery" not in st, st)
before = int(time.time())
c.get("/api/pull", headers={**dev, "X-Battery": "87"})
st = c.get("/api/state").json()
check("the reader's battery reaches the page", st.get("battery") == 87, st)
check("with when it said so", st.get("batteryAt", 0) >= before, st)
for junk in ("", "abc", "101", "-5", "8.5", "1000"):
    c.get("/api/pull", headers={**dev, "X-Battery": junk})
st = c.get("/api/state").json()
check("a missing or garbled reading leaves the last real one", st.get("battery") == 87, st)
b = c.get("/api/battery")
check("the graph's readings are served", b.status_code == 200, b.status_code)
pts = b.json().get("points")
check("one reading per check-in that carried one", [p[1] for p in pts] == [87], pts)
check("and nothing claimed from one reading", "daysLeft" not in b.json(), b.json())
anon = TestClient(app).get("/api/battery")
check("a browser with no reader gets none", anon.status_code == 401, anon.status_code)

for raw, want in (("0", 0), ("100", 100), ("42", 42), (" 7 ", 7), ("", None), ("x", None), ("101", None)):
    check(f"X-Battery {raw!r} reads as {want}", store.parse_battery(raw) == want, store.parse_battery(raw))

# The log keeps thirty days and trims itself, rather than growing for as long
# as the fridge lives.
fridge_id = next(iter(json.loads(store._index_path().read_text()).values()))
fr = store.Fridge(fridge_id)
now = int(time.time())
fr.battery_path.write_text(f"{now - store.BATTERY_KEEP_S - 60} 99\n{now - 60} 50\n")
check("readings older than thirty days are not drawn", fr.battery_log(now) == [(now - 60, 50)], fr.battery_log(now))
old_line = f"{now - store.BATTERY_KEEP_S - 60} 99\n"
fr.battery_path.write_text(old_line * (store.BATTERY_LOG_TRIM_BYTES // len(old_line) + 1))
fr._record_battery(now, 49)
check(
    "and the log is cut back to its window once it outgrows it",
    fr.battery_path.read_text() == f"{now} 49\n",
    fr.battery_path.stat().st_size,
)

# --- what the graph says about the readings ----------------------------------
H, D = 3600, 86400
T0 = 1_700_000_000


def hourly(start_pct, per_day, days, t0=T0):
    return [(t0 + h * H, round(start_pct - per_day * h / 24)) for h in range(days * 24 + 1)]


def outlook(points, after=0):
    """The outlook as the page would get it, `after` seconds past the last reading."""
    return store.battery_outlook(points, (points[-1][0] if points else T0) + after)


steady = hourly(80, 2, 10)
out = outlook(steady)
check("two points a day from 60% is about thirty days", 29 <= out.get("days_left", 0) <= 31, out)
check("and no charge is claimed when there was none", "charged_at" not in out, out)
check(
    "and it counts down while the reader is silent, rather than standing still",
    22 <= outlook(steady, 7 * D).get("days_left", 0) <= 24,
    outlook(steady, 7 * D),
)
check(
    "down to nothing once the projection has run out, never below",
    outlook(steady, 60 * D).get("days_left") == 0,
    outlook(steady, 60 * D),
)

charged = hourly(60, 2, 5) + [(T0 + 5 * D + H, 95)] + hourly(94, 1, 4, T0 + 5 * D + 2 * H)
out = outlook(charged)
check("a jump up is a charge, dated by the reading that showed it", out.get("charged_at") == T0 + 5 * D + H, out)
check(
    "and the estimate is measured since the charge, not across it",
    85 <= out.get("days_left", 0) <= 95,
    out,
)

# Days on the cable at 100%, then a real discharge. Fitted from the plateau the
# battery looks like it barely drains, and the page would promise months.
plateau = [(T0 + d * D, 100) for d in range(5)] + [(T0 + (5 + d) * D, 100 - 4 * d) for d in range(3)]
out = store.battery_outlook(plateau, plateau[-1][0])
check("time held at 100% on the cable is not counted as a slow drain", 21 <= out.get("days_left", 0) <= 24, out)

# A charge seen in small steps -- Check now pressed on the cable -- is still one.
creep = hourly(60, 2, 4) + [(T0 + 4 * D + i * H, 52 + 2 * i) for i in range(1, 25)]
check("a charge seen two points at a time still counts", "charged_at" in outlook(creep), outlook(creep))

short = hourly(80, 2, 1)
check("a day of readings says nothing about how long is left", "days_left" not in outlook(short))
flat = [(T0 + h * H, 70) for h in range(24 * 5)]
check("a battery that is not falling says nothing either", "days_left" not in outlook(flat))
wobble = [(T0 + h * H, 70 + (h % 2)) for h in range(24 * 5)]
check("a one-point wobble is not a charge", "charged_at" not in outlook(wobble))
# The X4 and PaperMono read voltage and report in tens, so a reading sitting on
# a boundary flips between two of them.
tens = [(T0 + d * D, p) for d, p in enumerate([80, 80, 70, 70, 60, 70, 60, 60, 50, 50])]
check("a ten-point wobble on a voltage board is not a charge", "charged_at" not in outlook(tens), outlook(tens))
check("no readings, nothing to say", store.battery_outlook([], T0) == {})
check("a superscript digit is not a reading", store.parse_battery("\u00b2") is None)

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
