#!/usr/bin/env python3
"""Turn assets_local/picross/pictures.txt into a flash-resident nonogram bank.

The interesting part is not the formatting, it is the two properties this
script REFUSES to ship a puzzle without, the same way gen_dungeons.py refuses a
dungeon with anything but one solution:

  UNIQUE        exactly one grid satisfies the clues. A nonogram without this is
                broken: the player fills in something defensible and the game
                calls it wrong, and nothing anywhere is at fault except the data.

  LINE-SOLVABLE reachable by reasoning one row or column at a time, never
                needing a guess-and-backtrack. Most good published picross is
                line-solvable, and on a device with no undo tree a puzzle that
                needs bifurcation feels unfair.

Both are CONSTRUCTED here, not sampled for (see the `construct-dont-verify`
memory). The clues are DERIVED from the picture, so they can never disagree
with it. Then a line-solver runs the picture from a blank grid using only
single-line deductions: if it determines every cell and lands back on the
source picture, the puzzle is line-solvable AND unique -- because a line-solver
that determines every cell has proved every cell was forced, which is exactly
what one solution means. An independent exhaustive count (a different
implementation, in the spirit of the dungeon's two-languages cross-check) then
confirms the count is 1.

The pictures in pictures.txt are ORIGINAL artwork authored for this fork, so
there is no third-party licence to record -- see docs/apps/picross.md.

  python3 tools_local/picross/gen_picross.py

Writes src/apps_local/picross/PicrossPuzzles.h. Takes a second or two.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SOURCE = os.path.join(ROOT, "assets_local", "picross", "pictures.txt")
OUTPUT = os.path.join(ROOT, "src", "apps_local", "picross", "PicrossPuzzles.h")


def parse(path):
    """Read the text source into (name, cells) with cells a list of 0/1 rows."""
    puzzles = []
    lines = [ln.rstrip("\n") for ln in open(path, encoding="utf-8")]
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i].rstrip()
        if not line.strip() or line.lstrip().startswith("#"):
            i += 1
            continue
        # A grid line is made only of '#' and '.'; anything else is the name.
        if set(line.strip()) <= {"#", "."}:
            sys.exit("stray grid line with no name at line %d: %r" % (i + 1, line))
        name = line.strip()
        i += 1
        cells = []
        while i < n and set(lines[i].strip()) <= {"#", "."} and lines[i].strip():
            row = lines[i].strip()
            cells.append([1 if ch == "#" else 0 for ch in row])
            i += 1
        if not cells:
            sys.exit("%s has no grid" % name)
        size = len(cells)
        for r, row in enumerate(cells):
            if len(row) != size:
                sys.exit(
                    "%s: grid is not square -- row %d has %d cells, expected %d"
                    % (name, r, len(row), size)
                )
        puzzles.append((name, cells))
    return puzzles


def clues_for_line(line):
    """Run lengths of solid cells. An empty line is [0], as picross shows it."""
    runs = []
    run = 0
    for v in line:
        if v:
            run += 1
        elif run:
            runs.append(run)
            run = 0
    if run:
        runs.append(run)
    return runs if runs else [0]


def row_clues(cells):
    return [clues_for_line(row) for row in cells]


def col_clues(cells):
    n = len(cells)
    return [clues_for_line([cells[r][c] for r in range(n)]) for c in range(n)]


def placements(clues, n):
    """Every arrangement of `clues` (run lengths) in a line of length n."""
    runs = [c for c in clues if c > 0]
    if not runs:
        yield (0,) * n
        return

    def go(runs, n):
        first = runs[0]
        rest = runs[1:]
        rest_min = sum(rest) + len(rest)  # each remaining run: its length + a gap
        last_start = n - (first + rest_min)
        for start in range(0, last_start + 1):
            prefix = (0,) * start + (1,) * first
            if rest:
                for tail in go(rest, n - start - first - 1):
                    yield prefix + (0,) + tail
            else:
                yield prefix + (0,) * (n - start - first)

    yield from go(runs, n)


def solve_line(clues, n, known):
    """Tighten `known` (list of -1/0/1) using every placement consistent with it.

    Returns the tightened line, or None if the clues cannot be satisfied at all.
    """
    all_one = [1] * n  # stays 1 at i when every candidate fills i
    all_zero = [1] * n  # stays 1 at i when every candidate leaves i empty
    any_cand = False
    for p in placements(clues, n):
        if all(known[i] == -1 or known[i] == p[i] for i in range(n)):
            any_cand = True
            for i in range(n):
                if p[i]:
                    all_zero[i] = 0
                else:
                    all_one[i] = 0
    if not any_cand:
        return None
    out = list(known)
    for i in range(n):
        if all_one[i]:
            out[i] = 1
        elif all_zero[i]:
            out[i] = 0
    return out


def line_solve(rows, cols, n):
    """Solve using only single-line deductions. Returns the grid (-1 = unknown)."""
    grid = [[-1] * n for _ in range(n)]
    changed = True
    while changed:
        changed = False
        for r in range(n):
            res = solve_line(rows[r], n, grid[r])
            if res is None:
                return None
            if res != grid[r]:
                grid[r] = res
                changed = True
        for c in range(n):
            col = [grid[r][c] for r in range(n)]
            res = solve_line(cols[c], n, col)
            if res is None:
                return None
            if res != col:
                for r in range(n):
                    grid[r][c] = res[r]
                changed = True
    return grid


def count_solutions(rows, cols, n, limit=2):
    """Exhaustive count, up to `limit`. Independent of the line-solver above.

    Rows are placed one at a time; a partial column is pruned the moment its
    prefix matches no legal placement of that column's clue.
    """
    row_opts = [list(placements(rows[r], n)) for r in range(n)]
    col_pats = [list(placements(cols[c], n)) for c in range(n)]
    grid = [None] * n
    count = 0

    def prefix_ok(r):
        for c in range(n):
            prefix = tuple(grid[i][c] for i in range(r + 1))
            if not any(pat[: r + 1] == prefix for pat in col_pats[c]):
                return False
        return True

    def dfs(r):
        nonlocal count
        if count >= limit:
            return
        if r == n:
            count += 1
            return
        for opt in row_opts[r]:
            grid[r] = opt
            if prefix_ok(r):
                dfs(r + 1)
            grid[r] = None
            if count >= limit:
                return

    dfs(0)
    return count


SIZES = (5, 10, 15)


def evaluate(name, cells):
    """Prove a candidate is shippable. Returns (ok, reason).

    A puzzle ships only if its DERIVED clues admit exactly one picture and that
    picture is reachable by single-line reasoning. Both are proved here the same
    way the strict path proves them; the only difference between curate and
    strict is what happens to a failure -- reported, or fatal.
    """
    n = len(cells)
    if n not in SIZES:
        return False, "size %d not in %r" % (n, SIZES)
    rows = row_clues(cells)
    cols = col_clues(cells)
    solved = line_solve(rows, cols, n)
    if solved is None:
        return False, "clues are contradictory (a solver bug or a bad grid)"
    if any(-1 in solved[r] for r in range(n)):
        return (
            False,
            "NOT line-solvable -- single-line reasoning leaves cells undetermined",
        )
    if solved != cells:
        return (
            False,
            "AMBIGUOUS -- line-solving reaches a different grid than the source",
        )
    count = count_solutions(rows, cols, n)
    if count != 1:
        return False, "has %d solutions" % count
    return True, "unique, line-solvable"


def curate(puzzles):
    """Report pass/fail per candidate without emitting or aborting."""
    passed = []
    by_size = {}
    for name, cells in puzzles:
        ok, reason = evaluate(name, cells)
        n = len(cells)
        if ok:
            passed.append((name, cells))
            by_size[n] = by_size.get(n, 0) + 1
            print("  PASS  %-16s %2dx%-2d" % (name, n, n))
        else:
            print("  FAIL  %-16s %2dx%-2d  %s" % (name, n, n, reason))
    print(
        "\n%d of %d candidates pass; by size: %s"
        % (
            len(passed),
            len(puzzles),
            ", ".join("%dx%d:%d" % (s, s, by_size[s]) for s in sorted(by_size)),
        )
    )
    return passed


def emit(puzzles):
    maxsize = max((len(cells) for _, cells in puzzles), default=5)
    hexdigits = (maxsize + 3) // 4  # 5->2, 10->3, 15->4
    emitted = []
    longest = 0
    max_row_runs = 0
    max_col_runs = 0
    max_run = 0
    for name, cells in puzzles:
        n = len(cells)
        ok, reason = evaluate(name, cells)
        if not ok:
            sys.exit(
                "%s: %s. Redesign it or drop it (run --curate to triage)."
                % (name, reason)
            )
        rows = row_clues(cells)
        cols = col_clues(cells)
        bits = []
        for r in range(maxsize):
            value = 0
            if r < n:
                for c in range(n):
                    if cells[r][c]:
                        value |= 1 << c
            bits.append(value)
        longest = max(longest, len(name))
        max_row_runs = max(max_row_runs, max(len(rc) for rc in rows))
        max_col_runs = max(max_col_runs, max(len(cc) for cc in cols))
        max_run = max(max_run, max(max(rc) for rc in rows), max(max(cc) for cc in cols))
        emitted.append(
            '    {"%s", %d, {%s}},'
            % (
                name.upper(),
                n,
                ", ".join(("0x%0" + str(hexdigits) + "X") % b for b in bits),
            )
        )
        print("  %-16s %2dx%-2d  unique, line-solvable" % (name, n, n))

    header = HEADER % {
        "count": len(emitted),
        "rows": "\n".join(emitted),
        "maxsize": maxsize,
        "longest": longest,
        "maxrowruns": max_row_runs,
        "maxcolruns": max_col_runs,
        "maxrun": max_run,
    }
    with open(OUTPUT, "w", encoding="utf-8") as out:
        out.write(header)
    print("wrote %s (%d puzzles)" % (os.path.relpath(OUTPUT, ROOT), len(emitted)))


def main():
    args = sys.argv[1:]
    do_curate = "--curate" in args
    paths = [a for a in args if not a.startswith("--")]
    source = paths[0] if paths else SOURCE
    puzzles = parse(source)
    print("read %d pictures from %s" % (len(puzzles), os.path.relpath(source, ROOT)))
    if do_curate:
        curate(puzzles)
    else:
        emit(puzzles)


HEADER = """#pragma once

// Every Picross picture, as flash-resident data.
//
// GENERATED by tools_local/picross/gen_picross.py from
// assets_local/picross/pictures.txt. Do not edit by hand.
//
// Only the solution bitmap is stored; the row and column clues are DERIVED from
// it (PicrossCore::deriveClues), so the clues can never disagree with the
// picture. The generator proves each puzzle is UNIQUE and LINE-SOLVABLE before
// it emits the bank and refuses the rest -- see docs/apps/picross.md.
//
// rows[r] holds the solid cells of row r as a bitmask, bit c (from the left)
// set when cell (r, c) is filled in the finished picture. Rows past `size` are
// zero, so a 5x5 uses rows[0..4] and the low 5 bits of each.

#include <cstdint>

namespace picross {

// The widest board this fork ships, computed from the bank by the generator.
// Boards, saves and clue buffers size themselves from this rather than from a
// literal, so a bigger picture is a data change and not a code change. rows[] is
// uint16_t, so this must stay <= 16.
constexpr int kMaxSize = %(maxsize)d;

struct Puzzle {
  const char* name;
  uint8_t size;             // 5, 10 or 15
  uint16_t rows[kMaxSize];  // bit c set when (row, col) is solid
};

constexpr Puzzle kPuzzles[] = {
%(rows)s
};

constexpr int kPuzzleCount = static_cast<int>(sizeof(kPuzzles) / sizeof(kPuzzles[0]));

// The longest name in the bank, so a screen sizes its buffer from this rather
// than measuring at runtime, and the generator keeps it honest.
constexpr int kMaxNameLen = %(longest)d;

// The most clue numbers any one row or column carries, and the largest single
// run. The clue gutters are sized from these so no clue is ever elided, and the
// generator recomputes them from the bank on every run.
constexpr int kMaxRowRuns = %(maxrowruns)d;
constexpr int kMaxColRuns = %(maxcolruns)d;
constexpr int kMaxRun = %(maxrun)d;

}  // namespace picross
"""


if __name__ == "__main__":
    main()
