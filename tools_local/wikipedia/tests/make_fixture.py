#!/usr/bin/env python3
"""Picks the committed test rows out of the 3,000-row research sample.

    python3 tools_local/wikipedia/tests/make_fixture.py <sample.jsonl.gz>

writes fixtures/rows.jsonl.gz beside this file: every row the tests need to
exercise a feature (tables both simple and complex, definition lists,
ordered lists, galleries, infobox lists, non-ASCII titles, undrawable runs
in the lead, one article over 64 KB, one duplicate title) plus a few plain
ones, kept small enough to commit. The selection is deterministic.
"""

import collections
import gzip
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fixtures", "rows.jsonl.gz")


def part_types(parts, acc):
    for p in parts or []:
        if isinstance(p, dict):
            acc[p.get("type")] += 1
            part_types(p.get("has_parts"), acc)


def has_cjk_lead(row):
    secs = (
        json.loads(row["sections"])
        if isinstance(row.get("sections"), str)
        else row.get("sections") or []
    )
    if not secs:
        return False
    for p in secs[0].get("has_parts") or []:
        v = p.get("value") or ""
        if "(" in v and any(0x3000 <= ord(c) <= 0x9FFF for c in v):
            return True
    return False


def main(sample):
    rows = []
    with gzip.open(sample, "rt", encoding="utf-8") as f:
        for line in f:
            rows.append(json.loads(line))
    want = collections.OrderedDict()

    def take(key, pred, n):
        got = 0
        for r in rows:
            if got >= n:
                break
            if id(r) in {id(x) for x in want.values()}:
                continue
            if pred(r):
                want[f"{key}:{r['name']}"] = r
                got += 1

    def types_of(r):
        acc = collections.Counter()
        secs = (
            json.loads(r["sections"])
            if isinstance(r.get("sections"), str)
            else r.get("sections") or []
        )
        part_types(secs, acc)
        return acc

    def simple_table(r):
        for t in (
            json.loads(r["tables"])
            if isinstance(r.get("tables"), str)
            else r.get("tables") or []
        ):
            rr = (t.get("headers") or []) + (t.get("rows") or [])
            if rr and max(len(x) for x in rr) <= 4:
                return True
        return False

    def complex_table(r):
        for t in (
            json.loads(r["tables"])
            if isinstance(r.get("tables"), str)
            else r.get("tables") or []
        ):
            rr = (t.get("headers") or []) + (t.get("rows") or [])
            if rr and max(len(x) for x in rr) > 4:
                return True
        return False

    take("simple_table", lambda r: r.get("tables") and simple_table(r), 4)
    take("complex_table", lambda r: r.get("tables") and complex_table(r), 3)
    take("definition_list", lambda r: types_of(r)["definition_list"] > 0, 4)
    take("ordered_list", lambda r: types_of(r)["ordered_list"] > 0, 3)
    take("gallery", lambda r: types_of(r)["gallery"] > 0, 1)
    take(
        "infobox_list",
        lambda r: r.get("infoboxes") and '"type": "list"' in r["infoboxes"],
        4,
    )
    take("non_ascii_title", lambda r: any(ord(c) > 127 for c in r["name"]), 6)
    take("cjk_lead", has_cjk_lead, 6)
    take("big", lambda r: len(r.get("sections") or "") > 150000, 1)
    take(
        "nested_list",
        lambda r: '"list_item"' in (r.get("sections") or "")
        and '"has_parts":[{"type":"list_item"' in (r.get("sections") or ""),
        3,
    )
    take("no_infobox", lambda r: not r.get("infoboxes"), 4)
    take("plain", lambda r: True, 4)
    names = collections.Counter(r["name"] for r in want.values())
    dup = [n for n, c in collections.Counter(r["name"] for r in rows).items() if c > 1]
    if dup and names[dup[0]] == 0:
        for r in rows:
            if r["name"] == dup[0]:
                want[f"dup:{id(r)}"] = r
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with gzip.open(OUT, "wt", encoding="utf-8", compresslevel=9) as f:
        for r in want.values():
            f.write(json.dumps(r, ensure_ascii=False, separators=(",", ":")) + "\n")
    print(f"{len(want)} rows -> {OUT} ({os.path.getsize(OUT)} bytes)")
    for k in want:
        print("  ", k)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
