#!/usr/bin/env python3
"""Turn the assets_local/picross source files into a flash-resident nonogram bank.

One file per ORIGIN -- see SOURCES below -- because the bank mixes this fork's
own CC0 artwork with puzzles used by permission, and those carry different
rights.

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

Every picture carries its PROVENANCE -- who drew it, under what licence, and
where it came from -- and the generator writes that into the header beside the
bitmap. The pictures in pictures.txt are ORIGINAL artwork authored for this
fork, so their row says so; a picture imported from anywhere else (see
import_picross.py) carries its own. Provenance is recorded at the moment a
picture enters the bank because retrofitting it afterwards is how this fork has
been burned before: by the time anyone asks, the origin is a guess.

  python3 tools_local/picross/gen_picross.py

Writes src/apps_local/picross/PicrossPuzzles.h. Takes a second or two.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ASSETS = os.path.join(ROOT, "assets_local", "picross")
OUTPUT = os.path.join(ROOT, "src", "apps_local", "picross", "PicrossPuzzles.h")

# The files the bank is built from. One file per origin, and a file is shipped
# only by being in this list.
#
# pictures.txt IS DELIBERATELY NOT HERE. It holds 68 pictures this fork drew --
# valid, CC0, and still generated on request -- and Mario's call is that the
# hand-drawn artwork "is not and won't be close to good enough" beside the
# designed puzzles, so the shipped bank is the import alone. The file stays in
# the repository because deleting a generator's input destroys reproducible
# work for nothing; add it back to this tuple to emit it. See
# assets_local/picross/PROVENANCE.md, which records the decision so a later
# session does not find 68 unused puzzles and helpfully re-add them.
#
# A file named here that is missing is a hard error, not a shorter bank: a
# silently smaller bank is exactly the failure this list exists to prevent.
SOURCES = (
    os.path.join(ASSETS, "janko.txt"),
)


# The provenance keys a picture may carry, and what an unset one means. `source`
# is empty for artwork this fork drew: there is nowhere it came from.
PROV_KEYS = ("author", "license", "source")
PROV_DEFAULT = {"author": "unknown", "license": "unknown", "source": ""}


def parse(path):
    """Read the text source into (name, cells, provenance) records.

    `cells` is a list of 0/1 rows; `provenance` is a dict over PROV_KEYS.

    Two directive forms, both introduced by '@' so they can never be mistaken
    for a grid line (which is only '#' and '.') or for a name (no name starts
    with '@'):

      @@author Someone      a FILE-LEVEL default, in force from here down
      @author Someone       overrides it for the NEXT puzzle only

    A file that declares nothing gets PROV_DEFAULT, which says "unknown" rather
    than silently claiming the picture is ours.
    """
    puzzles = []
    lines = [ln.rstrip("\n") for ln in open(path, encoding="utf-8")]
    i = 0
    n = len(lines)
    defaults = dict(PROV_DEFAULT)
    pending = {}
    while i < n:
        line = lines[i].rstrip()
        if not line.strip() or line.lstrip().startswith("#"):
            i += 1
            continue
        if line.lstrip().startswith("@"):
            text = line.strip()
            sticky = text.startswith("@@")
            body = text.lstrip("@")
            key, _, value = body.partition(" ")
            key = key.strip().lower()
            if key not in PROV_KEYS:
                sys.exit(
                    "line %d: unknown provenance key %r (expected one of %s)"
                    % (i + 1, key, ", ".join(PROV_KEYS))
                )
            (defaults if sticky else pending)[key] = value.strip()
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
        prov = dict(defaults)
        prov.update(pending)
        pending = {}
        puzzles.append((name, cells, prov))
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
    # The picture must USE the grid it claims. An empty edge row or column means
    # the drawing is not cropped to its bounding box, so the puzzle is smaller
    # than its label -- and the label is what tells the player how hard it is.
    # Interior empty lines are legitimate (a picture may genuinely have a gap);
    # only the four edges are required to carry ink.
    used_rows = [r for r in range(n) if any(cells[r])]
    used_cols = [c for c in range(n) if any(cells[r][c] for r in range(n))]
    if not used_rows or not used_cols:
        return False, "the picture is empty"
    r0, r1 = used_rows[0], used_rows[-1]
    c0, c1 = used_cols[0], used_cols[-1]
    if r0 != 0 or r1 != n - 1 or c0 != 0 or c1 != n - 1:
        return False, (
            "does not fill its %dx%d grid -- ink spans rows %d..%d, cols %d..%d "
            "(%dx%d). Redraw it to touch all four edges, or move it to a smaller tier."
            % (n, n, r0, r1, c0, c1, r1 - r0 + 1, c1 - c0 + 1)
        )
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
    for name, cells, prov in puzzles:
        ok, reason = evaluate(name, cells)
        n = len(cells)
        if ok:
            passed.append((name, cells, prov))
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


def cstring(text):
    """A C string literal for `text`. Escapes rather than trusting the input."""
    out = []
    for ch in text:
        if ch in ('"', "\\"):
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif 32 <= ord(ch) < 127:
            out.append(ch)
        else:
            # The header is compiled as plain C++ with no encoding promises, so
            # anything outside ASCII goes in as an escape rather than as bytes.
            out.append("".join("\\x%02X" % b for b in ch.encode("utf-8")))
    return '"%s"' % "".join(out)


def sort_by_size(puzzles):
    """Order the bank by size, ascending, stable within a size.

    This is not cosmetic. The picker recovers its size tabs by RUN-SCANNING the
    bank (PicrossScreens::sizeGroups): it starts a new group whenever the size
    changes from the previous entry. An unsorted bank therefore produces one
    group per alternation, and everything past the last group the picker can
    hold is unreachable from the tabs -- with no error anywhere, because
    nothing is wrong from the picker's point of view. Sorting here makes each
    size exactly one contiguous run, which is the property the picker needs and
    host-tests/picross now asserts over the shipped header.
    """
    return sorted(puzzles, key=lambda entry: len(entry[1]))


def emit(puzzles, sources):
    """Write the bank. `sources` is the origin files it was built from.

    The generated header names them, rather than the one file this script used
    to read: a reader who opens the bank and sees only "pictures.txt" has been
    told the imported half is this fork's own work.
    """
    puzzles = sort_by_size(puzzles)
    maxsize = max((len(cells) for _, cells, _ in puzzles), default=5)
    hexdigits = (maxsize + 3) // 4  # 5->2, 10->3, 15->4
    emitted = []
    longest = 0
    max_row_runs = 0
    max_col_runs = 0
    max_run = 0
    # One row per DISTINCT (author, licence, source), so a bank imported from
    # one collection costs one row and not one per puzzle -- and two puzzles
    # that share an origin cannot drift apart.
    prov_rows = []
    prov_index = {}
    for name, cells, prov in puzzles:
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
        key = tuple(prov.get(k, PROV_DEFAULT[k]) for k in PROV_KEYS)
        if key not in prov_index:
            prov_index[key] = len(prov_rows)
            prov_rows.append(key)
        emitted.append(
            '    {%s, %d, %d, {%s}},'
            % (
                cstring(name.upper()),
                n,
                prov_index[key],
                ", ".join(("0x%0" + str(hexdigits) + "X") % b for b in bits),
            )
        )
        print("  %-16s %2dx%-2d  unique, line-solvable" % (name, n, n))

    # `provenance` is a uint16_t index, so the table has to fit one. A bank with
    # more distinct origins than that wants a wider field, not a truncated
    # table, and saying so here is cheaper than debugging the wrap. The field
    # was a uint8_t until a 531-puzzle import produced 532 distinct rows: every
    # imported puzzle carries its OWN source URL, which is the field that
    # actually identifies it, so the dedup that makes this fork's own bank one
    # row does nothing for a collection.
    if len(prov_rows) > 65535:
        sys.exit(
            "%d distinct provenances; Puzzle::provenance is a uint16_t and holds 65535. "
            "Widen the field (and kProvenanceCount with it) before importing more."
            % len(prov_rows)
        )

    provenances = "\n".join(
        "    {%s, %s, %s}," % (cstring(a), cstring(li), cstring(src))
        for a, li, src in prov_rows
    )
    sizes = sorted({len(cells) for _, cells, _ in puzzles})

    header = HEADER % {
        "sources": ", ".join(os.path.relpath(p, ROOT) for p in sources),
        "count": len(emitted),
        "rows": "\n".join(emitted),
        "provenances": provenances,
        "sizegroups": len(sizes),
        "maxsize": maxsize,
        "longest": longest,
        "longestauthor": max((len(a) for a, _li, _src in prov_rows), default=0),
        "maxrowruns": max_row_runs,
        "maxcolruns": max_col_runs,
        "maxrun": max_run,
    }
    with open(OUTPUT, "w", encoding="utf-8") as out:
        out.write(header)
    clang_format(OUTPUT)
    print("wrote %s (%d puzzles)" % (os.path.relpath(OUTPUT, ROOT), len(emitted)))


def clang_format(path):
    """Format the generated header in place, so regenerating never churns.

    A generated file that is not clang-clean fails the format gate, and "fix it
    by hand" is a trap: the next run of this script puts the long lines straight
    back. The rows are far past the 120-column limit once kMaxSize is 15, so the
    generator owns the wrapping by handing the file to the same formatter the
    gate uses. Deterministic, so running this script twice is a no-op.
    """
    import shutil
    import subprocess

    binary = None
    for candidate in ("clang-format-21", "clang-format"):
        if shutil.which(candidate):
            binary = candidate
            break
    if binary is None:
        print("  WARNING: no clang-format found; %s may fail the format gate" % os.path.basename(path))
        return
    subprocess.run([binary, "-i", path], check=True)
    print("  formatted with %s" % binary)


def main():
    args = sys.argv[1:]
    do_curate = "--curate" in args
    paths = [a for a in args if not a.startswith("--")] or list(SOURCES)
    puzzles = []
    for source in paths:
        # parse() starts each file with a fresh set of file-level defaults, so
        # pictures.txt's `@@license CC0-1.0` cannot leak onto the imported bank
        # and janko.txt's cannot leak back. That isolation is the reason the
        # origins are separate files and is worth not losing.
        here = parse(source)
        puzzles.extend(here)
        print("read %d pictures from %s" % (len(here), os.path.relpath(source, ROOT)))
    # Names repeat ON PURPOSE across sizes -- HEART is drawn at 5x5, 10x10 and
    # 15x15 and is the same subject each time -- so a name is not a key here and
    # a uniqueness check over names would refuse the bank as designed.
    print("%d pictures in the bank" % len(puzzles))
    if do_curate:
        curate(puzzles)
    else:
        emit(puzzles, paths)


HEADER = """#pragma once

// Every Picross picture, as flash-resident data.
//
// GENERATED by tools_local/picross/gen_picross.py from
// %(sources)s.
// Do not edit by hand.
//
// THE BANK HAS MORE THAN ONE ORIGIN AND THEY CARRY DIFFERENT RIGHTS. Every
// puzzle indexes a kProvenances[] row naming its author, its licence and where
// it came from; a row that is not this fork's own CC0 is used BY PERMISSION and
// is not licensed to anybody. Read assets_local/picross/PROVENANCE.md before
// copying puzzles out of this file.
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

// Where a picture came from and what may be done with it. Recorded per puzzle
// because a bank that mixes origins -- this fork's own drawings and an imported
// collection -- cannot state its licence any other way, and a picture whose
// origin was not written down at import is a picture nobody can clear later.
//
// `source` is empty for artwork drawn for this fork: there is nowhere it came
// from. `license` is an SPDX identifier where one exists and a plain phrase
// where it does not; it is never empty, because "no licence recorded" and "all
// rights reserved" are the same thing and must read as such.
struct Provenance {
  const char* author;
  const char* license;
  const char* source;
};

constexpr Provenance kProvenances[] = {
%(provenances)s
};

constexpr int kProvenanceCount = static_cast<int>(sizeof(kProvenances) / sizeof(kProvenances[0]));

struct Puzzle {
  const char* name;
  uint8_t size;             // 5, 10 or 15
  uint16_t provenance;      // index into kProvenances
  uint16_t rows[kMaxSize];  // bit c set when (row, col) is solid
};

constexpr Puzzle kPuzzles[] = {
%(rows)s
};

constexpr int kPuzzleCount = static_cast<int>(sizeof(kPuzzles) / sizeof(kPuzzles[0]));

// How many DISTINCT sizes the bank holds, and therefore how many tabs the
// picker draws. The bank is emitted size-sorted, so each size is one contiguous
// run and the picker's run-scan recovers exactly this many groups. It is
// derived from the bank rather than written as a literal: a 4-slot array with a
// silent `break` made every puzzle past the fourth run unreachable from the
// tabs, and nothing anywhere reported it. host-tests/picross asserts the
// sortedness this number assumes.
constexpr int kSizeGroupCount = %(sizegroups)d;

// The longest name in the bank, so a screen sizes its buffer from this rather
// than measuring at runtime, and the generator keeps it honest.
constexpr int kMaxNameLen = %(longest)d;

// The longest AUTHOR name in the bank, for the same reason: the win screen
// credits the designer of an imported picture and sizes its buffer from this.
// Derived rather than written down, because a designer added by a later import
// would otherwise be silently truncated on the one screen that credits them.
constexpr int kMaxAuthorLen = %(longestauthor)d;

// The most clue numbers any one row or column carries, and the largest single
// run. The clue gutters are sized from these so no clue is ever elided, and the
// generator recomputes them from the bank on every run.
constexpr int kMaxRowRuns = %(maxrowruns)d;
constexpr int kMaxColRuns = %(maxcolruns)d;
constexpr int kMaxRun = %(maxrun)d;

// The provenance of puzzle `index`. Out of range answers the first row rather
// than reading past the table, the same way Board::load() clamps.
constexpr const Provenance& provenanceOf(const Puzzle& puzzle) {
  return kProvenances[puzzle.provenance < kProvenanceCount ? puzzle.provenance : 0];
}

}  // namespace picross
"""


if __name__ == "__main__":
    main()
