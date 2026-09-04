#!/usr/bin/env python3
"""The event poster's promises, with the HTTP layer stubbed.

  * off means off: without both variables nothing is sent and the log says
    so exactly once;
  * a post is the body docs/workflow/events.md describes, with the key in
    both headers, and it returns before the request does;
  * a board that is down, or a prop that cannot be JSON, costs one log line
    and nothing else.

And one that is about the repo rather than the module: bridge/events.py is
duplicated into both bridges because neither image can COPY a file from
outside its own directory. The two copies must stay byte-identical, or a fix
lands on one and not its twin.

Run: .venv/bin/python tests/test_events.py
"""

import hashlib
import json
import logging
import os
import pathlib
import sys
import threading
import time
import urllib.error

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))

from bridge import events  # noqa: E402

checks = 0
failures = 0


def ok(cond, label):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print(f"  FAIL: {label}")


class Capture(logging.Handler):
    def __init__(self):
        super().__init__()
        self.records = []

    def emit(self, record):
        self.records.append(record)


class FakeResponse:
    def read(self):
        return b""

    def close(self):
        pass


def main():
    log = logging.getLogger("bridge.events")
    cap = Capture()
    log.addHandler(cap)
    log.setLevel(logging.INFO)
    sent = []

    def record(req, timeout):
        sent.append((req, timeout))
        return FakeResponse()

    def off_lines():
        return sum("events are off" in r.getMessage() for r in cap.records)

    # --- off means off
    os.environ.pop("SUPABASE_URL", None)
    os.environ.pop("SUPABASE_ANON_KEY", None)
    events._off_logged = False
    events._urlopen = record
    ok(
        events.post("anki", "sync", props={"cards": 1}) is None,
        "nothing is queued without the variables",
    )
    events.post("anki", "sync", props={"cards": 2})
    ok(sent == [], "and nothing reaches the HTTP layer")
    ok(
        off_lines() == 1,
        f"the log says events are off exactly once, said it {off_lines()} times",
    )
    os.environ["SUPABASE_URL"] = "https://x.supabase.co/"
    ok(events.post("anki", "sync") is None and sent == [], "one variable is not enough")
    ok(off_lines() == 1, "and a later post does not repeat the line")

    # --- the body
    os.environ["SUPABASE_ANON_KEY"] = "anon-test-key"
    cap.records.clear()
    dev = events.device_id("tok")
    t = events.post("anki", "sync", device=dev, props={"cards": 3, "seconds": 1.5})
    ok(t is not None, "with both variables a post is queued")
    t.join(5)
    ok(len(sent) == 1, f"one request, got {len(sent)}")
    req, timeout = sent[0]
    ok(
        req.full_url == "https://x.supabase.co/rest/v1/events",
        f"the trailing slash is folded, got {req.full_url}",
    )
    ok(req.get_method() == "POST", "it is a POST")
    ok(timeout == events.TIMEOUT_S, f"the request carries the timeout, got {timeout}")
    ok(
        req.get_header("Apikey") == "anon-test-key"
        and req.get_header("Authorization") == "Bearer anon-test-key",
        "the key rides in both headers",
    )
    ok(
        req.get_header("Content-type") == "application/json"
        and req.get_header("Prefer") == "return=minimal",
        "JSON in, nothing back",
    )
    body = json.loads(req.data)
    ok(
        body
        == {
            "service": "anki",
            "event": "sync",
            "level": "info",
            "device": dev,
            "props": {"cards": 3, "seconds": 1.5},
        },
        f"the body is the contract's, got {body}",
    )
    ok(cap.records == [], "a delivered event logs nothing")

    # --- device_id
    ok(dev != hashlib.sha256(b"tok").hexdigest(), "the device hash is salted")
    ok(len(dev) == 64 and dev == events.device_id("tok"), "and stable")
    ok("tok" not in req.data.decode(), "the raw id is not in the body")
    t = events.post("anki", "heartbeat")
    t.join(5)
    ok(
        "device" not in json.loads(sent[-1][0].data),
        "no device means no device field, not an empty one",
    )

    # --- it returns before the request does
    gate = threading.Event()

    def slow(req, timeout):
        gate.wait(5)
        return FakeResponse()

    events._urlopen = slow
    t0 = time.monotonic()
    t = events.post("anki", "sync")
    elapsed = time.monotonic() - t0
    gate.set()
    t.join(5)
    ok(
        elapsed < 0.5,
        f"post returned in {elapsed:.3f}s while the request was still open",
    )

    # --- a board that is down
    cap.records.clear()

    def down(req, timeout):
        raise urllib.error.URLError("connection refused")

    events._urlopen = down
    t = events.post("anki", "sync", level="error", props={"message": "x"})
    t.join(5)
    ok(not t.is_alive(), "the request thread finishes")
    ok(
        len(cap.records) == 1 and cap.records[0].levelno == logging.WARNING,
        f"one warning line, got {[r.getMessage() for r in cap.records]}",
    )
    ok("dropped" in cap.records[0].getMessage(), "and it says the event was dropped")

    # --- a prop that cannot be JSON
    cap.records.clear()
    events._urlopen = record
    before = len(sent)
    ok(
        events.post("anki", "sync", props={"bad": object()}) is None,
        "an unserialisable prop is dropped, not raised",
    )
    ok(len(sent) == before and len(cap.records) == 1, "one warning, no request")

    # --- the twin
    other = [s for s in ("study-bridge", "read-bridge") if s != ROOT.name]
    twin = ROOT.parent / other[0] / "bridge" / "events.py"
    mine = ROOT / "bridge" / "events.py"
    if twin.exists():
        ok(
            mine.read_bytes() == twin.read_bytes(),
            f"bridge/events.py is byte-identical to its twin in {other[0]}",
        )
    else:
        print(f"  (no twin at {twin}; the byte-identity check needs the whole repo)")

    print(f"{checks} checks, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
