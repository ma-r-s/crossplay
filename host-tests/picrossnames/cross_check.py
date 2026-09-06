#!/usr/bin/env python3
"""Does the Python width rule agree with the JavaScript one?

Driven by test_logic.js, which hands over every string it judged and the verdict
it reached. Any disagreement is a failure: a name the browser tool accepts and
gen_picross.py then refuses is 137 names rejected at the end of a day's work.
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools_local/picross"))
import name_width  # noqa: E402

cases = json.loads(pathlib.Path(sys.argv[1]).read_text())
data = name_width.load()
bad = []
for case in cases:
    mine = name_width.judge(case["name"], data)["level"]
    if mine != case["level"]:
        bad.append((case["name"], case["level"], mine))

if bad:
    print(f"{len(bad)} disagreement(s) between the JS rule and name_width.py:")
    for name, js, py in bad[:10]:
        print(f"  {name!r}: JavaScript says {js}, Python says {py}")
    sys.exit(1)
print(f"cross-check: {len(cases)} strings, both rules agree")
