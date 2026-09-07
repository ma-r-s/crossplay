#!/usr/bin/env python3
"""The whole HTTP surface, end to end, against the fake Instapaper.

The fake runs as a real uvicorn process and the bridge runs in-process over
an ASGI transport. That split is deliberate: what has to be real is the leg
where OAuth signatures cross a socket, because the signature the bridge
builds is then verified by something that did not build it. The bridge's own
HTTP surface gains nothing from a second process and loses the session
cookie, which is Secure and so is never sent over a plaintext loopback.

Covers: sign-in and its refusals, CSRF on claim, the pairing handshake,
device auth and revocation, a first sync with downloads, a second sync that
delivers nothing because the delta worked, reading progress pushed up,
archive intents, a per-article failure that does not cost the sync, the
unrenderable verdict, and the fact that nothing here ever calls delete.

Run: .venv/bin/python tests/test_api.py
"""

import asyncio
import json
import os
import pathlib
import shutil
import socket
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(HERE))
from portguard import assert_alive, popen_group, reap, require_free_port  # noqa: E402

BASE_PORT = int(os.environ.get("BRIDGE_TEST_PORT", "8996"))
FAKE_PORT = BASE_PORT + 1
BRIDGE_PORT = BASE_PORT + 2

USER, PW = "mario@example.com", "reading-pw"
CONSUMER_KEY, CONSUMER_SECRET = "fake-consumer-key", "fake-consumer-secret"

checks = 0
failures = 0


def ok(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"  FAIL: {what}")


def wait_port(port, timeout=25):
    deadline = time.time() + timeout
    while time.time() < deadline:
        s = socket.socket()
        s.settimeout(0.5)
        try:
            s.connect(("127.0.0.1", port))
            s.close()
            return
        except OSError:
            time.sleep(0.2)
    raise RuntimeError(f"nothing opened port {port}")


PROSE = (
    "<p>" + ("The quick brown fox jumps over the lazy dog. " * 12) + "</p>"
    "<p>" + ("Sphinx of black quartz, judge my vow. " * 12) + "</p>"
)
CHINESE = "<p>" + ("我有一百块钱，还有一本很好的书。" * 20) + "</p>"


def fixture_state():
    return {
        "users": {USER: {"password": PW, "token": "tok-1", "secret": "sec-1"}},
        "bookmarks": [
            {
                "bookmark_id": 101,
                "url": "https://www.example.com/one",
                "title": "The first article",
                "description": "",
                "time": 1756000000,
                "progress": 0.0,
                "progress_timestamp": 0,
                "folder": "unread",
                "text": PROSE,
            },
            {
                "bookmark_id": 102,
                "url": "https://blog.example.org/two",
                "title": "It’s the second — really",
                "description": "",
                "time": 1756000100,
                "progress": 0.0,
                "progress_timestamp": 0,
                "folder": "unread",
                "text": PROSE,
            },
            {
                "bookmark_id": 103,
                "url": "https://example.net/broken",
                "title": "This one has no text",
                "description": "",
                "time": 1756000200,
                "folder": "unread",
                "text_fails": True,
            },
            {
                "bookmark_id": 104,
                "url": "https://example.cn/chinese",
                "title": "Written in Chinese",
                "description": "",
                "time": 1756000300,
                "folder": "unread",
                "text": CHINESE,
            },
        ],
    }


async def poll_job(client, headers, job, tries=80):
    for _ in range(tries):
        r = await client.get(f"/api/sync/status?job={job}", headers=headers)
        body = r.json()
        if body["status"] in ("done", "error"):
            return body
        await asyncio.sleep(0.25)
    return {"status": "timeout"}


async def run(tmp, state_file):
    import httpx
    from cryptography.fernet import Fernet

    from bridge.app import app

    fake = f"http://127.0.0.1:{FAKE_PORT}"
    transport = httpx.ASGITransport(app=app)
    async with (
        httpx.AsyncClient(
            transport=transport, base_url="https://bridge", timeout=60
        ) as client,
        httpx.AsyncClient(base_url=fake, timeout=30) as fakec,
    ):
        r = await client.get("/healthz")
        ok(r.status_code == 200 and r.json()["ok"], "healthz answers")

        # --- sign-in
        r = await client.post("/login", data={"username": USER, "password": "wrong"})
        ok("did not accept" in r.text, "a wrong password is refused in a sentence")
        r = await client.post(
            "/login", data={"username": "nobody@example.com", "password": "x"}
        )
        ok("invitation-only" in r.text, "an account outside the allowlist is refused")

        r = await client.post("/login", data={"username": USER, "password": PW})
        ok(r.status_code == 303, "a good password signs in")
        cookie = r.cookies.get("read_session")
        ok(bool(cookie), "a session cookie is set")
        session = json.loads(
            Fernet(os.environ["READ_FERNET_KEY"].encode()).decrypt(cookie.encode())
        )
        csrf = session["csrf"]

        r = await client.get("/devices")
        ok("No reader paired yet" in r.text, "the devices page starts empty")
        ok(
            "never deletes anything" in r.text,
            "the page says what the token can and cannot do",
        )

        # --- pairing
        r = await client.post("/api/pair/start")
        pair = r.json()
        ok(len(pair["code"]) == 8, "a pairing code is eight characters")
        ok(
            not set(pair["code"]) & set("01OI"),
            "the code alphabet has no ambiguous glyphs",
        )

        r = await client.post("/api/pair/claim", data={"code": pair["code"]})
        ok(r.status_code == 401, "claiming without the CSRF token is refused")

        r = await client.post(
            "/api/pair/claim", data={"code": pair["code"], "csrf": csrf}
        )
        ok(
            "confirm on the reader" in r.text,
            "a claimed code waits for the device to confirm",
        )

        r = await client.get(f"/api/pair/poll?pollToken={pair['pollToken']}")
        delivered = r.json()
        token = delivered["deviceToken"]
        ok(
            delivered["username"] == USER,
            "poll names the account, for the confirm screen",
        )
        headers = {"Authorization": f"Bearer {token}"}

        r = await client.get(f"/api/pair/poll?pollToken={pair['pollToken']}")
        ok(r.status_code == 410, "a pairing is one-shot")

        # --- device auth
        r = await client.post("/api/sync", json={"have": [], "archive": []})
        ok(r.status_code == 401, "syncing without a token is refused")
        r = await client.post(
            "/api/sync", json={"have": []}, headers={"Authorization": "Bearer nonsense"}
        )
        ok(
            r.status_code == 401 and "not paired" in r.json()["error"],
            "an unknown token gets a sentence",
        )

        # --- first sync
        r = await client.post(
            "/api/sync", json={"have": [], "archive": []}, headers=headers
        )
        ok(r.status_code == 200, "a paired device may sync")
        result = await poll_job(client, headers, r.json()["job"])
        ok(
            result["status"] == "done",
            f"the first sync finishes ({result.get('message', '')})",
        )
        summary = result.get("summary", {})
        articles = {a["id"]: a for a in summary.get("articles", [])}
        ok(
            set(articles) == {101, 102, 104},
            f"three articles delivered, not the broken one ({sorted(articles)})",
        )
        ok(
            [f["id"] for f in summary["failed"]] == [103],
            "the one with no text is named in failed",
        )
        ok(
            "could not produce" in summary["failed"][0]["why"],
            "the failure carries Instapaper's reason",
        )

        a101 = articles[101]
        ok(a101["domain"] == "example.com", "the row's subtitle is the domain")
        ok(a101["renderable"] is True, "an English article is renderable")
        ok(
            a101["words"] > 100 and a101["minutes"] >= 1,
            "word count and reading time are computed",
        )
        ok(
            len(a101["sha"]) == 16,
            "each article carries a content sha for the device to compare",
        )
        ok(
            articles[104]["renderable"] is False,
            "a Chinese article is marked unrenderable",
        )
        ok(
            articles[102]["title"] == "It's the second -- really",
            "titles are folded to what the cut draws",
        )

        # --- downloading
        r = await client.get(f"/api/article/101/{a101['hash']}", headers=headers)
        ok(r.status_code == 200, "an article downloads")
        ok(r.headers["content-type"].startswith("text/plain"), "as text/plain")
        ok(len(r.content) == a101["bytes"], "the manifest's byte count is the file's")
        ok(
            "quick brown fox" in r.text and "<p>" not in r.text,
            "the text arrived flattened",
        )

        r = await client.get("/api/article/101/..%2f..%2fstate", headers=headers)
        ok(r.status_code == 404, "a traversal in the hash reaches nothing")
        r = await client.get("/api/article/999/abc", headers=headers)
        ok(
            r.status_code == 404,
            "an article the bridge does not have is a 404 with a sentence",
        )

        # --- second sync: the delta must deliver nothing
        have = [
            {"id": a["id"], "hash": a["hash"], "progress": 0.0, "progressAt": 0}
            for a in articles.values()
        ]
        r = await client.post(
            "/api/sync", json={"have": have, "archive": []}, headers=headers
        )
        result = await poll_job(client, headers, r.json()["job"])
        ok(result["status"] == "done", "the second sync finishes")
        second = result["summary"]
        ok(second["articles"] == [], "nothing is re-sent when nothing changed")
        ok(second["deleteIds"] == [], "and nothing is falsely reported deleted")

        # --- reading progress goes up
        have[0]["progress"] = 0.42
        have[0]["progressAt"] = 1756100000
        r = await client.post(
            "/api/sync", json={"have": have, "archive": []}, headers=headers
        )
        result = await poll_job(client, headers, r.json()["job"])
        ok(result["status"] == "done", "the progress sync finishes")
        remote = (await fakec.get("/_test/state")).json()
        pushed = [b for b in remote["bookmarks"] if b["bookmark_id"] == have[0]["id"]][
            0
        ]
        ok(
            abs(float(pushed["progress"]) - 0.42) < 0.01,
            "the reader's progress reached Instapaper",
        )
        ok(
            pushed["progress_timestamp"] == 1756100000,
            "with the reader's own timestamp",
        )

        # A progress push changes the bookmark's hash, so it comes back down
        # -- and the bridge must NOT have re-fetched its text for that.
        back = {a["id"]: a for a in result["summary"]["articles"]}
        ok(have[0]["id"] in back, "the changed bookmark comes back with a new hash")
        ok(
            back[have[0]["id"]]["sha"] == a101["sha"],
            "its text is unchanged, so the sha is too",
        )

        # --- archiving
        r = await client.post(
            "/api/sync", json={"have": have, "archive": [102]}, headers=headers
        )
        result = await poll_job(client, headers, r.json()["job"])
        ok(
            result["summary"]["archived"] == [102],
            "the archive intent is confirmed back",
        )
        remote = (await fakec.get("/_test/state")).json()
        moved = [b for b in remote["bookmarks"] if b["bookmark_id"] == 102][0]
        ok(moved["folder"] == "archive", "and the article really moved on Instapaper")
        ok(
            102 in result["summary"]["deleteIds"],
            "an archived article is reported gone from unread",
        )

        # Archiving something already archived must not fail: the device's
        # queue is at-least-once by design.
        r = await client.post(
            "/api/sync", json={"have": [], "archive": [102, 999]}, headers=headers
        )
        result = await poll_job(client, headers, r.json()["job"])
        ok(result["status"] == "done", "a repeated archive does not break the sync")
        ok(
            sorted(result["summary"]["archived"]) == [102, 999],
            "a bookmark that is already gone counts as archived",
        )

        # --- an unknown job answers a sentence, not a 404
        r = await client.get("/api/sync/status?job=nope", headers=headers)
        ok(
            r.status_code == 200 and "restarted" in r.json()["message"],
            "a forgotten job is explained",
        )

        # --- events: what a finished sync tells the board, and what a board
        # that is down costs it (nothing). The HTTP layer is stubbed at the one
        # name events.py sends through, so these assert the exact body.
        import urllib.error
        import urllib.request

        import bridge.app as app_mod
        from bridge import engine as engine_mod
        from bridge import events
        from bridge import jobs as jobs_mod
        from bridge import store as store_mod
        from bridge.ratelimit import Window

        os.environ["SUPABASE_URL"] = "https://board.test"
        os.environ["SUPABASE_ANON_KEY"] = "anon-test-key"
        posted = []

        class Taken:
            def read(self):
                return b""

            def close(self):
                pass

        def take(req, timeout):
            posted.append(req)
            return Taken()

        def refuse(req, timeout):
            posted.append(req)
            raise urllib.error.URLError("connection refused")

        async def settle(n):
            # The post rides its own thread; give it a moment to land.
            for _ in range(50):
                if len(posted) >= n:
                    return
                await asyncio.sleep(0.1)

        events._urlopen = take
        # Each job reads the clock twice; pinned so `seconds` is asserted
        # exactly rather than as "some number".
        ticks = iter(
            [
                100.0,
                102.5,
                200.0,
                200.4,
                300.0,
                300.1,
                400.0,
                400.2,
                500.0,
                502.0,
                600.0,
                600.5,
            ]
        )
        jobs_mod._clock = lambda: next(ticks, time.monotonic())
        # The syncs above spent this user's window; this block gets a fresh
        # one with the same limits.
        app_mod.SYNC_USER = Window(6, 300)
        expected_device = events.device_id(store_mod.uid_for(USER))

        r = await client.post(
            "/api/sync", json={"have": [], "archive": []}, headers=headers
        )
        result = await poll_job(client, headers, r.json()["job"])
        ok(
            result["status"] == "done",
            f"the events sync finishes ({result.get('message', '')})",
        )
        came_down = len(result["summary"]["articles"])
        ok(came_down > 0, "and delivered something, so the count below is not a zero")
        await settle(1)
        ok(len(posted) == 1, f"a finished sync posts one event, got {len(posted)}")
        wire_body = json.loads(posted[0].data)
        ok(
            wire_body
            == {
                "service": "instapaper",
                "event": "sync",
                "level": "info",
                "device": expected_device,
                "props": {"articles": came_down, "seconds": 2.5},
            },
            f"a sync posts the contract's body, got {wire_body}",
        )
        ok(
            posted[0].full_url == "https://board.test/rest/v1/events",
            "to the events table",
        )
        ok(posted[0].get_header("Apikey") == "anon-test-key", "with the public key")
        ok(
            USER not in posted[0].data.decode()
            and store_mod.uid_for(USER) not in posted[0].data.decode(),
            "neither the address nor the account id is in the event",
        )

        # A board that refuses the event cannot fail the sync.
        events._urlopen = refuse
        r = await client.post(
            "/api/sync", json={"have": [], "archive": []}, headers=headers
        )
        result = await poll_job(client, headers, r.json()["job"])
        ok(
            result["status"] == "done" and "articles" in result["summary"],
            f"a board that is down does not fail the sync, got {result['status']}",
        )
        await settle(2)
        ok(len(posted) == 2, "and the event was attempted, not skipped")

        # A sync that dies posts what it died of: an Instapaper refusal ...
        events._urlopen = take
        real_cycle = engine_mod.sync_cycle

        def refused(*a, **kw):
            raise jobs_mod.Refused("Instapaper refused: 1041 for bookmark 77")

        engine_mod.sync_cycle = refused
        try:
            r = await client.post(
                "/api/sync", json={"have": [], "archive": []}, headers=headers
            )
            result = await poll_job(client, headers, r.json()["job"])
        finally:
            engine_mod.sync_cycle = real_cycle
        ok(result["status"] == "error", f"the refused sync fails, got {result}")
        await settle(3)
        wire_body = json.loads(posted[2].data)
        ok(
            wire_body
            == {
                "service": "instapaper",
                "event": "sync",
                "level": "error",
                "device": expected_device,
                "props": {
                    "message": "Refused: Instapaper refused: 1041 for bookmark 77"
                },
            },
            f"a refusal posts its sentence at level error, got {wire_body}",
        )

        # ... and a bridge fault.
        def dead(*a, **kw):
            raise RuntimeError("disk full")

        engine_mod.sync_cycle = dead
        try:
            r = await client.post(
                "/api/sync", json={"have": [], "archive": []}, headers=headers
            )
            result = await poll_job(client, headers, r.json()["job"])
        finally:
            engine_mod.sync_cycle = real_cycle
        ok(result["status"] == "error", f"the broken sync fails, got {result}")
        await settle(4)
        wire_body = json.loads(posted[3].data)
        ok(
            wire_body["level"] == "error"
            and wire_body["props"] == {"message": "RuntimeError: disk full"},
            f"a bridge fault posts its cause at level error, got {wire_body}",
        )

        # --- the device headers: a reader that says who it is, and what it
        # has to report, on the request it was making anyway. The sync event
        # is counted under the header's id, not the account hash, and carries
        # the health numbers; the crash and the update post as firmware
        # events of their own, from the middleware, whatever the endpoint.
        del posted[:]
        app_mod.SYNC_USER = Window(6, 300)
        DEV = "0" * 64
        report = {
            "battery_pct": 50,
            "heap_min_kb": 100,
            "uptime_h": 1,
            "crash": {
                "message": "assert failed: x (reset: panic)",
                "version": "1.12.12",
                "backtrace": "",
            },
            "ota": {
                "attempted": True,
                "ok": False,
                "error": "too_large",
                "path": "ota",
            },
        }
        talking = {
            **headers,
            "User-Agent": "CrossPlay-ESP32-1.12.13",
            "X-CrossPlay-Device": DEV,
            "X-CrossPlay-Board": "x4pro",
            "X-CrossPlay-Report": json.dumps(report, separators=(",", ":")),
        }
        r = await client.post(
            "/api/sync", json={"have": [], "archive": []}, headers=talking
        )
        ok(
            r.status_code == 200,
            f"a sync with the device headers is accepted, got {r.status_code}",
        )
        result = await poll_job(client, headers, r.json()["job"])
        ok(result["status"] == "done", f"and finishes, got {result}")
        came_down = len(result["summary"]["articles"])
        await settle(3)
        ok(
            len(posted) == 3,
            f"the sync, the crash and the update are three events, got {len(posted)}",
        )
        bodies = sorted(
            (json.loads(p.data) for p in posted),
            key=lambda b: (b["service"], b["event"]),
        )
        ok(
            bodies[0]
            == {
                "service": "firmware",
                "event": "crash",
                "level": "error",
                "device": DEV,
                "version": "1.12.12",
                "board": "x4pro",
                "props": {
                    "message": "assert failed: x (reset: panic)",
                    "backtrace": "",
                    "app": "firmware",
                    "via": "instapaper",
                },
            },
            f"the crash posts as the firmware's, via instapaper, got {bodies[0]}",
        )
        ok(
            bodies[1]
            == {
                "service": "firmware",
                "event": "update",
                "level": "error",
                "device": DEV,
                "version": "1.12.13",
                "board": "x4pro",
                "props": {
                    "attempted": True,
                    "ok": False,
                    "error": "too_large",
                    "path": "ota",
                    "app": "firmware",
                    "message": "update failed: too_large (ota)",
                },
            },
            f"the failed update posts as an error with its message, got {bodies[1]}",
        )
        ok(
            bodies[2]
            == {
                "service": "instapaper",
                "event": "sync",
                "level": "info",
                "device": DEV,
                "version": "1.12.13",
                "board": "x4pro",
                "props": {
                    "articles": came_down,
                    "seconds": 2.0,
                    "battery_pct": 50,
                    "heap_min_kb": 100,
                    "uptime_h": 1,
                },
            },
            f"the sync is counted under the device's own id with its health, got {bodies[2]}",
        )
        ok(
            expected_device not in "".join(p.data.decode() for p in posted),
            "the account hash is not used when the device names itself",
        )

        # A report past the cap is not a report; the request is still served
        # and still counted, without health, and nothing else posts.
        del posted[:]
        oversize = dict(talking)
        oversize["X-CrossPlay-Report"] = json.dumps(
            {"battery_pct": 50, "crash": {"message": "x" * 1180}}
        )
        ok(
            len(oversize["X-CrossPlay-Report"]) >= 1200,
            "the oversize report is at least 1200 bytes",
        )
        r = await client.post(
            "/api/sync", json={"have": [], "archive": []}, headers=oversize
        )
        ok(
            r.status_code == 200,
            f"an oversize report does not fail the request, got {r.status_code}",
        )
        result = await poll_job(client, headers, r.json()["job"])
        ok(result["status"] == "done", f"and the sync finishes, got {result}")
        await settle(1)
        await asyncio.sleep(0.3)
        ok(len(posted) == 1, f"only the sync posts, got {len(posted)}")
        wire_body = json.loads(posted[0].data)
        ok(
            wire_body["device"] == DEV
            and wire_body["board"] == "x4pro"
            and wire_body["props"]
            == {"articles": len(result["summary"]["articles"]), "seconds": 0.5},
            f"counted under the id, with no health from the ignored report, got {wire_body}",
        )

        # A crash on a request the service refuses is not posted: the device
        # will carry it again, and posting it now would count it twice.
        del posted[:]
        r = await client.post(
            "/api/sync",
            json={"have": []},
            headers={k: v for k, v in talking.items() if k != "Authorization"},
        )
        ok(r.status_code == 401, "no token is still refused, headers or not")
        await asyncio.sleep(0.3)
        ok(posted == [], f"and a refused request posts nothing, got {len(posted)}")

        events._urlopen = urllib.request.urlopen
        jobs_mod._clock = time.monotonic
        del os.environ["SUPABASE_URL"], os.environ["SUPABASE_ANON_KEY"]

        # --- revocation
        page = await client.get("/devices")
        token_hash = page.text.split("name=token_hash value='")[1].split("'")[0]
        r = await client.post(
            "/devices/revoke", data={"csrf": csrf, "token_hash": token_hash}
        )
        ok(r.status_code == 303, "a device can be unpaired from the page")
        r = await client.post("/api/sync", json={"have": []}, headers=headers)
        ok(
            r.status_code == 401 and "not paired anymore" in r.json()["error"],
            "a revoked token is refused with the sentence that tells the device to re-pair",
        )

        # --- the one thing this service must never do
        remote = (await fakec.get("/_test/state")).json()
        ok(
            "delete_was_called" not in remote,
            "the bridge never called bookmarks/delete",
        )

        # --- the sign-in backoff, on the real HTTP surface.
        #
        # Last, because it deliberately locks the account it uses. The design
        # made per-username exponential lockout a precondition for opening
        # registration to everyone, so this is that condition being exercised
        # rather than merely existing: enough wrong passwords and the CORRECT
        # one is refused too, which is the only version that stops an attacker
        # who happens to guess right.
        from bridge.ratelimit import Lockout

        # From a FRESH visitor IP. The per-IP window is 5 per 5 minutes and this
        # suite has already spent that budget on the sign-in checks above, so
        # without this the IP limiter answers first and the username lockout is
        # never reached -- the test would pass on the wrong mechanism. The
        # header is the real one: client_ip() reads cf-connecting-ip, because
        # behind the tunnel the socket peer is always cloudflared.
        visitor = {"CF-Connecting-IP": "203.0.113.7"}
        for _ in range(Lockout.FREE_FAILURES + 1):
            await client.post(
                "/login", data={"username": USER, "password": "wrong"}, headers=visitor
            )
        r = await client.post(
            "/login", data={"username": USER, "password": PW}, headers=visitor
        )
        ok(
            "Slow down" in r.text,
            "a locked-out account is refused even with the right password",
        )
        ok("Try again in" in r.text, "and is told roughly how long to wait")

    print(f"{checks} checks, {failures} failed")
    return 1 if failures else 0


def main():
    tmp = tempfile.mkdtemp(prefix="readbridge-test-")
    state_file = pathlib.Path(tmp) / "fake.json"
    state_file.write_text(json.dumps(fixture_state()))

    from cryptography.fernet import Fernet

    env = dict(os.environ)
    env.update(
        {
            "FAKE_INSTAPAPER_STATE": str(state_file),
            "FAKE_CONSUMER_KEY": CONSUMER_KEY,
            "FAKE_CONSUMER_SECRET": CONSUMER_SECRET,
            "READ_DATA": str(pathlib.Path(tmp) / "data"),
            "READ_FERNET_KEY": Fernet.generate_key().decode(),
            "READ_ALLOWLIST": USER,
            "READ_CONSUMER_KEY": CONSUMER_KEY,
            "READ_CONSUMER_SECRET": CONSUMER_SECRET,
            "READ_INSTAPAPER_BASE": f"http://127.0.0.1:{FAKE_PORT}",
            "PYTHONPATH": str(ROOT),
        }
    )
    # The bridge is imported AFTER this, because instapaper.BASE is read from
    # the environment at import time and a stale value would send the suite at
    # the real Instapaper.
    os.environ.update({k: v for k, v in env.items() if k.startswith("READ_")})

    procs = []
    try:
        require_free_port(FAKE_PORT, "the fake Instapaper")
        procs.append(
            popen_group(
                [
                    sys.executable,
                    "-m",
                    "uvicorn",
                    "tests.fake_instapaper:app",
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(FAKE_PORT),
                    "--log-level",
                    "warning",
                ],
                cwd=ROOT,
                env=env,
            )
        )
        wait_port(FAKE_PORT)
        assert_alive(procs[-1], "the fake Instapaper")
        return asyncio.run(run(tmp, state_file))
    finally:
        for p in procs:
            reap(p)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
