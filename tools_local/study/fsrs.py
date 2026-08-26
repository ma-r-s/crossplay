"""FSRS-5, the replay half only.

Newer Anki no longer stores a card's memory state: `cards.data` used to carry
`{"s": stability, "d": difficulty}` and now carries `{"pos": n, "dr": 0.9}`,
with the state recomputed from the review log on demand. The converter used to
read `"s"` directly, so a collection written by current Anki imported every
reviewed card as brand new (found 2026-08-25, on a deck with 6300 reviews).

This module is the fallback: replay a card's own review history through the
same FSRS-5 update rules the device scheduler uses, and hand the converter the
stability/difficulty Anki would have stored. It is a deliberate mirror of
src/apps_local/study/StudyFsrs.cpp -- keep the two in step, and keep the
formulas free of cleverness so the correspondence stays line-for-line.
test_fsrs.py holds this file to the same committed oracle as the C++ side:
host-tests/study/FsrsVectors.h, real cards with the state Anki itself computed.

The only intended divergence is precision: the device runs single-precision
floats, Python runs doubles. The shared tolerance in the tests (2%) is far
wider than that gap.
"""

import math

NUM_PARAMS = 19

# Anki's published FSRS-5 defaults, for a deck without optimized parameters.
# Same table as kDefaultParams in StudyFsrs.cpp.
DEFAULT_PARAMS = (
    0.40255,
    1.18385,
    3.173,
    15.69105,
    7.1949,
    0.5345,
    1.4604,
    0.0046,
    1.54575,
    0.1192,
    1.01925,
    1.9395,
    0.11,
    0.29605,
    2.2698,
    0.2315,
    2.9898,
    0.51655,
    0.6621,
)

# The FSRS-5 forgetting curve is a power function; DECAY is fixed in FSRS-5
# and FACTOR is derived so that R(S) is exactly 0.9.
_DECAY = -0.5
_FACTOR = 0.9 ** (1.0 / _DECAY) - 1.0  # 19/81

_MIN_STABILITY = 0.01
_MAX_STABILITY = 36500.0


def _clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def _initial_stability(w, rating):
    return _clamp(w[rating - 1], _MIN_STABILITY, _MAX_STABILITY)


def _initial_difficulty(w, rating):
    return _clamp(w[4] - math.exp(w[5] * (rating - 1)) + 1.0, 1.0, 10.0)


def _next_difficulty(w, d, rating):
    delta = -w[6] * (rating - 3)
    damped = d + delta * ((10.0 - d) / 9.0)
    reverted = w[7] * _initial_difficulty(w, 4) + (1.0 - w[7]) * damped
    return _clamp(reverted, 1.0, 10.0)


def _short_term_stability(w, s, rating):
    inc = math.exp(w[17] * (rating - 3.0 + w[18]))
    if rating >= 3:
        inc = max(inc, 1.0)
    else:
        inc = min(inc, 1.0)
    return s * inc


def _recall_stability(w, d, s, retr, rating):
    hard_penalty = w[15] if rating == 2 else 1.0
    easy_bonus = w[16] if rating == 4 else 1.0
    return s * (
        1.0
        + math.exp(w[8])
        * (11.0 - d)
        * s ** (-w[9])
        * (math.exp((1.0 - retr) * w[10]) - 1.0)
        * hard_penalty
        * easy_bonus
    )


def _forget_stability(w, d, s, retr):
    return (
        w[11]
        * d ** (-w[12])
        * ((s + 1.0) ** w[13] - 1.0)
        * math.exp((1.0 - retr) * w[14])
    )


def _retrievability(s, elapsed_days):
    if s <= 0.0:
        return 1.0
    t = max(0.0, float(elapsed_days))
    return (1.0 + _FACTOR * t / s) ** _DECAY


def replay(steps, params=None):
    """Run one card's whole review history; return (stability, difficulty).

    `steps` is the same shape the regression vectors use: a sequence of
    (elapsed_days, rating) with elapsed_days counted from the previous review
    (0 for the first, and for any same-day review) and rating the 1-4 button.
    Filtered-deck ("cram") reviews and manual reschedules must already be
    excluded, exactly as gen_fsrs_vectors.py excludes them.

    Returns (0.0, 0.0) for an empty history: the caller treats that as "no
    state", the same as a card Anki has never seen answered.
    """
    w = params if params is not None else DEFAULT_PARAMS
    if len(w) != NUM_PARAMS:
        raise ValueError(f"FSRS-5 wants {NUM_PARAMS} weights, got {len(w)}")

    stability = 0.0
    difficulty = 0.0
    learned = False
    for elapsed_days, rating in steps:
        if not learned:
            stability = _initial_stability(w, rating)
            difficulty = _initial_difficulty(w, rating)
            learned = True
            continue
        if elapsed_days <= 0:
            stability = _short_term_stability(w, stability, rating)
        else:
            retr = _retrievability(stability, elapsed_days)
            if rating == 1:
                # A lapse must never be worth more than not lapsing.
                stability = min(
                    _forget_stability(w, difficulty, stability, retr), stability
                )
            else:
                stability = _recall_stability(w, difficulty, stability, retr, rating)
        stability = _clamp(stability, _MIN_STABILITY, _MAX_STABILITY)
        difficulty = _next_difficulty(w, difficulty, rating)
    return stability, difficulty
