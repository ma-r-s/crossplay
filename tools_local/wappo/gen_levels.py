#!/usr/bin/env python3
"""Generate Wappo's 125 levels into src/apps_local/wappo/WappoLevels.h.

    python3 tools_local/wappo/gen_levels.py            # all levels, 4 workers
    python3 tools_local/wappo/gen_levels.py --check    # re-solve the committed file
    python3 tools_local/wappo/gen_levels.py --avoid other.h  # reject boards like another set

Every level is searched for, not designed: start from a random board, then keep
mutating it (move a wall, a pit, a monster, the start or the exit) while the
optimal solution gets closer to that level's target par. A board is kept only
when it is solvable, its optimum is the target par (within PAR_SLACK), and the
monsters matter -- the par is well above the plain walking distance to the exit.
The par stored is the optimum the solver found, so par is always reachable and
never beatable.

The rules are the game's (WappoCore.cpp): each player step, every free monster
takes two steps (three once merged), sideways towards the player first and up or
down only when a wall blocks that; a monster entering a pit is stuck for three
turns; two monsters on one square merge into one that ignores pits.

Deterministic: the same seed gives the same file.
"""
import argparse
import multiprocessing
import os
import random
import re
import sys
from collections import deque

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "src", "apps_local", "wappo", "WappoLevels.h")
SEED = 20261007

# Target par per level -- the original game's difficulty curve, as numbers only:
# a slow climb from 20 to the low 30s with a long "spike" level every so often.
# No layout is taken from it; every board below is searched for from scratch.
TARGET_PAR = [
    20, 20, 21, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 24, 20, 20, 27, 21, 21, 21,
    21, 21, 21, 21, 21, 21, 21, 36, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 42, 27, 22, 22, 22, 22,
    22, 23, 22, 22, 22, 22, 22, 22, 22, 49, 22, 23, 23, 23, 23, 23, 23, 23, 52, 23, 23, 23, 23, 23, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 58, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26,
    26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27, 28, 28, 29, 29, 29, 23, 30, 30, 30, 31, 33, 63,
]
TWO_MONSTERS = set(range(11, 125, 3))  # about a third of the levels, from level 12 on
PAR_SLACK = 1  # how far from the target an optimum may land (spikes get 2)
MONSTER_MARGIN = 8  # par must beat the monster-free walk to the exit by this much
MAX_WALL_OVERLAP = 0.35  # with --avoid: largest shared share of walls, under any mirror

UP, DOWN, LEFT, RIGHT = 0, 1, 2, 3
DELTA = (-6, 6, -1, 1)
PERIMETER = [c for c in range(36) if c < 6 or c >= 30 or c % 6 in (0, 5)]
ALL_WALLS = [(c, c + 1) for c in range(36) if c % 6 != 5] + [(c, c + 6) for c in range(30)]


class Board:
    __slots__ = ("walls", "pits", "exit", "player", "monsters")

    def __init__(self, walls, pits, exit_, player, monsters):
        self.walls, self.pits, self.exit, self.player, self.monsters = walls, pits, exit_, player, monsters

    def key(self):
        return (tuple(sorted(self.walls)), tuple(sorted(self.pits)), self.exit, self.player, tuple(self.monsters))


def open_sides(b):
    """open[c][d]: can a piece leave cell c in direction d (the game's CellState)."""
    op = [[c >= 6, c < 30, c % 6 != 0, c % 6 != 5] for c in range(36)]
    for a, c in b.walls:
        if c - a == 6:
            op[a][DOWN] = op[c][UP] = False
        else:
            op[a][RIGHT] = op[c][LEFT] = False
    e = b.exit
    if e % 6 == 0:
        op[e][LEFT] = True
    elif e % 6 == 5:
        op[e][RIGHT] = True
    elif e < 6:
        op[e][UP] = True
    else:
        op[e][DOWN] = True
    return op


def chase(op, pos, player):
    mx, my, px, py = pos % 6, pos // 6, player % 6, player // 6
    o = op[pos]
    if mx < px and o[RIGHT]:
        return pos + 1
    if mx > px and o[LEFT]:
        return pos - 1
    if my < py and o[DOWN]:
        return pos + 6
    if my > py and o[UP]:
        return pos - 6
    return pos


def step(b, op, pits, state, d):
    """One player move. Returns ('win'|'lose'|'play', next_state)."""
    p, m0, t0, m1, t1, red = state
    if not op[p][d]:
        return None
    p += DELTA[d]
    if not 0 <= p < 36 or p in pits:
        return "lose", None
    if p == m0 or p == m1:
        return "lose", None
    t0, t1 = max(0, t0 - 1), max(0, t1 - 1)
    if m0 < 0 and m1 < 0:
        return ("win", None) if p == b.exit else ("play", (p, m0, t0, m1, t1, red))
    mons = [m0, m1]
    traps = [t0, t1]
    for _ in range(3 if red else 2):
        for i in (0, 1):
            if mons[i] < 0 or traps[i]:
                continue
            n = chase(op, mons[i], p)
            if n == p:
                return "lose", None
            if n != mons[i] and n in pits and not red:
                traps[i] = 4
            mons[i] = n
        if mons[0] >= 0 and mons[0] == mons[1]:
            mons[1], traps[0], red = -1, 0, True
            break
    if p == b.exit:
        return "win", None
    return "play", (p, mons[0], traps[0], mons[1], traps[1], red)


def solve(b, limit=80):
    """Optimal number of moves, or None. Breadth-first over game states."""
    op = open_sides(b)
    pits = set(b.pits)
    m = list(b.monsters) + [-1] * (2 - len(b.monsters))
    start = (b.player, m[0], 0, m[1], 0, False)
    seen = {start}
    frontier = [start]
    for depth in range(1, limit + 1):
        nxt = []
        for s in frontier:
            for d in range(4):
                r = step(b, op, pits, s, d)
                if r is None:
                    continue
                kind, ns = r
                if kind == "win":
                    return depth
                if kind == "play" and ns not in seen:
                    seen.add(ns)
                    nxt.append(ns)
        if not nxt:
            return None
        frontier = nxt
    return None


def walk(b):
    """Monster-free shortest walk to the exit, avoiding pits."""
    op = open_sides(b)
    q, seen = deque([(b.player, 0)]), {b.player}
    while q:
        p, dist = q.popleft()
        if p == b.exit:
            return dist
        for d in range(4):
            if op[p][d]:
                n = p + DELTA[d]
                if 0 <= n < 36 and n not in seen and n not in b.pits:
                    seen.add(n)
                    q.append((n, dist + 1))
    return None


def free_cell(rng, taken):
    while True:
        c = rng.randrange(36)
        if c not in taken:
            return c


def random_board(rng, n_monsters, n_pits, n_walls):
    exit_ = rng.choice(PERIMETER)
    taken = {exit_}
    pits = []
    for _ in range(n_pits):
        pits.append(free_cell(rng, taken))
        taken.add(pits[-1])
    player = free_cell(rng, taken)
    taken.add(player)
    monsters = []
    for _ in range(n_monsters):
        monsters.append(free_cell(rng, taken | {player + d for d in DELTA}))
        taken.add(monsters[-1])
    walls = set(rng.sample(ALL_WALLS, n_walls))
    return Board(walls, pits, exit_, player, monsters)


def mutate(rng, b):
    walls, pits, monsters = set(b.walls), list(b.pits), list(b.monsters)
    exit_, player = b.exit, b.player
    taken = set(pits) | set(monsters) | {exit_, player}
    kind = rng.random()
    if kind < 0.45 and walls:
        walls.remove(rng.choice(sorted(walls)))
        walls.add(rng.choice([w for w in ALL_WALLS if w not in walls]))
    elif kind < 0.55:
        if rng.random() < 0.5 and len(walls) > 4:
            walls.remove(rng.choice(sorted(walls)))
        elif len(walls) < 16:
            walls.add(rng.choice([w for w in ALL_WALLS if w not in walls]))
    elif kind < 0.65 and pits:
        i = rng.randrange(len(pits))
        pits[i] = free_cell(rng, taken)
    elif kind < 0.80 and monsters:
        i = rng.randrange(len(monsters))
        monsters[i] = free_cell(rng, taken | {player + d for d in DELTA})
    elif kind < 0.92:
        player = free_cell(rng, taken | {m + d for m in monsters for d in DELTA})
    else:
        exit_ = rng.choice([c for c in PERIMETER if c not in taken])
    return Board(walls, pits, exit_, player, monsters)


# Wall sets of another level file (--avoid), in all four mirror images.
AVOID = []


def mirrored(c, mx, my):
    x, y = c % 6, c // 6
    return (5 - y if my else y) * 6 + (5 - x if mx else x)


def load_avoid(path):
    AVOID.clear()
    if path:
        for b, _ in parse_header(path):
            for mx in (0, 1):
                for my in (0, 1):
                    AVOID.append({tuple(sorted((mirrored(a, mx, my), mirrored(c, mx, my)))) for a, c in b.walls})


def too_similar(b):
    walls = set(b.walls)
    return any(len(w & walls) / max(1, len(w | walls)) > MAX_WALL_OVERLAP for w in AVOID)


def score(b, target):
    par = solve(b, limit=target + 4)
    w = walk(b)
    if par is None or w is None:
        return 1000, par
    short = max(0, (w + MONSTER_MARGIN) - par)  # the monsters must make it hard
    return abs(par - target) * 10 + short * 5, par


def generate(level):
    """Hill-climb towards the exact target par; settle for PAR_SLACK only if
    the exact one does not turn up (long spike levels get one move more)."""
    rng = random.Random(SEED * 1000 + level)
    target = TARGET_PAR[level]
    slack = PAR_SLACK if target < 36 else PAR_SLACK + 1
    n_monsters = 2 if level in TWO_MONSTERS else 1
    n_pits = 1 + (level % 3 == 0) + (level % 7 == 0 and level > 30)
    n_walls = 8 + (level * 4) // 125
    fallback = None
    for restart in range(400):
        b = random_board(rng, n_monsters, n_pits, n_walls)
        cost, par = score(b, target)
        for _ in range(600):
            if cost == 0 and not too_similar(b):
                return level, b, par
            if cost <= slack * 10 and fallback is None and not too_similar(b):
                fallback = (b, par)
            nb = mutate(rng, b)
            ncost, npar = score(nb, target)
            if ncost <= cost:
                b, cost, par = nb, ncost, npar
        if fallback is not None and restart >= 40:
            return level, fallback[0], fallback[1]
    if fallback is not None:
        return level, fallback[0], fallback[1]
    raise RuntimeError(f"level {level + 1}: no board found")


def emit(levels):
    out = [
        "// Generated by tools_local/wappo/gen_levels.py -- do not edit by hand.",
        f"// {len(levels)} levels, seed {SEED}. Par is each board's optimal solution length.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace wappo {",
        "",
        f"constexpr int kLevelCount = {len(levels)};",
        "constexpr int kBoardWidth = 6;",
        "constexpr int kBoardHeight = 6;",
        "constexpr int kCellCount = 36;",
        "",
        "struct Wall {",
        "  int8_t c1;",
        "  int8_t c2;",
        "};",
        "",
        "struct Special {",
        "  int8_t cell;",
        "  int8_t type;  // 1 = Pit, 2 = Exit",
        "};",
        "",
        "struct LevelDef {",
        "  int8_t playerStart;",
        "  int8_t par;",
        "  int8_t monsterCount;",
        "  int8_t monsterStart[2];",
        "  int8_t wallCount;",
        "  Wall walls[18];",
        "  int8_t specialCount;",
        "  Special specials[4];",
        "};",
        "",
        "// One level per line; clang-format would give every field its own.",
        "// clang-format off",
        "constexpr LevelDef kLevels[kLevelCount] = {",
    ]
    for i, (b, par) in enumerate(levels):
        walls = ", ".join(f"{{{a}, {c}}}" for a, c in sorted(b.walls))
        specials = ", ".join([f"{{{b.exit}, 2}}"] + [f"{{{p}, 1}}" for p in b.pits])
        m = list(b.monsters) + [-1] * (2 - len(b.monsters))
        out.append(f"    // Level {i + 1}")
        out.append(
            f"    {{{b.player}, {par}, {len(b.monsters)}, {{{m[0]}, {m[1]}}}, {len(b.walls)}, {{{walls}}}, "
            f"{1 + len(b.pits)}, {{{specials}}}}},"
        )
    out += ["};", "// clang-format on", "", "}  // namespace wappo", ""]
    return "\n".join(out)


def parse_header(path):
    levels = []
    pat = r"\{(-?\d+), (\d+), (\d), \{(-?\d+), (-?\d+)\}, (\d+), \{(.*?)\}, (\d+), \{(.*?)\}\},"
    for m in re.finditer(pat, open(path).read()):
        player, par, mc, m0, m1, _, walls, _, specials = m.groups()
        walls = {tuple(map(int, w)) for w in re.findall(r"\{(\d+), (\d+)\}", walls)}
        sp = [tuple(map(int, s)) for s in re.findall(r"\{(\d+), (\d+)\}", specials)]
        exit_ = [c for c, t in sp if t == 2][0]
        pits = [c for c, t in sp if t == 1]
        monsters = [int(m0), int(m1)][: int(mc)]
        levels.append((Board(walls, pits, exit_, int(player), monsters), int(par)))
    return levels


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="re-solve the committed levels and compare pars")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--avoid", help="a WappoLevels.h whose wall layouts the new boards must not resemble")
    args = ap.parse_args()

    if args.check:
        levels = parse_header(OUT)
        if len(levels) != len(TARGET_PAR):
            sys.exit(f"{OUT} holds {len(levels)} levels, expected {len(TARGET_PAR)}")
        bad = [i + 1 for i, (b, par) in enumerate(levels) if solve(b) != par]
        print(f"all {len(levels)} pars are optimal" if not bad else f"pars wrong on levels {bad}")
        sys.exit(1 if bad else 0)

    with multiprocessing.Pool(args.jobs, initializer=load_avoid, initargs=(args.avoid,)) as pool:
        found = {}
        for level, b, par in pool.imap_unordered(generate, range(len(TARGET_PAR))):
            found[level] = (b, par)
            print(f"level {level + 1:3d}: target {TARGET_PAR[level]:2d} -> par {par:2d} ({len(found)}/125)", flush=True)
    levels = [found[i] for i in range(len(TARGET_PAR))]
    keys = [b.key() for b, _ in levels]
    assert len(set(keys)) == len(keys), "duplicate boards"
    open(OUT, "w").write(emit(levels))
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
