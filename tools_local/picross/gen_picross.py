#!/usr/bin/env python3
"""Turn the assets_local/picross source files into a flash-resident nonogram bank.

One file per ORIGIN -- see SOURCES below -- because a bank may mix sources that
carry different rights, and parse() starts each file with fresh file-level
defaults so one file's declaration can never leak onto another's puzzles.

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

The bank ships one string per puzzle, its NAME, which the win screen reveals
once the picture is solved. Nothing else about a puzzle reaches the device: no
author, no licence, no source URL. There is nobody to credit for the shipped
bank and no licence to honour, and the ~34KB of source URLs the previous bank
carried bought a credit no player ever read.

  python3 tools_local/picross/gen_picross.py

Writes src/apps_local/picross/PicrossPuzzles.h. Takes a second or two.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))
ASSETS = os.path.join(ROOT, "assets_local", "picross")
OUTPUT = os.path.join(ROOT, "src", "apps_local", "picross", "PicrossPuzzles.h")

# The files the bank is built from. One file per origin, and a file is shipped
# only by being in this list.
#
# pictures.txt IS DELIBERATELY NOT HERE. It holds 68 pictures this fork drew --
# valid, CC0, and still generated on request -- and Mario's call is that the
# hand-drawn artwork "is not and won't be close to good enough" beside designed
# puzzles, so the shipped bank is the import alone. The file stays in the
# repository because deleting a generator's input destroys reproducible work for
# nothing, and because it is the worked example of this file format: adding it
# back to this tuple is one line. Nothing else reads it.
#
# A file named here that is missing is a hard error, not a shorter bank: a
# silently smaller bank is exactly the failure this list exists to prevent.
SOURCES = (os.path.join(ASSETS, "nonograms.txt"),)

# The sizes that reach the device, and therefore the picker's size tabs -- it
# derives its groups from the bank rather than from a list of its own.
#
# 15x15 is not here and will not be: Mario played one on the panel and the call
# is his, "it's just not gonna work". A 15x15 lands on 19px cells behind a 177px
# row-clue gutter, and on glass that is a grid you can read but not reliably tap.
# 5, 8, 9 and 10 all land on cells a finger can hit.
#
# This is a FILTER over the sources rather than a re-import, so widening or
# narrowing it is one edit and a regenerate. Narrowing it changes the SAVE
# FORMAT as surely as widening it does -- see kSaveVersion in PicrossActivity.
SHIPPED_SIZES = (5, 8, 9, 10)

# THE PUZZLE'S NAME IS ITS TITLE IN THE SOURCE FILE, and the win screen reveals
# exactly that string. The bank arrived titled -- 199 names somebody wrote for
# the pictures -- so there is no annotation pass to do and nothing to key.
#
# This file is an OVERRIDE LAYER over those titles, and it is empty. It exists
# for the case the titles do not cover: a picture Mario wants revealed under a
# different word from the one the pack shipped. Keys are the source title; an
# entry replaces it, an absent one leaves it alone, and a key matching no
# puzzle is a HARD ERROR -- a typo that silently renames nothing is exactly how
# an annotation pass ends up half-applied with nobody the wiser.
#
# A bank index is never a key. The bank is emitted size-sorted and renumbers
# whenever it changes, so an index names a different puzzle after any edit.
#
# It is NOT the place to fix a title that does not fit the panel. That belongs
# in assets_local/picross/title-overrides.json, which the IMPORTER applies, so
# the fix survives a re-import instead of being restored by the next one.
NAMES = os.path.join(ASSETS, "name-overrides.json")

# A name is accepted exactly when it RENDERS AT FULL SIZE, which is measured
# rather than approximated by a character count. tools_local/picross/name_fit.py
# is the one place that answers it, and host-tests/picrossnames re-asks it of the
# header that shipped, re-measures the band from the real screen builder, and
# pins the measurement to its own recorded corpus.
#
# THIS USED TO BE A NINE-CHARACTER CAP, and a character count is the wrong
# instrument for a variable-width font in both directions at once: it refused
# "CHRISTMAS TREE", which measures 410px against a 448px band and fits whole,
# while ten capital Ws (437px) also fits and eleven does not. Every fixed count
# is either too tight for good names or too loose for wide ones. Measuring is
# not a stricter rule, it is the actual question.
#
# The alphabet is not a list here either. A name may use any printable ASCII the
# cut draws; what is refused is a character the cut has NO GLYPH for, and
# measure() reports those as holes rather than measuring them -- a missing glyph
# draws nothing and advances the pen by nothing, so a broken string would
# otherwise measure as a comfortable fit.
import name_fit  # noqa: E402  (same directory; see sys.path below)


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


# The board sizes this app has any layout for. It is a tier list, not a bound:
# a picture at a size not named here is refused by the gate rather than drawn
# badly. 8 and 9 arrived with Mario's nonogram pack; they sit between the 5x5
# and the 10x10 in cell size and need no layout work, because the grid, the clue
# gutters and the tile thumbnails are all computed from the size rather than
# written down. 15 stays here and out of SHIPPED_SIZES: it is a size the gate
# understands and the panel cannot show (19px cells behind a 177px gutter).
SIZES = (5, 8, 9, 10, 15)


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


def check_name(text, label, fit, widest_run):
    """Refuse a name the win screen cannot draw at full size. Returns `text`.

    Both failures are HARD ERRORS and neither is visible on a green build,
    which is the whole reason they are errors:

    A MISSING GLYPH IS A HOLE, not a box. The display cut has no fallback, so a
    character it lacks draws nothing and advances the pen by nothing -- the
    broken name measures as a comfortable fit and renders with a gap in it.

    AN OVERLONG NAME IS NOT CLIPPED, it is SHRUNK. `toybox::fittedTitle` walks
    down the font slots until one fits and nothing logs that it did, so the
    reveal is simply set two-thirds size with nobody told. That is the nastier
    of the two, because the screen looks fine.
    """
    landed = fit(text)
    if landed.holes:
        sys.exit(
            "%s: %r uses %s, which the display cut has no glyph for. A missing "
            "glyph is a HOLE in the word, not a box -- it draws nothing and "
            "advances the pen by nothing, so the name would measure as a "
            "comfortable fit and render with a gap in it."
            % (label, text, ", ".join(repr(ch) for ch in landed.holes))
        )
    if not landed.full_size:
        sys.exit(
            "%s: %r measures %dpx against the win screen's %dpx band, so "
            "toybox::fittedTitle would set it %s. It does not clip an overlong "
            "name, it SHRINKS it, and nothing reports that -- which is why this "
            "is an error rather than a warning. Shorter, or narrower letters: "
            "%d capital Ws fit. A title that comes from the corpus is renamed in "
            "assets_local/picross/title-overrides.json, which the importer "
            "applies, so a re-import does not put the long one back."
            % (
                label,
                text,
                landed.width,
                fit.band,
                "at %s" % landed.rung if landed.rung else "smaller still",
                widest_run,
            )
        )
    return text


def widest_ws(fit):
    """How many capital Ws fit the band, computed rather than written down.

    Named in an error message so the reader gets a scale for "too wide". The
    number follows the font and the band, and a literal here would be the very
    thing measuring replaced.
    """
    run = 1
    while fit("W" * (run + 1)).full_size and run < 64:
        run += 1
    return run


def load_names(source_names):
    """The OVERRIDE layer over the source titles, keyed by source title.

    The bank arrives titled and those titles are the names, so this file is
    normally empty and `emit` falls back to the title for every puzzle. What it
    is for is the one picture Mario wants revealed under a different word from
    the one the corpus shipped.

    Everything here is a HARD ERROR rather than a warning, and that is the
    point: the failure this guards against is not a crash, it is a picture
    quietly revealing the wrong name -- or none -- with a green build and a file
    that looks full. Nothing downstream can tell a key that matched nothing from
    a puzzle nobody chose to rename.
    """
    if not os.path.exists(NAMES):
        return {}
    import json

    with open(NAMES, encoding="utf-8") as f:
        raw = json.load(f)
    if not isinstance(raw, dict):
        sys.exit("%s: expected a JSON object of {source title: NAME}" % NAMES)

    fit = name_fit.fitter()
    widest_run = widest_ws(fit)

    known = set(source_names)
    names = {}
    for key, value in raw.items():
        if key.startswith("_"):
            continue  # "_comment" and friends
        target = str(key).strip()
        if target not in known:
            sys.exit(
                "%s: key %r names no puzzle in the shipped bank. Keys are the "
                "puzzle's title in %s, exactly as written there; a bank index is "
                "never a key, because the bank is emitted size-sorted and "
                "renumbers on any change."
                % (NAMES, key, os.path.relpath(SOURCES[0], ROOT))
            )
        if not isinstance(value, str):
            sys.exit("%s: %r is not a string" % (NAMES, key))
        text = value.strip()
        if not text:
            continue  # an entry left blank simply keeps the source title
        check_name(text, "%s: override for %r" % (NAMES, key), fit, widest_run)
        if target in names and names[target] != text:
            sys.exit(
                "%s: %s is renamed twice, %r and %r"
                % (NAMES, target, names[target], text)
            )
        names[target] = text
    return names


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

    The generated header names them rather than assuming one file, so a reader
    who opens the bank can see which origins it was built from.
    """
    puzzles = sort_by_size(puzzles)
    overrides = load_names([name for name, _cells, _prov in puzzles])
    # EVERY SHIPPED NAME IS MEASURED HERE, not just the overridden ones. The
    # titles arrive with the corpus, which means the widest one arrives with it
    # too: the pack this bank was built from carried exactly one name too wide
    # for the band, and the failure mode is silent -- fittedTitle shrinks rather
    # than truncates. Measuring the whole bank on every regenerate is what stops
    # the next corpus introducing one nobody notices.
    fit = name_fit.fitter()
    widest_run = widest_ws(fit)
    maxsize = max((len(cells) for _, cells, _ in puzzles), default=5)
    hexdigits = (maxsize + 3) // 4  # 5->2, 10->3, 15->4
    emitted = []
    longest = 0
    for name, cells, _prov in puzzles:
        n = len(cells)
        ok, reason = evaluate(name, cells)
        if not ok:
            sys.exit(
                "%s: %s. Redesign it or drop it (run --curate to triage)."
                % (name, reason)
            )
        bits = []
        for r in range(maxsize):
            value = 0
            if r < n:
                for c in range(n):
                    if cells[r][c]:
                        value |= 1 << c
            bits.append(value)
        # The puzzle's title IS its name; the override file only ever replaces
        # one. A puzzle with an empty title ships unnamed and the win screen
        # draws no name band at all.
        display = overrides.get(name, name)
        if display:
            check_name(display, "%s: name for %r" % (os.path.relpath(OUTPUT, ROOT), name), fit, widest_run)
        longest = max(longest, len(display))
        emitted.append(
            "    {%s, %d, {%s}},"
            % (
                cstring(display),
                n,
                ", ".join(("0x%0" + str(hexdigits) + "X") % b for b in bits),
            )
        )
        print("  %-16s %2dx%-2d  unique, line-solvable" % (name, n, n))

    sizes = sorted({len(cells) for _, cells, _ in puzzles})

    header = HEADER % {
        "sources": ", ".join(os.path.relpath(p, ROOT) for p in sources),
        "count": len(emitted),
        "rows": "\n".join(emitted),
        "sizegroups": len(sizes),
        "maxsize": maxsize,
        "longest": longest,
    }
    with open(OUTPUT, "w", encoding="utf-8") as out:
        out.write(header)
    clang_format(OUTPUT)
    print("wrote %s (%d puzzles)" % (os.path.relpath(OUTPUT, ROOT), len(emitted)))
    unnamed = sum(1 for name, _cells, _prov in puzzles if not overrides.get(name, name))
    if unnamed:
        print(
            "  %d of %d puzzles have no name; their win screen draws no name band."
            % (unnamed, len(puzzles))
        )
    else:
        print(
            "  every puzzle is named, and every name measured against the %dpx band"
            % fit.band
        )
    if overrides:
        print("  %d name(s) overridden from %s" % (len(overrides), os.path.relpath(NAMES, ROOT)))


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
        print(
            "  WARNING: no clang-format found; %s may fail the format gate"
            % os.path.basename(path)
        )
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
    # The size filter runs BEFORE the gate, so --curate reports on what would
    # actually ship rather than on a tier that cannot reach the device.
    dropped = [p for p in puzzles if len(p[1]) not in SHIPPED_SIZES]
    puzzles = [p for p in puzzles if len(p[1]) in SHIPPED_SIZES]
    if dropped:
        by_size = {}
        for _name, cells, _prov in dropped:
            by_size[len(cells)] = by_size.get(len(cells), 0) + 1
        print(
            "held back %d pictures whose size is not in %r: %s"
            % (
                len(dropped),
                SHIPPED_SIZES,
                ", ".join("%dx%d:%d" % (s, s, by_size[s]) for s in sorted(by_size)),
            )
        )
    if not puzzles:
        sys.exit(
            "no pictures at %r survive the size filter -- SHIPPED_SIZES and the "
            "sources disagree" % (SHIPPED_SIZES,)
        )
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
// THE PUZZLES ARE NOT PUBLIC DOMAIN AND NOT THIS FORK'S OWN WORK. CrossPlay's
// code is MIT and a fork inherits that; these pictures came to the project
// privately and are not ours to relicense, republish, or offer as a downloadable
// pack. That is the whole of what is known and the whole of what is claimed --
// there is nobody to credit and no licence to honour, so no author, no licence
// and no source URL appears here or anywhere else. Do not add one.
//
// The one string a puzzle carries is its NAME: the reveal on the win screen,
// which is the puzzle's title in the source file. An empty name is a picture
// with no title, and the win screen draws no name band for it.
//
// Only the solution bitmap is stored; the row and column clues are DERIVED from
// it (PicrossCore::deriveClues), so the clues can never disagree with the
// picture. The generator proves each puzzle is UNIQUE and LINE-SOLVABLE before
// it emits the bank and refuses the rest -- see docs/apps/picross.md.
//
// rows[r] holds the solid cells of row r as a bitmask, bit c (from the left)
// set when cell (r, c) is filled in the finished picture. Rows past `size` are
// zero.

#include <cstdint>

namespace picross {

// The widest board this fork ships, computed from the bank by the generator.
// Boards, saves and clue buffers size themselves from this rather than from a
// literal, so a bigger picture is a data change and not a code change. rows[] is
// uint16_t, so this must stay <= 16.
constexpr int kMaxSize = %(maxsize)d;

struct Puzzle {
  // The picture's name, revealed on the win screen and nowhere else -- the name
  // IS the answer, which is why the picker and the board never draw it. EMPTY
  // for a picture with no title, and the win screen then draws no name band at
  // all rather than an empty one.
  const char* name;
  uint8_t size;
  uint16_t rows[kMaxSize];  // bit c set when (row, col) is solid
};

constexpr Puzzle kPuzzles[] = {
%(rows)s
};

constexpr int kPuzzleCount = static_cast<int>(sizeof(kPuzzles) / sizeof(kPuzzles[0]));

// How many DISTINCT sizes the bank holds, and therefore how many tabs the
// picker draws. It is DERIVED from the bank rather than written down, and that
// is load-bearing rather than tidy: this was once a literal 4 with a `break`
// under it, which is a silent data-loss bug and not a bound -- a fifth run had
// nowhere to go, the break fired, and every puzzle after it was unreachable
// from the tabs with nothing drawn wrong and nothing logged. The bank is four
// runs today, which is exactly the boundary that bug sat on.
//
// The bank is emitted size-sorted, so each size is one contiguous run;
// host-tests/picross asserts that sortedness, which is the property this number
// assumes and the one an appended import would break.
constexpr int kSizeGroupCount = %(sizegroups)d;

// The longest name in the bank, so a screen sizes its buffer from this rather
// than measuring at runtime, and the generator keeps it honest. Zero while no
// picture has been named.
constexpr int kMaxNameLen = %(longest)d;

}  // namespace picross
"""


if __name__ == "__main__":
    main()
