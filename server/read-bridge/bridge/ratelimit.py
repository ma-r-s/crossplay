"""Sliding-window rate limiter.

Its own module so it can be tested without importing the web application: the
limiter has no business needing FastAPI (or, on the study side, the deck
converter) to prove it counts correctly.
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
