#!/usr/bin/env python3
"""Build src/apps_local/go/GoPatterns.h from the MoGo 3x3 playout patterns.

    python3 tools_local/go/gen_go_patterns.py [--check <ardu_go/pattern_table.h>]

The thirteen templates below are Petr Baudis's, from michi (MIT), which is
itself a transcription of Gelly, Wang, Munos and Teytaud 2006. They are the
source of truth here rather than anybody's compiled table: a table is bytes
nobody can read, and the one table available to copy has no working consumer
left in the project it came from, so there was nothing to check it against.

This expands them instead -- every rotation, every reflection, both colour
assignments -- into the same 961-byte encoding ArduGO uses, and `--check` diffs
the result against ArduGO's bytes. The two agree, which is what makes either of
them trustworthy.

The output is committed, because a checkout should build without this script.
See assets_local/go/README.md.
"""

import itertools
import pathlib
import re
import sys

# The thirteen, as three rows of three. The centre is always the empty point the
# move would go on.
#
#   X  one colour        O  the other        .  empty
#   x  not X (so O, empty or off-board)      o  not O
#   ?  anything at all, off-board included   #  off-board
TEMPLATES = [
    "XOX" "..." "???",  # 1  hane, enclosing
    "XO." "..." "?.?",  # 2  hane, non-cutting
    "XO?" "X.." "x.?",  # 3  hane, magari
    ".O." "X.." "...",  # 4  katatsuke, or diagonal attachment
    "XO?" "O.o" "?o?",  # 5  cut, unprotected
    "XO?" "O.X" "???",  # 6  cut, peeped
    "?X?" "O.O" "ooo",  # 7  cut two, de
    "OX?" "o.O" "???",  # 8  cut keima
    "X.?" "O.?" "##?",  # 9  side, chase
    "OX?" "X.O" "###",  # 10 side, block side cut
    "?X?" "x.O" "###",  # 11 side, block side connection
    "?XO" "x.x" "###",  # 12 side, sagari
    "?OX" "X.O" "###",  # 13 side, cut
]

EMPTY, MINE, OPP, OUT = 0, 1, 2, 3

# What each character accepts, with A and B the two colours.
def accepts(ch, a, b):
    if ch == "X":
        return (a,)
    if ch == "O":
        return (b,)
    if ch == ".":
        return (EMPTY,)
    if ch == "x":
        return (b, EMPTY, OUT)
    if ch == "o":
        return (a, EMPTY, OUT)
    if ch == "?":
        return (a, b, EMPTY, OUT)
    if ch == "#":
        return (OUT,)
    raise ValueError(f"pattern character {ch!r}")


# The eight neighbours, in the order the index reads them.
ORDER = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
# Where each of those sits in a nine-character template.
CELL = {(-1, -1): 0, (-1, 0): 1, (-1, 1): 2, (0, -1): 3, (0, 1): 5, (1, -1): 6, (1, 0): 7, (1, 1): 8}

SIZE = 9
OFFSETS = [0, 4, 35, 39, 70, 891, 922, 926, 957]
TOTAL_BYTES = 961


def symmetries(cells):
    """All eight of a 3x3 template, as dicts keyed by (dy, dx)."""
    base = {(dy, dx): cells[CELL[(dy, dx)]] for dy, dx in ORDER}
    out = []
    for flip in (False, True):
        current = {(dy, dx if not flip else -dx): v for (dy, dx), v in base.items()}
        for _ in range(4):
            out.append(dict(current))
            # Rotate a quarter turn: (dy, dx) -> (dx, -dy).
            current = {(dx, -dy): v for (dy, dx), v in current.items()}
    return out


def on_board(cls):
    """Which of ORDER exist for a point in this position class."""
    clsy, clsx = divmod(cls, 3)
    rows = {0: [0, 1], 1: [-1, 0, 1], 2: [-1, 0]}[clsy]
    cols = {0: [0, 1], 1: [-1, 0, 1], 2: [-1, 0]}[clsx]
    return [(dy, dx) for dy, dx in ORDER if dy in rows and dx in cols]


def build():
    table = bytearray(TOTAL_BYTES)
    matched = [0] * 9
    for template in TEMPLATES:
        for shape in symmetries(template):
            for a, b in ((MINE, OPP), (OPP, MINE)):
                choices = [accepts(shape[step], a, b) for step in ORDER]
                for assignment in itertools.product(*choices):
                    value = dict(zip(ORDER, assignment))
                    for cls in range(9):
                        live = on_board(cls)
                        # A configuration belongs to a class only when exactly
                        # its off-board neighbours are off the board. This is
                        # the whole reason the table is 961 bytes and not 8192:
                        # off-board is a property of WHERE YOU ARE.
                        if any(value[s] == OUT for s in live):
                            continue
                        if any(value[s] != OUT for s in ORDER if s not in live):
                            continue
                        index = 0
                        mult = 1
                        for step in live:
                            index += value[step] * mult
                            mult *= 3
                        bit = OFFSETS[cls] * 8 + index
                        table[bit // 8] |= 1 << (bit % 8)
                        matched[cls] += 1
    return table, matched


def emit(table):
    out = pathlib.Path(__file__).resolve().parents[2] / "src/apps_local/go/GoPatterns.h"
    lines = [
        "#pragma once",
        "",
        "// GENERATED by tools_local/go/gen_go_patterns.py. Do not edit.",
        "//",
        "// The MoGo 3x3 playout patterns (Gelly, Wang, Munos and Teytaud, 2006) as",
        "// transcribed by Petr Baudis in michi (MIT), expanded over every rotation,",
        "// reflection and colour swap. See assets_local/go/README.md.",
        "//",
        "// One bit a local configuration: set means this shape is worth playing. The",
        "// index is base-3 over the ON-BOARD neighbours in the order NW N NE W E SW S",
        "// SE -- 0 empty, 1 the mover's own, 2 the opponent's, least significant first.",
        "// Which neighbours exist is the position CLASS, clsy * 3 + clsx, where each",
        "// axis is 0 at the low edge, 1 in the middle and 2 at the high edge.",
        "//",
        "// Off-board is a property of WHERE YOU ARE rather than of the neighbour, which",
        "// is why this is 961 bytes where michi's two-bits-a-neighbour encoding of the",
        "// same patterns is 8,192: most of its 65,536 configurations are geometrically",
        "// impossible.",
        "",
        "#include <cstdint>",
        "",
        "namespace gopatterns {",
        "",
        "constexpr uint16_t kOffset[9] = {" + ", ".join(str(v) for v in OFFSETS) + "};",
        "",
        "constexpr uint8_t kBits[%d] = {" % TOTAL_BYTES,
    ]
    for i in range(0, len(table), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in table[i:i + 16]) + ",")
    lines += ["};", "", "}  // namespace gopatterns", ""]
    out.write_text("\n".join(lines))
    return out


def main():
    table, matched = build()
    # The expansion has to have produced something in every class, or a corner
    # of the board silently has no policy at all.
    for cls in range(9):
        assert matched[cls] > 0, f"class {cls} matched no pattern"
    set_bits = sum(bin(b).count("1") for b in table)
    print(f"expanded {len(TEMPLATES)} templates to {set_bits} configurations")

    if "--check" in sys.argv:
        other = pathlib.Path(sys.argv[sys.argv.index("--check") + 1]).read_text()
        theirs = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", re.search(r"PAT3_BITS\[961\]\s*=\s*\{(.*?)\};", other, re.S).group(1))]
        their_offsets = [int(v) for v in re.search(r"PAT3_OFFSET\[9\]\s*=\s*\{([^}]*)\}", other).group(1).replace("\n", "").split(",") if v.strip()]
        assert their_offsets == OFFSETS, f"offsets differ: {their_offsets}"
        differing = [i for i, (x, y) in enumerate(zip(table, theirs)) if x != y]
        if differing:
            print(f"DIFFERS from the reference in {len(differing)} of {TOTAL_BYTES} bytes: {differing[:12]}")
            return 1
        print(f"identical to the reference table, all {TOTAL_BYTES} bytes")

    print(f"wrote {emit(table)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
