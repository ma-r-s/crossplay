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

# The sizes that reach the device. Mario played a 15x15 on the panel and the
# call is his: "it's just not gonna work". A 15x15 lands on 19px cells with a
# 177px row-clue gutter, and on glass that is a grid you can read but not
# reliably tap. So the game is 10x10 and nothing else.
#
# This is a FILTER rather than a re-import. janko.txt keeps all 321 puzzles for
# the same reason pictures.txt stays in the tree: a generator's input is
# reproducible work, and re-deriving the dropped tier later must not mean
# re-crawling janko.at. Widening this tuple is the whole of putting 15x15 back
# -- and it would also need the layout work docs/apps/picross.md describes.
SHIPPED_SIZES = (10,)

# Mario's own name for each picture, the thing the win screen reveals. This
# REPLACES the designer/licence/URL payload that used to sit beside every
# puzzle: the attribution is not firmware's job (it costs flash and no player
# reads it), so it now lives in assets_local/picross/PROVENANCE.md alone, and
# the per-puzzle string the bank does carry is the name.
#
# Keys are the puzzle's identity, and BOTH forms are accepted: the name in the
# source file ("JANKO222") and the bare janko id it ends in ("222"), because
# those are the two things a namer built against janko.txt or against the
# puzzle's URL would naturally produce. A key matching no puzzle is a HARD
# ERROR -- a typo that silently names nothing is exactly how an annotation pass
# ends up half-applied with nobody the wiser.
#
# What is NOT accepted as a key is a bank index. The bank is emitted size-sorted
# and renumbers whenever it changes, so an index names a different puzzle after
# any edit.
#
# A puzzle with no entry ships with an EMPTY name and the win screen draws no
# name band at all. "JANKO222" is an id, not a name, and a reveal that names the
# picture "JANKO222" is worse than a reveal that says nothing.
NAMES = os.path.join(ASSETS, "janko-names.json")

# The credit for every shipped puzzle, generated into PROVENANCE.md between
# these markers. See write_credits().
CREDITS = os.path.join(ASSETS, "PROVENANCE.md")
CREDITS_BEGIN = "<!-- BEGIN GENERATED CREDITS -->"
CREDITS_END = "<!-- END GENERATED CREDITS -->"

# What a name may contain, and it is the namer's alphabet, not a guess: A-Z,
# digits, space, hyphen, apostrophe. The Toybox display cut has no fallback box
# -- a glyph it lacks is a HOLE in the word (see the typography-fold memory) --
# so a name carrying anything else is refused here rather than shipped as a gap
# nobody sees until a device renders it.
NAME_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -'")

# The longest name the namer will hand over. The win screen shrinks a name that
# does not fit its band (toybox::fittedTitle), so an over-long name is not
# clipped, it is shrunk until it is unreadable -- and nothing reports that. The
# limit is enforced at entry and re-enforced here, because a file can be edited
# after the namer has written it.
NAME_MAX = 9


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


def janko_id(name):
    """The trailing digits of a source name ("JANKO222" -> "222"), or None."""
    digits = ""
    for ch in reversed(name):
        if not ch.isdigit():
            break
        digits = ch + digits
    return digits or None


def load_names(source_names):
    """Mario's per-puzzle names, keyed by source name and checked to death.

    `source_names` is every puzzle name the bank is about to ship. Every key in
    the file must match one of them, by source name or by trailing janko id.

    Everything here is a HARD ERROR rather than a warning, and that is the
    point: the failure this guards against is not a crash, it is 137 pictures
    quietly revealing the wrong name -- or no name -- with a green build and a
    file that looks full. A name is data Mario typed by hand, once, and nothing
    downstream can tell a missing entry from a picture he has not reached yet.
    """
    if not os.path.exists(NAMES):
        return {}
    import json

    with open(NAMES, encoding="utf-8") as f:
        raw = json.load(f)
    if not isinstance(raw, dict):
        sys.exit("%s: expected a JSON object of {puzzle id: NAME}" % NAMES)

    by_id = {}
    for name in source_names:
        by_id.setdefault(name, name)
        ident = janko_id(name)
        if ident is not None:
            # A bare id is only a usable key while it names ONE puzzle. Two
            # sources ending in the same digits would make "222" ambiguous, and
            # resolving it to whichever came first is how a name lands on the
            # wrong picture. Drop the short form rather than guess.
            by_id[ident] = None if ident in by_id else name

    names = {}
    for key, value in raw.items():
        if key.startswith("_"):
            continue  # "_comment" and friends, the shape janko-authors.json uses
        target = by_id.get(str(key).strip().upper())
        if target is None:
            sys.exit(
                "%s: key %r names no puzzle in the shipped bank (or names more "
                "than one). Keys are the source name ('JANKO222') or its janko "
                "id ('222'); a bank index is never a key." % (NAMES, key)
            )
        if not isinstance(value, str):
            sys.exit("%s: %r is not a string" % (NAMES, key))
        text = value.strip().upper()
        if not text:
            continue  # an entry Mario has not filled in yet is simply unnamed
        bad = sorted(set(text) - NAME_CHARS)
        if bad:
            sys.exit(
                "%s: name %r for %s uses %s, which the display cut may not have "
                "-- a missing glyph is a HOLE in the word, not a box. Allowed: "
                "A-Z, 0-9, space, hyphen, apostrophe."
                % (NAMES, value, key, ", ".join(repr(ch) for ch in bad))
            )
        if len(text) > NAME_MAX:
            sys.exit(
                "%s: name %r for %s is %d characters; the win screen's band "
                "holds %d before it starts shrinking the type."
                % (NAMES, value, key, len(text), NAME_MAX)
            )
        if target in names and names[target] != text:
            sys.exit("%s: %s is named twice, %r and %r" % (NAMES, target, names[target], text))
        names[target] = text
    return names


def write_credits(puzzles):
    """Rewrite the per-puzzle credit table in PROVENANCE.md from the bank.

    The attribution no longer reaches the device -- it is not firmware's job and
    it cost ~34KB of flash in source URLs alone -- so this file is now the ONLY
    place the credit lives, and the repository is public. A credit file that
    silently stops matching the shipped puzzles is worse than none, so it is
    GENERATED from the same list the header is, in the same pass, rather than
    maintained beside it. host-tests/picrossprov re-derives the mapping from
    janko.txt and the shipped bitmaps and checks this table against it, so a
    hand-edit or a stale regenerate is caught rather than believed.
    """
    rows = [
        "| Puzzle | Designer | Licence | Source |",
        "| --- | --- | --- | --- |",
    ]
    for name, _cells, prov in puzzles:
        rows.append(
            "| `%s` | %s | %s | <%s> |"
            % (
                name,
                prov.get("author", PROV_DEFAULT["author"]),
                prov.get("license", PROV_DEFAULT["license"]),
                prov.get("source", PROV_DEFAULT["source"]),
            )
        )
    block = "\n".join(
        [
            CREDITS_BEGIN,
            "",
            "<!-- GENERATED by tools_local/picross/gen_picross.py. Do not edit by hand. -->",
            "",
            "%d puzzles ship, and every one of them is here." % len(puzzles),
            "",
        ]
        + rows
        + ["", CREDITS_END]
    )

    with open(CREDITS, encoding="utf-8") as f:
        text = f.read()
    if CREDITS_BEGIN not in text or CREDITS_END not in text:
        sys.exit(
            "%s has no %s / %s block. That block is the credit for every shipped "
            "puzzle and nothing else carries it; add the markers back rather "
            "than letting this run write the table nowhere."
            % (os.path.relpath(CREDITS, ROOT), CREDITS_BEGIN, CREDITS_END)
        )
    head = text[: text.index(CREDITS_BEGIN)]
    tail = text[text.index(CREDITS_END) + len(CREDITS_END) :]
    with open(CREDITS, "w", encoding="utf-8") as f:
        f.write(head + block + tail)
    print("wrote the credit table in %s (%d puzzles)" % (os.path.relpath(CREDITS, ROOT), len(puzzles)))


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
    names = load_names([name for name, _cells, _prov in puzzles])
    unnamed = [name for name, _cells, _prov in puzzles if name not in names]
    maxsize = max((len(cells) for _, cells, _ in puzzles), default=5)
    hexdigits = (maxsize + 3) // 4  # 5->2, 10->3, 15->4
    emitted = []
    longest = 0
    max_row_runs = 0
    max_col_runs = 0
    max_run = 0
    for name, cells, _prov in puzzles:
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
        display = names.get(name, "")
        longest = max(longest, len(display))
        max_row_runs = max(max_row_runs, max(len(rc) for rc in rows))
        max_col_runs = max(max_col_runs, max(len(cc) for cc in cols))
        max_run = max(max_run, max(max(rc) for rc in rows), max(max(cc) for cc in cols))
        emitted.append(
            '    {%s, %d, {%s}},'
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
        "maxrowruns": max_row_runs,
        "maxcolruns": max_col_runs,
        "maxrun": max_run,
    }
    with open(OUTPUT, "w", encoding="utf-8") as out:
        out.write(header)
    clang_format(OUTPUT)
    print("wrote %s (%d puzzles)" % (os.path.relpath(OUTPUT, ROOT), len(emitted)))
    # The credit leaves the device with this change, so it has to land somewhere
    # in the same pass that writes the bank. Not "somewhere later, by hand".
    write_credits(puzzles)
    if unnamed:
        print(
            "  %d of %d puzzles have no name yet; their win screen draws no name band.\n"
            "  Names go in %s -- see load_names()."
            % (len(unnamed), len(puzzles), os.path.relpath(NAMES, ROOT))
        )
    else:
        print("  every puzzle is named")


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
// THESE PUZZLES ARE THIRD-PARTY WORK, USED BY PERMISSION AND NOT LICENSED TO
// ANYBODY. Read assets_local/picross/PROVENANCE.md before copying any of them
// out of this file. A fork of this repository does NOT inherit the permission.
//
// THE ATTRIBUTION IS DELIBERATELY NOT IN THIS FILE, and its absence is not an
// oversight -- do not "fix" it by adding the designers back. Every puzzle used
// to carry an author, a rights string and a source URL, and the URLs alone were
// ~34KB of an ~51KB bank: a URL cost more flash than the puzzle it pointed at,
// on a device where flash is the scarce thing and no player ever reads it.
// Mario's call. The full per-puzzle mapping (puzzle, designer, licence, source)
// now lives in assets_local/picross/PROVENANCE.md, is GENERATED into that file
// by the same run that writes this one, and is checked against this bank by
// host-tests/picrossprov -- so the credit obligation is met, in one place, by a
// mechanism rather than by memory.
//
// The one string a puzzle does carry is its NAME: the reveal on the win screen,
// written by hand into assets_local/picross/janko-names.json. An empty name is
// a picture nobody has named yet, and the win screen draws no name band for it.
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
  // Mario's name for the picture, revealed on the win screen. EMPTY for a
  // picture nobody has named yet, and the win screen then draws no name band --
  // a reveal that names the picture after its catalogue id would be worse than
  // one that says nothing. See janko-names.json.
  const char* name;
  uint8_t size;
  uint16_t rows[kMaxSize];  // bit c set when (row, col) is solid
};

constexpr Puzzle kPuzzles[] = {
%(rows)s
};

constexpr int kPuzzleCount = static_cast<int>(sizeof(kPuzzles) / sizeof(kPuzzles[0]));

// How many DISTINCT sizes the bank holds. ONE today: the game is 10x10 and
// nothing else (Mario, on the panel: "it's just not gonna work" about 15x15),
// so the picker draws no size tabs at all -- there is nothing to choose
// between. It stays derived rather than written as a literal because a 4-slot
// array with a silent `break` once made every puzzle past the fourth run
// unreachable from the tabs with nothing anywhere reporting it, and because
// this is the number the picker checks before deciding whether tabs exist.
// The bank is emitted size-sorted, so each size is one contiguous run;
// host-tests/picross asserts the sortedness this number assumes.
constexpr int kSizeGroupCount = %(sizegroups)d;

// The longest name in the bank, so a screen sizes its buffer from this rather
// than measuring at runtime, and the generator keeps it honest. Zero while no
// picture has been named.
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
