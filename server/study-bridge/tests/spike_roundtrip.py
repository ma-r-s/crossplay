#!/usr/bin/env python3
"""Prove the bridge's core loop against Anki's own self-hosted sync server.

This is the USN round-trip the plan demands settled before code: a cycle
whose ONLY change is a device review must upload, and a fresh full download
elsewhere must show it. Run directly:

    .venv/bin/python tests/spike_roundtrip.py
"""

import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
PY = sys.executable

PORT = int(os.environ.get("BRIDGE_TEST_PORT", "8996")) + 2
USER, PW = "mario", "spike-pw"
ENDPOINT = f"http://127.0.0.1:{PORT}/"


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


def main():
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="bridge-spike-"))
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
            [PY, "-m", "anki.syncserver"],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        wait_port(PORT)

        from anki.collection import Collection

        # --- Client A: a collection with one card, pushed up.
        col_a = Collection(str(tmp / "a.anki2"))
        auth = col_a.sync_login(USER, PW, ENDPOINT)
        print("auth:", type(auth).__name__, "endpoint:", auth.endpoint)

        nt = col_a.models.by_name("Basic")
        note = col_a.new_note(nt)
        note["Front"], note["Back"] = "spike-front", "spike-back"
        col_a.add_note(note, col_a.decks.get_current_id())
        card_id = note.card_ids()[0]

        out = col_a.sync_collection(auth, sync_media=False)
        print(
            "first sync required:",
            out.required,
            "new_endpoint:",
            out.new_endpoint or "-",
        )
        if out.new_endpoint:
            auth.endpoint = out.new_endpoint
        if out.required:  # first-ever sync: server empty, upload ours
            col_a.full_upload_or_download(
                auth=auth, server_usn=out.server_media_usn, upload=True
            )
            print("full upload done")

        # --- The bridge's move: a backdated device review, raw SQL,
        # usn=-1, then bump col mod THROUGH pylib so the meta exchange
        # notices, then a normal sync.
        review_ms = int((time.time() - 3600) * 1000)  # an hour ago
        col_a.db.execute(
            "insert into revlog (id, cid, usn, ease, ivl, lastIvl, factor, time, type)"
            " values (?, ?, -1, 3, 1, 0, 2500, 4000, 0)",
            review_ms,
            card_id,
        )
        col_a.db.execute(
            "update cards set reps = reps + 1, mod = ?, usn = -1 where id = ?",
            int(time.time()),
            card_id,
        )
        # The documented gap: without touching col.mod the meta exchange can
        # say "no changes". set_config writes through the backend and bumps it.
        col_a.set_config("bridgeLastApply", review_ms)

        out2 = col_a.sync_collection(auth, sync_media=False)
        print("second sync required:", out2.required)
        assert not out2.required, "review-only cycle demanded a full sync?!"

        # --- Client B: fresh full download; the review must be there.
        col_b = Collection(str(tmp / "b.anki2"))
        auth_b = col_b.sync_login(USER, PW, ENDPOINT)
        out_b = col_b.sync_collection(auth_b, sync_media=False)
        print("B sync required:", out_b.required)
        if out_b.new_endpoint:
            auth_b.endpoint = out_b.new_endpoint
        col_b.full_upload_or_download(
            auth=auth_b, server_usn=out_b.server_media_usn, upload=False
        )

        rows = col_b.db.all("select id, cid, ease, usn from revlog")
        print("B revlog:", rows)
        assert any(r[0] == review_ms and r[1] == card_id for r in rows), (
            "the device review did not survive the round trip"
        )
        got_usn = [r[3] for r in rows if r[0] == review_ms][0]
        assert got_usn != -1, "review arrived but with usn still -1 (never assigned)"
        print("ROUNDTRIP OK: backdated raw-SQL review synced up and back down")
    finally:
        if server:
            server.terminate()
            server.wait(timeout=10)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
