#!/usr/bin/env python3
"""The per-username backoff, and the reason it exists.

The design named three preconditions for opening registration to everyone:
per-IP caps, global caps, and per-username EXPONENTIAL lockout -- and said
registration stays behind an allowlist "until those exist AND HAVE BEEN
EXERCISED". This file is the second half of that sentence for the third
condition; a limiter nobody has driven is a limiter nobody knows the shape of.

Time is injected rather than slept, so the doubling is asserted at its real
intervals instead of at whatever a test is willing to wait for.

Run: .venv/bin/python tests/test_lockout.py
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from bridge.ratelimit import Lockout  # noqa: E402

checks = 0
failures = 0


def ok(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"  FAIL: {what}")


def main():
    T = 1_000_000.0

    # --- a clean key is never held
    lk = Lockout()
    ok(lk.locked_for("nobody@example.com", T) == 0.0, "an unknown username may try")

    # --- typos are free, then the penalty starts. A person who mistypes once
    # and gets it right must not be turned away; that is the ordinary shape of
    # signing in, and the API suite caught it by doing exactly that.
    for n in range(Lockout.FREE_FAILURES):
        lk.record_failure("a@example.com", T)
        ok(lk.locked_for("a@example.com", T) == 0.0, f"failure {n + 1} of {Lockout.FREE_FAILURES} is free")

    lk.record_failure("a@example.com", T)
    ok(lk.locked_for("a@example.com", T) == Lockout.BASE_S, "the first CHARGED failure locks for the base interval")
    ok(lk.locked_for("a@example.com", T + Lockout.BASE_S - 1) > 0, "still locked one second before it lapses")
    ok(lk.locked_for("a@example.com", T + Lockout.BASE_S) == 0.0, "and free once it has")

    # --- consecutive failures DOUBLE, which is the whole mechanism
    lk = Lockout()
    for _ in range(Lockout.FREE_FAILURES):
        lk.record_failure("b@example.com", T)
    for n, expected in ((1, 30.0), (2, 60.0), (3, 120.0), (4, 240.0)):
        lk.record_failure("b@example.com", T)
        ok(lk.locked_for("b@example.com", T) == expected,
           f"charged failure {n} locks for {expected}s (got {lk.locked_for('b@example.com', T)})")

    # --- and stop doubling, or one bad afternoon locks an account geologically
    lk = Lockout()
    for _ in range(20):
        lk.record_failure("c@example.com", T)
    ok(lk.locked_for("c@example.com", T) == Lockout.MAX_S, "the penalty is capped")

    # --- a correct password clears it. This is only reachable when Instapaper
    # accepted the credentials, so it cannot be used to reset the penalty.
    lk = Lockout()
    for _ in range(Lockout.FREE_FAILURES + 1):
        lk.record_failure("d@example.com", T)
    ok(lk.locked_for("d@example.com", T) > 0, "locked once past the free failures")
    lk.record_success("d@example.com")
    ok(lk.locked_for("d@example.com", T) == 0.0, "a successful sign-in clears the backoff")

    # --- failures are forgiven, so yesterday's typo is not today's fifth
    lk = Lockout()
    for _ in range(Lockout.FREE_FAILURES + 1):
        lk.record_failure("e@example.com", T)
    for _ in range(Lockout.FREE_FAILURES + 1):
        lk.record_failure("e@example.com", T + Lockout.FORGET_S + 1)
    ok(lk.locked_for("e@example.com", T + Lockout.FORGET_S + 1) == Lockout.BASE_S,
       "a failure after the forget window starts the count over, not resumes it")

    # --- ONE USER'S LOCKOUT IS NOT ANOTHER'S. The failure that would matter
    # most: a shared counter would let anyone lock anyone out by guessing at
    # their address, turning a defence into a denial of service.
    lk = Lockout()
    for _ in range(6):
        lk.record_failure("victim@example.com", T)
    ok(lk.locked_for("victim@example.com", T) > 0, "the probed account is held")
    ok(lk.locked_for("bystander@example.com", T) == 0.0, "and nobody else is")

    # --- the key space is bounded, the same problem Window had once the
    # service opened: a username tried once and never again must not live
    # forever.
    lk = Lockout()
    for i in range(Lockout.SWEEP_EVERY * 2):
        lk.record_failure(f"scan{i}@example.com", T)
    before = len(lk._state)
    # Past the free failures, so this key is genuinely serving a penalty and
    # the sweep has something it could wrongly discard.
    for _ in range(Lockout.FREE_FAILURES + 1):
        lk.record_failure("later@example.com", T + Lockout.FORGET_S + 1)
    for i in range(Lockout.SWEEP_EVERY):
        lk.record_failure(f"more{i}@example.com", T + Lockout.FORGET_S + 1)
    ok(len(lk._state) < before, f"stale keys are swept ({before} -> {len(lk._state)})")
    ok(lk.locked_for("later@example.com", T + Lockout.FORGET_S + 1) > 0,
       "and sweeping does not free a key that is still serving its penalty")

    print(f"{checks} checks, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
