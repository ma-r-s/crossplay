#!/usr/bin/env python3
"""The sync cycle's safety rules, driven directly rather than through HTTP.

These three are in their own file because each one only fires in a state the
API suite cannot reach cheaply, and each one is a rule whose failure is
silent and expensive:

  * the limit guard, which is the difference between a stale row and a wiped
    reading list;
  * the clock rule, which is the difference between one stuck article and a
    progress timestamp nothing can ever beat;
  * the fetch cap, which is the difference between a first sync that says
    "more next time" and one that appears to have stopped early.

Run: .venv/bin/python tests/test_engine.py
"""

import asyncio
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))

BASE_PORT = int(os.environ.get("BRIDGE_TEST_PORT", "8996"))
FAKE_PORT = BASE_PORT + 3
USER = "mario@example.com"
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


PROSE = "<p>" + ("The quick brown fox jumps over the lazy dog. " * 12) + "</p>"


def bookmarks(n):
    return [
        {
            "bookmark_id": 200 + i,
            "url": f"https://example.com/{i}",
            "title": f"Article {i}",
            "description": "",
            "time": 1756000000 + i,
            "progress": 0.0,
            "progress_timestamp": 0,
            "folder": "unread",
            "text": PROSE,
        }
        for i in range(n)
    ]


def main():
    tmp = tempfile.mkdtemp(prefix="readbridge-engine-")
    state_file = pathlib.Path(tmp) / "fake.json"
    state_file.write_text(
        json.dumps(
            {
                "users": {USER: {"password": "pw", "token": "tok-1", "secret": "sec-1"}},
                "bookmarks": bookmarks(6),
            }
        )
    )

    from cryptography.fernet import Fernet

    env = dict(os.environ)
    env.update(
        {
            "FAKE_INSTAPAPER_STATE": str(state_file),
            "FAKE_CONSUMER_KEY": CONSUMER_KEY,
            "FAKE_CONSUMER_SECRET": CONSUMER_SECRET,
            "PYTHONPATH": str(ROOT),
        }
    )
    os.environ.update(
        {
            "READ_DATA": str(pathlib.Path(tmp) / "data"),
            "READ_FERNET_KEY": Fernet.generate_key().decode(),
            "READ_ALLOWLIST": USER,
            "READ_CONSUMER_KEY": CONSUMER_KEY,
            "READ_CONSUMER_SECRET": CONSUMER_SECRET,
            "READ_INSTAPAPER_BASE": f"http://127.0.0.1:{FAKE_PORT}",
        }
    )

    proc = subprocess.Popen(
        [
            sys.executable, "-m", "uvicorn", "tests.fake_instapaper:app",
            "--host", "127.0.0.1", "--port", str(FAKE_PORT), "--log-level", "warning",
        ],
        cwd=ROOT, env=env,
    )
    try:
        wait_port(FAKE_PORT)
        from bridge import engine, instapaper, store

        st = store.UserStore("engine-test").ensure()

        # --- the clock rule
        now = 1_756_000_000
        cleaned = engine.sanitize_have(
            [
                {"id": 1, "hash": "a", "progress": 0.5, "progressAt": now - 10},
                {"id": 2, "hash": "b", "progress": 0.5, "progressAt": now + 400_000},
                {"id": "junk"},
            ],
            now=now,
        )
        ok(len(cleaned) == 2, "an unparseable entry is dropped, the rest survive")
        ok(cleaned[0]["progressAt"] == now - 10, "a sane timestamp passes through")
        ok(
            cleaned[1]["progressAt"] == 0 and cleaned[1]["progress"] == 0.0,
            "a timestamp from the future loses its progress, not its place in `have`",
        )
        ok(cleaned[1]["id"] == 2, "the id stays, or Instapaper re-sends that article forever")
        ok(
            len(engine.sanitize_have([{"id": i} for i in range(500)])) == engine.MAX_ARTICLES,
            "the posted index is trimmed to what the reader can hold",
        )

        # --- the fetch cap
        original_cap = engine.MAX_FETCH_PER_SYNC
        engine.MAX_FETCH_PER_SYNC = 2
        try:
            summary = engine.sync_cycle(st, "tok-1", "sec-1", [], [])
        finally:
            engine.MAX_FETCH_PER_SYNC = original_cap
        ok(len(summary["articles"]) == 2, "a first sync delivers only what it had time to prepare")
        ok(summary["withheld"] == 4, f"and says how many are still coming ({summary['withheld']})")

        # The withheld ones are NOT in `have` next time, so they arrive later.
        have = [{"id": a["id"], "hash": a["hash"]} for a in summary["articles"]]
        summary2 = engine.sync_cycle(st, "tok-1", "sec-1", have, [])
        ok(len(summary2["articles"]) == 4, "the rest arrive on the next sync")
        ok(summary2["withheld"] == 0, "and nothing is left withheld")
        ok(summary2["deleteIds"] == [], "the ones already held are not reported deleted")

        # --- the limit guard
        # Ask Instapaper for fewer than the account holds and its delete_ids
        # correctly names everything outside the window. Passing those on
        # would delete real articles off the reader.
        every = [{"id": a["id"], "hash": a["hash"]} for a in summary["articles"] + summary2["articles"]]
        original_limit = instapaper.LIST_LIMIT
        instapaper.LIST_LIMIT = 3
        try:
            squeezed = engine.sync_cycle(st, "tok-1", "sec-1", every, [])
        finally:
            instapaper.LIST_LIMIT = original_limit
        ok(
            squeezed["deleteIds"] == [],
            f"deletions from a truncated listing are suppressed ({squeezed['deleteIds']})",
        )

        # And with the real limit, a genuine removal still comes through.
        remote = json.loads(state_file.read_text())
        remote["bookmarks"] = [b for b in remote["bookmarks"] if b["bookmark_id"] != 200]
        state_file.write_text(json.dumps(remote))
        after = engine.sync_cycle(st, "tok-1", "sec-1", every, [])
        ok(after["deleteIds"] == [200], f"a real removal is reported ({after['deleteIds']})")
        ok(
            not st.article_dir(200).exists(),
            "and the bridge drops its cached text for it",
        )

        print(f"{checks} checks, {failures} failed")
        return 1 if failures else 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
