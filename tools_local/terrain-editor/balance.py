#!/usr/bin/env python3
"""Balance a traced board into a layout for the panel, and draw the result.

    tools_local/terrain-editor/balance.py boards/castle-field.json -o /tmp/out.svg

Runs on a laptop, never on the device: the balanced coordinates are baked into
the terrain table, so the firmware only ever reads numbers.

WHAT TWO FAILED ATTEMPTS TAUGHT, because both are easy to repeat:

  1. Ranking slots by hops from the H.Q. and spreading each rank evenly gives
     tidy rows and a diamond. The rank widths decide the shape, and the region
     areas came out worse than the trace.
  2. Springs and repulsion give a centre-heavy blob. Both forces pull inward,
     so the corners empty out -- and scaling the bounding box to the frame
     hides it, because the box fills while the inside does not.

So the trace is not the problem. A person tracing a printed board naturally
pushes to the corners, because the board was designed to fill its rectangle.
What a trace is bad at is symmetry: nobody clicks symmetrically.

The approach here is therefore to KEEP the trace and repair it, in this order:

  * find the board's own symmetry in the graph, and enforce it exactly;
  * nudge region areas toward each other, bounded so the shape survives;
  * push outward to the frame, so the corners stay used.
"""

import argparse
import json
import math
import sys

# The play box on a 480x800 panel: 448 between the margins, and the height left
# once the header, the medals line, the rack and the capsule are taken.
BOX_W, BOX_H = 448.0, 590.0


def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def slots(model):
    """Every slot as (x, y), bases first then H.Q., matching the firmware."""
    return [(float(b["x"]), float(b["y"])) for b in model["bases"]] + [
        (float(q["x"]), float(q["y"])) for q in model["hqs"]
    ]


def adjacency(model, n):
    adj = [set() for _ in range(n)]
    for a, b in model["edges"]:
        adj[a].add(b)
        adj[b].add(a)
    return adj


def region_area(pts, members):
    """A region's area, as the polygon through the bases fencing it.

    A two-base region is the water between adjacent bridges and has no polygon,
    so the gap between the pair stands in. Without this the optimiser reads zero
    and squeezes the river to nothing.
    """
    ring = [pts[i] for i in members]
    if len(ring) == 2:
        (ax, ay), (bx, by) = ring
        return math.hypot(bx - ax, by - ay) ** 2 / 4.0
    cx = sum(p[0] for p in ring) / len(ring)
    cy = sum(p[1] for p in ring) / len(ring)
    ring.sort(key=lambda p: math.atan2(p[1] - cy, p[0] - cx))
    total = 0.0
    for i, (x1, y1) in enumerate(ring):
        x2, y2 = ring[(i + 1) % len(ring)]
        total += x1 * y2 - x2 * y1
    return abs(total) / 2.0


# --- symmetry ---------------------------------------------------------------


def find_symmetry(model, pts, flip):
    """Pair every slot with the one sitting where `flip` sends it.

    Returns the permutation, or None when the board does not actually have that
    symmetry -- checked against the graph, not just the positions, so a board
    that merely looks symmetric is not forced into it.
    """
    n = len(pts)
    nb = len(model["bases"])
    adj = adjacency(model, n)
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    lo_x, hi_x, lo_y, hi_y = min(xs), max(xs), min(ys), max(ys)

    perm = [-1] * n
    for i, (x, y) in enumerate(pts):
        tx, ty = flip(x, y, lo_x, hi_x, lo_y, hi_y)
        best, bestd = -1, None
        for j in range(n):
            # A base can only pair with a base, an H.Q. only with an H.Q.
            if (i < nb) != (j < nb):
                continue
            d = math.hypot(pts[j][0] - tx, pts[j][1] - ty)
            if bestd is None or d < bestd:
                best, bestd = j, d
        perm[i] = best

    if sorted(perm) != list(range(n)):
        return None  # not a permutation, so not a symmetry
    for i in range(n):
        if {perm[k] for k in adj[i]} != adj[perm[i]]:
            return None  # positions line up but the paths do not
    return perm


def symmetrise(model, pts):
    """Average each slot with its mirror and its 180-degree partner.

    This is the repair the trace actually needs. It keeps every bit of the
    trace's reach into the corners and only removes the wobble.
    """
    n = len(pts)
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    lo_x, hi_x, lo_y, hi_y = min(xs), max(xs), min(ys), max(ys)
    out = list(pts)
    applied = []

    mirror = find_symmetry(model, out, lambda x, y, lx, hx, ly, hy: (lx + hx - x, y))
    if mirror:
        applied.append("mirror")
        out = [
            (
                (out[i][0] + (lo_x + hi_x - out[mirror[i]][0])) / 2.0,
                (out[i][1] + out[mirror[i]][1]) / 2.0,
            )
            for i in range(n)
        ]

    turn = find_symmetry(
        model, out, lambda x, y, lx, hx, ly, hy: (lx + hx - x, ly + hy - y)
    )
    if turn:
        applied.append("half-turn")
        out = [
            (
                (out[i][0] + (lo_x + hi_x - out[turn[i]][0])) / 2.0,
                (out[i][1] + (lo_y + hi_y - out[turn[i]][1])) / 2.0,
            )
            for i in range(n)
        ]
    return out, applied


# --- shaping ----------------------------------------------------------------


def equalise(model, pts, weight, rounds, leash):
    """Nudge region areas toward each other, on a leash back to the start.

    The leash is the whole point. Let the area term run free and it becomes the
    centre-heavy blob again; bounded, it straightens the areas while the trace's
    shape survives.
    """
    if not weight:
        return pts
    anchor = list(pts)
    regions = [r["bases"] for r in model["regions"]]
    pinned = {len(model["bases"]) + i for i in range(len(model["hqs"]))}
    out = list(pts)

    for _ in range(rounds):
        areas = [region_area(out, m) for m in regions]
        mean = sum(areas) / len(areas)
        push = [(0.0, 0.0)] * len(out)
        for members, area in zip(regions, areas):
            cx = sum(out[i][0] for i in members) / len(members)
            cy = sum(out[i][1] for i in members) / len(members)
            gain = (mean - area) / mean * weight
            for i in members:
                dx, dy = out[i][0] - cx, out[i][1] - cy
                d = math.hypot(dx, dy) or 0.001
                px, py = push[i]
                push[i] = (px + dx / d * gain, py + dy / d * gain)

        for i in range(len(out)):
            if i in pinned:
                continue
            nx, ny = out[i][0] + push[i][0], out[i][1] + push[i][1]
            # Back onto the leash if it has wandered too far from the trace.
            dx, dy = nx - anchor[i][0], ny - anchor[i][1]
            d = math.hypot(dx, dy)
            if d > leash:
                nx = anchor[i][0] + dx / d * leash
                ny = anchor[i][1] + dy / d * leash
            out[i] = (nx, ny)
    return out


def from_scratch(model, rounds=200, grid=26, spring=0.45, seed=11):
    """Lay the board out with no trace at all: the only inputs are which H.Q. is
    near and which is far, exactly as Mario proposed.

    The ingredient the earlier attempts were missing is a term that rewards
    COVERING the rectangle. Springs and repulsion both pull inward, so their
    equilibrium is a blob whatever the weights, and scaling it up afterwards
    just makes a bigger blob.

    Lloyd relaxation is the direct answer to "spread these points over this
    rectangle": sample the box, give each sample to its nearest slot, move each
    slot to the mean of the samples it won. Corners get used because a corner
    sample has to belong to somebody. Springs are interleaved so slots joined by
    a path stay near each other, and the H.Q. are pinned.
    """
    import random

    rng = random.Random(seed)
    n = len(slots(model))
    nb = len(model["bases"])
    edges = model["edges"]
    degree = [0] * n
    for a, b in edges:
        degree[a] += 1
        degree[b] += 1

    pinned = {}
    for i, q in enumerate(model["hqs"]):
        pinned[nb + i] = (BOX_W / 2.0, BOX_H - 24.0 if q["seat"] == 0 else 24.0)

    pts = [(rng.uniform(40, BOX_W - 40), rng.uniform(40, BOX_H - 40)) for _ in range(n)]
    for i, pos in pinned.items():
        pts[i] = pos

    samples = [
        (x + grid / 2.0, y + grid / 2.0)
        for x in range(0, int(BOX_W), grid)
        for y in range(0, int(BOX_H), grid)
    ]

    for step in range(rounds):
        total = [(0.0, 0.0)] * n
        count = [0] * n
        for sx, sy in samples:
            best, bestd = 0, None
            for i, (px, py) in enumerate(pts):
                d = (px - sx) ** 2 + (py - sy) ** 2
                if bestd is None or d < bestd:
                    best, bestd = i, d
            tx, ty = total[best]
            total[best] = (tx + sx, ty + sy)
            count[best] += 1
        moved = list(pts)
        for i in range(n):
            if count[i]:
                moved[i] = (total[i][0] / count[i], total[i][1] / count[i])

        pull = [(0.0, 0.0)] * n
        for a, b in edges:
            dx, dy = moved[b][0] - moved[a][0], moved[b][1] - moved[a][1]
            pull[a] = (pull[a][0] + dx * spring, pull[a][1] + dy * spring)
            pull[b] = (pull[b][0] - dx * spring, pull[b][1] - dy * spring)

        blend = 0.5 * (1.0 - step / float(rounds)) + 0.08
        for i in range(n):
            if i in pinned:
                pts[i] = pinned[i]
                continue
            deg = max(1, degree[i])
            nx = moved[i][0] + pull[i][0] / deg * blend
            ny = moved[i][1] + pull[i][1] / deg * blend
            pts[i] = (min(BOX_W - 20, max(20, nx)), min(BOX_H - 20, max(20, ny)))

    out, _ = symmetrise(model, pts)
    return out


def regions_repel(model, pts, rounds=400, push=1.0, hq_push=1.4, spring=0.05):
    """Mario's formulation: the regions shove each other apart, and the H.Q.
    shove too but never move.

    The earlier area term treated each region alone -- grow if small, shrink if
    large -- which says nothing about where a region should BE, so regions
    drifted together and the board bunched. Repulsion is the missing half: a
    region is pushed off its neighbours, so they end up sharing the box instead
    of competing for the middle of it.

    The bodies are the regions; the bases follow, each carried by the regions it
    fences. A light spring on the paths keeps a base that fences nothing from
    drifting away.
    """
    nb = len(model["bases"])
    hqs = [pts[nb + i] for i in range(len(model["hqs"]))]
    regions = [r["bases"] for r in model["regions"]]
    edges = model["edges"]
    out = list(pts)
    # What a path should measure if the bases were spread over the whole box.
    rest = math.sqrt(BOX_W * BOX_H / max(1, nb)) * 0.95

    for step in range(rounds):
        cool = 1.0 - step / float(rounds)
        force = [(0.0, 0.0)] * len(out)

        centres = []
        for members in regions:
            cx = sum(out[i][0] for i in members) / len(members)
            cy = sum(out[i][1] for i in members) / len(members)
            centres.append((cx, cy))

        # Region against region.
        for a in range(len(regions)):
            for b in range(a + 1, len(regions)):
                dx = centres[a][0] - centres[b][0]
                dy = centres[a][1] - centres[b][1]
                d = math.hypot(dx, dy) or 0.001
                # Bigger regions shove harder, which is what evens them out:
                # a fat region claims room until its neighbours push back.
                mag = push * (BOX_W + BOX_H) / (d + 40.0)
                ux, uy = dx / d, dy / d
                for i in regions[a]:
                    fx, fy = force[i]
                    force[i] = (fx + ux * mag / len(regions[a]), fy + uy * mag / len(regions[a]))
                for i in regions[b]:
                    fx, fy = force[i]
                    force[i] = (fx - ux * mag / len(regions[b]), fy - uy * mag / len(regions[b]))

        # The H.Q. push and never move.
        for hx, hy in hqs:
            for i in range(nb):
                dx, dy = out[i][0] - hx, out[i][1] - hy
                d = math.hypot(dx, dy) or 0.001
                mag = hq_push * (BOX_W + BOX_H) / (d + 40.0)
                fx, fy = force[i]
                force[i] = (fx + dx / d * mag, fy + dy / d * mag)

        # A tether along the paths, pulling toward a REST LENGTH rather than
        # toward zero. Without the rest length this is pure contraction: the
        # board collapses and the repulsion spends itself fighting it, which is
        # exactly what the first sweep measured.
        for a, b in edges:
            if a >= nb or b >= nb:
                continue
            dx, dy = out[b][0] - out[a][0], out[b][1] - out[a][1]
            d = math.hypot(dx, dy) or 0.001
            pull = (d - rest) * spring
            ux, uy = dx / d, dy / d
            fx, fy = force[a]
            force[a] = (fx + ux * pull, fy + uy * pull)
            fx, fy = force[b]
            force[b] = (fx - ux * pull, fy - uy * pull)

        for i in range(nb):
            sx = max(-10.0, min(10.0, force[i][0])) * cool
            sy = max(-10.0, min(10.0, force[i][1])) * cool
            out[i] = (
                min(BOX_W - 18, max(18, out[i][0] + sx)),
                min(BOX_H - 18, max(18, out[i][1] + sy)),
            )

    done, _ = symmetrise(model, out)
    return done


def regions_pack(model, pts, rounds=500, pack=0.55, wall=0.5, spring=0.10, tidy=0.25):
    """Every region shoves every other, and the H.Q. shove without moving.

    The previous attempt applied the shove to each region's member bases, and a
    base shared by two regions then received both pushes and cancelled them --
    so the strongest interaction on the board, between neighbouring regions, was
    the one being nulled. Here the force acts on the region CENTRES and the
    bases follow, so nothing cancels.

    Each region is treated as a disc of the radius it would have if the box were
    shared out equally between them. Two discs closer than the sum of their
    radii push apart by the overlap, and the walls push inward. That is circle
    packing, and packing equal circles into a rectangle is exactly "equal areas,
    whole board used" stated as a physics problem.
    """
    nb = len(model["bases"])
    hqs = [pts[nb + i] for i in range(len(model["hqs"]))]
    regions = [r["bases"] for r in model["regions"]]
    edges = [(a, b) for a, b in model["edges"] if a < nb and b < nb]
    out = list(pts)

    # The radius each region would have with the box shared out equally.
    target = math.sqrt(BOX_W * BOX_H / (len(regions) * math.pi))
    rest = math.sqrt(BOX_W * BOX_H / max(1, nb)) * 0.95
    # An H.Q. is an obstacle of about a region's size, so the board opens out
    # around it instead of piling onto it.
    hq_radius = target * 0.8

    for step in range(rounds):
        cool = 1.0 - 0.7 * step / float(rounds)
        centres = []
        for members in regions:
            cx = sum(out[i][0] for i in members) / len(members)
            cy = sum(out[i][1] for i in members) / len(members)
            centres.append((cx, cy))

        shove = [(0.0, 0.0)] * len(regions)

        # Every region against every other. No exceptions, no falloff to zero:
        # two regions overlapping by a lot push by a lot.
        for a in range(len(regions)):
            for b in range(a + 1, len(regions)):
                dx = centres[a][0] - centres[b][0]
                dy = centres[a][1] - centres[b][1]
                d = math.hypot(dx, dy) or 0.001
                overlap = target * 2.0 - d
                if overlap <= 0:
                    continue
                ux, uy = dx / d, dy / d
                mag = overlap * pack * 0.5
                shove[a] = (shove[a][0] + ux * mag, shove[a][1] + uy * mag)
                shove[b] = (shove[b][0] - ux * mag, shove[b][1] - uy * mag)

        # The H.Q., static but solid.
        for hx, hy in hqs:
            for a in range(len(regions)):
                dx, dy = centres[a][0] - hx, centres[a][1] - hy
                d = math.hypot(dx, dy) or 0.001
                overlap = target + hq_radius - d
                if overlap <= 0:
                    continue
                shove[a] = (shove[a][0] + dx / d * overlap * pack,
                            shove[a][1] + dy / d * overlap * pack)

        # The walls, pushing any region that has drifted outside back in.
        for a in range(len(regions)):
            cx, cy = centres[a]
            fx, fy = shove[a]
            if cx < target:
                fx += (target - cx) * wall
            if cx > BOX_W - target:
                fx -= (cx - (BOX_W - target)) * wall
            if cy < target:
                fy += (target - cy) * wall
            if cy > BOX_H - target:
                fy -= (cy - (BOX_H - target)) * wall
            shove[a] = (fx, fy)

        # Bases follow the regions they fence, by the average of their shoves.
        force = [(0.0, 0.0)] * len(out)
        share = [0] * len(out)
        for a, members in enumerate(regions):
            for i in members:
                force[i] = (force[i][0] + shove[a][0], force[i][1] + shove[a][1])
                share[i] += 1
        for i in range(nb):
            if share[i]:
                force[i] = (force[i][0] / share[i], force[i][1] / share[i])

        # Paths keep their length, so a base fencing nothing still travels with
        # its neighbours and the graph does not tear.
        for a, b in edges:
            dx, dy = out[b][0] - out[a][0], out[b][1] - out[a][1]
            d = math.hypot(dx, dy) or 0.001
            pull = (d - rest) * spring
            ux, uy = dx / d, dy / d
            force[a] = (force[a][0] + ux * pull, force[a][1] + uy * pull)
            force[b] = (force[b][0] - ux * pull, force[b][1] - uy * pull)

        # And a light pull of each base toward the middle of the regions it
        # fences, which is what turns a shoved region into a round one.
        for a, members in enumerate(regions):
            for i in members:
                dx = centres[a][0] - out[i][0]
                dy = centres[a][1] - out[i][1]
                d = math.hypot(dx, dy) or 0.001
                slack = d - target
                force[i] = (force[i][0] + dx / d * slack * tidy / max(1, share[i]),
                            force[i][1] + dy / d * slack * tidy / max(1, share[i]))

        for i in range(nb):
            sx = max(-9.0, min(9.0, force[i][0])) * cool
            sy = max(-9.0, min(9.0, force[i][1])) * cool
            out[i] = (min(BOX_W - 16, max(16, out[i][0] + sx)),
                      min(BOX_H - 16, max(16, out[i][1] + sy)))

    done, _ = symmetrise(model, out)
    return done


def fit(pts, stretch=True, inset=26.0):
    """Scale to the play box. Stretched by default: the board is a graph, not a
    picture, so nothing in it can be made wrong by using both axes fully."""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    w = (max(xs) - min(xs)) or 1.0
    h = (max(ys) - min(ys)) or 1.0
    tw, th = BOX_W - inset * 2, BOX_H - inset * 2
    kx = tw / w if stretch else min(tw / w, th / h)
    ky = th / h if stretch else kx
    ox = inset + (tw - w * kx) / 2.0 - min(xs) * kx
    oy = inset + (th - h * ky) / 2.0 - min(ys) * ky
    return [(p[0] * kx + ox, p[1] * ky + oy) for p in pts]


# --- measuring --------------------------------------------------------------


def hull_area(pts):
    """Convex hull area: the honest measure of how much board is actually used.

    The bounding box is not, which is how the last round scored a centre-heavy
    layout at 100% fill.
    """
    p = sorted(set((round(x, 3), round(y, 3)) for x, y in pts))
    if len(p) < 3:
        return 0.0

    def half(points):
        out = []
        for pt in points:
            while len(out) >= 2:
                (x1, y1), (x2, y2) = out[-2], out[-1]
                if (x2 - x1) * (pt[1] - y1) - (y2 - y1) * (pt[0] - x1) <= 0:
                    out.pop()
                else:
                    break
            out.append(pt)
        return out

    hull = half(p)[:-1] + half(p[::-1])[:-1]
    total = 0.0
    for i, (x1, y1) in enumerate(hull):
        x2, y2 = hull[(i + 1) % len(hull)]
        total += x1 * y2 - x2 * y1
    return abs(total) / 2.0


def corner_reach(pts):
    """How close the four corners of the box come to having a slot near them.

    Reported as a percentage where 100 means a slot sits right in each corner.
    This is the number that says what Mario could see and the last metric could
    not.
    """
    worst = 0.0
    for cx, cy in ((0, 0), (BOX_W, 0), (0, BOX_H), (BOX_W, BOX_H)):
        worst = max(worst, min(math.hypot(x - cx, y - cy) for x, y in pts))
    diag = math.hypot(BOX_W, BOX_H) / 2.0
    return max(0.0, 1.0 - worst / diag)


def asymmetry(model, pts):
    """Mean mismatch, in pixels, between a slot and its mirror partner."""
    mirror = find_symmetry(model, pts, lambda x, y, lx, hx, ly, hy: (lx + hx - x, y))
    if not mirror:
        return None
    xs = [p[0] for p in pts]
    lo, hi = min(xs), max(xs)
    return sum(
        math.hypot(
            pts[i][0] - (lo + hi - pts[mirror[i]][0]), pts[i][1] - pts[mirror[i]][1]
        )
        for i in range(len(pts))
    ) / len(pts)


def crossings(model, pts):
    def side(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    edges, total = model["edges"], 0
    for i in range(len(edges)):
        a1, a2 = edges[i]
        for j in range(i + 1, len(edges)):
            b1, b2 = edges[j]
            if len({a1, a2, b1, b2}) < 4:
                continue
            p, q, r, s = pts[a1], pts[a2], pts[b1], pts[b2]
            if (side(p, q, r) > 0) != (side(p, q, s) > 0) and (side(r, s, p) > 0) != (
                side(r, s, q) > 0
            ):
                total += 1
    return total


def report(model, pts):
    areas = [region_area(pts, r["bases"]) for r in model["regions"]]
    mean = sum(areas) / len(areas)
    off = asymmetry(model, pts)
    return {
        "spread": max(areas) / max(1e-6, min(areas)),
        "cv": (sum((a - mean) ** 2 for a in areas) / len(areas)) ** 0.5 / mean,
        "hull": hull_area(pts) / (BOX_W * BOX_H),
        "corners": corner_reach(pts),
        "nearest": min(
            math.hypot(pts[i][0] - pts[j][0], pts[i][1] - pts[j][1])
            for i in range(len(pts))
            for j in range(i + 1, len(pts))
        ),
        "crossings": crossings(model, pts),
        "skew": off,
    }


# --- drawing ----------------------------------------------------------------


def panel(model, pts, title, stats, ox, oy):
    nb = len(model["bases"])
    out = [f'<g transform="translate({ox},{oy})">']
    out.append(
        f'<rect width="{BOX_W}" height="{BOX_H}" fill="#fff" stroke="#bbb" stroke-dasharray="4 4"/>'
    )

    for r in model["regions"]:
        ring = [pts[i] for i in r["bases"]]
        cx = sum(p[0] for p in ring) / len(ring)
        cy = sum(p[1] for p in ring) / len(ring)
        if len(ring) > 2:
            ring.sort(key=lambda p: math.atan2(p[1] - cy, p[0] - cx))
            pathd = " ".join(f"{x:.0f},{y:.0f}" for x, y in ring)
            out.append(
                f'<polygon points="{pathd}" fill="#2f6fdb" fill-opacity="0.14" stroke="#2f6fdb" '
                f'stroke-opacity="0.35"/>'
            )
        else:
            (ax, ay), (bx, by) = ring
            out.append(
                f'<line x1="{ax:.0f}" y1="{ay:.0f}" x2="{bx:.0f}" y2="{by:.0f}" stroke="#2f6fdb" '
                f'stroke-width="16" stroke-opacity="0.18" stroke-linecap="round"/>'
            )
        out.append(
            f'<text x="{cx:.0f}" y="{cy + 4:.0f}" font-size="11" fill="#2f6fdb" text-anchor="middle" '
            f'font-family="monospace">{r["medals"]}</text>'
        )

    for a, b in model["edges"]:
        out.append(
            f'<line x1="{pts[a][0]:.0f}" y1="{pts[a][1]:.0f}" x2="{pts[b][0]:.0f}" y2="{pts[b][1]:.0f}" '
            f'stroke="#111" stroke-width="3"/>'
        )

    for i, (x, y) in enumerate(pts):
        if i >= nb:
            out.append(f'<circle cx="{x:.0f}" cy="{y:.0f}" r="17" fill="#111"/>')
            out.append(
                f'<text x="{x:.0f}" y="{y + 5:.0f}" font-size="12" fill="#fff" text-anchor="middle" '
                f'font-family="monospace">HQ</text>'
            )
        else:
            out.append(
                f'<rect x="{x - 16:.0f}" y="{y - 16:.0f}" width="32" height="32" rx="7" fill="#fff" '
                f'stroke="#111" stroke-width="3"/>'
            )
            out.append(
                f'<text x="{x:.0f}" y="{y + 5:.0f}" font-size="12" fill="#111" text-anchor="middle" '
                f'font-family="monospace">{i}</text>'
            )

    skew = (
        "exact"
        if stats["skew"] is not None and stats["skew"] < 0.5
        else (f"{stats['skew']:.0f}px off" if stats["skew"] is not None else "none")
    )
    out.append(
        f'<text x="0" y="-32" font-size="17" font-family="monospace" font-weight="bold">{title}</text>'
    )
    out.append(
        f'<text x="0" y="-15" font-size="12" font-family="monospace" fill="#555">'
        f"areas {stats['spread']:.1f}x spread &#183; {stats['cv'] * 100:.0f}% variation &#183; "
        f"board used {stats['hull'] * 100:.0f}% &#183; corners {stats['corners'] * 100:.0f}%</text>"
    )
    out.append(
        f'<text x="0" y="1" font-size="12" font-family="monospace" fill="#555">'
        f"symmetry {skew} &#183; closest pair {stats['nearest']:.0f}px &#183; "
        f"crossings {stats['crossings']}</text>"
    )
    out.append("</g>")
    return "\n".join(out)


def candidates(model):
    """The shortlist, after several formulations were tried and dropped.

    What survives: the trace as drawn, the trace repaired, the trace with its
    areas forced level, and the packing that beat all of them.
    """
    raw = [(x / 1000.0 * BOX_W, y / 1000.0 * BOX_H) for x, y in slots(model)]
    sym, _ = symmetrise(model, raw)
    filled = fit(sym, stretch=True)
    return [
        ("AS TRACED", fit(raw, stretch=False)),
        ("SYMMETRISED + FILL", filled),
        ("SYM + AREAS, LEASHED", fit(equalise(model, sym, 4.0, 200, leash=45.0), stretch=True)),
        # Mario's idea, once the cancellation was fixed and the regions were
        # modelled as discs to be packed rather than points to be repelled.
        ("REGIONS PACK", fit(regions_pack(model, filled, pack=0.9, spring=0.12, tidy=0.25), stretch=True)),
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("json")
    ap.add_argument("-o", "--out", default="/tmp/balance.svg")
    args = ap.parse_args()
    model = load(args.json)

    cands = candidates(model)
    gap = 80
    width = int(len(cands) * (BOX_W + gap) + gap)
    height = int(BOX_H + 220)
    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}"><rect width="{width}" height="{height}" fill="#f6f7f9"/>'
    ]
    for i, (label, pts) in enumerate(cands):
        stats = report(model, pts)
        svg.append(panel(model, pts, label, stats, gap + i * (BOX_W + gap), 130))
        print(
            f"{label:24s} areas {stats['spread']:5.1f}x  var {stats['cv'] * 100:4.0f}%  "
            f"used {stats['hull'] * 100:3.0f}%  corners {stats['corners'] * 100:3.0f}%  "
            f"sym {'exact' if stats['skew'] is not None and stats['skew'] < 0.5 else stats['skew']}  "
            f"cross {stats['crossings']}",
            file=sys.stderr,
        )
    svg.append(
        f'<text x="{gap}" y="56" font-size="22" font-family="monospace" font-weight="bold">'
        f"{model['name']} &#183; layout candidates</text>"
    )
    svg.append(
        f'<text x="{gap}" y="82" font-size="13" font-family="monospace" fill="#555">'
        f"Each box is the play area on a 480x800 panel. Shaded = a region, number = medals it pays. "
        f'"Board used" is the convex hull, not the bounding box.</text>'
    )
    svg.append("</svg>")

    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write("\n".join(svg))
    print(args.out)


if __name__ == "__main__":
    main()
