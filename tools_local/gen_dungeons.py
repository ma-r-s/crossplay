#!/usr/bin/env python3
"""Turn assets_local/dungeons/dungeons.txt into a flash-resident puzzle bank.

The interesting part is not the formatting: it is that this script SOLVES every
puzzle before it emits it, exhaustively, and refuses to write the header unless
each one has exactly one solution.

That matters because a puzzle with two solutions is unplayable in a way no test
of the app could catch -- the player finds a valid arrangement of walls, the
game says no, and nothing anywhere is wrong except the data. Deriving the
answer instead of transcribing it also means the source file cannot disagree
with the header, because there is only one place the answer exists.

  python3 tools_local/gen_dungeons.py

Writes src/apps_local/dungeon/DungeonPuzzles.h. Takes about a minute.
"""

import os
import sys
from itertools import combinations

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SOURCE = os.path.join(ROOT, "assets_local", "dungeons", "dungeons.txt")
OUTPUT = os.path.join(ROOT, "src", "apps_local", "dungeon", "DungeonPuzzles.h")


def parse(path):
    """Read the text source into (code, name, rowClues, colClues, cells)."""
    puzzles = []
    lines = [ln.rstrip("\n") for ln in open(path, encoding="utf-8")]
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.strip() or line.startswith("#"):
            i += 1
            continue
        code, name = line.split(" ", 1)
        cols = [int(ch) for ch in lines[i + 1].strip()]
        n = len(cols)
        rows, cells = [], []
        for r in range(n):
            row = lines[i + 2 + r]
            rows.append(int(row[0]))
            cells.append(list(row[1 : 1 + n]))
            if len(cells[-1]) != n:
                sys.exit("%s: row %d is not %d cells wide" % (code, r, n))
        puzzles.append((code, name, rows, cols, cells))
        i += 2 + n
    return puzzles


def solve(row_clues, col_clues, cells, limit=2):
    """Every arrangement of walls satisfying all six rules, up to `limit`.

    Rows are placed one at a time. The pruning that makes this fast enough to
    run over the whole bank is that once row r+1 is placed, every cell in row r
    has its full neighbourhood, so the dead-end rule can be decided there rather
    than at the end -- which is what turns a search over 2^64 boards into one
    that finishes in milliseconds.
    """
    n = len(cells)
    mons = {(r, c) for r in range(n) for c in range(n) if cells[r][c] == "M"}
    chests = {(r, c) for r in range(n) for c in range(n) if cells[r][c] == "T"}

    # Every 3x3 block holding exactly one chest is a candidate treasure room.
    rooms = []
    for tr in range(n - 2):
        for tc in range(n - 2):
            inside = frozenset((tr + i, tc + j) for i in range(3) for j in range(3))
            if len(inside & chests) == 1:
                rooms.append(inside)
    # A 2x2 of open floor is legal only inside a treasure room, so a block that
    # no candidate room could contain can be pruned during the search.
    room_2x2 = set()
    for inside in rooms:
        tr = min(r for r, _ in inside)
        tc = min(c for _, c in inside)
        for i in range(2):
            for j in range(2):
                room_2x2.add((tr + i, tc + j))

    row_options = []
    for r in range(n):
        opts = []
        for wallset in combinations(range(n), row_clues[r]):
            ws = set(wallset)
            if ws & mons or ws & chests:
                continue  # a monster or a chest stands on floor, never in a wall
            opts.append(tuple(1 if c in ws else 0 for c in range(n)))
        row_options.append(opts)

    suffix = [0] * (n + 1)
    for r in range(n - 1, -1, -1):
        suffix[r] = suffix[r + 1] + row_clues[r]

    grid = [None] * n
    col_used = [0] * n
    solutions = []

    def degrees_ok(r):
        """Dead ends hold monsters and monsters sit in dead ends, for row r."""
        cur = grid[r]
        up = grid[r - 1] if r > 0 else None
        down = grid[r + 1] if r + 1 < n else None
        for c in range(n):
            if cur[c]:
                continue
            deg = 0
            if up is not None and up[c] == 0:
                deg += 1
            if down is not None and down[c] == 0:
                deg += 1
            if c > 0 and cur[c - 1] == 0:
                deg += 1
            if c < n - 1 and cur[c + 1] == 0:
                deg += 1
            if deg == 0:
                return False  # a sealed cell is not connected to anything
            if (deg == 1) != ((r, c) in mons):
                return False
        return True

    def whole_board_ok(g):
        floor = [(r, c) for r in range(n) for c in range(n) if g[r][c] == 0]
        if not floor:
            return False
        open_cells = set(floor)

        stack, seen = [floor[0]], {floor[0]}
        while stack:
            r, c = stack.pop()
            for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                p = (r + dr, c + dc)
                if p in open_cells and p not in seen:
                    seen.add(p)
                    stack.append(p)
        if len(seen) != len(open_cells):
            return False  # the dungeon must be one connected space

        # Each chest sits in a 3x3 room of floor with exactly one way in, and no
        # two chests share a room.
        claimed = set()
        for chest in sorted(chests):
            room = None
            for inside in rooms:
                if chest not in inside or not inside <= open_cells:
                    continue
                if inside & mons or len(inside & chests) != 1:
                    continue
                doors = 0
                for r, c in inside:
                    for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        p = (r + dr, c + dc)
                        if p not in inside and p in open_cells:
                            doors += 1
                if doors != 1:
                    continue
                room = inside
                break
            if room is None or (room & claimed):
                return False
            claimed |= room

        # Corridors are one cell wide: any 2x2 of floor must be room, not hall.
        for r in range(n - 1):
            for c in range(n - 1):
                block = {(r, c), (r, c + 1), (r + 1, c), (r + 1, c + 1)}
                if block <= open_cells and not block <= claimed:
                    return False
        return True

    def place(r):
        if len(solutions) >= limit:
            return
        if r == n:
            if degrees_ok(n - 1) and whole_board_ok(grid):
                solutions.append([row[:] for row in grid])
            return
        previous = grid[r - 1] if r > 0 else None
        for opt in row_options[r]:
            ok = True
            for c in range(n):
                filled = col_used[c] + opt[c]
                if filled > col_clues[c] or col_clues[c] - filled > n - 1 - r:
                    ok = False
                    break
            if not ok:
                continue
            if sum(col_clues) - sum(col_used) - row_clues[r] != suffix[r + 1]:
                continue
            if previous is not None:
                for c in range(n - 1):
                    if (
                        previous[c] == 0
                        and previous[c + 1] == 0
                        and opt[c] == 0
                        and opt[c + 1] == 0
                        and (r - 1, c) not in room_2x2
                    ):
                        ok = False
                        break
                if not ok:
                    continue
            grid[r] = list(opt)
            if previous is None or degrees_ok(r - 1):
                for c in range(n):
                    col_used[c] += opt[c]
                place(r + 1)
                for c in range(n):
                    col_used[c] -= opt[c]
            grid[r] = None
            if len(solutions) >= limit:
                return

    place(0)
    return solutions


def mask(predicate, cells):
    """Cells matching `predicate` as a bit-per-cell mask, bit r*8+c."""
    value = 0
    for r, row in enumerate(cells):
        for c, ch in enumerate(row):
            if predicate(ch):
                value |= 1 << (r * 8 + c)
    return value


def main():
    puzzles = parse(SOURCE)
    print("read %d puzzles from %s" % (len(puzzles), os.path.relpath(SOURCE, ROOT)))

    rows = []
    longest = 0
    for code, name, row_clues, col_clues, cells in puzzles:
        found = solve(row_clues, col_clues, cells)
        if len(found) != 1:
            sys.exit(
                "%s (%s) has %d solutions; a puzzle with anything but one is "
                "unplayable" % (code, name, len(found))
            )
        walls = 0
        for r, row in enumerate(found[0]):
            for c, wall in enumerate(row):
                if wall:
                    walls |= 1 << (r * 8 + c)
        size = len(cells)
        tier = 0 if code.startswith("f") else int(code.split(".")[0])
        longest = max(longest, len(name))
        rows.append(
            '    {"%s", %d, %d, {%s}, {%s}, 0x%016XULL, 0x%016XULL, 0x%016XULL},'
            % (
                name.upper(),
                tier,
                size,
                ",".join(str(x) for x in row_clues + [0] * (8 - size)),
                ",".join(str(x) for x in col_clues + [0] * (8 - size)),
                mask(lambda ch: ch == "M", cells),
                mask(lambda ch: ch == "T", cells),
                walls,
            )
        )
        print("  %-4s %-34s one solution" % (code, name))

    header = HEADER % {
        "count": len(rows),
        "rows": "\n".join(rows),
        "longest": longest,
    }
    with open(OUTPUT, "w", encoding="utf-8") as out:
        out.write(header)
    print("wrote %s" % os.path.relpath(OUTPUT, ROOT))


HEADER = """#pragma once

// Every puzzle in D&Diagrams, as flash-resident data.
//
// GENERATED by tools_local/gen_dungeons.py from
// assets_local/dungeons/dungeons.txt. Do not edit by hand.
//
// `walls` is not transcribed, it is derived: the generator solves each puzzle
// exhaustively and refuses to emit the bank unless every one has exactly one
// solution. So the answer cannot disagree with the clues, and a puzzle that
// would reject a legitimate arrangement of walls cannot reach the device.
// docs/dungeons.md has the provenance and the cross-check.
//
// Bit r*8+c, row-major from the top-left, for every mask -- including the 6x6
// tutorial, which simply leaves the right and bottom bits clear.

#include <cstdint>

namespace dungeon {

struct Puzzle {
  const char* name;
  // 1 to 8 for the campaign, 0 for the tutorial. The campaign is eight tiers of
  // eight, easiest first, which is the order the original presents them in.
  uint8_t tier;
  uint8_t size;
  uint8_t rowClues[8];
  uint8_t colClues[8];
  uint64_t monsters;
  uint64_t chests;
  // The one solution.
  uint64_t walls;
};

constexpr Puzzle kPuzzles[] = {
%(rows)s
};

constexpr int kPuzzleCount = static_cast<int>(sizeof(kPuzzles) / sizeof(kPuzzles[0]));

// The longest name in the bank. A screen sizes its buffer from this rather than
// measuring at runtime, and the generator keeps it honest.
constexpr int kMaxNameLen = %(longest)d;

}  // namespace dungeon
"""


if __name__ == "__main__":
    main()
