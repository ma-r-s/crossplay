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
    w("  };")
    w("  const R regions[] = {")
    for r in model["regions"]:
        mask = " | ".join(f"(1u << {m})" for m in sorted(r["bases"]))
        w(f"      {{{mask}, {r['medals']}}},")
    w("  };")
    w("  t.regionCount = static_cast<uint8_t>(sizeof(regions) / sizeof(regions[0]));")
    w("  for (int i = 0; i < t.regionCount; ++i) {")
    w("    t.regions[i].bases = regions[i].bases;")
    w("    t.regions[i].medals = regions[i].medals;")
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
