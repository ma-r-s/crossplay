"""Rate limiting: a sliding-window counter, and a failure-driven lockout.

Its own module so it can be tested without importing the web application: the
limiter has no business needing FastAPI (or, on the study side, the deck
converter) to prove it counts correctly.

BYTE-IDENTICAL TWIN of server/read-bridge/bridge/ratelimit.py. Until
2026-09-05 this file held only Window, and this service's sign-in was guarded
by a flat per-username cap while its twin had exponential backoff and a global
ceiling; server/attacks.py found the difference by spraying 80 fresh
address/account pairs at both and being refused by one of them. Neither
Dockerfile can COPY a file from outside its own directory, so the module is
duplicated rather than shared -- a change to either belongs in both.
"""

import time


class Window:
    """Sliding-window counter, keyed by IP or username.

    A key's own entry is pruned when that key is seen again, which is enough
    while the key space is a handful of invited users. These services are open,
    so the key space is every address on the internet that ever touches them,
    and a key seen ONCE and never again would live forever -- growth without
    bound over time, fed by nothing more hostile than background scanning.
    So stale keys are swept.
    """

    # Rare enough that the common path stays the dict lookup it always was,
    # often enough that a stream of unique keys cannot outrun it: at most this
    # many expired entries accumulate between sweeps.
    SWEEP_EVERY = 256

    def __init__(self, limit: int, per_s: float):
        self.limit, self.per_s = limit, per_s
        self.hits: dict[str, list[float]] = {}
        self._calls_since_sweep = 0

    def _sweep(self, now: float) -> None:
        # A key whose NEWEST hit has expired can no longer deny anything, so it
        # is pure residue. Rebuilt rather than mutated in place: deleting from
        # a dict while iterating it raises. Note this bounds the dict by the
        # keys seen within per_s, NOT by nothing -- a flood inside one window
        # is still held, which is what makes the limiter work.
        cutoff = now - self.per_s
        self.hits = {k: v for k, v in self.hits.items() if v and v[-1] > cutoff}

    def allow(self, key: str) -> bool:
        now = time.time()
        self._calls_since_sweep += 1
        if self._calls_since_sweep >= self.SWEEP_EVERY:
            self._calls_since_sweep = 0
            self._sweep(now)
        hits = [t for t in self.hits.get(key, []) if t > now - self.per_s]
        if len(hits) >= self.limit:
            self.hits[key] = hits
            return False
        hits.append(now)
        self.hits[key] = hits
        return True

# ---------------------------------------------------------------------------
# Failure-driven backoff, which is a different instrument from the counter
# above and answers a different question. Window asks "is this key making too
# many requests"; Lockout asks "is this key getting the password wrong". They
# are both here because a service that is open to everyone needs both, and
# because keeping them in one module is what made it obvious that the flat
# per-username Window and this class were fighting over the same key.
# ---------------------------------------------------------------------------


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
