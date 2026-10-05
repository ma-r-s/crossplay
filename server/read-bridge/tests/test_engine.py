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
                "users": {
                    USER: {"password": "pw", "token": "tok-1", "secret": "sec-1"}
                },
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

    require_free_port(FAKE_PORT, "the fake Instapaper")
    proc = popen_group(
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
    try:
        wait_port(FAKE_PORT)
        assert_alive(proc, "the fake Instapaper")
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
        ok(
            cleaned[1]["id"] == 2,
            "the id stays, or Instapaper re-sends that article forever",
        )
        ok(
            len(engine.sanitize_have([{"id": i} for i in range(500)]))
            == engine.MAX_ARTICLES,
            "the posted index is trimmed to what the reader can hold",
        )

        # --- the fetch cap
        original_cap = engine.MAX_FETCH_PER_SYNC
        engine.MAX_FETCH_PER_SYNC = 2
        try:
            summary = engine.sync_cycle(st, "tok-1", "sec-1", [], [])
        finally:
            engine.MAX_FETCH_PER_SYNC = original_cap
        ok(
            len(summary["articles"]) == 2,
            "a first sync delivers only what it had time to prepare",
        )
        ok(
            summary["withheld"] == 4,
            f"and says how many are still coming ({summary['withheld']})",
        )

        # The withheld ones are NOT in `have` next time, so they arrive later.
        have = [{"id": a["id"], "hash": a["hash"]} for a in summary["articles"]]
        summary2 = engine.sync_cycle(st, "tok-1", "sec-1", have, [])
        ok(len(summary2["articles"]) == 4, "the rest arrive on the next sync")
        ok(summary2["withheld"] == 0, "and nothing is left withheld")
        ok(
            summary2["deleteIds"] == [],
            "the ones already held are not reported deleted",
        )

        # --- the limit guard
        # Ask Instapaper for fewer than the account holds and its delete_ids
        # correctly names everything outside the window. Passing those on
        # would delete real articles off the reader.
        every = [
            {"id": a["id"], "hash": a["hash"]}
            for a in summary["articles"] + summary2["articles"]
        ]
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
        remote["bookmarks"] = [
            b for b in remote["bookmarks"] if b["bookmark_id"] != 200
        ]
        state_file.write_text(json.dumps(remote))
        after = engine.sync_cycle(st, "tok-1", "sec-1", every, [])
        ok(
            after["deleteIds"] == [200],
            f"a real removal is reported ({after['deleteIds']})",
        )
        ok(
            not st.article_dir(200).exists(),
            "and the bridge drops its cached text for it",
        )

        # --- the Instaparser key (GitHub #298, card #655)
        # From 2026-09-30 Instapaper answers get_text with error 1044 unless
        # the request carries an Instaparser key, and every article of every
        # sync failed: "0 new or updated. 12 Instapaper could not prepare".
        remote = json.loads(state_file.read_text())
        remote["instaparser_key"] = "ipk-test"
        state_file.write_text(json.dumps(remote))
        keyed = store.UserStore("instaparser-test").ensure()
        os.environ.pop("INSTAPARSER_API_KEY", None)
        bare = engine.sync_cycle(keyed, "tok-1", "sec-1", [], [])
        ok(
            not bare["articles"] and len(bare["failed"]) == len(remote["bookmarks"]),
            f"without a key every article fails, as it did live ({len(bare['failed'])})",
        )
        ok(
            all("1044" not in f["why"] for f in bare["failed"]),
            "and the reader is given a sentence, not an error code",
        )

        os.environ["INSTAPARSER_API_KEY"] = "ipk-test"
        original_gap = instapaper.PARSE_GAP_S
        instapaper.PARSE_GAP_S = 0.3
        try:
            started = time.monotonic()
            fixed = engine.sync_cycle(keyed, "tok-1", "sec-1", [], [])
            took = time.monotonic() - started
        finally:
            instapaper.PARSE_GAP_S = original_gap
            os.environ.pop("INSTAPARSER_API_KEY", None)
        n = len(remote["bookmarks"])
        # The fake checks the OAuth signature over every body field, so an
        # article arriving proves the key was both sent and signed.
        ok(
            len(fixed["articles"]) == n and not fixed["failed"],
            f"with the key every article arrives ({len(fixed['articles'])}/{n})",
        )
        ok(
            took >= (n - 1) * 0.3,
            f"one parse per gap, the free tier's rate ({took:.2f}s for {n})",
        )

        # --- what a parse costs
        # Every attempt can spend a credit, so the per-sync cap counts
        # attempts: with a refused key, a sync stops at the cap rather than
        # walking the whole listing at one parse a second.
        capped = store.UserStore("instaparser-cap").ensure()
        original_cap = engine.MAX_FETCH_PER_SYNC
        engine.MAX_FETCH_PER_SYNC = 2
        try:
            refused = engine.sync_cycle(capped, "tok-1", "sec-1", [], [])
        finally:
            engine.MAX_FETCH_PER_SYNC = original_cap
        ok(
            len(refused["failed"]) == 2 and refused["withheld"] == n - 2,
            f"refusals count toward the cap ({len(refused['failed'])} tried,"
            f" {refused['withheld']} withheld)",
        )

        # And an article Instapaper has no text for is asked about once, not
        # on every sync for as long as it stays in the list.
        remote = json.loads(state_file.read_text())
        template = bookmarks(1)[0]
        remote["bookmarks"].append(
            dict(template, bookmark_id=300, url="https://example.com/no-text", text_fails=True)
        )
        remote["bookmarks"].append(
            dict(template, bookmark_id=301, url="https://example.com/short", text="<p>Hi.</p>")
        )
        state_file.write_text(json.dumps(remote))
        calls = []
        real_get_text = instapaper.Instapaper.get_text

        def counting(self, bookmark_id):
            calls.append(bookmark_id)
            return real_get_text(self, bookmark_id)

        instapaper.Instapaper.get_text = counting
        os.environ["INSTAPARSER_API_KEY"] = "ipk-test"
        instapaper.PARSE_GAP_S = 0
        try:
            engine.sync_cycle(keyed, "tok-1", "sec-1", [], [])
            first_calls = sorted(calls)
            calls.clear()
            again = engine.sync_cycle(keyed, "tok-1", "sec-1", [], [])
        finally:
            instapaper.Instapaper.get_text = real_get_text
            instapaper.PARSE_GAP_S = original_gap
            os.environ.pop("INSTAPARSER_API_KEY", None)
        ok(first_calls == [300, 301], f"the first sync asks about both ({first_calls})")
        ok(calls == [], f"the next sync asks about neither ({calls})")
        ok(
            sorted(f["id"] for f in again["failed"]) == [300, 301],
            "and still reports both as not prepared",
        )

        print(f"{checks} checks, {failures} failed")
        return 1 if failures else 0
    finally:
        reap(proc)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
