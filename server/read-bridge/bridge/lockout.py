"""Per-username backoff for the sign-in endpoint.

The design (docs/apps/instapaper-plan.md, and the Anki bridge's plan it copies)
made three things the precondition for opening registration to everyone:
aggressive per-IP caps, GLOBAL caps, and per-username EXPONENTIAL lockout. When
the allowlist was opened, only the first existed -- login was guarded by a
per-IP window and a flat per-username window, and the global cap was on /api/sync
rather than on the endpoint that is a credential-stuffing oracle. This is the
third one.

What it is for, stated narrowly so nobody mistakes its reach: the sign-in
endpoint exchanges a username and password with INSTAPAPER and reports whether
they were accepted. That makes this service an oracle for someone else's
credentials, running on Mario's hardware. A flat window lets an attacker probe
one account at a fixed rate forever; doubling makes a sustained attack on any
single account cost time that grows without bound, which is the whole trick.

WHAT IT DOES NOT DO, and this matters more than what it does: it cannot touch
DISTRIBUTED stuffing, where each attempt is a different username from a
different address. Nothing in this process can -- per-IP and per-username
counters are both defeated by having many of each. That case needs a control in
front of the service (Cloudflare rate limiting), which is an open item and needs
Mario's hands on a dashboard. Opening the allowlist is what turned that item
from prudent to load-bearing.

Lives beside Window conceptually; kept in its own module so it does not collide
with the in-flight move of Window into bridge/ratelimit.py. Fold the two
together when that lands.
"""

import time


class Lockout:
    """Exponential per-key backoff, driven by failures rather than by traffic.

    A Window counts every attempt; this counts only the ones that were WRONG,
    so a person who signs in successfully is never slowed down no matter how
    often they do it, and an attacker is slowed by the thing that identifies
    them as one.
    """

    # Failures that cost nothing. People mistype passwords and the penalty
    # should not fall on them: two free attempts covers a typo and a
    # wrong-saved-password, while an attacker's third guess is already the one
    # that starts paying. Found by the API suite, which signs in wrongly and
    # then correctly -- the ordinary shape of a real session -- and was refused
    # on the correct password until this existed.
    FREE_FAILURES = 2
    # First lockout, doubling per consecutive failure after the free ones:
    # 30s, 1m, 2m, 4m, ...
    BASE_S = 30.0
    # Doubling has to stop somewhere or an early mistake locks an account for a
    # geological interval. Eight failures reaches this; a real person mistyping
    # twice waits half a minute.
    MAX_S = 3600.0
    # Failures older than this are forgiven, so yesterday's typo does not make
    # today's typo the fifth one.
    FORGET_S = 86400.0
    SWEEP_EVERY = 256

    def __init__(self):
        # key -> [consecutive_failures, last_failure_at]
        self._state: dict[str, list] = {}
        self._calls_since_sweep = 0

    def _sweep(self, now: float) -> None:
        # Same unbounded-key-space problem a Window has once the service is
        # open: a username tried once and never again would live forever.
        self._state = {k: v for k, v in self._state.items() if now - v[1] < self.FORGET_S}

    def _penalty(self, failures: int) -> float:
        charged = failures - self.FREE_FAILURES
        if charged <= 0:
            return 0.0
        return min(self.MAX_S, self.BASE_S * (2 ** (charged - 1)))

    def locked_for(self, key: str, now: float | None = None) -> float:
        """Seconds still to wait, or 0.0 when the key may try."""
        now = time.time() if now is None else now
        entry = self._state.get(key)
        if not entry:
            return 0.0
        failures, last = entry
        if now - last >= self.FORGET_S:
            return 0.0
        remaining = self._penalty(failures) - (now - last)
        return remaining if remaining > 0 else 0.0

    def record_failure(self, key: str, now: float | None = None) -> None:
        now = time.time() if now is None else now
        self._calls_since_sweep += 1
        if self._calls_since_sweep >= self.SWEEP_EVERY:
            self._calls_since_sweep = 0
            self._sweep(now)
        entry = self._state.get(key)
        # A failure after the forget window starts the count again rather than
        # resuming it, or an account probed once a day would eventually lock
        # permanently on nobody's fault.
        failures = entry[0] + 1 if entry and now - entry[1] < self.FORGET_S else 1
        self._state[key] = [failures, now]

    def record_success(self, key: str) -> None:
        """Clears the count. Only reachable when the password was right, so it
        cannot be used to reset the penalty from outside."""
        self._state.pop(key, None)
