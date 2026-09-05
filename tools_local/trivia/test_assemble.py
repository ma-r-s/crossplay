#!/usr/bin/env python3
"""The rating-fed assembler's invariants, as the failures that earned them.

    python3 tools_local/trivia/test_assemble.py

Standard library only and no corpus needed, so this never skips. Every case is
a real defect measured on the local rating run, written so that undoing the fix
turns it red rather than quietly costing questions nobody counts.
"""

import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import assemble_pack as A  # noqa: E402
import calibrate_levels as CAL  # noqa: E402

FAILED = []


def check(name, ok, detail=""):
    print(
        f"  [{'ok  ' if ok else 'FAIL'}] {name}{('  -- ' + detail) if detail and not ok else ''}"
    )
    if not ok:
        FAILED.append(name)


# --- rule 3: r -> d is absolute -----------------------------------------------
def test_levels():
    check(
        "r maps to d with high r easy",
        A.level(10) == 1 and A.level(0) == 5,
        f"level(10)={A.level(10)} level(0)={A.level(5)}",
    )
    check(
        "every r in 0..10 lands in 1..5", all(1 <= A.level(r) <= 5 for r in range(11))
    )
    check(
        "the mapping is monotonic",
        all(A.level(r) >= A.level(r + 1) for r in range(10)),
        [A.level(r) for r in range(11)],
    )
    # The point of rule 3: the same r must give the same d whatever else is in
    # the run. A quantile mapping passes every check above and fails this one.
    small = [A.level(r) for r in (9, 7, 5, 3, 1)]
    large = [A.level(r) for r in (9, 7, 5, 3, 1)] * 40
    check(
        "a level does not depend on the size of the run",
        small == large[: len(small)] and set(large) == set(small),
    )
    check(
        "level() reads no state at all",
        A.level.__code__.co_freevars == () and "LEVELS" in A.level.__code__.co_names,
    )


# --- rule 1: join on the stored id --------------------------------------------
def test_stored_id():
    # A row exactly like the 349: the clue text was repaired after the id was
    # minted, so the id no longer hashes its own text. It must still join.
    corpus = [
        {
            "id": "aaaaaaaaaaaa",
            "q": "This general surrendered at Appomattox to Ulysses S. Grant in 1865",
            "a": "Robert E. Lee",
            "y": 1990,
            "alt": [],
            "w": [],
        }
    ]
    enriched = {"aaaaaaaaaaaa": {"id": "aaaaaaaaaaaa", "r": 6, "w": [], "bad": False}}
    pack, _ = A.assemble(corpus, enriched)
    check(
        "a repaired clue still joins on its stored id",
        len(pack) == 1,
        f"{len(pack)} rows",
    )
    check(
        "the stored id survives into the pack",
        pack and pack[0]["id"] == "aaaaaaaaaaaa",
        pack[0]["id"] if pack else "-",
    )
    # ...and the id really has moved, so the test above is not vacuous: if the
    # assembler re-derived, the join would miss and the row would vanish.
    check(
        "the case really is a moved id",
        A.rederive(corpus[0]["q"]) != corpus[0]["id"],
        "rederive matches, so this row would join either way",
    )
    # An unrated question is simply excluded, which is why re-deriving is
    # invisible: no error, just a smaller pack.
    pack2, _ = A.assemble(
        corpus, {A.rederive(corpus[0]["q"]): enriched["aaaaaaaaaaaa"]}
    )
    check(
        "keying on the re-derived id loses the row with no error",
        len(pack2) == 0,
        f"{len(pack2)} rows",
    )


# --- rule 2: no `w` key below three options -----------------------------------
def test_short_option_sets():
    for n in (0, 1, 2):
        w = A.pick_options("Australia", (), ["Argentina", "Indonesia"][:n], [])
        check(f"{n} candidate(s) yields no option set", w == [], str(w))
    corpus = [
        {
            "id": "b" * 12,
            "q": "This continent was sighted by Willem Jansz in 1606",
            "a": "Australia",
            "y": 1990,
            "alt": [],
            "w": [],
        }
    ]
    enriched = {
        "b" * 12: {
            "id": "b" * 12,
            "r": 5,
            "w": ["Argentina", "Indonesia"],
            "bad": False,
        }
    }
    pack, _ = A.assemble(corpus, enriched)
    check("a 2-option question keeps its clue", len(pack) == 1, f"{len(pack)} rows")
    check(
        "a 2-option question carries NO w key",
        pack and "w" not in pack[0],
        str(pack[0].get("w")) if pack else "-",
    )
    # The reason the key is dropped rather than shortened: test_pack.py asserts
    # every question that HAS options has at least three of them.
    mc = [x for x in pack if x.get("w")]
    check("test_pack's MC invariant holds", all(len(x["w"]) >= 3 for x in mc))


# --- the longest-option defect ------------------------------------------------
def test_answer_never_longest():
    # Model options at their measured worst: in-band individually, all shorter
    # than the answer, so the set gives the answer away without any knowledge.
    answer = "Mediterranean"
    scattered = ["Caribbean", "Baltic Sea", "Adriatic"]
    check(
        "the case is really the defect",
        all(A.distractors.length_ok(answer, c) for c in scattered)
        and all(len(c) < len(answer) for c in scattered)
        and len(scattered) >= 3,
        "the fixture no longer reproduces the bug",
    )
    w = A.pick_options(answer, (), scattered, [])
    check(
        "three in-band but all-shorter options are refused outright",
        w == [],
        str(w),
    )
    # Given a candidate that CAN cover, the set must use it.
    w = A.pick_options(answer, (), scattered, ["Bering Strait"])
    check("a covering option is pulled in when one exists", len(w) == 3, str(w))
    check(
        "the answer is not strictly the longest option",
        w and max(len(c) for c in w) >= len(answer),
        f"answer {len(answer)} vs longest option {max((len(c) for c in w), default=0)}",
    )
    check(
        "the covering option leads, so it survives any truncation to 3",
        w and len(w[0]) >= len(answer),
        str(w),
    )


def test_band_holds():
    """The band is what pulls the spread down; the cover is what kills the tell.
    Both must survive, so a fix to one cannot silently drop the other."""
    answer = "Rome"
    # 12 characters is outside 4/0.55 = 7.3, so it must not be selected even
    # though it would satisfy the cover rule on its own.
    w = A.pick_options(answer, (), ["Constantinople", "Ravenna", "Milan", "Pisa"], [])
    check(
        "an out-of-band option is refused even though it covers",
        "Constantinople" not in w,
        str(w),
    )
    check(
        "all selected options are inside the band",
        all(A.distractors.length_ok(answer, c) for c in w),
        str(w),
    )

    # The spread the defect is actually about: scattered lengths around a fixed
    # answer must come back tighter than they went in.
    answer = "Napoleon"
    noisy = ["Xerxes", "Charlemagne", "Attila", "Augustus", "Tiberius", "Nero"]
    w = A.pick_options(answer, (), noisy, [])
    before = statistics.pstdev([len(c) for c in noisy[:3]] + [len(answer)])
    after = statistics.pstdev([len(c) for c in w] + [len(answer)])
    check(
        "selection narrows the length spread",
        after < before,
        f"{before:.2f} -> {after:.2f}",
    )


def test_pack_shape():
    """An assembled row must be exactly what pack_format.write can encode."""
    corpus = [
        {
            "id": "c" * 12,
            "q": "This Italian city is built on more than a hundred islands",
            "a": "Venice",
            "y": 1995,
            "alt": ["Venezia"],
            "w": ["Naples", "Verona"],
        }
    ]
    enriched = {
        "c" * 12: {
            "id": "c" * 12,
            "r": 8,
            "w": ["Genoa", "Turin", "Ravenna"],
            "bad": False,
        }
    }
    pack, _ = A.assemble(corpus, enriched)
    x = pack[0]
    check("d, y, q, a and id are all present", {"d", "y", "q", "a", "id"} <= set(x))
    check("d is in 1..5", 1 <= x["d"] <= 5, str(x["d"]))
    check(
        "a `bad` rating removes the question",
        A.assemble(corpus, {"c" * 12: {"id": "c" * 12, "r": 8, "bad": True}})[0] == [],
    )
    check(
        "exactly STORED options are kept when available",
        x.get("w") and len(x["w"]) == A.STORED,
        str(x.get("w")),
    )
    check(
        "no distractor equals the answer",
        all(c.lower() != x["a"].lower() for c in x.get("w", [])),
    )


# --- rule 4: US-centric ships tagged, and the device toggle hides it ----------
def us_row(flag):
    corpus = [
        {
            "id": "d" * 12,
            "q": "This president appears on the United States one-dollar bill",
            "a": "Washington",
            "y": 1995,
            "alt": [],
            "w": [],
        }
    ]
    enriched = {
        "d" * 12: {"id": "d" * 12, "r": 2, "w": [], "bad": False, "us": flag},
    }
    return corpus, enriched


def test_us_ships_tagged():
    # A US-centric question SHIPS now -- the filter moved to the device toggle,
    # which reads this flag. The build no longer drops anything.
    corpus, enriched = us_row(True)
    pack, stats = A.assemble(corpus, enriched)
    check(
        "a us_centric question ships (no build-time drop)",
        len(pack) == 1,
        f"{len(pack)} rows reached the pack",
    )
    check(
        "and it carries the us flag the toggle reads",
        pack[0].get("us") is True,
        str(pack[0]),
    )
    check(
        "nothing is dropped for being us_centric",
        not any(k.startswith("rejected: us_centric") for k in stats),
        str(dict(stats)),
    )
    # The rating record is read, never consumed: a future re-rating is not
    # needed to flip the toggle, and "not removed from our data" holds.
    check(
        "the rating record still carries its us flag afterwards",
        enriched["d" * 12].get("us") is True,
        "assemble() mutated the ratings",
    )
    check(
        "the corpus row is untouched too",
        corpus[0]["q"].startswith("This president"),
    )

    # An international question ships with us:false -- the flag distinguishes
    # the two, which is the whole point of carrying it.
    corpus, enriched = us_row(False)
    pack, _ = A.assemble(corpus, enriched)
    check("an international question ships", len(pack) == 1)
    check(
        "and is tagged us:false so the toggle keeps it visible",
        pack[0].get("us") is False,
        str(pack[0]),
    )

    # The calibrator still measures the international-only population: the levels
    # mean "hard for an international table", so survivors() must still be able
    # to drop the US-centric rows when the calibrator asks.
    corpus, enriched = us_row(True)
    intl_only = list(A.survivors(corpus, enriched, kick_us=True))
    check(
        "survivors(kick_us=True) still hides US-centric for the calibrator",
        len(intl_only) == 0,
        f"{len(intl_only)} rows survived the calibrator's international filter",
    )


# --- the thresholds are re-derivable, not hand-tuned --------------------------
def test_calibrator_models_the_shipped_mapping():
    """calibrate_levels must score the function that actually ships.

    If the two mappings drift apart the calibration is about a different tool,
    and it would still print a confident recommendation.
    """
    floors = tuple(f for f, _ in A.LEVELS)
    check(
        "the calibrator's mapping agrees with assemble_pack.level for every r",
        all(CAL.level_with(r, floors) == A.level(r) for r in range(11)),
        str([(r, CAL.level_with(r, floors), A.level(r)) for r in range(11)]),
    )
    check(
        "the shipped floors are strictly descending",
        all(floors[i] > floors[i + 1] for i in range(len(floors) - 1)),
        str(floors),
    )
    check(
        "the shipped floors are a candidate the calibrator can reach",
        len(floors) == 4 and all(1 <= f <= 11 for f in floors),
        str(floors),
    )


def test_calibration_is_minimax():
    """The chooser must optimise the WORST view, not the pooled one.

    Thresholds get chosen on a partial run, so a set that is best on the rows
    rated so far and falls apart on a shifted population is the failure mode
    this rule exists to prevent. Scoring on the pooled distribution instead
    passes every other check in this file.
    """
    w1 = [44, 52, 7, 12, 8, 56, 31, 18, 51, 58, 50]
    w2 = [10, 43, 50, 46, 59, 14, 4, 53, 22, 39, 48]
    rows = [r for r in range(11) for _ in range(w1[r])]
    rows += [r for r in range(11) for _ in range(w2[r])]
    samples = CAL.views(rows)
    scored = CAL.choose(samples)
    winner = scored[0][1]

    pooled_best = min(
        ((CAL.spread(rows, f)[1], f) for _, f, _ in scored), key=lambda t: (t[0], t[1])
    )
    worst_of_pooled = next(w for w, f, _ in scored if f == pooled_best[1])

    # The fixture must actually separate the two rules, or the test is vacuous.
    check(
        "the case really separates pooled from minimax",
        pooled_best[1] != winner
        and pooled_best[0] < CAL.spread(rows, winner)[1]
        and worst_of_pooled > CAL.SPREAD_LIMIT,
        f"pooled-best {list(pooled_best[1])} @ {pooled_best[0]:.3f}, "
        f"minimax {list(winner)}",
    )
    check(
        "the pooled-best candidate is refused because a view fails",
        winner != pooled_best[1],
        f"picked {list(winner)}, the pooled favourite",
    )
    check(
        "the chosen set passes the spread check on every view",
        all(r <= CAL.SPREAD_LIMIT for r in scored[0][2].values()),
        str({k: round(v, 3) for k, v in scored[0][2].items()}),
    )
    check(
        "choose() is deterministic",
        [f for _, f, _ in CAL.choose(samples)] == [f for _, f, _ in scored],
    )
    # An empty tier is not a difficulty level: a candidate that leaves one
    # unpopulated must never be offered, however balanced the rest looks.
    counts, ratio = CAL.spread([10] * 50, (9, 8, 7, 5))
    check(
        "a candidate with an empty level scores as unusable",
        ratio == float("inf"),
        f"{dict(counts)} scored {ratio}",
    )


def main():
    print("assemble_pack invariants\n")
    for t in (
        test_levels,
        test_stored_id,
        test_short_option_sets,
        test_answer_never_longest,
        test_band_holds,
        test_pack_shape,
        test_us_ships_tagged,
        test_calibrator_models_the_shipped_mapping,
        test_calibration_is_minimax,
    ):
        t()
    print(f"\n{len(FAILED)} failed")
    return 1 if FAILED else 0


if __name__ == "__main__":
    raise SystemExit(main())
