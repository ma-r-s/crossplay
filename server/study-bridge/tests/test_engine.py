#!/usr/bin/env python3
"""The sync engine against Anki's own self-hosted server, end to end.

Proves the three properties the plan demands:
 1. a cycle whose only change is a device review pushes it upstream
    (a fresh desktop pull sees it);
 2. the journal drops reviews only after a confirmed push;
 3. a forced full download replaces the mirror and the journal re-applies
    everything not yet pushed -- no review is ever lost to a full sync.

Run: .venv/bin/python tests/test_engine.py
"""

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
sys.path.insert(0, str(HERE))

import deck_to_anki as d2a  # noqa: E402
from bridge import engine, journal as journal_mod, store as store_mod, wire  # noqa: E402
from portguard import assert_alive, popen_group, reap, require_free_port  # noqa: E402

PORT = int(os.environ.get("BRIDGE_TEST_PORT", "8996")) + 1
USER, PW = "mario", "engine-pw"
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
    raise RuntimeError(f"sync server never opened port {port}")


def revlog_bytes(card_id, at_ms, rating=3, state=0, elapsed=0, interval=1, flags=0):
    return struct.pack(
        d2a.REVLOG_RECORD, card_id, at_ms, rating, state, elapsed, interval, 0, flags
    )


def cards_bytes(entries):
    out = b""
    for e in entries:
        out += struct.pack(
            "<qffiiHH",
            e["id"],
            e["stability"],
            e["difficulty"],
            e["due"],
            e["last"],
            e["reps"],
            e["lapses"],
        )
        out += bytes([e["state"], 0]) + struct.pack("<H", 0)
    return out


def main():
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="bridge-engine-"))
    os.environ["BRIDGE_DATA"] = str(tmp / "data")
    os.environ["BRIDGE_FERNET_KEY"] = (
        "x" * 0
        or __import__("cryptography.fernet", fromlist=["Fernet"])
        .Fernet.generate_key()
        .decode()
    )
    server = None
    try:
        env = dict(
            os.environ,
            SYNC_USER1=f"{USER}:{PW}",
            SYNC_BASE=str(tmp / "server"),
            SYNC_HOST="127.0.0.1",
            SYNC_PORT=str(PORT),
        )
        require_free_port(PORT, "the sync server")
        server = popen_group(
            [sys.executable, "-m", "anki.syncserver"],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        wait_port(PORT)
        assert_alive(server, "the sync server")

        from anki.collection import Collection

        # --- "Desktop": two cards, pushed to the server.
        desktop = Collection(str(tmp / "desktop.anki2"))
        auth = desktop.sync_login(USER, PW, ENDPOINT)
        hostkey = auth.hkey
        nt = desktop.models.by_name("Basic")
        for front in ("alpha", "beta"):
            note = desktop.new_note(nt)
            note["Front"], note["Back"] = front, front.upper()
            desktop.add_note(note, desktop.decks.get_current_id())
        out = desktop.sync_collection(auth, sync_media=False)
        if out.required:
            desktop.full_upload_or_download(
                auth=auth, server_usn=out.server_media_usn, upload=True
            )

        # --- Bridge user: bootstrap cycle (empty mirror -> full download).
        st = store_mod.UserStore(store_mod.uid_for(USER)).ensure()
        jr = journal_mod.Journal(st.journal_path)
        summary = engine.sync_cycle(st, jr, hostkey, ENDPOINT, {})
        ok(summary["pulled"], "bootstrap cycle should full-download the mirror")

        mirror = Collection(str(st.collection_path))
        card_ids = [r[0] for r in mirror.db.all("select id from cards order by id")]
        mirror.close()
        ok(len(card_ids) == 2, f"mirror should hold 2 cards, has {len(card_ids)}")

        # --- Device reviews card 0 an hour ago; posts binary tail + cards.
        ms1 = int((time.time() - 3600) * 1000)
        reviews = wire.parse_revlog(revlog_bytes(card_ids[0], ms1))
        dev_cards = wire.parse_cards(
            cards_bytes(
                [
                    dict(
                        id=card_ids[0],
                        stability=2.5,
                        difficulty=5.0,
                        due=100,
                        last=99,
                        reps=1,
                        lapses=0,
                        state=2,
                    ),
                ]
            )
        )
        ok(jr.ingest(reviews) == 1, "one new review should ingest")
        ok(jr.ingest(reviews) == 0, "re-posting the same tail must be a no-op")

        summary = engine.sync_cycle(st, jr, hostkey, ENDPOINT, dev_cards)
        ok(
            summary["applied"] == 1,
            f"cycle should apply 1 review, applied {summary['applied']}",
        )
        ok(jr.counts()["held"] == 0, "journal must be empty after a confirmed push")

        # --- Desktop pulls; the review must be there with a real usn.
        out = desktop.sync_collection(auth, sync_media=False)
        ok(
            out.required == 0,
            f"desktop pull should be a normal sync, required={out.required}",
        )
        rows = desktop.db.all(
            "select id, cid, usn from revlog where cid = ?", card_ids[0]
        )
        ok(
            any(r[0] == ms1 for r in rows),
            "device review missing from desktop after pull",
        )
        ok(all(r[2] != -1 for r in rows), "review reached desktop with usn=-1")

        # --- Forced full download: desktop full-uploads a changed collection
        # while the bridge holds an unpushed review. Nothing may be lost.
        note = desktop.new_note(nt)
        note["Front"], note["Back"] = "gamma", "GAMMA"
        desktop.add_note(note, desktop.decks.get_current_id())
        desktop.full_upload_or_download(auth=auth, server_usn=None, upload=True)

        ms2 = int((time.time() - 1800) * 1000)
        jr.ingest(wire.parse_revlog(revlog_bytes(card_ids[1], ms2)))
        dev_cards2 = wire.parse_cards(
            cards_bytes(
                [
                    dict(
                        id=card_ids[1],
                        stability=1.1,
                        difficulty=6.0,
                        due=101,
                        last=100,
                        reps=1,
                        lapses=0,
                        state=2,
                    ),
                ]
            )
        )
        summary = engine.sync_cycle(st, jr, hostkey, ENDPOINT, dev_cards2)
        ok(summary["pulled"], "forced full sync should be answered with a download")
        ok(
            summary["applied"] == 1,
            "the journaled review must be re-applied to the new mirror",
        )
        ok(jr.counts()["held"] == 0, "journal empty again after push")

        out = desktop.sync_collection(auth, sync_media=False)
        if out.required:  # desktop must adopt the merged server state
            desktop.full_upload_or_download(
                auth=auth, server_usn=out.server_media_usn, upload=False
            )
        rows = desktop.db.all("select id from revlog where cid = ?", card_ids[1])
        ok(
            any(r[0] == ms2 for r in rows),
            "review made during mirror replacement was lost",
        )

        desktop.close()
        print(f"{checks} checks, {failures} failed")
        sys.exit(1 if failures else 0)
    finally:
        reap(server)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
