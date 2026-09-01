#!/usr/bin/env python3
"""Rewrite a pack's multiple-choice distractors, in place, from the pack itself.

    tools_local/trivia/redistract.py in.dat out.dat

WHY THIS EXISTS RATHER THAN A REBUILD. The shipped pack's distractors gave the
answer away, and the cause was one over-tuned line in build_pack.py: candidates
were sorted by |len(candidate) - len(answer)| and truncated to the nearest 40.
For a five-letter country that is always about the same five-letter countries,
so Japan/India/Spain/China recurred across question after question and the real
answer became the option you had not seen before. Type matching was never the
problem; the pool was.

Fixing it does not need the Jeopardy source dataset, which this repo does not
carry: the answer type is recoverable from the clue text that ships in the pack,
so the pack can be re-distracted from itself.

WHAT CHANGED, in the selection:

  * candidates are drawn from the WHOLE type pool, not the nearest 40 by length
  * a length BAND replaces the sort, so the answer still does not stand out by
    being the longest or shortest option -- a tell needing no knowledge
  * least-used-first, so the pool spreads across the whole type instead of
    collapsing onto whichever few answers happen to sit near a common length.
    This is the half that actually kills the recurrence: without it, widening
    the pool alone still favours the same popular short answers.

Questions whose type has too small a pool lose their distractors and become
Quizmaster-only, exactly as before -- a bad option set is worse than no solo
question, because it teaches the player the pool rather than the answer.
"""

import collections
import random
import sys
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pack_format  # noqa: E402
from build_pack import answer_type, choose_distractors  # noqa: E402

def load(path):
    pack = pack_format.open_pack(path)
    items = []
    for i in range(pack["count"]):
        q = pack_format.read_one(pack, i)
        items.append(q)
    return items


def redistract(items, rng):
    by = collections.defaultdict(set)
    types = []
    for x in items:
        t = answer_type(x["q"])
        types.append(t)
        if t:
            by[t].add(x["a"])
    # Deduplicated: one answer appears under many clues, and a pool counted with
    # duplicates looks large while offering few distinct options.
    by = {t: sorted(v) for t, v in by.items()}

    used = collections.Counter()
    kept = dropped = 0
    for x, t in zip(items, types):
        picks = choose_distractors(by, x, t, used, rng)
        if len(picks) >= 3:
            x["w"] = picks
            kept += 1
        else:
            x["w"] = []
            dropped += 1
    return kept, dropped, used


def concentration(used):
    """Share of all distractor slots taken by the twenty most-used answers.

    This is the number that describes the defect: a player learns a pool they
    keep seeing. Lower is better.
    """
    total = sum(used.values())
    if total == 0:
        return 0.0
    top = sum(n for _, n in used.most_common(20))
    return top / total


def main():
    if len(sys.argv) != 3:
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

    kept, dropped, used = redistract(items, random.Random(20260901))
    size = pack_format.write(items, dst)

    print(f"  records            {len(items)}")
    print(f"  with distractors   {kept}   (was {was_playable})")
    print(f"  quizmaster only    {dropped}")
    print(f"  distinct options   {len(used)}   (was {len(before)})")
    print(
        f"  top-20 share       {concentration(used) * 100:.1f}%"
        f"   (was {concentration(before) * 100:.1f}%)"
    )
    print(f"  wrote              {dst}  {size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
