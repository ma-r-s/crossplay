#!/usr/bin/env python3
"""Checks that the terrain checker actually rejects broken boards.

A validator nobody has tried to break is a validator that has never been
tested, and this one is the only thing standing between a mistraced board and
the firmware. Each mutant below breaks one rule; all of them must be caught.

    tools_local/terrain-editor/selftest.py
"""

import copy
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from to_cpp import check, emit  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
GOOD = os.path.join(HERE, "boards", "castle-field.json")


def mutants(good):
    def m(name, fn):
        board = copy.deepcopy(good)
        fn(board)
        return name, board

    def drop_hq(b):
        b["hqs"] = [q for q in b["hqs"] if q["seat"] == 0]

    def strand(b):
        # Base 4 keeps its slot but loses every path to it.
        b["edges"] = [e for e in b["edges"] if 4 not in e]

    def lone_region(b):
        b["regions"][0]["bases"] = [0]

    def region_names_hq(b):
        b["regions"][0]["bases"] = [0, 16]

    def unreachable_objective(b):
        b["objective"] = 99

    def gate_admits_nothing(b):
        b["bases"][4]["special"] = "gate"
        b["bases"][4]["gate"] = []

    def gate_on_a_non_gate(b):
        b["bases"][6]["gate"] = ["4"]

    def duplicate_path(b):
        b["edges"].append(list(b["edges"][0]))

    def self_loop(b):
        b["edges"].append([3, 3])

    def path_off_the_board(b):
        b["edges"].append([0, 99])

    def unknown_special(b):
        b["bases"][0]["special"] = "teleport"

    def zero_medals(b):
        b["regions"][0]["medals"] = 0

    return [
        m("an H.Q. missing for one seat", drop_hq),
        m("a base no path reaches", strand),
        m("a region fenced by one base", lone_region),
        m("a region that names an H.Q.", region_names_hq),
        m("an objective the medals cannot reach", unreachable_objective),
        m("a gate that admits nothing", gate_admits_nothing),
        m("gate values on a base that is not a gate", gate_on_a_non_gate),
        m("the same path listed twice", duplicate_path),
        m("a path from a base to itself", self_loop),
        m("a path to a slot that does not exist", path_off_the_board),
        m("a special nobody implements", unknown_special),
        m("a region that pays nothing", zero_medals),
    ]


def main():
    with open(GOOD, encoding="utf-8") as fh:
        good = json.load(fh)

    checks = 0
    errs = check(good)
    if errs:
        print(f"FAIL: the known-good board was rejected: {errs}")
        return 1
    checks += 1

    # It must also produce something, not merely validate.
    if "buildCastleField" not in emit(good):
        print("FAIL: the generator produced no builder for a good board")
        return 1
    checks += 1

    for name, board in mutants(good):
        if not check(board):
            print(f"FAIL: {name} was accepted")
            return 1
        checks += 1

    print(
        f"editor     {checks} checks, 0 failed  ({len(mutants(good))} mutants, all caught)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
