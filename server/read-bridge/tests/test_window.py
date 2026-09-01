#!/usr/bin/env python3
"""The rate limiter's memory, which only matters because this service is open.

Window.allow() prunes a key's own entry when that key is seen again. That is
enough while the key space is a handful of invited users. With the allowlist
open the key space is every address that touches the service, so a key seen
ONCE and never again would live forever: growth without bound over TIME, on a
box with a gigabyte of RAM, fed by nothing more hostile than background
scanning.

Run: .venv/bin/python tests/test_window.py
"""

import pathlib
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from bridge.ratelimit import Window  # noqa: E402

checks = 0
failures = 0


def ok(cond, label):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print(f"FAIL {label}")


def main():
    # Unique keys that never come back, left to go stale. This is the open
    # service: nobody returns, so nothing prunes itself.
    #
    # The keys must actually EXPIRE for the sweep to have anything to do. An
    # earlier version of this test inserted 1024 keys inside a one-second
    # window and asserted they were gone, which the limiter was right to
    # refuse -- keys inside their window are live and dropping them would
    # forgive an attack in progress.
    w = Window(limit=5, per_s=0.05)
    for i in range(Window.SWEEP_EVERY):
        w.allow(f"stale-{i}")
    time.sleep(0.12)
    for i in range(Window.SWEEP_EVERY + 1):
        w.allow(f"fresh-{i}")
    ok(
        len(w.hits) <= Window.SWEEP_EVERY + 1,
        f"keys that went stale are dropped ({len(w.hits)} retained of "
        f"{Window.SWEEP_EVERY * 2 + 1} inserted)",
    )
    ok(
        not any(k.startswith("stale-") for k in w.hits),
        "and it is the stale ones that went, not an arbitrary slice",
    )

    # The sweep must not cost anyone their limit: a key at its cap stays at its
    # cap across one, or the sweep would forgive an attacker mid-attack.
    w = Window(limit=2, per_s=300)
    ok(w.allow("victim"), "first attempt allowed")
    ok(w.allow("victim"), "second attempt allowed")
    ok(not w.allow("victim"), "third attempt denied")
    for i in range(Window.SWEEP_EVERY * 2):
        w.allow(f"noise-{i}")
    ok(not w.allow("victim"), "still denied after a sweep -- a sweep is not an amnesty")

    # And a genuinely expired key IS forgiven, which is the other half: a
    # limiter that only ever denies is also broken.
    w = Window(limit=1, per_s=0.05)
    ok(w.allow("slow"), "first attempt allowed")
    ok(not w.allow("slow"), "immediate retry denied")
    time.sleep(0.12)
    ok(w.allow("slow"), "allowed again once the window has passed")

    print(f"{checks} checks, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
