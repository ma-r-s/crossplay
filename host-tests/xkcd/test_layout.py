#!/usr/bin/env -S uv run --quiet --script
# /// script
# requires-python = ">=3.10"
# ///
"""The layout rule: which way round a comic goes, and how big.

**This is the logic that has broken most often in this app, and it had no tests
until it had broken four times.** Every one of those was found by hand, by
replaying the rule over the real archive and noticing the answer disagreed with
the model that chose the constants. That is not a process that survives being
forgotten, so the properties are written down here instead.

Tests the shipping functions in tools_local/xkcd/build_pack.py directly rather
than a port of them: a second implementation of a rule this fiddly would drift,
which is the same reason the pack and the device share one ditherer.
"""

import math
import pathlib
import sys

sys.path.insert(
    0, str(pathlib.Path(__file__).resolve().parents[2] / "tools_local" / "xkcd")
)

import importlib.util

_spec = importlib.util.spec_from_file_location(
    "build_pack",
    pathlib.Path(__file__).resolve().parents[2]
    / "tools_local"
    / "xkcd"
    / "build_pack.py",
)
bp = importlib.util.module_from_spec(_spec)
sys.modules["build_pack"] = bp
_spec.loader.exec_module(bp)

SHORT, LONG = bp.SHORT_SIDE, bp.LONG_SIDE
LIM = bp.Limits()

checks = 0
failures = 0


def check(cond, msg, *args):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print(f"FAIL  {msg % args if args else msg}")


def placed(W, H, cap, limits=LIM):
    """Run the full host decision: posture, then the grid snap, then the size."""
    posture, scale, pans = bp.layout(W, H, cap, limits)
    sw, sh = (H, W) if posture == "turned" else (W, H)
    scale = bp.snap_width(sw, sh, scale, pans)
    return posture, pans, scale, round(sw * scale), round(sh * scale)


def axes(w, h):
    """Panning axes actually needed once the artwork is on the portrait panel."""
    return (1 if w > SHORT else 0) + (1 if h > LONG else 0)


# --------------------------------------------------------------- the matrix
#
# One comic per cell of docs/apps/xkcd-pack-format.md, sized so that at its readable
# scale it lands in that cell, and asserted against the cell's verdict.


def test_matrix():
    cases = [
        # (W, H, cap, expected posture, expected panning, which cell)
        (300, 300, 12, "portrait", "whole", "narrow / short"),
        (600, 300, 12, "turned", "whole", "long-side wide / short"),
        (1400, 300, 12, "turned", "across", "wider than anything / short"),
        (300, 600, 12, "portrait", "whole", "narrow / long-side tall"),
        (600, 600, 12, "portrait", "across", "both middle band"),
        (1400, 600, 12, "portrait", "across", "wider than anything / long-side tall"),
        (300, 1400, 12, "portrait", "down", "narrow / taller than anything"),
        (600, 1400, 12, "turned", "down", "long-side wide / taller than anything"),
        (1400, 1400, 12, None, "both", "over on both"),
    ]
    for W, H, cap, want_posture, want_pans, cell in cases:
        posture, pans, scale, w, h = placed(W, H, cap)
        check(
            pans == want_pans,
            "%s (%dx%d): pans %s, expected %s",
            cell,
            W,
            H,
            pans,
            want_pans,
        )
        if want_posture:
            check(
                posture == want_posture,
                "%s (%dx%d): %s, expected %s",
                cell,
                W,
                H,
                posture,
                want_posture,
            )

    # The two cells Mario named, on the real comics that named them.
    posture, pans, scale, w, h = placed(733, 250, 9)
    check(
        posture == "turned" and pans == "across",
        "#1518 (733x250 cap 9) must turn and pan across, got %s / %s",
        posture,
        pans,
    )
    check(w <= SHORT, "#1518 must fill the short side, not overflow it: %d", w)


# ------------------------------------------------------------- the promises


def test_never_pans_twice_unnecessarily():
    """**The objective, as a property.** If either posture could have done it in
    fewer axes, the rule must not have chosen more."""
    worst = []
    for W in range(120, 1600, 37):
        for H in range(120, 1600, 41):
            for cap in (4, 7, 10, 12, 16, 22):
                posture, pans, scale, w, h = placed(W, H, cap)
                got = axes(w, h)
                # What each posture could achieve, ignoring the snap.
                best = min(
                    bp._posture(W, H, cap, SHORT, LONG, LIM)[0],
                    bp._posture(W, H, cap, LONG, SHORT, LIM)[0],
                )
                if got > best:
                    worst.append((W, H, cap, posture, got, best))
    check(
        not worst,
        "%d shapes pan on more axes than a posture allowed, e.g. %s",
        len(worst),
        worst[:3],
    )


def test_scale_stays_inside_its_allowance():
    """Shrink to fit is bounded: never below the cap floor, never past the
    upscale cap. The floor is the whole reason the allowance is safe."""
    for W in range(120, 1600, 53):
        for H in range(120, 1600, 59):
            for cap in (4, 7, 10, 12, 16, 22):
                posture, scale, pans = bp.layout(W, H, cap, LIM)
                check(
                    scale <= LIM.max_upscale + 1e-9,
                    "%dx%d cap %d scaled %.3f, past the %.1f cap",
                    W,
                    H,
                    cap,
                    scale,
                    LIM.max_upscale,
                )
                check(
                    cap * scale >= LIM.min_cap - 1e-6
                    or scale >= LIM.max_upscale - 1e-9,
                    "%dx%d cap %d gave %.1fpx lettering, under the %.0fpx floor",
                    W,
                    H,
                    cap,
                    cap * scale,
                    LIM.min_cap,
                )


def test_fixed_axis_goes_on_the_smallest_side():
    """Tiebreak 2, stated. When the same axis pans in both postures, the axis
    that does NOT pan belongs on the smallest side that holds it -- which is
    what settles #1518 and what counting taps got wrong for 334 comics."""
    for W in range(800, 1600, 29):
        for H in range(120, SHORT + 1, 17):  # height fits the short side
            posture, pans, scale, w, h = placed(W, H, 12)
            if pans != "across":
                continue
            check(
                posture == "turned",
                "%dx%d pans across with its height on the short side, so it must "
                "turn; got %s",
                W,
                H,
                posture,
            )


def test_snap_keeps_its_promise():
    """The grid snap must never add a panning axis, and must leave every column
    revealing a full step. Both halves have failed in production.

    The inputs come from layout() rather than being invented: the first version
    of this test fed snap_width a 2000px-tall image while claiming it panned
    across only, which layout() cannot produce, and then reported 56 failures
    against correct code. A property test is only as good as the states it
    feeds, and the reachable states are the ones the rule actually emits.
    """
    for W in range(120, 1600, 31):
        for H in range(120, 1600, 37):
            for cap in (4, 7, 10, 12, 16, 22):
                posture, scale, pans = bp.layout(W, H, cap, LIM)
                sw, sh = (H, W) if posture == "turned" else (W, H)
                before = axes(round(sw * scale), round(sh * scale))
                snapped = bp.snap_width(sw, sh, scale, pans)
                w, h = round(sw * snapped), round(sh * snapped)
                check(
                    axes(w, h) <= before,
                    "snap added an axis to %dx%d cap %d (%s, %s): %d -> %d",
                    W,
                    H,
                    cap,
                    posture,
                    pans,
                    before,
                    axes(w, h),
                )
                if w > SHORT:
                    check(
                        (w - bp.COLUMN_OVERLAP) % bp.COLUMN_STEP == 0,
                        "snapped width %d is off the column grid (%dx%d cap %d)",
                        w,
                        W,
                        H,
                        cap,
                    )

    # Grid alignment holds for any width, reachable or not.
    for sw in range(300, 3000, 23):
        scale = bp.snap_width(sw, 400, 1.0, "down")
        w = round(sw * scale)
        if w > SHORT:
            check(
                (w - bp.COLUMN_OVERLAP) % bp.COLUMN_STEP == 0,
                "snapped width %d is off the column grid (sw=%d)",
                w,
                sw,
            )

    # The regression that took both-axes from 2.8% to 8.9%: one column is legal.
    scale = bp.snap_width(481, 400, 1.0, "down")
    check(
        round(481 * scale) == SHORT,
        "a 481px width must snap DOWN to %d, not up to 912; got %d",
        SHORT,
        round(481 * scale),
    )


def test_unmeasurable_lettering_still_lays_out():
    """85 comics carry too little lettering to measure a cap height. They must
    still get a posture, and they must fit whole -- there is no readable scale
    to aim at, so panning would be guessing."""
    for W, H in ((740, 700), (300, 200), (200, 1500), (1500, 200)):
        posture, pans, scale, w, h = placed(W, H, None)
        check(pans == "whole", "cap-less %dx%d must fit whole, got %s", W, H, pans)
        check(posture in ("portrait", "turned"), "cap-less %dx%d got no posture", W, H)
        check(
            w <= SHORT and h <= LONG, "cap-less %dx%d does not fit: %dx%d", W, H, w, h
        )


def test_turning_has_to_buy_something():
    """A comic that fits whole either way stays portrait. Turning the device is
    a real cost and a tie means turning buys nothing."""
    for W in range(120, SHORT + 1, 19):
        for H in range(120, SHORT + 1, 19):
            posture, pans, scale, w, h = placed(W, H, 12)
            if pans == "whole":
                check(
                    posture == "portrait",
                    "%dx%d fits either way, so it must stay portrait; got %s",
                    W,
                    H,
                    posture,
                )


if __name__ == "__main__":
    test_matrix()
    test_never_pans_twice_unnecessarily()
    test_scale_stays_inside_its_allowance()
    test_fixed_axis_goes_on_the_smallest_side()
    test_snap_keeps_its_promise()
    test_unmeasurable_lettering_still_lays_out()
    test_turning_has_to_buy_something()
    print(
        f"{'FAIL' if failures else 'ok  '}  xkcd layout: {checks} checks, {failures} failed"
    )
    sys.exit(1 if failures else 0)
