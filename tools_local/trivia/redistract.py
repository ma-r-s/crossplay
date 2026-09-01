#!/usr/bin/env python3
"""Rewrite a pack's multiple-choice options, in place, from the pack itself.

    tools_local/trivia/redistract.py in.dat out.dat [out.jsonl]

WHY THIS EXISTS RATHER THAN A REBUILD. The Jeopardy source dataset is not in
this repo and does not need to be: a clue names its answer's type, its period
and often its region, so the pack carries everything the option-picker reads.
The selection itself lives in `distractors.py`, which `build_pack.py` also
imports -- a corpus refresh and a re-distraction must not drift apart.

The third argument writes the same pack as JSONL so `test_pack.py` can be run
on the result. Skipping that is how the shipped pack came to carry 486 option
sets with a twin in them ("van Gogh" beside "Van Gogh"): the invariant that
catches it has existed the whole time and was never run on redistracted output.
"""

import collections
import hashlib
import json
import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import distractors  # noqa: E402
import pack_format  # noqa: E402
from build_pack import dedash, norm_key  # noqa: E402


def load(path):
    pack = pack_format.open_pack(path)
    return [pack_format.read_one(pack, i) for i in range(pack["count"])]


def concentration(used):
    """Share of all distractor slots taken by the twenty most-used answers.

    A player learns a pool they keep seeing, so lower is better. It is not the
    headline number any more -- spread was the FIRST defect, and fixing it did
    not stop a cold player answering 70% of questions without knowing the fact
    -- but a regression here would bring the first defect back.
    """
    total = sum(used.values())
    if total == 0:
        return 0.0
    return sum(n for _, n in used.most_common(20)) / total


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    items = load(src)

    before = collections.Counter()
    was_playable = 0
    for x in items:
        if len(x.get("w", [])) >= 3:
            was_playable += 1
        for w in x.get("w", []):
            before[w.lower()] += 1

    dashed = dedash(items)
    index, kept, dropped, used = distractors.redistract(items, random.Random(20260901))
    size = pack_format.write(items, dst)

    print(f"  records            {len(items)}")
    print(f"  dashes replaced    {dashed}")
    print(
        f"  types found        {len(index.pools)}  ({sum(1 for k in index.keys if k)} clues typed)"
    )
    print(f"  with distractors   {kept}   (was {was_playable})")
    print(f"  quizmaster only    {dropped}")
    print(f"  distinct options   {len(used)}   (was {len(before)})")
    print(
        f"  top-20 share       {concentration(used) * 100:.1f}%"
        f"   (was {concentration(before) * 100:.1f}%)"
    )
    print(f"  wrote              {dst}  {size} bytes")

    if len(sys.argv) == 4:
        with open(sys.argv[3], "w", encoding="utf-8") as f:
            for x in items:
                row = {
                    "id": hashlib.sha1(norm_key(x["q"]).encode()).hexdigest()[:12],
                    "q": x["q"],
                    "a": x["a"],
                    "d": x["d"],
                    "y": x["y"],
                }
                if x.get("alt"):
                    row["alt"] = x["alt"]
                if x.get("w"):
                    row["w"] = x["w"]
                f.write(
                    json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n"
                )
        print(f"  wrote              {sys.argv[3]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
