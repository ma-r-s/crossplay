#!/usr/bin/env python3
"""The whole HTTP surface, end to end, with the local sync server as AnkiWeb.

Covers the paths the critic named: CSRF on claim, pairing expiry semantics,
device auth + revocation, the binary sync POST with ack offsets, the job
lifecycle, and a deck build downloaded by hash-checked file.

Run: .venv/bin/python tests/test_api.py
"""

import asyncio
import json
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
REPO = ROOT.parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(REPO / "tools_local" / "study"))

PORT = int(os.environ.get("BRIDGE_TEST_PORT", "8996")) + 0
USER, PW = "mario", "api-pw"
ENDPOINT = f"http://127.0.0.1:{PORT}/"

checks = 0
failures = 0


def ok(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"  FAIL: {what}")


def wait_port(port, timeout=20):
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
    raise RuntimeError("sync server never opened its port")


async def run(tmp):
    import httpx
    from cryptography.fernet import Fernet

    import deck_to_anki as d2a
    from bridge import decks as decks_mod
    from bridge.app import app

    decks_mod.TOOLS = REPO / "tools_local" / "study"

    from anki.collection import Collection

    # Desktop with two cards in the Default deck, pushed to "AnkiWeb".
    desktop = Collection(str(tmp / "desktop.anki2"))
    auth = desktop.sync_login(USER, PW, ENDPOINT)
    nt = desktop.models.by_name("Basic")
    for front in ("uno", "dos"):
        note = desktop.new_note(nt)
        note["Front"], note["Back"] = front, front.upper()
        desktop.add_note(note, desktop.decks.get_current_id())
    out = desktop.sync_collection(auth, sync_media=False)
    if out.required:
        desktop.full_upload_or_download(
            auth=auth, server_usn=out.server_media_usn, upload=True
        )

    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="https://bridge") as web:
        # --- Login (allowlist enforced elsewhere; here mario is allowed).
        r = await web.post("/login", data={"username": USER, "password": "wrong"})
        ok("did not accept" in r.text, "wrong password should be refused politely")
        r = await web.post("/login", data={"username": USER, "password": PW})
        ok(r.status_code == 303, f"login should redirect, got {r.status_code}")
        cookie = r.cookies.get("bridge_session")
        ok(bool(cookie), "login should set the session cookie")
        session = json.loads(
            Fernet(os.environ["BRIDGE_FERNET_KEY"].encode()).decrypt(cookie.encode())
        )

        # --- Pairing: start (device), claim (browser), poll (device).
        r = await web.post("/api/pair/start")
        pair = r.json()
        ok(len(pair["code"]) == 8, "pair code should be 8 chars")

        r = await web.post("/api/pair/claim", data={"code": pair["code"]})
        ok(r.status_code == 401, "claim without CSRF must be refused")

        r = await web.post(
            "/api/pair/claim",
            data={"code": pair["code"], "csrf": session["csrf"]},
        )
        ok(
            r.status_code == 200
            and "confirm on the reader" in r.text.lower()
            or "Almost" in r.text,
            "claim with CSRF should succeed",
        )

        r = await web.get("/api/pair/poll", params={"pollToken": pair["pollToken"]})
        polled = r.json()
        ok(polled["pending"] is False, "poll after claim should deliver")
        ok(polled["username"] == USER, "poll must name the claiming account")
        token = polled["deviceToken"]

        r = await web.get("/api/pair/poll", params={"pollToken": pair["pollToken"]})
        ok(r.status_code == 410, "a delivered pairing must be consumed")

        # --- Abandon: a code the device walked away from must stop being
        # claimable, and a declined registration must stop authorizing.
        r = await web.post("/api/pair/start")
        dead = r.json()
        r = await web.post("/api/pair/abandon", json={"pollToken": dead["pollToken"]})
        ok(r.json().get("ok") is True, "abandon should answer ok")
        r = await web.post(
            "/api/pair/claim",
            data={"code": dead["code"], "csrf": session["csrf"]},
        )
        ok("unknown or expired" in r.text, "an abandoned code must not be claimable")

        r = await web.post("/api/pair/start")
        ghost = r.json()
        await web.post(
            "/api/pair/claim", data={"code": ghost["code"], "csrf": session["csrf"]}
        )
        r = await web.get("/api/pair/poll", params={"pollToken": ghost["pollToken"]})
        ghost_token = r.json()["deviceToken"]
        r = await web.post("/api/pair/abandon", json={"deviceToken": ghost_token})
        ok(r.json().get("ok") is True, "decline should answer ok")
        r = await web.get(
            "/api/decks", headers={"Authorization": f"Bearer {ghost_token}"}
        )
        ok(r.status_code == 401, "a declined token must stop authorizing")

        dev = {"Authorization": f"Bearer {token}"}
        r = await web.get("/api/decks")
        ok(r.status_code == 401, "device endpoints must refuse without a token")

        # --- First sync: no reviews, bootstraps the mirror and builds decks.
        r = await web.post(
            "/api/decks/choose", headers=dev, json={"decks": ["Default"]}
        )
        ok(r.json()["chosen"] == ["Default"], "deck choice should persist")

        empty_header = json.dumps({"decks": []}).encode()
        body = struct.pack("<I", len(empty_header)) + empty_header
        r = await web.post("/api/sync", headers=dev, content=body)
        job = r.json()["job"]
        for _ in range(600):
            await asyncio.sleep(0.1)
            r = await web.get("/api/sync/status", headers=dev, params={"job": job})
            if r.json()["status"] in ("done", "error", "frozen"):
                break
        status = r.json()
        ok(status["status"] == "done", f"first sync should finish, got {status}")
        manifest = status["summary"]["manifests"][0]
        ok("deck.dat" in manifest["files"], "deck build should contain deck.dat")

        # --- Device reviews card uno; posts the binary tail + cards.dat.
        from bridge import store as store_mod

        st = store_mod.UserStore(store_mod.uid_for(USER))
        mirror = Collection(str(st.collection_path))
        card_id = mirror.db.all("select id from cards order by id")[0][0]
        mirror.close()

        ms = int((time.time() - 600) * 1000)
        revlog = struct.pack(d2a.REVLOG_RECORD, card_id, ms, 3, 0, 0, 1, 0, 0)
        cards = (
            struct.pack("<qffiiHH", card_id, 3.3, 5.0, 40, 39, 1, 0)
            + bytes([2, 0])
            + struct.pack("<H", 0)
        )
        header = json.dumps(
            {
                "decks": [
                    {
                        "slug": "default",
                        "revlogOffset": 0,
                        "revlogLen": len(revlog),
                        "cardsLen": len(cards),
                    }
                ]
            }
        ).encode()
        body = struct.pack("<I", len(header)) + header + revlog + cards

        r = await web.post("/api/sync", headers=dev, content=body)
        acked = r.json()
        ok(
            acked["ackOffsets"]["default"] == len(revlog),
            "ack must advance by the tail length",
        )
        job = acked["job"]
        for _ in range(600):
            await asyncio.sleep(0.1)
            r = await web.get("/api/sync/status", headers=dev, params={"job": job})
            if r.json()["status"] in ("done", "error", "frozen"):
                break
        status = r.json()
        ok(status["status"] == "done", f"review sync should finish, got {status}")
        ok(status["summary"]["applied"] == 1, "the review should apply")

        # --- The deck file is downloadable and byte-identical to its hash.
        manifest = status["summary"]["manifests"][0]
        r = await web.get(
            f"/api/deck/{manifest['slug']}/{manifest['buildId']}/deck.dat", headers=dev
        )
        import hashlib

        ok(
            hashlib.sha256(r.content).hexdigest()
            == manifest["files"]["deck.dat"]["sha256"],
            "downloaded deck.dat must match its manifest hash",
        )
        r = await web.get(
            f"/api/deck/{manifest['slug']}/{manifest['buildId']}/../../../state.json",
            headers=dev,
        )
        ok(r.status_code == 404, "path traversal out of the build dir must 404")

        # --- Desktop pulls and sees the device review.
        out = desktop.sync_collection(auth, sync_media=False)
        rows = desktop.db.all("select id from revlog where cid = ?", card_id)
        ok(any(r0[0] == ms for r0 in rows), "desktop should see the device review")

        # --- A card pulled into a filtered deck still belongs to its home deck.
        did_home = desktop.decks.id("Filtered Source")
        n_f = desktop.new_note(nt)
        n_f["Front"], n_f["Back"] = "borrowed", "BORROWED"
        desktop.add_note(n_f, did_home)
        dyn = desktop.decks.new_filtered("Custom Study Session")
        desktop.sched.rebuild_filtered_deck(dyn)
        moved = desktop.db.scalar("select count(*) from cards where odid != 0")
        ok(moved >= 1, f"the filtered deck should have borrowed a card, got {moved}")
        desktop.sync_collection(auth, sync_media=False)
        r = await web.post("/api/sync", headers=dev, content=struct.pack("<I", len(empty_header)) + empty_header)
        jf = r.json()["job"]
        for _ in range(600):
            await asyncio.sleep(0.1)
            if (await web.get("/api/sync/status", headers=dev, params={"job": jf})).json()["status"] in (
                "done",
                "error",
                "frozen",
            ):
                break
        seen = {d["name"]: d["cards"] for d in (await web.get("/api/decks", headers=dev)).json()["decks"]}
        ok(
            "Custom Study Session" not in seen,
            f"a filtered deck must not be offered as a choice, got {sorted(seen)}",
        )
        ok(
            seen.get("Filtered Source") == 1,
            f"a borrowed card still counts for its home deck, got {seen.get('Filtered Source')}",
        )

        # --- A review whose card is gone is counted, not silently dropped.
        gone = struct.pack(d2a.REVLOG_RECORD, 999999999999, ms + 1, 3, 0, 0, 1, 0, 0)
        header = json.dumps(
            {
                "decks": [
                    {
                        "slug": "default",
                        "revlogOffset": 0,
                        "revlogLen": len(gone),
                        "cardsLen": 0,
                    }
                ]
            }
        ).encode()
        r = await web.post(
            "/api/sync",
            headers=dev,
            content=struct.pack("<I", len(header)) + header + gone,
        )
        jm = r.json()["job"]
        for _ in range(600):
            await asyncio.sleep(0.1)
            st_m = (await web.get("/api/sync/status", headers=dev, params={"job": jm})).json()
            if st_m["status"] in ("done", "error", "frozen"):
                break
        ok(
            st_m["summary"].get("missing") == 1,
            f"a review for an absent card must be counted missing, got {st_m['summary'].get('missing')}",
        )

        # --- A parent deck reports its subdecks' cards, or the reader hides it.
        parent = desktop.decks.id("Shared::Level 1")
        note = desktop.new_note(nt)
        note["Front"], note["Back"] = "sub", "SUB"
        desktop.add_note(note, parent)
        desktop.sync_collection(auth, sync_media=False)
        r = await web.post("/api/sync", headers=dev, content=struct.pack("<I", len(empty_header)) + empty_header)
        j2 = r.json()["job"]
        for _ in range(600):
            await asyncio.sleep(0.1)
            if (await web.get("/api/sync/status", headers=dev, params={"job": j2})).json()["status"] in (
                "done",
                "error",
                "frozen",
            ):
                break
        listed = {d["name"]: d["cards"] for d in (await web.get("/api/decks", headers=dev)).json()["decks"]}
        ok("Shared" in listed, f"the parent deck should be listed, got {sorted(listed)}")
        ok(
            listed.get("Shared") == 1,
            f"a parent's count must include its subdecks, got {listed.get('Shared')}",
        )

        # --- A deck the converter refuses costs only itself.
        desktop.decks.id("Empty Parent")  # created with no cards of its own
        desktop.sync_collection(auth, sync_media=False)
        r = await web.post(
            "/api/decks/choose", headers=dev, json={"decks": ["Default", "Empty Parent"]}
        )
        ok(r.status_code == 200, "choosing an unbuildable deck should be accepted")
        body = struct.pack("<I", len(empty_header)) + empty_header
        r = await web.post("/api/sync", headers=dev, content=body)
        job = r.json()["job"]
        for _ in range(600):
            await asyncio.sleep(0.1)
            r = await web.get("/api/sync/status", headers=dev, params={"job": job})
            if r.json()["status"] in ("done", "error", "frozen"):
                break
        status = r.json()
        ok(
            status["status"] == "done",
            f"one unbuildable deck must not fail the sync, got {status}",
        )
        ok(
            status["summary"]["failedDecks"] == ["Empty Parent"],
            f"the failed deck should be named, got {status['summary'].get('failedDecks')}",
        )
        ok(
            any(m["deck"] == "Default" for m in status["summary"]["manifests"]),
            "the buildable deck should still be built",
        )

        # --- Events: what a finished sync tells the board, and what a board
        # that is down costs it (nothing). The HTTP layer is stubbed at the one
        # name events.py sends through, so these assert the exact body.
        import urllib.error
        import urllib.request

        import bridge.app as app_mod
        from bridge import engine as engine_mod
        from bridge import events
        from bridge import jobs as jobs_mod
        from bridge import pairing as pairing_mod
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

        async def finish(job_id):
            for _ in range(600):
                await asyncio.sleep(0.1)
                r = await web.get(
                    "/api/sync/status", headers=dev, params={"job": job_id}
                )
                if r.json()["status"] in ("done", "error", "frozen"):
                    break
            return r.json()

        events._urlopen = take
        # Each job reads the clock twice; pinned so `seconds` is asserted
        # exactly rather than as "some number".
        ticks = iter([100.0, 102.5, 200.0, 200.4, 300.0, 300.1, 400.0, 400.2])
        jobs_mod._clock = lambda: next(ticks, time.monotonic())
        # The syncs above spent this user's window; this block gets a fresh
        # one with the same limits.
        app_mod.SYNC_USER = Window(6, 300)
        expected_device = events.device_id(pairing_mod.token_hash(token))

        r = await web.post(
            "/api/sync",
            headers=dev,
            content=struct.pack("<I", len(empty_header)) + empty_header,
        )
        status = await finish(r.json()["job"])
        ok(status["status"] == "done", f"the events sync should finish, got {status}")
        await settle(1)
        ok(len(posted) == 1, f"a finished sync posts one event, got {len(posted)}")
        wire_body = json.loads(posted[0].data)
        ok(
            wire_body
            == {
                "service": "anki",
                "event": "sync",
                "level": "info",
                "device": expected_device,
                "props": {"cards": 0, "reviews": 0, "seconds": 2.5},
            },
            f"a sync posts the contract's body, got {wire_body}",
        )
        ok(
            posted[0].full_url == "https://board.test/rest/v1/events",
            "to the events table",
        )
        ok(posted[0].get_header("Apikey") == "anon-test-key", "with the public key")
        ok(
            token not in posted[0].data.decode()
            and pairing_mod.token_hash(token) not in posted[0].data.decode(),
            "neither the token nor its hash is in the event",
        )

        # A board that refuses the event cannot fail the sync.
        events._urlopen = refuse
        r = await web.post(
            "/api/sync",
            headers=dev,
            content=struct.pack("<I", len(empty_header)) + empty_header,
        )
        status = await finish(r.json()["job"])
        ok(
            status["status"] == "done" and status["summary"]["manifests"],
            f"a board that is down does not fail the sync, got {status['status']}",
        )
        await settle(2)
        ok(len(posted) == 2, "and the event was attempted, not skipped")

        # A sync that dies posts what it died of: the generic branch ...
        events._urlopen = take
        real_cycle = engine_mod.sync_cycle

        def dead(*a, **kw):
            raise RuntimeError("AnkiWeb answered 503 for user 42")

        engine_mod.sync_cycle = dead
        try:
            r = await web.post(
                "/api/sync",
                headers=dev,
                content=struct.pack("<I", len(empty_header)) + empty_header,
            )
            status = await finish(r.json()["job"])
        finally:
            engine_mod.sync_cycle = real_cycle
        ok(status["status"] == "error", f"the broken sync should fail, got {status}")
        await settle(3)
        wire_body = json.loads(posted[2].data)
        ok(
            wire_body
            == {
                "service": "anki",
                "event": "sync",
                "level": "error",
                "device": expected_device,
                "props": {"message": "RuntimeError: AnkiWeb answered 503 for user 42"},
            },
            f"a failure posts its message at level error, got {wire_body}",
        )

        # ... and the frozen one, whose message is the sentence the reader shows.
        def frozen(*a, **kw):
            raise engine_mod.Frozen("AnkiWeb wants an upload.")

        engine_mod.sync_cycle = frozen
        try:
            r = await web.post(
                "/api/sync",
                headers=dev,
                content=struct.pack("<I", len(empty_header)) + empty_header,
            )
            status = await finish(r.json()["job"])
        finally:
            engine_mod.sync_cycle = real_cycle
        ok(status["status"] == "frozen", f"the frozen sync should freeze, got {status}")
        await settle(4)
        wire_body = json.loads(posted[3].data)
        ok(
            wire_body["level"] == "error"
            and wire_body["props"] == {"message": "Frozen: AnkiWeb wants an upload."},
            f"a frozen sync posts its sentence at level error, got {wire_body}",
        )

        events._urlopen = urllib.request.urlopen
        jobs_mod._clock = time.monotonic
        del os.environ["SUPABASE_URL"], os.environ["SUPABASE_ANON_KEY"]

        # --- Revocation kills the token.
        th = __import__("bridge.pairing", fromlist=["token_hash"]).token_hash(token)
        store_mod.revoke_device(st.uid, th)
        r = await web.get("/api/decks", headers=dev)
        ok(r.status_code == 401, "a revoked token must be refused")

    desktop.close()


def main():
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="bridge-api-"))
    os.environ["BRIDGE_DATA"] = str(tmp / "data")
    from cryptography.fernet import Fernet

    os.environ["BRIDGE_FERNET_KEY"] = Fernet.generate_key().decode()
    os.environ["BRIDGE_ALLOWLIST"] = USER
    os.environ["BRIDGE_ANKIWEB_ENDPOINT"] = ENDPOINT
    server = None
    try:
        env = dict(
            os.environ,
            SYNC_USER1=f"{USER}:{PW}",
            SYNC_BASE=str(tmp / "server"),
            SYNC_HOST="127.0.0.1",
            SYNC_PORT=str(PORT),
        )
        server = subprocess.Popen(
            [sys.executable, "-m", "anki.syncserver"],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        wait_port(PORT)
        asyncio.run(run(tmp))
        print(f"{checks} checks, {failures} failed")
        sys.exit(1 if failures else 0)
    finally:
        if server:
            server.terminate()
            server.wait(timeout=10)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
