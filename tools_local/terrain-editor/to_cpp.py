#!/usr/bin/env python3
"""Turn a terrain traced in the editor into the firmware's terrain source.

    tools_local/terrain-editor/to_cpp.py castle-field.json > /tmp/out.cpp
    tools_local/terrain-editor/to_cpp.py --check castle-field.json

The point of this script is that nothing gets hand-transcribed. A board arrives
as JSON from tools_local/terrain-editor/index.html and becomes a
`constexpr Terrain buildX()` with no human copying indices between the two,
which is exactly where a wrong path would come from.

--check runs the structural rules the editor shows and
host-tests/toybattle/test_toybattle.cpp asserts, and exits non-zero on the first
failure. Run it before pasting anything into the tree.
"""

import argparse
import json
import re
import sys

SPECIAL = {
    "none": "None",
    "recall": "Recall",
    "draw": "Draw",
    "shove": "Shove",
    "exhume": "Exhume",
    "suppress": "Suppress",
    "gate": "Gate",
    "nullify": "Nullify",
}

# The editor writes gate values as printed on the board; the firmware wants a
# bitmask over Troop, where the joker is Kwak and sits at bit 0.
TROOP_BIT = {"joker": 0, "1": 1, "2": 2, "3": 3, "4": 4, "5": 5, "6": 6, "7": 7}
TROOP_NAME = ["Kwak", "Skully", "Capn", "Jumbo", "Hook", "XB42", "Star", "Roxy"]

MAX_BASES, MAX_HQ, MAX_REGIONS, MAX_EDGES = 32, 4, 16, 72




def align(values, tol=20):
    """Snap near-equal coordinates onto one shared level.

    A hand trace puts a column at 0, 8, 13, 13, 16 when it means one column.
    Anything within `tol` of a running cluster joins it and the whole cluster
    takes its mean, so rows line up and columns line up.

    The tolerance is in normalised units and belongs to the BOARD, not to this
    function, because no global number can work. Castle Field's wells sit 35
    units inside its corner bases and must stay there; Volcanic Jungle's centre
    column is 40 units wide and is all one column. The real distinction on one
    board is smaller than the hand jitter on another, so the two ranges overlap
    and any single tolerance is wrong for somebody. Boards declare
    "alignTolerance"; 20 is the default because it is right for most.
    """
    order = sorted(range(len(values)), key=lambda i: values[i])
    out = list(values)
    cluster = [order[0]]
    for i in order[1:]:
        if values[i] - values[cluster[-1]] <= tol:
            cluster.append(i)
            continue
        mean = round(sum(values[j] for j in cluster) / len(cluster))
        for j in cluster:
            out[j] = mean
        cluster = [i]
    mean = round(sum(values[j] for j in cluster) / len(cluster))
    for j in cluster:
        out[j] = mean
    return out


def symmetrise(values):
    """Mirror the levels about the midline, so the board is actually symmetric.

    Run after align(), on levels rather than on points: each level is paired
    with the one the same distance from the other end and both take the average
    of the two. A board with an odd number of levels pins its middle one to 500.
    """
    levels = sorted(set(values))
    n = len(levels)
    fixed = {}
    for i in range(n // 2):
        lo, hi = levels[i], levels[n - 1 - i]
        mean = (lo + (1000 - hi)) / 2
        fixed[lo] = round(mean)
        fixed[hi] = round(1000 - mean)
    if n % 2:
        fixed[levels[n // 2]] = 500
    return [fixed[v] for v in values]


def order_ring(points):
    """The fence bases in order around the region, so they form a polygon."""
    cx = sum(p[0] for p in points) / len(points)
    cy = sum(p[1] for p in points) / len(points)
    import math

    return sorted(points, key=lambda p: math.atan2(p[1] - cy, p[0] - cx))


def medal_anchor(points):
    """Where a region's medals go: the roomiest spot inside it.

    The centre of the fence bases is the obvious answer and it is wrong in the
    same way for every thin region -- a triangle of two column bases and one
    centre base puts its centroid a third of the way across, hard against the
    column, and a flat quad puts it right between the two bases that pinch it.
    What reads as centred is the point furthest from anything drawn, which is
    the pole of inaccessibility: maximise the distance to the nearest fence base
    and to the nearest path between them.

    Baked here rather than computed on the device, because it never changes
    once the board is authored.
    """
    import math

    ring = order_ring(points)
    n = len(ring)

    def inside(x, y):
        hit = False
        for i in range(n):
            ax, ay = ring[i]
            bx, by = ring[(i + 1) % n]
            if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
                hit = not hit
        return hit

    def clearance(x, y):
        best = min(math.hypot(x - px, y - py) for px, py in ring)
        for i in range(n):
            ax, ay = ring[i]
            bx, by = ring[(i + 1) % n]
            dx, dy = bx - ax, by - ay
            span = dx * dx + dy * dy
            t = 0.0 if span == 0 else max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / span))
            best = min(best, math.hypot(x - (ax + t * dx), y - (ay + t * dy)))
        return best

    xs = [p[0] for p in ring]
    ys = [p[1] for p in ring]
    # A rectangular region has a whole LINE of equally roomy points, not one, so
    # "the roomiest" does not pick a spot by itself -- the scan just keeps the
    # first it meets, and on a point-symmetric board the two halves are scanned
    # in different orders and disagree. Ties go to the point nearest the middle,
    # which is both symmetric and where the eye expects it.
    mx = sum(xs) / n
    my = sum(ys) / n
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    best = None
    # Coarse, then three refinements each a quarter of the last window. Two
    # passes left the two halves of a point-symmetric board eleven units apart,
    # which is five pixels on the panel and visible -- the search has to
    # converge tighter than the eye, not merely land in the right region.
    for _ in range(4):
        steps = 24
        for i in range(steps + 1):
            for j in range(steps + 1):
                x = x0 + (x1 - x0) * i / steps
                y = y0 + (y1 - y0) * j / steps
                if not inside(x, y):
                    continue
                c = clearance(x, y)
                if c <= 0:
                    continue
                pull = -math.hypot(x - mx, y - my)
                # Half a unit of clearance is well under a pixel on the panel,
                # so anything inside that counts as a tie.
                if best is None or c > best[2] + 0.5 or (c > best[2] - 0.5 and pull > best[3]):
                    best = (x, y, c, pull)
        if best is None:
            break
        wx, wy = (x1 - x0) / 4.0, (y1 - y0) / 4.0
        x0, x1 = best[0] - wx, best[0] + wx
        y0, y1 = best[1] - wy, best[1] + wy

    if best is None:
        return (round(sum(xs) / n), round(sum(ys) / n))
    return (round(best[0]), round(best[1]))


def normalise(values):
    """Spread `values` across 0..1000, preserving their relative spacing."""
    low, high = min(values), max(values)
    span = high - low
    if span <= 0:
        return [0 for _ in values]
    return [round((v - low) * 1000 / span) for v in values]


def check(model):
    """Every rule the firmware asserts about a terrain. Returns a list of errors."""
    errs = []
    bases, hqs = model.get("bases", []), model.get("hqs", [])
    edges, regions = model.get("edges", []), model.get("regions", [])
    nb, nh = len(bases), len(hqs)
    slots = nb + nh

    if not 1 <= nb <= MAX_BASES:
        errs.append(f"{nb} bases; the firmware allows 1..{MAX_BASES}")
    if not 1 <= nh <= MAX_HQ:
        errs.append(f"{nh} H.Q.; the firmware allows 1..{MAX_HQ}")
    if len(edges) > MAX_EDGES:
        errs.append(f"{len(edges)} paths; the firmware allows {MAX_EDGES}")
    if len(regions) > MAX_REGIONS:
        errs.append(f"{len(regions)} regions; the firmware allows {MAX_REGIONS}")
    if not model.get("objective", 0) > 0:
        errs.append("objective must be at least 1")

    seats = {q.get("seat") for q in hqs}
    if seats != {0, 1}:
        errs.append(f"both seats need an H.Q.; found seats {sorted(seats)}")

    seen_edges = set()
    for a, b in edges:
        if not (0 <= a < slots and 0 <= b < slots):
            errs.append(f"path ({a},{b}) names a slot that does not exist")
            continue
        if a == b:
            errs.append(f"path ({a},{b}) joins a slot to itself")
        key = (min(a, b), max(a, b))
        if key in seen_edges:
            errs.append(f"path ({a},{b}) is listed twice")
        seen_edges.add(key)

    # Nothing stranded: a slot no path reaches is a slot no troop can stand on.
    if slots:
        adj = {i: set() for i in range(slots)}
        for a, b in seen_edges:
            adj[a].add(b)
            adj[b].add(a)
        seen, stack = {0}, [0]
        while stack:
            for nxt in adj[stack.pop()]:
                if nxt not in seen:
                    seen.add(nxt)
                    stack.append(nxt)
        if len(seen) != slots:
            errs.append(f"{slots - len(seen)} slot(s) no path reaches")

    medals = 0
    for i, r in enumerate(regions):
        members = r.get("bases", [])
        if len(members) < 2:
            errs.append(f"region {i} is fenced by {len(members)} base(s); needs 2+")
        for m in members:
            if not 0 <= m < nb:
                errs.append(f"region {i} names slot {m}, which is not a base")
        if len(set(members)) != len(members):
            errs.append(f"region {i} names the same base twice")
        if r.get("medals", 0) <= 0:
            errs.append(f"region {i} pays no medals")
        medals += r.get("medals", 0)
    if medals < model.get("objective", 0):
        errs.append(
            f"{medals} medals on the board cannot reach an objective of {model['objective']}"
        )

    for i, b in enumerate(bases):
        kind = b.get("special", "none")
        if kind not in SPECIAL:
            errs.append(f"base {i} has an unknown special {kind!r}")
        if kind == "gate" and not b.get("gate"):
            errs.append(f"base {i} is a gate that admits nothing")
        if kind != "gate" and b.get("gate"):
            errs.append(f"base {i} lists gate values but is not a gate")
        for v in b.get("gate", []):
            if v not in TROOP_BIT:
                errs.append(f"base {i} admits {v!r}, which is not a troop")
    return errs


def ident(name):
    parts = re.split(r"[^A-Za-z0-9]+", name.strip().lower())
    return "".join(p.capitalize() for p in parts if p)


def emit(model):
    bases, hqs = model["bases"], model["hqs"]
    nb, nh = len(bases), len(hqs)
    name = model["name"]
    fn = "build" + ident(name)
    out = []
    w = out.append

    w(f"// {name}. Traced in tools_local/terrain-editor and generated by its")
    w("// to_cpp.py -- do not hand-edit, retrace and regenerate instead.")
    w("//")
    w("// Topology is the printed board's. The coordinates are a starting layout")
    w("// for the panel, not a copy of the artwork.")
    w(f"constexpr Terrain {fn}() {{")
    w("  Terrain t{};")
    w(f'  t.name = "{name}";')
    w(f"  t.baseCount = {nb};")
    w(f"  t.hqCount = {nh};")
    for i, q in enumerate(hqs):
        w(f"  t.hqSeat[{i}] = {q['seat']};")
    w(f"  t.medalsObjective = {model['objective']};")
    w("")

    # Normalised to fill 0..1000 in both axes, because the tracing zoom is not
    # part of the board. Castle Field happened to be traced edge to edge and
    # City of Clouds did not, and the difference showed up as one board using
    # the whole panel and the other sitting in a margin -- which is a property
    # of how the picture was loaded, not of the terrain.
    #
    # A single row or column normalises to 0, which is correct: there is nothing
    # to spread.
    xs = normalise([b["x"] for b in bases] + [q["x"] for q in hqs])
    ys = normalise([b["y"] for b in bases] + [q["y"] for q in hqs])

    # A hand trace does not put a column on one x. `align` snaps near-equal
    # coordinates onto a shared level so rows and columns line up, and
    # `symmetrise` mirrors those levels about the midline for a board that is
    # actually symmetric. Opt-in per board, because not every terrain is:
    # Caribbean Sea is deliberately lopsided, 2 H.Q. against 1.
    sym = model.get("symmetry", "none")
    if sym not in ("none", "horizontal", "vertical", "both"):
        raise SystemExit(f"unknown symmetry {sym!r}: none, horizontal, vertical or both")
    tol = int(model.get("alignTolerance", 20))
    if sym != "none":
        before = (sorted(set(xs)), sorted(set(ys)))
        xs, ys = align(xs, tol), align(ys, tol)
        after = (sorted(set(xs)), sorted(set(ys)))
        # Say what was merged. A snap that silently folds two real columns into
        # one is the failure mode here, and it is invisible unless it is
        # printed.
        print(
            f"  align(tol={tol}): x {len(before[0])}->{len(after[0])} levels, "
            f"y {len(before[1])}->{len(after[1])} levels",
            file=sys.stderr,
        )
    if sym in ("horizontal", "both"):
        xs = symmetrise(xs)
    if sym in ("vertical", "both"):
        ys = symmetrise(ys)
    w(f"  const uint16_t xs[{nb + nh}] = {{{', '.join(str(v) for v in xs)}}};")
    w(f"  const uint16_t ys[{nb + nh}] = {{{', '.join(str(v) for v in ys)}}};")
    w(f"  for (int i = 0; i < {nb + nh}; ++i) {{")
    w("    t.x[i] = xs[i];")
    w("    t.y[i] = ys[i];")
    w("  }")
    w("")

    edges = sorted({(min(a, b), max(a, b)) for a, b in model["edges"]})
    w("  const Edge edges[] = {")
    for i in range(0, len(edges), 6):
        chunk = ", ".join(f"{{{a}, {b}}}" for a, b in edges[i : i + 6])
        w(f"      {chunk},")
    w("  };")
    w("  t.edgeCount = static_cast<uint8_t>(sizeof(edges) / sizeof(edges[0]));")
    w("  for (int i = 0; i < t.edgeCount; ++i) t.edges[i] = edges[i];")
    w("")

    w("  struct R {")
    w("    uint32_t bases;")
    w("    uint8_t medals;")
    w("    uint16_t x;")
    w("    uint16_t y;")
    w("  };")
    w("  const R regions[] = {")
    for r in model["regions"]:
        mask = " | ".join(f"(1u << {m})" for m in sorted(r["bases"]))
        # Where the medals sit, worked out here rather than on the device: the
        # roomiest point inside the region, which is what reads as centred.
        # An H.Q. joined to two of the fence bases is part of the face even
        # though it can never be part of the mask -- an H.Q. is not a base, so
        # holding the region does not require it. Left out, the end regions of a
        # board like City of Clouds compute their anchor from a flat triangle
        # and land it exactly on the top edge of the centre base.
        ring = [(xs[m], ys[m]) for m in r["bases"]]
        for h in range(nb, nb + nh):
            joined = sum(1 for a, b in model["edges"] if (a == h and b in r["bases"]) or (b == h and a in r["bases"]))
            if joined >= 2:
                ring.append((xs[h], ys[h]))
        ax, ay = medal_anchor(ring)
        w(f"      {{{mask}, {r['medals']}, {ax}, {ay}}},")
    w("  };")
    w("  t.regionCount = static_cast<uint8_t>(sizeof(regions) / sizeof(regions[0]));")
    w("  for (int i = 0; i < t.regionCount; ++i) {")
    w("    t.regions[i].bases = regions[i].bases;")
    w("    t.regions[i].medals = regions[i].medals;")
    w("    t.regions[i].x = regions[i].x;")
    w("    t.regions[i].y = regions[i].y;")
    w("  }")

    specials = [
        (i, b) for i, b in enumerate(bases) if b.get("special", "none") != "none"
    ]
    if specials:
        w("")
        for i, b in specials:
            w(
                f"  t.special[{i}] = static_cast<uint8_t>(Special::{SPECIAL[b['special']]});"
            )
            if b["special"] == "gate":
                bits = sorted(TROOP_BIT[v] for v in b["gate"])
                expr = " | ".join(
                    f"(1u << static_cast<int>(Troop::{TROOP_NAME[x]}))" for x in bits
                )
                w(f"  t.gate[{i}] = static_cast<uint8_t>({expr});")
    w("  return t;")
    w("}")
    w("")
    w(f"const Terrain k{ident(name)} = withAdjacency({fn}());")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("json", help="a board exported from the terrain editor")
    ap.add_argument("--check", action="store_true", help="validate only, emit nothing")
    args = ap.parse_args()

    with open(args.json, encoding="utf-8") as fh:
        model = json.load(fh)

    errs = check(model)
    if errs:
        print(
            f"{model.get('name', args.json)}: {len(errs)} problem(s)", file=sys.stderr
        )
        for e in errs:
            print(f"  - {e}", file=sys.stderr)
        return 1

    medals = sum(r["medals"] for r in model["regions"])
    print(
        f"{model['name']}: {len(model['bases'])} bases, {len(model['hqs'])} H.Q., "
        f"{len(model['edges'])} paths, {len(model['regions'])} regions, "
        f"{medals} medals, objective {model['objective']}",
        file=sys.stderr,
    )
    if not args.check:
        print(emit(model))
    return 0


if __name__ == "__main__":
    sys.exit(main())
